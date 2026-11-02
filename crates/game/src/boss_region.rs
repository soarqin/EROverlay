//! Optional update-side current play-region service. FieldArea is dynamically
//! located by the existing scanner; only its historical field layout is versioned.
use crate::{
    GameServices,
    boss_runtime::context_identity,
    map,
    memory::{pointer, read},
    scan::PeImage,
};

const FIELD_TYPE: &str = ".?AVFieldArea@CS@@";
const POST_112_FILE_VERSION: u64 = 0x0002_0002_0000_0000;
#[derive(Clone, Copy)]
pub(super) struct RegionLayout {
    offset: usize,
    vtable: usize,
}

fn file_version(image: &PeImage<'_>) -> Option<u64> {
    let (_, resources) = image.section(".rsrc")?;
    const KEY: &[u8] = b"V\0S\0_\0V\0E\0R\0S\0I\0O\0N\0_\0I\0N\0F\0O\0\0\0";
    let mut found = None;
    for key in memchr::memmem::find_iter(resources, KEY) {
        let Some(start) = key.checked_sub(6) else {
            continue;
        };
        let word = |at| {
            resources
                .get(at..at + 2)
                .map(|b| u16::from_le_bytes(b.try_into().unwrap()))
        };
        let dword = |at| {
            resources
                .get(at..at + 4)
                .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
        };
        let length = usize::from(word(start)?);
        let value = (key + KEY.len() + 3) & !3;
        if word(start + 2) != Some(52)
            || word(start + 4) != Some(0)
            || length < value + 52 - start
            || start.checked_add(length)? > resources.len()
            || dword(value) != Some(0xfeef04bd)
            || dword(value + 4) != Some(0x00010000)
        {
            continue;
        }
        let version = (u64::from(dword(value + 8)?) << 32) | u64::from(dword(value + 12)?);
        if version == 0 || found.is_some_and(|prior| prior != version) {
            return None;
        }
        found = Some(version);
    }
    found
}

pub(super) fn scan_layout(image: &[u8], base: usize) -> Option<RegionLayout> {
    let image = PeImage::new(image, base);
    let version = file_version(&image)?;
    let table = image.vtable(FIELD_TYPE);
    if table == 0 {
        return None;
    }
    Some(RegionLayout {
        offset: if version < POST_112_FILE_VERSION {
            0xe4
        } else {
            0xe8
        },
        vtable: base.checked_add(table as usize)?,
    })
}

impl GameServices {
    /// The original native play-region ID, not a packed map block or a title ID.
    /// No flag queries, asset IO, discovery or writes occur in this update call.
    pub fn boss_region(&self, context: u64) -> Option<u32> {
        let layout = self.boss_region_layout?;
        let b = self.bindings?;
        let display = self.read_display_state();
        if b.field == 0
            || display.valid == 0
            || display.loading_or_logo != 0
            || display.full_screen_map != 0
        {
            return None;
        }
        let c = map::context(self.base, &b)?;
        let igt = read::<u32>(c.game_data.checked_add(0xa0)?)?;
        let manager = pointer(self.base.checked_add(b.event_flags as usize)?);
        if igt == 0
            || igt > 3_599_999_999
            || context_identity(&c, manager, self.generation) != context
        {
            return None;
        }
        let slot = self.base.checked_add(b.field as usize)?;
        let field = pointer(slot);
        if field == 0 || read::<usize>(field) != Some(layout.vtable) {
            return None;
        }
        let address = field.checked_add(layout.offset)?;
        let region: u32 = read(address)?;
        if region == 0
            || region == u32::MAX
            || pointer(slot) != field
            || read::<usize>(field) != Some(layout.vtable)
            || read::<u32>(address) != Some(region)
            || pointer(self.base.checked_add(b.menu as usize)?) != c.menu
            || pointer(self.base.checked_add(b.game_data as usize)?) != c.game_data
            || pointer(self.base.checked_add(b.event_flags as usize)?) != manager
            || !map::unchanged(&c, &b.layout)
        {
            return None;
        }
        Some(region)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::runtime_perf::NativeMemory;
    fn resource_pe(version: u64) -> (Vec<u8>, usize) {
        let base = 0x140000000usize;
        let mut b = vec![0; 0x5000];
        let word = |b: &mut [u8], p: usize, v: u16| b[p..p + 2].copy_from_slice(&v.to_le_bytes());
        let dword = |b: &mut [u8], p: usize, v: u32| b[p..p + 4].copy_from_slice(&v.to_le_bytes());
        word(&mut b, 0, 0x5a4d);
        dword(&mut b, 0x3c, 0x80);
        dword(&mut b, 0x80, 0x4550);
        word(&mut b, 0x84, 0x8664);
        word(&mut b, 0x86, 3);
        word(&mut b, 0x94, 0xf0);
        word(&mut b, 0x98, 0x20b);
        dword(&mut b, 0xd0, 0x5000);
        for (i, name, rva) in [
            (0, b".data".as_slice(), 0x1000),
            (1, b".rdata", 0x2000),
            (2, b".rsrc", 0x3000),
        ] {
            let p = 0x188 + i * 40;
            b[p..p + name.len()].copy_from_slice(name);
            dword(&mut b, p + 8, 0x1000);
            dword(&mut b, p + 12, rva);
        }
        b[0x1110..0x1110 + FIELD_TYPE.len()].copy_from_slice(FIELD_TYPE.as_bytes());
        for (p, v) in [(0x2100, 1), (0x210c, 0x1100), (0x2114, 0x2100)] {
            dword(&mut b, p, v);
        }
        b[0x2200..0x2208].copy_from_slice(&(base as u64 + 0x2100).to_le_bytes());
        let key: Vec<_> = "VS_VERSION_INFO\0"
            .encode_utf16()
            .flat_map(u16::to_le_bytes)
            .collect();
        word(&mut b, 0x3100, 92);
        word(&mut b, 0x3102, 52);
        b[0x3106..0x3106 + key.len()].copy_from_slice(&key);
        dword(&mut b, 0x3128, 0xfeef04bd);
        dword(&mut b, 0x312c, 0x10000);
        dword(&mut b, 0x3130, (version >> 32) as u32);
        dword(&mut b, 0x3134, version as u32);
        (b, base)
    }
    #[test]
    fn version_resource_selects_only_the_cpp_field_offset_and_requires_primary_rtti() {
        for (version, offset) in [
            (0x0002_0000_0001_0000, 0xe4),
            (POST_112_FILE_VERSION, 0xe8),
            (0x0003_0063_0000_0000, 0xe8),
        ] {
            let (mut b, base) = resource_pe(version);
            let layout = scan_layout(&b, base).unwrap();
            assert_eq!((layout.offset, layout.vtable), (offset, base + 0x2208));
            b[0x30] ^= 1;
            assert_eq!(
                scan_layout(&b, base).unwrap().offset,
                offset,
                "unknown EXE hash is not a gate"
            );
            b[0x2104] = 8;
            assert!(
                scan_layout(&b, base).is_none(),
                "secondary RTTI cannot prove FieldArea"
            );
        }
        let (mut b, base) = resource_pe(POST_112_FILE_VERSION);
        b[0x3128] = 0;
        assert!(scan_layout(&b, base).is_none());
        assert!(scan_layout(&b[..64], base).is_none());
    }
    #[test]
    fn production_region_requires_fresh_context_and_typed_field_without_flag_reads() {
        let mut f = NativeMemory::new();
        f.word(0x20a0, 123456);
        f.ptr(0x48, 0x8000);
        f.ptr(0x8000, 0x8100);
        f.word(0x80e4, 777777);
        f.word(0x80e8, 6100123);
        let mut service = f.service();
        let context = service.boss_state().unwrap().context;
        assert!(
            service.boss_region(context).is_none(),
            "unproved layout stays unavailable"
        );
        service.boss_region_layout = Some(RegionLayout {
            offset: 0xe8,
            vtable: f.bytes.as_ptr() as usize + 0x8100,
        });
        crate::memory::set_tracking(true);
        let before = crate::memory::syscall_counters();
        assert_eq!(service.boss_region(context), Some(6100123));
        let reads = crate::memory::syscall_counters().since(before);
        crate::memory::set_tracking(false);
        assert!(
            reads.rpm_calls < 50,
            "bounded scalar/context reads only: {reads:?}"
        );
        service.boss_region_layout.as_mut().unwrap().offset = 0xe4;
        assert_eq!(service.boss_region(context), Some(777777));
        service.boss_region_layout.as_mut().unwrap().offset = 0xe8;
        f.ptr(0x2058, 0x3800);
        assert!(service.boss_region(context).is_none(), "save changed");
        f.ptr(0x2058, 0x3000);
        f.ptr(0x8000, 0x8200);
        assert!(service.boss_region(context).is_none(), "wrong object type");
        f.ptr(0x8000, 0x8100);
        f.word(0x80e8, 0);
        assert!(service.boss_region(context).is_none());
        f.word(0x80e8, 6100123);
        service.generation += 1;
        assert!(
            service.boss_region(context).is_none(),
            "reload generation changed"
        );
        service.generation -= 1;
        f.bytes[0x1088 + 61] = 1;
        assert!(service.boss_region(context).is_none(), "full-screen map");
        f.bytes[0x1088 + 61] = 0;
        f.word(0x80e8, u32::MAX);
        assert!(service.boss_region(context).is_none());
        f.word(0x80e8, 6100123);
        f.word(0x20a0, 0);
        assert!(service.boss_region(context).is_none(), "menu/invalid save");
    }
    #[test]
    #[ignore = "archived EXEs as data only; set EROVERLAY_GAME_ARCHIVES"]
    fn archived_cpp_region_layouts_reuse_dynamic_field_scanning() {
        let root = std::path::PathBuf::from(std::env::var_os("EROVERLAY_GAME_ARCHIVES").unwrap());
        let mut n = 0;
        for version in ["1.10.1", "1.12", "1.17.1"] {
            let raw = std::fs::read(root.join(version).join("Game/eldenring.exe")).unwrap();
            let (mut mapped, base) = crate::scan::map_pe(&raw).unwrap();
            let bindings = crate::scan_image(&mapped, base).unwrap();
            let layout = scan_layout(&mapped, base).unwrap();
            assert_ne!(bindings.field, 0);
            assert_eq!(layout.offset, if version == "1.10.1" { 0xe4 } else { 0xe8 });
            mapped[0x30] ^= 1;
            assert_eq!(scan_layout(&mapped, base).unwrap().offset, layout.offset);
            mapped[0x30] ^= 1;
            let delta = 0x2_0000_0000;
            crate::scan::relocate(&mut mapped, delta).unwrap();
            let moved = scan_layout(&mapped, base + delta as usize).unwrap();
            assert_eq!(
                (moved.offset, moved.vtable),
                (layout.offset, layout.vtable + delta as usize)
            );
            println!(
                "boss_region archive={version} field_rva={:x} offset={:x} typed_vtable={:x} unknown_hash=accept aslr=8GiB",
                bindings.field,
                layout.offset,
                layout.vtable - base
            );
            n += 1;
        }
        assert_eq!(n, 3);
    }
}
