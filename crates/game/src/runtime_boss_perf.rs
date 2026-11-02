// Opt-in comparison against the unmodified C++ BossDataSet::update(), rather
// than the Minimap's public event reader. The native allocation is shared by
// both implementations; this is an update-component profile, not game FPS.
mod boss_profile {
    use super::NativeMemory;
    use crate::memory;
    use std::{
        collections::BTreeSet, ffi::c_void, os::windows::ffi::OsStrExt, path::Path, time::Instant,
    };

    unsafe extern "C" {
        fn er_cpp_boss_fixture(base: usize, directory: *const u16) -> *mut c_void;
        fn er_cpp_drop_boss_fixture(fixture: *mut c_void);
        fn er_cpp_boss_ids_fixture(fixture: *mut c_void, ids: *mut u32, capacity: usize) -> usize;
        fn er_cpp_update_boss_fixture(fixture: *mut c_void);
        fn er_cpp_boss_snapshot_fixture(
            fixture: *mut c_void,
            dead: *mut u8,
            length: usize,
            metadata: *mut i32,
        ) -> bool;
    }
    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn GetCurrentThread() -> isize;
        fn SetThreadAffinityMask(thread: isize, mask: usize) -> usize;
    }
    struct ThreadAffinity(usize);
    impl ThreadAffinity {
        fn cpu_zero() -> Self {
            // SAFETY: only this offline test thread is pinned; Drop restores
            // its previous mask, including when an assertion unwinds.
            let previous = unsafe { SetThreadAffinityMask(GetCurrentThread(), 1) };
            assert_ne!(
                previous, 0,
                "profile requires logical CPU 0 to be available"
            );
            Self(previous)
        }
    }
    impl Drop for ThreadAffinity {
        fn drop(&mut self) {
            // SAFETY: restores the mask returned for this same test thread.
            unsafe { SetThreadAffinityMask(GetCurrentThread(), self.0) };
        }
    }

    struct CppBoss(*mut c_void);
    impl CppBoss {
        fn new(f: &NativeMemory, directory: &Path) -> Self {
            let wide: Vec<u16> = directory.as_os_str().encode_wide().chain([0]).collect();
            // SAFETY: C++ copies the directory and borrows a stable allocation
            // owned by f. Tests keep exactly one C++ Boss fixture alive.
            let result =
                Self(unsafe { er_cpp_boss_fixture(f.bytes.as_ptr() as usize, wide.as_ptr()) });
            assert!(!result.0.is_null());
            result
        }
        fn ids(&self) -> Vec<u32> {
            // SAFETY: self owns the native fixture; the second call receives
            // the exact capacity returned by the first call.
            let count = unsafe { er_cpp_boss_ids_fixture(self.0, std::ptr::null_mut(), 0) };
            let mut ids = vec![0; count];
            assert_eq!(
                unsafe { er_cpp_boss_ids_fixture(self.0, ids.as_mut_ptr(), ids.len()) },
                count
            );
            ids
        }
        fn update(&self) {
            // SAFETY: self owns a fully initialized fixture whose native
            // memory remains allocated and writable until after Drop.
            unsafe { er_cpp_update_boss_fixture(self.0) };
        }
        fn snapshot(&self, length: usize) -> (Vec<u8>, [i32; 3]) {
            let mut dead = vec![2; length];
            let mut metadata = [0; 3];
            // SAFETY: both output buffers have the exact required size.
            assert!(unsafe {
                er_cpp_boss_snapshot_fixture(
                    self.0,
                    dead.as_mut_ptr(),
                    dead.len(),
                    metadata.as_mut_ptr(),
                )
            });
            (dead, metadata)
        }
    }
    impl Drop for CppBoss {
        fn drop(&mut self) {
            // SAFETY: only this wrapper deletes the native fixture.
            unsafe { er_cpp_drop_boss_fixture(self.0) };
        }
    }

    const NODES: usize = 0xb0000;
    const BUFFERS: usize = 0xd0000;
    const STRIDE: usize = 125; // Native divisor 1000, MSB-first bits.
    const SENTINEL: usize = 0xf0100;

    fn tree(f: &mut NativeMemory, categories: &[u32], start: usize, kind: u32) -> usize {
        if categories.is_empty() {
            return SENTINEL;
        }
        let middle = categories.len() / 2;
        let index = start + middle;
        let node = NODES + index * 0x40;
        let left = tree(f, &categories[..middle], start, kind);
        let right = tree(f, &categories[middle + 1..], index + 1, kind);
        f.ptr(node, left);
        f.ptr(node + 0x10, right);
        f.word(node + 0x20, categories[middle]);
        f.word(node + 0x28, kind);
        if kind == 1 {
            f.word(node + 0x30, index as u32);
        } else {
            f.ptr(node + 0x30, BUFFERS + index * STRIDE);
        }
        node
    }

    fn arrange(f: &mut NativeMemory, ids: &[u32], kind: u32) -> Vec<u32> {
        let categories: Vec<_> = ids
            .iter()
            .map(|id| id / 1000)
            .chain([0])
            .collect::<BTreeSet<_>>()
            .into_iter()
            .collect();
        assert!(BUFFERS + categories.len() * STRIDE < 0xf0000);
        f.bytes[NODES..NODES + categories.len() * 0x40].fill(0);
        f.word(0xf001c, 1000);
        f.word(0xf0020, STRIDE as u32);
        f.ptr(0xf0028, BUFFERS);
        let root = tree(f, &categories, 0, kind);
        f.ptr(SENTINEL + 8, root);
        f.word(0x2094, 37);
        f.word(0x20a0, 123456);
        f.word(0x5014, 0x0a00_0000);
        categories
    }

    fn pattern(f: &mut NativeMemory, ids: &[u32], categories: &[u32], phase: usize) -> Vec<u8> {
        f.bytes[BUFFERS..BUFFERS + categories.len() * STRIDE].fill(0x5a);
        let mut write = |id: u32, value: bool| {
            let category = categories.binary_search(&(id / 1000)).unwrap();
            let address = BUFFERS + category * STRIDE + (id % 1000 / 8) as usize;
            let bit = 1 << (7 - id % 8);
            f.bytes[address] = (f.bytes[address] & !bit) | if value { bit } else { 0 };
        };
        write(101, true);
        ids.iter()
            .enumerate()
            .map(|(i, &id)| {
                let value = (i + phase).is_multiple_of(3);
                write(id, value);
                u8::from(value)
            })
            .collect()
    }

    #[test]
    #[ignore = "explicit C++ export + real event catalog; update components, not game FPS"]
    fn cpp_and_rust_boss_update_profile() {
        let _affinity = ThreadAffinity::cpu_zero();
        let reference =
            std::path::PathBuf::from(std::env::var_os("EROVERLAY_CPP_REFERENCE").unwrap());
        assert!(
            std::fs::read_to_string(reference.join("reference.json"))
                .unwrap()
                .contains("afab7e14db2b62ae0bede980d5d0c96634321350")
        );
        let original = reference.join("src/boss");
        let vanilla_ids = {
            let memory = NativeMemory::new();
            CppBoss::new(&memory, &original).ids()
        };
        assert_eq!(vanilla_ids.len(), 207);
        assert_eq!(vanilla_ids.iter().collect::<BTreeSet<_>>().len(), 207);
        let source = std::env::var_os("EROVERLAY_BOSS_PROFILE_CATALOG")
            .expect("set EROVERLAY_BOSS_PROFILE_CATALOG to archived_catalog_pipeline output");
        let catalog = std::fs::read_to_string(source).unwrap();
        let ids: Vec<u32> = catalog
            .lines()
            .filter_map(|line| line.strip_prefix("boss,"))
            .map(|line| line.split(',').next().unwrap().parse().unwrap())
            .collect::<BTreeSet<_>>()
            .into_iter()
            .collect();
        assert_eq!(ids.len(), 211);
        assert!(vanilla_ids.iter().all(|id| ids.binary_search(id).is_ok()));
        let generated =
            Path::new(env!("CARGO_MANIFEST_DIR")).join("../../target/boss-read-profile/211");
        let data = generated.join("data/engUS");
        std::fs::create_dir_all(&data).unwrap();
        let entries: Vec<_> = ids
            .iter()
            .map(|id| format!("{{\"boss\":\"{id}\",\"place\":\"profile\",\"flag_id\":{id}}}"))
            .collect();
        std::fs::write(
            data.join("bosses.json"),
            format!(
                "[{{\"region_name\":\"profile\",\"regions\":[],\"bosses\":[{}]}}]",
                entries.join(",")
            ),
        )
        .unwrap();
        let samples = std::env::var("EROVERLAY_READ_PROFILE_SAMPLES")
            .ok()
            .and_then(|s| s.parse::<usize>().ok())
            .unwrap_or(2000)
            .clamp(100, 50000);
        memory::set_tracking(false);
        for directory in [&original, &generated] {
            for kind in [1, 2] {
                let mut f = NativeMemory::new();
                let cpp = CppBoss::new(&f, directory);
                let ids = cpp.ids();
                let categories = arrange(&mut f, &ids, kind);
                let mut rust = f.service();
                let mut out = vec![2; ids.len()];
                for phase in 0..3 {
                    let expected = pattern(&mut f, &ids, &categories, phase);
                    let before = f.bytes[BUFFERS..BUFFERS + categories.len() * STRIDE].to_vec();
                    cpp.update();
                    let (dead, metadata) = cpp.snapshot(ids.len());
                    assert!(rust.boss_flags(&ids, &mut out));
                    assert_eq!(dead, expected);
                    assert_eq!(out, expected);
                    assert_eq!(
                        metadata,
                        [expected.iter().map(|&v| v as i32).sum(), 37, 123456]
                    );
                    assert_eq!(
                        before,
                        f.bytes[BUFFERS..BUFFERS + categories.len() * STRIDE]
                    );
                }
                for scenario in ["full_update", "full_update_cold", "loading_hidden"] {
                    let hidden = scenario == "loading_hidden";
                    let cold = scenario == "full_update_cold";
                    f.bytes[0x1718] = u8::from(hidden);
                    let retained = cpp.snapshot(ids.len()).0;
                    if hidden {
                        // Both old and new readers must stop updating flags
                        // while loading, even when the underlying bits change.
                        assert_ne!(pattern(&mut f, &ids, &categories, 0), retained);
                    }
                    for run in 1..=3 {
                        let order = if run % 2 == 0 {
                            ["rust", "cpp"]
                        } else {
                            ["cpp", "rust"]
                        };
                        for implementation in order {
                            let mut times = Vec::with_capacity(samples);
                            for sample in 0..samples + 100 {
                                if implementation == "rust" && cold {
                                    rust.flags.reset();
                                }
                                let start = Instant::now();
                                if implementation == "cpp" {
                                    cpp.update();
                                } else {
                                    std::hint::black_box(rust.boss_flags(&ids, &mut out));
                                }
                                let elapsed = start.elapsed().as_nanos() as u64;
                                if sample >= 100 {
                                    times.push(elapsed);
                                }
                            }
                            times.sort_unstable();
                            let p = |n: usize| times[(samples * n).div_ceil(100).saturating_sub(1)];
                            println!(
                                "boss_update_perf {{\"implementation\":\"{implementation}\",\"scenario\":\"{scenario}\",\"storage_kind\":{kind},\"flags\":{},\"run\":{run},\"samples\":{samples},\"p50_ns\":{},\"p95_ns\":{},\"p99_ns\":{}}}",
                                ids.len(),
                                p(50),
                                p(95),
                                p(99)
                            );
                        }
                    }
                    // Counters are collected separately, so timing does not
                    // include the optional TLS diagnostic accounting.
                    if cold {
                        rust.flags.reset();
                    }
                    memory::set_tracking(true);
                    let before = memory::syscall_counters();
                    let valid = rust.boss_flags(&ids, &mut out);
                    let stats = memory::syscall_counters().since(before);
                    memory::set_tracking(false);
                    println!(
                        "boss_update_reads {{\"scenario\":\"{scenario}\",\"storage_kind\":{kind},\"flags\":{},\"rust_rpm_calls\":{},\"rust_virtual_queries\":{},\"rust_copied_bytes\":{},\"rust_failed_reads\":{},\"cpp_rpm_calls\":0,\"cpp_virtual_queries\":0}}",
                        ids.len(),
                        stats.rpm_calls,
                        stats.virtual_queries,
                        stats.bytes,
                        stats.failed_reads
                    );
                    assert_eq!(valid, !hidden);
                    if hidden {
                        assert!(out.iter().all(|&v| v == 2));
                        assert_eq!(cpp.snapshot(ids.len()).0, retained);
                    } else {
                        assert_eq!(cpp.snapshot(ids.len()).0, out);
                    }
                }
            }
        }
    }
}
