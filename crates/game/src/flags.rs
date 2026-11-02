use crate::memory::{pointer, read, read_pointer};
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
fn category_storage(
    manager: usize,
    category: u32,
    diagnostic: &mut FlagDiagnostics,
) -> Result<usize, FlagReason> {
    use FlagReason::*;
    let get = |p| read::<u32>(p).ok_or(MemoryRead);
    let get_ptr = |p| read_pointer(p).ok_or(MemoryRead);
    let sentinel = pointer(manager.checked_add(0x38).ok_or(MemoryRead)?);
    if sentinel == 0 {
        return Err(TreeUnavailable);
    }
    let mut node = pointer(sentinel + 8);
    let mut candidate = sentinel;
    let mut terminal = false;
    for _ in 0..64 {
        if node == 0 {
            return Err(TreeUnavailable);
        }
        if read::<u8>(node + 0x19).ok_or(MemoryRead)? != 0 {
            terminal = true;
            break;
        }
        if get(node + 0x20)? >= category {
            candidate = node;
            node = get_ptr(node)?;
        } else {
            node = get_ptr(node + 0x10)?;
        }
    }
    if !terminal {
        return Err(TreeLimit);
    }
    if candidate == sentinel || get(candidate + 0x20)? != category {
        return Ok(0);
    }
    diagnostic.storage_kind = get(candidate + 0x28)?;
    match diagnostic.storage_kind {
        1 => {
            let stride = get(manager + 0x20)?;
            let index = get(candidate + 0x30)?;
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
            let base = pointer(manager + 0x28);
            if base == 0 {
                return Err(StorageUnavailable);
            }
            base.checked_add(offset as usize).ok_or(StorageUnavailable)
        }
        2 => {
            let p = get_ptr(candidate + 0x30)?;
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
        for (&id, value) in ids.iter().zip(out) {
            *value = match self.address_inner(manager, id) {
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
        use FlagReason::*;
        let now = Instant::now();
        if manager != self.manager || self.expiry.is_none_or(|expiry| now >= expiry) {
            self.reset();
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
            let p = category_storage(manager, category, &mut self.diagnostic)?;
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
}
