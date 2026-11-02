use crate::memory::{GamePod, pointer, read, read_memory, read_pointer};
use std::{
    collections::HashMap,
    time::{Duration, Instant},
};
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum FlagReason {
    #[default]
    Ready,
    ManagerUnavailable,
    DivisorUnavailable,
    TreeUnavailable,
    TreeLimit,
    StrideInvalid,
    StorageUnavailable,
    MemoryRead,
}
impl FlagReason {
    pub fn key(self) -> &'static str {
        match self {
            Self::Ready => "ready",
            Self::ManagerUnavailable => "manager_unavailable",
            Self::DivisorUnavailable => "divisor_unavailable",
            Self::TreeUnavailable => "tree_unavailable",
            Self::TreeLimit => "tree_limit",
            Self::StrideInvalid => "stride_invalid",
            Self::StorageUnavailable => "storage_unavailable",
            Self::MemoryRead => "memory_read",
        }
    }
}
#[derive(Clone, Copy, Debug, Default)]
pub struct FlagDiagnostics {
    pub reason: FlagReason,
    pub reads: u64,
    pub failures: u64,
    pub cache_misses: u64,
    pub id: u32,
    pub manager: usize,
    pub divisor: u32,
    pub stride: u32,
    pub category: u32,
    pub storage_kind: u32,
    pub storage_index: u32,
}

/// Lives only for one batch, never across saves or address-cache retirement.
/// Missing/full unreadable nodes retain the scalar reader's field semantics.
#[derive(Default)]
struct BatchTree {
    root: Option<Result<(usize, usize), FlagReason>>,
    // Also holds the packed manager header's stride/base fields. It uses the
    // same bounded snapshot and scalar fallback as a tree node.
    nodes: HashMap<usize, Option<[u8; 0x38]>>,
}
#[derive(Default)]
struct TreeReads(Option<BatchTree>);
impl TreeReads {
    fn batch() -> Self {
        Self(Some(BatchTree::default()))
    }
    fn reset(&mut self) {
        if let Some(batch) = &mut self.0 {
            batch.root = None;
            batch.nodes.clear();
        }
    }
    fn root(&mut self, manager: usize) -> Result<(usize, usize), FlagReason> {
        if let Some(result) = self.0.as_ref().and_then(|batch| batch.root) {
            return result;
        }
        let result = (|| {
            let sentinel = pointer(manager.checked_add(0x38).ok_or(FlagReason::MemoryRead)?);
            if sentinel == 0 {
                return Err(FlagReason::TreeUnavailable);
            }
            Ok((sentinel, pointer(sentinel + 8)))
        })();
        if let Some(batch) = &mut self.0 {
            batch.root = Some(result);
        }
        result
    }
    fn field<T: GamePod>(&mut self, node: usize, offset: usize) -> Option<T> {
        if let Some(batch) = &mut self.0 {
            // Bound hostile trees independently of the existing 64-hop limit.
            // At capacity, retain scalar reads rather than evicting and
            // repeatedly copying the same inaccessible/cyclic nodes.
            if batch.nodes.len() < 4096 || batch.nodes.contains_key(&node) {
                let snapshot = batch.nodes.entry(node).or_insert_with(|| {
                    let mut bytes = [0; 0x38];
                    read_memory(node, &mut bytes).then_some(bytes)
                });
                if let Some(bytes) = snapshot {
                    return Some(T::decode(bytes.get(offset..offset.checked_add(T::SIZE)?)?));
                }
            }
        }
        read(node.checked_add(offset)?)
    }
    fn pointer(&mut self, node: usize, offset: usize) -> Option<usize> {
        if self.0.is_none() {
            return read_pointer(node.checked_add(offset)?);
        }
        let value: usize = self.field(node, offset)?;
        (value <= 0x0000_7fff_ffff_ffff).then_some(value)
    }
}

fn category_storage(
    manager: usize,
    category: u32,
    diagnostic: &mut FlagDiagnostics,
    tree: &mut TreeReads,
) -> Result<usize, FlagReason> {
    use FlagReason::*;
    let (sentinel, mut node) = tree.root(manager)?;
    let mut candidate = sentinel;
    let mut terminal = false;
    for _ in 0..64 {
        if node == 0 {
            return Err(TreeUnavailable);
        }
        if tree.field::<u8>(node, 0x19).ok_or(MemoryRead)? != 0 {
            terminal = true;
            break;
        }
        if tree.field::<u32>(node, 0x20).ok_or(MemoryRead)? >= category {
            candidate = node;
            node = tree.pointer(node, 0).ok_or(MemoryRead)?;
        } else {
            node = tree.pointer(node, 0x10).ok_or(MemoryRead)?;
        }
    }
    if !terminal {
        return Err(TreeLimit);
    }
    if candidate == sentinel || tree.field::<u32>(candidate, 0x20).ok_or(MemoryRead)? != category {
        return Ok(0);
    }
    diagnostic.storage_kind = tree.field(candidate, 0x28).ok_or(MemoryRead)?;
    match diagnostic.storage_kind {
        1 => {
            let stride = tree.field::<u32>(manager, 0x20).ok_or(MemoryRead)?;
            let index = tree.field::<u32>(candidate, 0x30).ok_or(MemoryRead)?;
            diagnostic.stride = stride;
            diagnostic.storage_index = index;
            // Match the active-mask reader in util/gameflags.cpp. A smaller
            // stride does not invalidate a requested byte that is readable;
            // RPM below validates that byte rather than an inferred category
            // allocation size. The offline C++ differential fixture covers it.
            if stride == 0 {
                return Err(StrideInvalid);
            }
            let offset = (stride as u64)
                .checked_mul(index as u64)
                .ok_or(StorageUnavailable)?;
            if offset > u32::MAX as u64 {
                return Err(StorageUnavailable);
            }
            let base = tree.pointer(manager, 0x28).unwrap_or(0);
            if base == 0 {
                return Err(StorageUnavailable);
            }
            base.checked_add(offset as usize).ok_or(StorageUnavailable)
        }
        2 => {
            let p = tree.pointer(candidate, 0x30).ok_or(MemoryRead)?;
            if p == 0 {
                Err(StorageUnavailable)
            } else {
                Ok(p)
            }
        }
        _ => Ok(0),
    }
}
#[derive(Default)]
pub struct EventFlagCache {
    manager: usize,
    divisor: u32,
    expiry: Option<Instant>,
    storage: HashMap<u32, usize>,
    diagnostic: FlagDiagnostics,
}
impl EventFlagCache {
    pub fn reset(&mut self) {
        self.manager = 0;
        self.divisor = 0;
        self.expiry = None;
        self.storage.clear();
    }
    pub fn read(&mut self, manager: usize, id: u32) -> Option<bool> {
        self.diagnostic.reads = self.diagnostic.reads.wrapping_add(1);
        self.diagnostic.id = id;
        self.diagnostic.manager = manager;
        self.diagnostic.divisor = 0;
        self.diagnostic.category = 0;
        self.diagnostic.stride = 0;
        self.diagnostic.storage_kind = 0;
        self.diagnostic.storage_index = 0;
        let result = self.read_inner(manager, id);
        match result {
            Ok(value) => {
                self.diagnostic.reason = FlagReason::Ready;
                Some(value)
            }
            Err(reason) => {
                self.diagnostic.reason = reason;
                self.diagnostic.failures = self.diagnostic.failures.wrapping_add(1);
                None
            }
        }
    }
    pub fn diagnostics(&self) -> FlagDiagnostics {
        self.diagnostic
    }
    fn read_inner(&mut self, manager: usize, id: u32) -> Result<bool, FlagReason> {
        if id == 0 {
            return Ok(false);
        }
        let (address, mask) = self.address_inner(manager, id)?;
        if address == 0 {
            return Ok(false);
        }
        let Some(bits) = read::<u8>(address) else {
            self.storage.remove(&(id / self.divisor));
            return Err(FlagReason::MemoryRead);
        };
        Ok(bits & mask != 0)
    }

    /// Unknown/unallocated flags remain distinct from a cleared flag. Bytes
    /// shared by multiple flags are copied once for this snapshot.
    pub fn read_many(&mut self, manager: usize, ids: &[u32], out: &mut [u8]) {
        let mut bytes = HashMap::<usize, Option<u8>>::with_capacity(ids.len());
        let mut tree = TreeReads::batch();
        for (&id, value) in ids.iter().zip(out) {
            *value = match self.address_with_tree(manager, id, &mut tree) {
                Ok((address, mask)) if address != 0 && id != 0 && id != u32::MAX => {
                    match *bytes.entry(address).or_insert_with(|| read::<u8>(address)) {
                        Some(byte) => u8::from(byte & mask != 0),
                        None => 2,
                    }
                }
                _ => 2,
            };
        }
    }

    /// Freshly resolve every target before any mutation; a missing category
    /// aborts the whole request. Arena flags precede the primary completion bit.
    pub fn prepare_writes(
        &mut self,
        manager: usize,
        ids: &[(u32, bool)],
    ) -> Option<Vec<(usize, u8, bool)>> {
        self.reset();
        let mut writes = Vec::with_capacity(ids.len());
        for &(id, value) in ids {
            if id == 0 || id == u32::MAX {
                return None;
            }
            let (address, mask) = self.address_inner(manager, id).ok()?;
            if address == 0
                || !crate::memory::writable_byte(address)
                || read::<u8>(address).is_none()
            {
                return None;
            }
            writes.push((address, mask, value));
        }
        Some(writes)
    }

    fn address_inner(&mut self, manager: usize, id: u32) -> Result<(usize, u8), FlagReason> {
        self.address_with_tree(manager, id, &mut TreeReads::default())
    }
    fn address_with_tree(
        &mut self,
        manager: usize,
        id: u32,
        tree: &mut TreeReads,
    ) -> Result<(usize, u8), FlagReason> {
        use FlagReason::*;
        let now = Instant::now();
        if manager != self.manager || self.expiry.is_none_or(|expiry| now >= expiry) {
            self.reset();
            tree.reset();
            if manager == 0 {
                return Err(ManagerUnavailable);
            }
            self.divisor = read(manager + 0x1c).ok_or(MemoryRead)?;
            self.diagnostic.divisor = self.divisor;
            if self.divisor == 0 {
                return Err(DivisorUnavailable);
            }
            self.manager = manager;
            self.expiry = Some(now + Duration::from_secs(1));
        }
        let category = id / self.divisor;
        self.diagnostic.divisor = self.divisor;
        self.diagnostic.category = category;
        let storage = if let Some(storage) = self.storage.get(&category) {
            *storage
        } else {
            self.diagnostic.cache_misses = self.diagnostic.cache_misses.wrapping_add(1);
            let p = category_storage(manager, category, &mut self.diagnostic, tree)?;
            if self.storage.len() >= 8192 {
                self.storage.clear();
            }
            self.storage.insert(category, p);
            p
        };
        if storage == 0 {
            return Ok((0, 0));
        }
        let remainder = id % self.divisor;
        Ok((
            storage
                .checked_add((remainder >> 3) as usize)
                .ok_or(MemoryRead)?,
            1 << (7 - (remainder & 7)),
        ))
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    use crate::runtime_perf::NativeMemory;
    #[test]
    fn direct_storage_big_endian_bit_order_and_cycle_guard() {
        let mut manager = [0u64; 8];
        let mut sentinel = [0u64; 8];
        let mut node = [0u64; 8];
        let bytes = [0b1000_0001u8];
        manager[0x18 / 8] = 8u64 << 32; // divisor at 0x1c
        manager[0x38 / 8] = sentinel.as_ptr() as u64;
        sentinel[1] = node.as_ptr() as u64;
        sentinel[3] = 1 << 8;
        node[0] = sentinel.as_ptr() as u64;
        node[2] = sentinel.as_ptr() as u64;
        node[4] = 1;
        node[5] = 2;
        node[6] = bytes.as_ptr() as u64;
        let mut cache = EventFlagCache::default();
        let p = manager.as_ptr() as usize;
        assert_eq!(cache.read(p, 8), Some(true));
        assert_eq!(cache.read(p, 9), Some(false));
        assert_eq!(cache.read(p, 15), Some(true));
        node[0] = node.as_ptr() as u64;
        std::hint::black_box(&node);
        cache.reset();
        assert_eq!(cache.read(p, 8), None);
    }

    #[test]
    fn batch_nodes_reduce_os_reads_without_retaining_tree_or_values_across_batches() {
        let mut f = NativeMemory::new();
        for (node, category, storage) in [
            (0xf0200, 2, 0xf0300),
            (0xf0400, 1, 0xf0310),
            (0xf0500, 3, 0xf0320),
        ] {
            f.ptr(node, 0xf0100);
            f.ptr(node + 0x10, 0xf0100);
            f.word(node + 0x20, category);
            f.word(node + 0x28, 2);
            f.ptr(node + 0x30, storage);
        }
        f.ptr(0xf0200, 0xf0400);
        f.ptr(0xf0210, 0xf0500);
        f.bytes[0xf0300] = 0x42;
        f.bytes[0xf0310] = 0x81;
        f.bytes[0xf0320] = 0xa5;
        let manager = f.bytes.as_ptr() as usize + 0xf0000;
        let ids = [8, 9, 15, 16, 17, 23, 24, 25, 31];
        let mut scalar = EventFlagCache::default();
        let mut batch = EventFlagCache::default();
        crate::memory::set_tracking(true);
        let before = crate::memory::syscall_counters();
        let expected: Vec<_> = ids
            .iter()
            .map(|&id| u8::from(scalar.read(manager, id).unwrap()))
            .collect();
        let scalar_reads = crate::memory::syscall_counters().since(before);
        let before = crate::memory::syscall_counters();
        let mut out = [2; 9];
        batch.read_many(manager, &ids, &mut out);
        let batch_reads = crate::memory::syscall_counters().since(before);
        crate::memory::set_tracking(false);
        assert_eq!(out.as_slice(), expected);
        assert!(batch_reads.rpm_calls < scalar_reads.rpm_calls);
        assert_eq!(batch_reads.failed_reads, 0);

        // New bytes are copied even while the category address cache is hot.
        f.bytes[0xf0310] = 0;
        batch.read_many(manager, &[8, 9, 15], &mut out[..3]);
        assert_eq!(out[..3], [0; 3]);
        // A newly requested category must use this batch's tree, not a node
        // snapshot retained from the preceding traversal.
        f.word(0xf0520, 4);
        batch.read_many(manager, &[32], &mut out[..1]);
        assert_eq!(out[0], 1);
        // Existing category addresses retire at the original one-second TTL.
        f.ptr(0xf0430, 0xf0340);
        f.bytes[0xf0340] = 0x80;
        batch.expiry = Some(Instant::now() - Duration::from_secs(1));
        batch.read_many(manager, &[8], &mut out[..1]);
        assert_eq!(out[0], 1);

        let mut next_save = NativeMemory::new();
        next_save.bytes[0xf0300] = 0;
        let next_manager = next_save.bytes.as_ptr() as usize + 0xf0000;
        batch.read_many(next_manager, &[8, 0, u32::MAX, 999], &mut out[..4]);
        assert_eq!(out[..4], [0, 2, 2, 2]);
        batch.read_many(0, &[8], &mut out[..1]);
        assert_eq!(out[0], 2);
        // A corrupt self-cycle remains bounded, even when its node is cached.
        next_save.ptr(0xf0200, 0xf0200);
        batch.read_many(next_manager, &[8], &mut out[..1]);
        assert_eq!(out[0], 2);
    }

    #[cfg(windows)]
    #[test]
    fn cross_page_batch_snapshots_fall_back_only_to_required_readable_fields() {
        use std::ffi::c_void;
        #[link(name = "kernel32")]
        unsafe extern "system" {
            fn VirtualAlloc(
                address: *mut c_void,
                size: usize,
                kind: u32,
                protect: u32,
            ) -> *mut c_void;
            fn VirtualProtect(
                address: *mut c_void,
                size: usize,
                protect: u32,
                old: *mut u32,
            ) -> i32;
            fn VirtualFree(address: *mut c_void, size: usize, kind: u32) -> i32;
        }
        struct Allocation(*mut c_void);
        impl Drop for Allocation {
            fn drop(&mut self) {
                // SAFETY: only this fixture owns the reservation.
                assert_ne!(unsafe { VirtualFree(self.0, 0, 0x8000) }, 0);
            }
        }
        const CHUNK: usize = 64 * 1024;
        // SAFETY: fixture-owned memory, never a game's allocation.
        let allocation =
            Allocation(unsafe { VirtualAlloc(std::ptr::null_mut(), CHUNK * 2, 0x3000, 4) });
        assert!(!allocation.0.is_null());
        let base = allocation.0 as usize;
        let node = base + CHUNK - 0x34;
        let mut manager = [0u64; 8];
        let mut sentinel = [0u64; 8];
        let bytes = [0x81u8];
        let nil = sentinel.as_ptr() as usize;
        manager[3] = 8u64 << 32;
        manager[4] = 1;
        manager[5] = bytes.as_ptr() as u64;
        manager[7] = nil as u64;
        sentinel[1] = node as u64;
        sentinel[3] = 1 << 8;
        // SAFETY: writes stay in the owned first chunk; unaligned fields are
        // intentional. The packed index's four bytes end at the boundary.
        unsafe {
            std::ptr::write_bytes(allocation.0.cast::<u8>(), 0, CHUNK);
            std::ptr::write_unaligned(node as *mut usize, nil);
            std::ptr::write_unaligned((node + 0x10) as *mut usize, nil);
            std::ptr::write_unaligned((node + 0x20) as *mut u32, 1);
            std::ptr::write_unaligned((node + 0x28) as *mut u32, 1);
        }
        let mut old = 0;
        // SAFETY: protects only this fixture's second chunk.
        assert_ne!(
            unsafe { VirtualProtect((base + CHUNK) as *mut c_void, CHUNK, 1, &mut old) },
            0
        );
        let p = manager.as_ptr() as usize;
        let mut cache = EventFlagCache::default();
        assert_eq!(cache.read(p, 8), Some(true));
        cache.reset();
        crate::memory::set_tracking(true);
        let before = crate::memory::syscall_counters();
        let mut out = [2; 3];
        cache.read_many(p, &[8, 9, 15], &mut out);
        let stats = crate::memory::syscall_counters().since(before);
        crate::memory::set_tracking(false);
        assert_eq!(out, [1, 0, 1]);
        assert_eq!(
            stats.failed_reads, 1,
            "one rejected full-node copy, then scalar fallback"
        );

        // A kind-2 pointer needs all eight bytes at +0x30. Its protected half
        // must remain unavailable, never inferred from a partial snapshot.
        unsafe { std::ptr::write_unaligned((node + 0x28) as *mut u32, 2) };
        cache.reset();
        assert_eq!(cache.read(p, 8), None);
        cache.read_many(p, &[8], &mut out[..1]);
        assert_eq!(out[0], 2);

        // Moving the node makes its key unreadable while nil/left stay valid.
        sentinel[1] = (base + CHUNK - 0x20) as u64;
        std::hint::black_box(&sentinel);
        cache.reset();
        assert_eq!(cache.read(p, 8), None);
        cache.read_many(p, &[8], &mut out[..1]);
        assert_eq!(out[0], 2);

        // A manager's unused prefix can be inaccessible while every field the
        // old packed reader needs remains readable. Reuse the reservation in
        // the opposite direction; tree nodes now live on the stack.
        let mut packed_node = [0u64; 8];
        packed_node[0] = nil as u64;
        packed_node[2] = nil as u64;
        packed_node[4] = 1;
        packed_node[5] = 1;
        sentinel[1] = packed_node.as_ptr() as u64;
        let p = base + CHUNK - 0x18;
        // SAFETY: restore the second owned chunk, then protect only the first.
        assert_ne!(
            unsafe { VirtualProtect((base + CHUNK) as *mut c_void, CHUNK, 4, &mut old) },
            0
        );
        assert_ne!(
            unsafe { VirtualProtect(base as *mut c_void, CHUNK, 1, &mut old) },
            0
        );
        // SAFETY: these required manager fields all lie in the writable second
        // chunk. The unreadable header prefix is intentionally never written.
        unsafe {
            std::ptr::write_unaligned((p + 0x1c) as *mut u32, 8);
            std::ptr::write_unaligned((p + 0x20) as *mut u32, 1);
            std::ptr::write_unaligned((p + 0x28) as *mut usize, bytes.as_ptr() as usize);
            std::ptr::write_unaligned((p + 0x38) as *mut usize, nil);
        }
        std::hint::black_box((&sentinel, &packed_node));
        cache.reset();
        assert_eq!(cache.read(p, 8), Some(true));
        cache.reset();
        crate::memory::set_tracking(true);
        let before = crate::memory::syscall_counters();
        cache.read_many(p, &[8, 9, 15], &mut out);
        let stats = crate::memory::syscall_counters().since(before);
        crate::memory::set_tracking(false);
        assert_eq!(out, [1, 0, 1]);
        assert_eq!(
            stats.failed_reads, 1,
            "one rejected manager copy, then scalar fallback"
        );
        // Required invalid manager fields retain their scalar failure result.
        unsafe { std::ptr::write_unaligned((p + 0x20) as *mut u32, 0) };
        cache.reset();
        assert_eq!(cache.read(p, 8), None);
        cache.read_many(p, &[8], &mut out[..1]);
        assert_eq!(out[0], 2);
        // SAFETY: both fields remain in the owned writable second chunk.
        unsafe {
            std::ptr::write_unaligned((p + 0x20) as *mut u32, 1);
            std::ptr::write_unaligned((p + 0x28) as *mut usize, 0);
        }
        cache.reset();
        assert_eq!(cache.read(p, 8), None);
        cache.read_many(p, &[8], &mut out[..1]);
        assert_eq!(out[0], 2);
    }
}
