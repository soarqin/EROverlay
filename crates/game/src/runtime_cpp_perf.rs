// Narrow component comparison: original C++ ParamRows/EventFlagCache/mask
// calculation versus the Rust mask calculation on identical native memory.
// This deliberately does not claim whole-update or game-frame equivalence.
#[test]
#[ignore = "original C++/Rust mask-reader component timings, not game frames"]
fn cpp_and_rust_mask_read_profile() {
    use std::ffi::c_void;
    unsafe extern "C" {
        fn er_cpp_flag_cache_fixture() -> *mut c_void;
        fn er_cpp_drop_flag_cache_fixture(cache: *mut c_void);
        fn er_cpp_masks_cached_fixture(
            table: usize,
            manager: usize,
            cache: *mut c_void,
            out: *mut u32,
        ) -> bool;
    }
    struct CppCache(*mut c_void);
    impl Drop for CppCache {
        fn drop(&mut self) {
            unsafe { er_cpp_drop_flag_cache_fixture(self.0) };
        }
    }
    let samples = std::env::var("EROVERLAY_READ_PROFILE_SAMPLES")
        .ok()
        .and_then(|s| s.parse::<usize>().ok())
        .unwrap_or(1000)
        .clamp(100, 100000);
    let mut fixture = NativeMemory::new();
    fixture.many_rows();
    let table = fixture.bytes.as_ptr() as usize + fixture.table;
    let manager = fixture.bytes.as_ptr() as usize + 0xf0000;
    let mut service = fixture.service();
    let cache = CppCache(unsafe { er_cpp_flag_cache_fixture() });
    assert!(!cache.0.is_null());
    for run in 1..=3 {
        for implementation in ["cpp", "rust"] {
            let mut timings = Vec::with_capacity(samples);
            for sample in 0..samples + 100 {
                let start = Instant::now();
                let masks = if implementation == "cpp" {
                    let mut masks = [0; 3];
                    // SAFETY: fixture-owned PARAM, manager, cache and output.
                    assert!(unsafe {
                        er_cpp_masks_cached_fixture(table, manager, cache.0, masks.as_mut_ptr())
                    });
                    masks
                } else {
                    service.read_masks(table, manager, false).unwrap()
                };
                let elapsed = start.elapsed().as_nanos() as u64;
                assert_eq!(masks, [u32::MAX; 3]);
                if sample >= 100 {
                    timings.push(elapsed);
                }
            }
            timings.sort_unstable();
            let p = |n: usize| timings[(samples * n).div_ceil(100).saturating_sub(1)];
            println!(
                "cpp_mask_read_perf {{\"implementation\":\"{implementation}\",\"run\":{run},\"samples\":{samples},\"rows\":96,\"p50_ns\":{},\"p95_ns\":{},\"p99_ns\":{}}}",
                p(50),
                p(95),
                p(99)
            );
        }
    }
}
