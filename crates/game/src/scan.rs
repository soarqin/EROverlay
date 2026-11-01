//! Startup-only discovery on an owned, mapped PE image. No code is executed.
//! The game contracts are ported from afab7e1, not selected by EXE version/hash.
use memchr::memmem;
mod game;
mod rules;

#[derive(Clone, Copy)]
struct Section {
    name: [u8; 8],
    rva: u32,
    size: usize,
}

/// Bounded PE/RTTI view shared by game-service and UI_SCENE discovery.
pub struct PeImage<'a> {
    image: &'a [u8],
    base: usize,
    sections: Vec<Section>,
}
impl<'a> PeImage<'a> {
    pub fn new(image: &'a [u8], base: usize) -> Self {
        let mut view = Self {
            image,
            base,
            sections: Vec::new(),
        };
        view.load_sections();
        view
    }
    fn load_sections(&mut self) {
        let Some(pe) = self
            .read_u32(0x3c)
            .filter(|p| *p > 0 && *p <= i32::MAX as u32)
        else {
            return;
        };
        let Some(nt) = self.bytes(pe, 264) else {
            return;
        };
        let u16_at = |p| u16::from_le_bytes(nt[p..p + 2].try_into().unwrap());
        let u32_at = |p| u32::from_le_bytes(nt[p..p + 4].try_into().unwrap());
        if self.read_u16(0) != Some(0x5a4d)
            || u32_at(0) != 0x4550
            || u16_at(4) != 0x8664
            || u16_at(24) != 0x20b
            || u32_at(80) as usize > self.image.len()
            || u16_at(6) > 96
        {
            return;
        }
        let Some(table) = pe
            .checked_add(24)
            .and_then(|p| p.checked_add(u16_at(20) as u32))
        else {
            return;
        };
        for i in 0..u16_at(6) as u32 {
            let Some(at) = table.checked_add(i * 40) else {
                break;
            };
            let Some(entry) = self.bytes(at, 40) else {
                break;
            };
            self.sections.push(Section {
                name: entry[..8].try_into().unwrap(),
                rva: u32::from_le_bytes(entry[12..16].try_into().unwrap()),
                size: u32::from_le_bytes(entry[8..12].try_into().unwrap()) as usize,
            });
        }
    }
    pub fn bytes(&self, rva: u32, len: usize) -> Option<&'a [u8]> {
        let start = rva as usize;
        self.image.get(start..start.checked_add(len)?)
    }
    pub fn read_u8(&self, rva: u32) -> Option<u8> {
        self.image.get(rva as usize).copied()
    }
    pub fn read_u16(&self, rva: u32) -> Option<u16> {
        Some(u16::from_le_bytes(self.bytes(rva, 2)?.try_into().ok()?))
    }
    pub fn read_u32(&self, rva: u32) -> Option<u32> {
        Some(u32::from_le_bytes(self.bytes(rva, 4)?.try_into().ok()?))
    }
    pub fn read_i32(&self, rva: u32) -> Option<i32> {
        Some(i32::from_le_bytes(self.bytes(rva, 4)?.try_into().ok()?))
    }
    pub fn read_u64(&self, rva: u32) -> Option<u64> {
        Some(u64::from_le_bytes(self.bytes(rva, 8)?.try_into().ok()?))
    }
    pub fn section(&self, name: &str) -> Option<(u32, &'a [u8])> {
        let section = self
            .sections
            .iter()
            .find(|s| s.name.split(|b| *b == 0).next() == Some(name.as_bytes()))?;
        (section.size != 0).then_some(())?;
        Some((section.rva, self.bytes(section.rva, section.size)?))
    }
    pub fn executable(&self, rva: u32) -> bool {
        self.section(".text")
            .is_some_and(|(start, bytes)| rva >= start && ((rva - start) as usize) < bytes.len())
    }
    pub fn matches(&self, rva: u32, query: &str) -> bool {
        let Some(pattern) = Pattern::parse(query) else {
            return false;
        };
        self.bytes(rva, pattern.bytes.len())
            .is_some_and(|bytes| pattern.matches(bytes))
    }
    pub fn scan_all(&self, query: &str, begin: u32, end: u32) -> Vec<u32> {
        let Some((text, bytes)) = self.section(".text") else {
            return Vec::new();
        };
        if begin < text || begin >= end || (end - text) as usize > bytes.len() {
            return Vec::new();
        }
        let Some(pattern) = Pattern::parse(query) else {
            return Vec::new();
        };
        pattern
            .find_all(&bytes[(begin - text) as usize..(end - text) as usize])
            .into_iter()
            .map(|p| begin + p as u32)
            .collect()
    }
    /// Returns zero when absent or ambiguous, including overlapping matches.
    pub fn unique(&self, query: &str, section: &str) -> u32 {
        let Some((rva, bytes)) = self.section(section) else {
            return 0;
        };
        let Some(pattern) = Pattern::parse(query) else {
            return 0;
        };
        let hits = pattern.find_all(bytes);
        if hits.len() == 1 {
            rva + hits[0] as u32
        } else {
            0
        }
    }
    pub fn relative_address(&self, instruction: u32, displacement: u32, size: u32) -> u32 {
        if instruction == 0
            || displacement > size
            || size - displacement < 4
            || self.bytes(instruction, size as usize).is_none()
        {
            return 0;
        }
        let Some(at) = instruction.checked_add(displacement) else {
            return 0;
        };
        let Some(delta) = self.read_i32(at) else {
            return 0;
        };
        let target = instruction as i64 + size as i64 + delta as i64;
        if target > 0 && target <= u32::MAX as i64 && target < (self.image.len() as i64) {
            target as u32
        } else {
            0
        }
    }
    pub fn relative(&self, instruction: u32, displacement: u32, size: u32) -> u32 {
        let target = self.relative_address(instruction, displacement, size);
        if target != 0 && self.executable(target) {
            target
        } else {
            0
        }
    }
    /// MSVC x64 TypeDescriptor -> primary COL -> vtable[-1], including ASLR.
    pub fn vtable(&self, type_name: &str) -> u32 {
        let Some((data_rva, data)) = self.section(".data") else {
            return 0;
        };
        let Some((rdata_rva, rdata)) = self.section(".rdata") else {
            return 0;
        };
        let name = type_name.as_bytes();
        if name.is_empty() {
            return 0;
        }
        let Some(hit) = memmem::find(data, name) else {
            return 0;
        };
        if hit < 16
            || hit + name.len() >= data.len()
            || data[hit + name.len()] != 0
            || memmem::find(&data[hit + name.len()..], name).is_some()
        {
            return 0;
        }
        let descriptor = data_rva + hit as u32 - 16;
        let mut result = 0;
        for offset in (0..rdata.len().saturating_sub(23)).step_by(4) {
            let word = |i: usize| {
                u32::from_le_bytes(
                    rdata[offset + i * 4..offset + i * 4 + 4]
                        .try_into()
                        .unwrap(),
                )
            };
            if word(0) != 1
                || word(1) != 0
                || word(3) != descriptor
                || word(5) != rdata_rva + offset as u32
            {
                continue;
            }
            let Some(locator) = self.base.checked_add(rdata_rva as usize + offset) else {
                return 0;
            };
            for p in (0..rdata.len().saturating_sub(15)).step_by(8) {
                let pointer = u64::from_le_bytes(rdata[p..p + 8].try_into().unwrap());
                if pointer != locator as u64 {
                    continue;
                }
                if result != 0 {
                    return 0;
                }
                result = rdata_rva + p as u32 + 8;
            }
        }
        result
    }
}

struct Pattern {
    bytes: Vec<Option<u8>>,
    anchor: std::ops::Range<usize>,
}
impl Pattern {
    fn parse(query: &str) -> Option<Self> {
        let bytes = query
            .split_ascii_whitespace()
            .map(|token| {
                if matches!(token, "?" | "??") {
                    Some(None)
                } else if token.len() == 2 {
                    u8::from_str_radix(token, 16).ok().map(Some)
                } else {
                    None
                }
            })
            .collect::<Option<Vec<_>>>()?;
        if bytes.is_empty() {
            return None;
        }
        let mut anchor = 0..0;
        let mut start = 0;
        for end in 0..=bytes.len() {
            if end == bytes.len() || bytes[end].is_none() {
                if end - start > anchor.len() {
                    anchor = start..end;
                }
                start = end + 1;
            }
        }
        Some(Self { bytes, anchor })
    }
    fn matches(&self, bytes: &[u8]) -> bool {
        bytes.len() >= self.bytes.len()
            && self
                .bytes
                .iter()
                .zip(bytes)
                .all(|(expected, actual)| expected.is_none_or(|expected| expected == *actual))
    }
    fn find_all(&self, bytes: &[u8]) -> Vec<usize> {
        if bytes.len() < self.bytes.len() {
            return Vec::new();
        }
        if self.anchor.is_empty() {
            return (0..=bytes.len() - self.bytes.len()).collect();
        }
        let anchor = self.bytes[self.anchor.clone()]
            .iter()
            .map(|b| b.unwrap())
            .collect::<Vec<_>>();
        let finder = memmem::Finder::new(&anchor);
        let mut next = 0;
        let mut result = Vec::new();
        while let Some(hit) = finder.find(&bytes[next..]) {
            let hit = hit + next;
            if let Some(start) = hit.checked_sub(self.anchor.start)
                && start + self.bytes.len() <= bytes.len()
                && self.matches(&bytes[start..])
            {
                result.push(start);
            }
            next = hit + 1;
        }
        result
    }
}

pub fn resolve(image: &[u8], base: usize) -> Result<crate::Bindings, String> {
    game::resolve(&PeImage::new(image, base))
}

fn u16_at(b: &[u8], p: usize) -> Option<u16> {
    Some(u16::from_le_bytes(
        b.get(p..p.checked_add(2)?)?.try_into().ok()?,
    ))
}
fn u32_at(b: &[u8], p: usize) -> Option<u32> {
    Some(u32::from_le_bytes(
        b.get(p..p.checked_add(4)?)?.try_into().ok()?,
    ))
}
fn u64_at(b: &[u8], p: usize) -> Option<u64> {
    Some(u64::from_le_bytes(
        b.get(p..p.checked_add(8)?)?.try_into().ok()?,
    ))
}
pub fn map_pe(raw: &[u8]) -> Option<(Vec<u8>, usize)> {
    if u16_at(raw, 0)? != 0x5a4d {
        return None;
    }
    let pe = u32_at(raw, 0x3c)? as usize;
    if u32_at(raw, pe)? != 0x4550
        || u16_at(raw, pe + 4)? != 0x8664
        || u16_at(raw, pe + 24)? != 0x20b
    {
        return None;
    }
    let sections = u16_at(raw, pe + 6)? as usize;
    if sections > 96 {
        return None;
    }
    let optional_size = u16_at(raw, pe + 20)? as usize;
    let image_size = u32_at(raw, pe + 24 + 56)? as usize;
    let headers = u32_at(raw, pe + 24 + 60)? as usize;
    if image_size > 512 * 1024 * 1024 || headers > image_size {
        return None;
    }
    let base = u64_at(raw, pe + 24 + 24)? as usize;
    let mut image = vec![0u8; image_size];
    image[..headers].copy_from_slice(raw.get(..headers)?);
    for i in 0..sections {
        let p = pe + 24 + optional_size + i * 40;
        let target = u32_at(raw, p + 12)? as usize;
        let count = u32_at(raw, p + 16)? as usize;
        let source = u32_at(raw, p + 20)? as usize;
        image
            .get_mut(target..target.checked_add(count)?)?
            .copy_from_slice(raw.get(source..source.checked_add(count)?)?);
    }
    Some((image, base))
}

pub fn relocate(image: &mut [u8], delta: u64) -> Option<()> {
    let pe = u32_at(image, 0x3c)? as usize;
    let reloc = pe + 24 + 112 + 5 * 8;
    let mut cursor = u32_at(image, reloc)? as usize;
    let end = cursor.checked_add(u32_at(image, reloc + 4)? as usize)?;
    if cursor == 0 || end > image.len() {
        return None;
    }
    while cursor < end {
        let page = u32_at(image, cursor)? as usize;
        let count = u32_at(image, cursor + 4)? as usize;
        if count < 8 || !count.is_multiple_of(2) || count > end - cursor {
            return None;
        }
        for p in (cursor + 8..cursor + count).step_by(2) {
            let entry = u16_at(image, p)?;
            if entry >> 12 == 0 {
                continue;
            }
            if entry >> 12 != 10 {
                return None;
            }
            let address = page.checked_add((entry & 0xfff) as usize)?;
            let value = u64_at(image, address)?.checked_add(delta)?;
            image
                .get_mut(address..address + 8)?
                .copy_from_slice(&value.to_le_bytes());
        }
        cursor += count;
    }
    Some(())
}

#[cfg(test)]
mod tests {
    use super::*;
    fn put16(bytes: &mut [u8], at: usize, value: u16) {
        bytes[at..at + 2].copy_from_slice(&value.to_le_bytes());
    }
    fn put32(bytes: &mut [u8], at: usize, value: u32) {
        bytes[at..at + 4].copy_from_slice(&value.to_le_bytes());
    }
    fn put64(bytes: &mut [u8], at: usize, value: u64) {
        bytes[at..at + 8].copy_from_slice(&value.to_le_bytes());
    }
    fn image() -> Vec<u8> {
        let mut image = vec![0; 0x10000];
        put16(&mut image, 0, 0x5a4d);
        put32(&mut image, 0x3c, 0x80);
        put32(&mut image, 0x80, 0x4550);
        put16(&mut image, 0x84, 0x8664);
        put16(&mut image, 0x86, 4);
        put16(&mut image, 0x94, 240);
        put16(&mut image, 0x98, 0x20b);
        put32(&mut image, 0xd0, 0x10000);
        for (i, name, rva) in [
            (0, ".text", 0x1000),
            (1, ".data", 0x4000),
            (2, ".rdata", 0x6000),
            (3, ".pdata", 0x8000),
        ] {
            let at = 0x188 + i * 40;
            image[at..at + name.len()].copy_from_slice(name.as_bytes());
            put32(&mut image, at + 8, 0x1000);
            put32(&mut image, at + 12, rva);
        }
        image
    }
    #[test]
    fn bounded_sections_overlapping_patterns_and_rip_calls() {
        let mut bytes = image();
        bytes[0x1000..0x1003].fill(0xaa);
        bytes[0x1010] = 0xe8;
        put32(&mut bytes, 0x1011, (0x1100i32 - 0x1015) as u32);
        let view = PeImage::new(&bytes, 0x140000000);
        assert_eq!(view.section(".text").unwrap().0, 0x1000);
        assert_eq!(view.scan_all("AA AA", 0x1000, 0x1003), [0x1000, 0x1001]);
        assert_eq!(view.unique("AA AA", ".text"), 0);
        assert!(view.matches(0x1000, "AA ?? AA"));
        assert!(!view.matches(0xffff, "00 00"));
        assert_eq!(view.relative(0x1010, 1, 5), 0x1100);
        assert_eq!(view.relative_address(0, 1, 5), 0);
        assert_eq!(view.relative_address(u32::MAX, 3, 7), 0);
        assert_eq!(view.relative_address(0x1010, 4, 5), 0);
        assert!(PeImage::new(&bytes[..0x90], 0).section(".text").is_none());
        assert!(Pattern::parse("GG").is_none());
        assert_eq!(
            Pattern::parse("?? ??").unwrap().find_all(&[1, 2, 3]),
            [0, 1]
        );
    }
    #[test]
    fn rtti_requires_unique_descriptor_and_primary_locator_with_aslr() {
        let mut bytes = image();
        let name = b".?AVWorldMapViewModel@CS@@";
        bytes[0x4110..0x4110 + name.len()].copy_from_slice(name);
        put32(&mut bytes, 0x6100, 1);
        put32(&mut bytes, 0x610c, 0x4100);
        put32(&mut bytes, 0x6114, 0x6100);
        let base = 0x140000000;
        put64(&mut bytes, 0x6200, base + 0x6100);
        let name = std::str::from_utf8(name).unwrap();
        assert_eq!(PeImage::new(&bytes, base as usize).vtable(name), 0x6208);
        assert_eq!(
            PeImage::new(&bytes, (base + 0x200000000) as usize).vtable(name),
            0
        );
        put64(&mut bytes, 0x6200, base + 0x200000000 + 0x6100);
        assert_eq!(
            PeImage::new(&bytes, (base + 0x200000000) as usize).vtable(name),
            0x6208
        );
        put64(&mut bytes, 0x6210, base + 0x200000000 + 0x6100);
        assert_eq!(
            PeImage::new(&bytes, (base + 0x200000000) as usize).vtable(name),
            0
        );
    }
    #[test]
    fn all_generated_rules_have_valid_two_anchor_and_body_patterns() {
        for rule in rules::RULES {
            for (query, _) in rule.anchors {
                assert!(Pattern::parse(query).is_some());
            }
            if let Some(body) = rule.body {
                let body = Pattern::parse(body).unwrap();
                for &call in rule.calls {
                    assert_eq!(body.bytes[call as usize], Some(0xe8));
                    assert!(call as usize + 5 <= body.bytes.len());
                }
            }
        }
        assert_eq!(rules::ROLES.len(), rules::ROLE_NAMES.len());
    }
    #[cfg(feature = "runtime-read-fixture")]
    fn cpp_scan(image: &[u8], base: usize) -> Result<crate::Bindings, String> {
        unsafe extern "C" {
            fn er_cpp_scan_fixture(
                image: *const u8,
                length: usize,
                base: usize,
                bindings: *mut crate::Bindings,
                error: *mut u8,
                capacity: usize,
            ) -> bool;
        }
        let mut bindings = crate::Bindings::default();
        let mut error = [0; 1024];
        // SAFETY: owned mapped PE and correctly sized live output buffers.
        if unsafe {
            er_cpp_scan_fixture(
                image.as_ptr(),
                image.len(),
                base,
                &mut bindings,
                error.as_mut_ptr(),
                error.len(),
            )
        } {
            Ok(bindings)
        } else {
            Err(String::from_utf8_lossy(
                &error[..error.iter().position(|b| *b == 0).unwrap_or(error.len())],
            )
            .into_owned())
        }
    }
    #[cfg(feature = "runtime-read-fixture")]
    fn contract(b: &crate::Bindings) -> [u32; 23] {
        let l = &b.layout;
        [
            b.submit,
            b.flush,
            b.enqueue,
            b.cancel,
            b.manager,
            b.allocator,
            b.retire_pool_owner,
            b.menu,
            b.game_data,
            b.event_flags,
            b.field,
            b.repository,
            b.reveal,
            l.size,
            l.menu_owner_offset,
            l.owner_view_offset,
            l.menu_info_offset,
            l.screen_state_offset,
            l.grace_stride,
            l.grace_normal_offset,
            l.alternate_icons,
            l.map_mask,
            l.repository_groups,
        ]
    }
    #[cfg(feature = "runtime-read-fixture")]
    #[test]
    #[ignore = "archived PE differential against explicitly exported C++ reference; no game execution"]
    fn cpp_scanner_matches_archived_executables() {
        use std::{path::PathBuf, time::Instant};
        let archives = PathBuf::from(
            std::env::var_os("EROVERLAY_GAME_ARCHIVES")
                .unwrap_or_else(|| "D:/Games/EldenRingExe".into()),
        );
        let mut inputs = std::fs::read_dir(&archives)
            .expect("set EROVERLAY_GAME_ARCHIVES to the archived EXE directory")
            .map(|e| e.unwrap().path())
            .collect::<Vec<_>>();
        inputs.sort();
        let mut count = 0;
        println!(
            "rust_scan_evidence version,result,scan_ms,submit,flush,enqueue,cancel,manager,allocator,retire_pool_owner,menu,game_data,event_flags,field,repository,reveal,layout_size,menu_owner_offset,owner_view_offset,menu_info_offset,screen_state_offset,grace_stride,grace_normal_offset,alternate_icons,map_mask,repository_groups"
        );
        for directory in inputs {
            let Some(exe) = [
                directory.join("eldenring.exe"),
                directory.join("Game/eldenring.exe"),
                directory.join("Game/ELDENRING.exe"),
            ]
            .into_iter()
            .find(|p| p.is_file()) else {
                continue;
            };
            let raw = std::fs::read(&exe).unwrap();
            let (mut image, base) = map_pe(&raw).expect("valid archived PE");
            let cpp =
                cpp_scan(&image, base).unwrap_or_else(|e| panic!("{} C++: {e}", exe.display()));
            let start = Instant::now();
            let rust =
                resolve(&image, base).unwrap_or_else(|e| panic!("{} Rust: {e}", exe.display()));
            let elapsed = start.elapsed().as_secs_f64() * 1000.0;
            assert_eq!(contract(&rust), contract(&cpp), "{}", exe.display());
            let name = directory.file_name().unwrap().to_string_lossy();
            println!(
                "rust_scan_evidence {name},PASS,{elapsed:.3},{}",
                contract(&rust)
                    .iter()
                    .map(|v| format!("{v:x}"))
                    .collect::<Vec<_>>()
                    .join(",")
            );
            // Full migrated scanner must keep C++ refusal and ASLR contracts too.
            image[rust.submit as usize] ^= 1;
            assert!(resolve(&image, base).is_err());
            assert!(cpp_scan(&image, base).is_err());
            image[rust.submit as usize] ^= 1;
            image[0x30] ^= 1;
            assert_eq!(contract(&resolve(&image, base).unwrap()), contract(&rust));
            image[0x30] ^= 1;
            let delta = 0x2_0000_0000;
            relocate(&mut image, delta).expect("valid relocation directory");
            assert_eq!(
                contract(&resolve(&image, base + delta as usize).unwrap()),
                contract(&rust)
            );
            assert_eq!(
                contract(&cpp_scan(&image, base + delta as usize).unwrap()),
                contract(&rust)
            );
            count += 1;
        }
        assert!(count > 0, "no archived EXEs found");
        println!(
            "rust_scan_differential PASS versions={count} all_bindings=23 altered_abi=reject unknown_hash=accept aslr_delta=8GiB"
        );
    }
}
