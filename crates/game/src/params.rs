use crate::memory::{read, read_memory};
use std::sync::Arc;
struct Snapshot {
    base: usize,
    bytes: Vec<u8>,
}
pub struct ParamRow {
    pub id: u32,
    pub address: usize,
    pub size: usize,
    snapshot: Option<Arc<Snapshot>>,
}
pub fn param_rows(table: usize) -> Option<Vec<ParamRow>> {
    const MAX: usize = 0x1000_0000;
    let mut header = [0u8; 48];
    if !read_memory(table, &mut header) || header[0x2c] != 0 {
        return None;
    }
    let count = u16::from_le_bytes(header[10..12].try_into().ok()?) as usize;
    let strings = u32::from_le_bytes(header[..4].try_into().ok()?) as usize;
    let format = header[0x2d] & 0x7f;
    if count == 0 || count > 20000 || !(2..=5).contains(&format) {
        return None;
    }
    let start = if format == 2 { 0x30 } else { 0x40 };
    let wide = format >= 4 && header[0x2e] & 2 != 0 && (format == 4 || header[0x2d] & 0x80 != 0);
    let stride = if wide { 24 } else { 12 };
    let end = start + count * stride;
    table.checked_add(MAX)?;
    let mut entries = Vec::with_capacity(count);
    let mut boundaries = Vec::with_capacity(count * 2 + 2);
    if (end..=MAX).contains(&strings) {
        boundaries.push(strings);
    }
    let file_size = table
        .checked_sub(16)
        .and_then(read::<u32>)
        .map(|n| n as usize)
        .filter(|n| (end..=MAX).contains(n));
    if let Some(size) = file_size {
        boundaries.push(size);
    }
    // Match util::ParamRows: copy a bounded native table once, so reading each
    // selected field does not issue another operating-system memory call.
    let snapshot = file_size
        .filter(|n| *n <= 4 * 1024 * 1024)
        .and_then(|size| {
            let mut bytes = vec![0; size];
            read_memory(table, &mut bytes).then(|| Arc::new(Snapshot { base: table, bytes }))
        });
    let mut live_directory = Vec::new();
    let directory = if let Some(s) = &snapshot {
        s.bytes.get(start..end)?
    } else {
        live_directory.resize(count * stride, 0);
        if !read_memory(table + start, &mut live_directory) {
            return None;
        }
        &live_directory
    };
    for entry in directory.chunks_exact(stride) {
        let id = u32::from_le_bytes(entry[..4].try_into().ok()?);
        let (offset, name) = if wide {
            (
                u64::from_le_bytes(entry[8..16].try_into().ok()?) as usize,
                u64::from_le_bytes(entry[16..24].try_into().ok()?) as usize,
            )
        } else {
            (
                u32::from_le_bytes(entry[4..8].try_into().ok()?) as usize,
                u32::from_le_bytes(entry[8..12].try_into().ok()?) as usize,
            )
        };
        if offset < end || offset > MAX || name > MAX || (name != 0 && name < end) {
            return None;
        }
        boundaries.push(offset);
        if name != 0 {
            boundaries.push(name);
        }
        entries.push((id, offset));
    }
    boundaries.sort_unstable();
    Some(
        entries
            .into_iter()
            .map(|(id, offset)| {
                let next = boundaries.partition_point(|x| *x <= offset);
                ParamRow {
                    id,
                    address: table + offset,
                    size: boundaries.get(next).map_or(0, |v| v - offset),
                    snapshot: snapshot.clone(),
                }
            })
            .collect(),
    )
}
impl ParamRow {
    pub fn field<T: crate::memory::GamePod>(&self, offset: usize) -> Option<T> {
        if offset <= self.size && T::SIZE <= self.size - offset {
            let address = self.address.checked_add(offset)?;
            if let Some(snapshot) = &self.snapshot
                && let Some(offset) = address.checked_sub(snapshot.base)
                && let Some(bytes) = snapshot.bytes.get(offset..offset.checked_add(T::SIZE)?)
            {
                return Some(T::decode(bytes));
            }
            read(address)
        } else {
            None
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn fixture(format: u8, extra: u8, sized: bool) -> Vec<u8> {
        let mut v = vec![0; 16 + 512];
        if sized {
            v[..4].copy_from_slice(&512u32.to_le_bytes());
        }
        let b = &mut v[16..];
        b[..4].copy_from_slice(&0x1f0u32.to_le_bytes());
        b[10..12].copy_from_slice(&2u16.to_le_bytes());
        b[0x2d] = format;
        b[0x2e] = extra;
        let start = if format & 0x7f == 2 { 0x30 } else { 0x40 };
        let wide =
            format & 0x7f >= 4 && extra & 2 != 0 && (format & 0x7f == 4 || format & 0x80 != 0);
        for i in 0..2 {
            let p = start + i * if wide { 24 } else { 12 };
            b[p..p + 4].copy_from_slice(&(i as u32).to_le_bytes());
            let at = p + if wide { 8 } else { 4 };
            b[at..at + 4].copy_from_slice(&(0x100u32 + i as u32 * 8).to_le_bytes());
            b[0x104 + i * 8..0x108 + i * 8].copy_from_slice(&(17u32 + i as u32).to_le_bytes());
        }
        v
    }
    #[test]
    fn native_format_variants_snapshot_selected_fields_without_more_os_reads() {
        for (format, extra) in [(2, 0), (3, 0), (4, 0), (4, 2), (5, 2), (0x85, 2)] {
            let mut bytes = fixture(format, extra, true);
            let p = bytes.as_ptr() as usize + 16;
            crate::memory::set_tracking(true);
            let before = crate::memory::syscall_counters();
            let rows = param_rows(p).unwrap();
            let copied = crate::memory::syscall_counters().since(before);
            assert_eq!(
                copied.rpm_calls, 3,
                "header, native length, one bounded table copy"
            );
            assert_eq!(copied.virtual_queries, 0);
            bytes[16 + 0x104..16 + 0x108].copy_from_slice(&99u32.to_le_bytes());
            let before = crate::memory::syscall_counters();
            assert_eq!(rows[0].field::<u32>(4), Some(17));
            assert_eq!(rows[1].field::<u32>(4), Some(18));
            assert_eq!(crate::memory::syscall_counters().since(before).rpm_calls, 0);
            crate::memory::set_tracking(false);
        }
    }
    #[test]
    fn unavailable_size_preserves_live_field_reads_and_nameless_bounds() {
        let mut bytes = fixture(0x84, 2, false);
        let p = bytes.as_ptr() as usize + 16;
        let rows = param_rows(p).unwrap();
        bytes[16 + 0x104..16 + 0x108].copy_from_slice(&99u32.to_le_bytes());
        assert_eq!(rows[0].field::<u32>(4), Some(99));
        assert_eq!(rows[1].size, 0x1f0 - 0x108);
        bytes[16..20].fill(0); // Neither native length nor strings bound last row.
        let rows = param_rows(p).unwrap();
        assert_eq!(rows[1].size, 0);
        assert_eq!(rows[1].field::<u32>(4), None);
    }
    #[test]
    fn row_boundaries_use_next_name_not_readable_page() {
        let mut allocation = vec![0u8; 16 + 256];
        let b = &mut allocation[16..];
        b[..4].copy_from_slice(&0xd0u32.to_le_bytes());
        b[10..12].copy_from_slice(&2u16.to_le_bytes());
        b[0x2d] = 2;
        for (i, id, off, name) in [(0, 3u32, 0x80u32, 0x88u32), (1, 7, 0x90, 0)] {
            let p = 0x30 + i * 12;
            b[p..p + 4].copy_from_slice(&id.to_le_bytes());
            b[p + 4..p + 8].copy_from_slice(&off.to_le_bytes());
            b[p + 8..p + 12].copy_from_slice(&name.to_le_bytes());
        }
        b[0x84..0x88].copy_from_slice(&101u32.to_le_bytes());
        let ptr = b.as_ptr() as usize;
        let rows = param_rows(ptr).unwrap();
        assert_eq!((rows[0].id, rows[0].size), (3, 8));
        assert_eq!(rows[0].field::<u32>(4), Some(101));
        assert_eq!(rows[0].field::<u64>(4), None);
        assert_eq!(rows[1].size, 0x40);
        allocation[16 + 0x34..16 + 0x38].copy_from_slice(&1u32.to_le_bytes());
        assert!(param_rows(ptr).is_none());
    }
}
