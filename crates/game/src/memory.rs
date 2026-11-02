//! Reads use OS copies, never references into mutable game memory.
use std::cell::Cell;
use std::ffi::c_void;

/// Optional per-thread counters. Sampling never locks or formats strings in a
/// read, and one consumer cannot reset another thread's diagnostic interval.
#[derive(Clone, Copy, Debug, Default)]
pub struct ReadStats {
    pub rpm_calls: u64,
    pub virtual_queries: u64,
    pub bytes: u64,
    pub failed_reads: u64,
}
impl ReadStats {
    pub fn since(self, earlier: Self) -> Self {
        Self {
            rpm_calls: self.rpm_calls.wrapping_sub(earlier.rpm_calls),
            virtual_queries: self.virtual_queries.wrapping_sub(earlier.virtual_queries),
            bytes: self.bytes.wrapping_sub(earlier.bytes),
            failed_reads: self.failed_reads.wrapping_sub(earlier.failed_reads),
        }
    }
}
thread_local! {
    static COUNTERS: Cell<Option<ReadStats>> = const { Cell::new(None) };
}
pub fn set_tracking(enabled: bool) {
    COUNTERS.with(|cell| {
        if enabled {
            if cell.get().is_none() {
                cell.set(Some(ReadStats::default()));
            }
        } else {
            cell.set(None);
        }
    });
}
pub fn syscall_counters() -> ReadStats {
    COUNTERS.with(|cell| cell.get().unwrap_or_default())
}
fn record_read(called: bool, bytes: usize, success: bool) {
    COUNTERS.with(|cell| {
        if let Some(mut stats) = cell.get() {
            stats.rpm_calls = stats.rpm_calls.wrapping_add(u64::from(called));
            stats.bytes = stats.bytes.wrapping_add(bytes as u64);
            stats.failed_reads = stats.failed_reads.wrapping_add(u64::from(!success));
            cell.set(Some(stats));
        }
    });
}

#[repr(C)]
#[derive(Default)]
struct MemoryBasicInformation {
    base_address: usize,
    allocation_base: usize,
    allocation_protect: u32,
    partition_id: u16,
    reserved: u16,
    region_size: usize,
    state: u32,
    protect: u32,
    kind: u32,
}
#[cfg(windows)]
#[link(name = "kernel32")]
unsafe extern "system" {
    fn GetCurrentProcess() -> isize;
    fn ReadProcessMemory(
        process: isize,
        address: *const c_void,
        buffer: *mut c_void,
        size: usize,
        copied: *mut usize,
    ) -> i32;
    fn VirtualQuery(
        address: *const c_void,
        info: *mut MemoryBasicInformation,
        length: usize,
    ) -> usize;
    fn GetModuleHandleW(name: *const u16) -> usize;
}

fn query(address: usize) -> Option<MemoryBasicInformation> {
    COUNTERS.with(|cell| {
        if let Some(mut stats) = cell.get() {
            stats.virtual_queries = stats.virtual_queries.wrapping_add(1);
            cell.set(Some(stats));
        }
    });
    let mut info = MemoryBasicInformation::default();
    #[cfg(windows)]
    // SAFETY: info is a writable, correctly sized platform structure.
    if unsafe {
        VirtualQuery(
            address as *const _,
            &mut info,
            size_of::<MemoryBasicInformation>(),
        )
    } != 0
    {
        return Some(info);
    }
    #[cfg(not(windows))]
    let _ = address;
    None
}
fn readable(info: &MemoryBasicInformation) -> bool {
    info.state == 0x1000
        && info.protect & 0x100 == 0
        && info.protect & (2 | 4 | 8 | 0x20 | 0x40 | 0x80) != 0
}
/// Used only by an explicit flag mutation after every target has been resolved.
pub(crate) fn writable_byte(address: usize) -> bool {
    query(address).is_some_and(|info| {
        info.state == 0x1000
            && info.protect & 0x100 == 0
            && info.protect & (4 | 8 | 0x40 | 0x80) != 0
    })
}
pub fn read_memory(address: usize, output: &mut [u8]) -> bool {
    if output.is_empty() {
        return true;
    }
    let Some(end) = address.checked_add(output.len()) else {
        record_read(false, 0, false);
        return false;
    };
    if address == 0 || end > 0x0000_8000_0000_0000 {
        record_read(false, 0, false);
        return false;
    }
    #[cfg(windows)]
    {
        let mut copied = 0;
        // SAFETY: Windows validates the entire source range in RPM itself. The
        // original C++ readers use the same OS-copy contract. A separate VQ
        // cannot prevent an unmap/protection race between validation and copy,
        // and previously added a system call to every scalar field. Never form
        // a Rust reference or perform a naked dereference of game-owned memory.
        let success = unsafe {
            ReadProcessMemory(
                GetCurrentProcess(),
                address as *const _,
                output.as_mut_ptr().cast(),
                output.len(),
                &mut copied,
            )
        } != 0
            && copied == output.len();
        record_read(true, copied, success);
        success
    }
    #[cfg(not(windows))]
    {
        record_read(false, 0, false);
        false
    }
}
/// Only integer/float POD representations may be copied from untrusted memory.
pub trait GamePod: Copy + Default {
    const SIZE: usize;
    fn decode(bytes: &[u8]) -> Self;
}
macro_rules! pod {
    ($($ty:ty),*)=>{$(impl GamePod for $ty {const SIZE:usize=size_of::<Self>();fn decode(bytes:&[u8])->Self {Self::from_le_bytes(bytes.try_into().unwrap())}})*};
}
pod!(u16, u32, u64, i16, i32, i64, f32, f64, usize);
impl GamePod for u8 {
    const SIZE: usize = 1;
    fn decode(bytes: &[u8]) -> Self {
        bytes[0]
    }
}
pub fn read<T: GamePod>(address: usize) -> Option<T> {
    let mut bytes = [0u8; 8];
    read_memory(address, &mut bytes[..T::SIZE]).then(|| T::decode(&bytes[..T::SIZE]))
}
pub fn pointer(address: usize) -> usize {
    read_pointer(address).unwrap_or(0)
}
pub fn read_pointer(address: usize) -> Option<usize> {
    let pointer = read::<usize>(address)?;
    // Windows user pointers are canonical; rejecting upper-half values also
    // prevents corrupted pointer arithmetic from wrapping before the OS copy.
    (pointer <= 0x0000_7fff_ffff_ffff).then_some(pointer)
}
pub fn main_module() -> usize {
    #[cfg(windows)]
    {
        // SAFETY: null selects the executable; no dereference occurs.
        unsafe { GetModuleHandleW(std::ptr::null()) }
    }
    #[cfg(not(windows))]
    {
        0
    }
}
pub fn image_snapshot(base: usize) -> Result<Vec<u8>, String> {
    let mz = read::<u16>(base).ok_or("cannot read executable header")?;
    let pe = read::<u32>(base.checked_add(0x3c).ok_or("header overflow")?)
        .ok_or("missing PE offset")? as usize;
    if mz != 0x5a4d
        || !(64..=1024 * 1024).contains(&pe)
        || read::<u32>(base + pe) != Some(0x4550)
        || read::<u16>(base + pe + 24) != Some(0x20b)
    {
        return Err("invalid x64 executable header".into());
    }
    let length = read::<u32>(base + pe + 24 + 56).ok_or("missing image size")? as usize;
    if !(4096..=1024 * 1024 * 1024).contains(&length) {
        return Err("executable image exceeds scan budget".into());
    }
    let mut image = vec![0u8; length];
    let mut cursor = 0;
    while cursor < length {
        let info = query(base + cursor).ok_or("cannot query executable memory")?;
        let next = info
            .base_address
            .checked_add(info.region_size)
            .and_then(|v| v.checked_sub(base))
            .ok_or("image region overflow")?
            .min(length);
        if next <= cursor {
            return Err("invalid executable region".into());
        }
        if readable(&info) && !read_memory(base + cursor, &mut image[cursor..next]) {
            return Err("executable changed while scanning".into());
        }
        cursor = next;
    }
    Ok(image)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn copies_and_rejects_null() {
        let value = 0x1234_5678u32;
        assert_eq!(read::<u32>(&value as *const _ as usize), Some(value));
        assert_eq!(read::<u32>(0), None);
        assert!(!read_memory(usize::MAX, &mut [0; 8]));
        let invalid = usize::MAX;
        assert_eq!(read_pointer(&invalid as *const _ as usize), None);
    }
    #[cfg(windows)]
    #[test]
    fn os_copy_rejects_protection_changes_guard_and_decommitted_ranges() {
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
        // Use allocation-granularity multiples, independent of page size.
        const CHUNK: usize = 64 * 1024;
        // SAFETY: creates fixture-owned readable/writable memory, never game memory.
        let allocation =
            Allocation(unsafe { VirtualAlloc(std::ptr::null_mut(), CHUNK * 2, 0x3000, 4) });
        assert!(!allocation.0.is_null());
        let address = allocation.0 as usize;
        unsafe {
            std::ptr::write_bytes(allocation.0.cast::<u8>(), 0x5a, CHUNK * 2);
        }
        set_tracking(true);
        let before = syscall_counters();
        assert_eq!(read::<u32>(address), Some(0x5a5a5a5a));
        let tail = (address + CHUNK) as *mut c_void;
        let mut old = 0;
        // SAFETY: changes protection only within the owned reservation.
        assert_ne!(unsafe { VirtualProtect(tail, CHUNK, 1, &mut old) }, 0);
        assert!(!read_memory(address + CHUNK - 8, &mut [0u8; 16]));
        assert_eq!(read::<u8>(address + CHUNK), None);
        assert_ne!(
            unsafe { VirtualProtect(tail, CHUNK, 4 | 0x100, &mut old) },
            0
        );
        assert_eq!(read::<u8>(address + CHUNK), None);
        assert_ne!(unsafe { VirtualProtect(tail, CHUNK, 4, &mut old) }, 0);
        assert_eq!(read::<u8>(address + CHUNK), Some(0x5a));
        assert_ne!(unsafe { VirtualFree(tail, CHUNK, 0x4000) }, 0);
        assert_eq!(read::<u8>(address + CHUNK), None);
        let stats = syscall_counters().since(before);
        assert_eq!(stats.rpm_calls, 6);
        assert_eq!(stats.virtual_queries, 0);
        assert_eq!(stats.failed_reads, 4);
        set_tracking(false);
    }
}
