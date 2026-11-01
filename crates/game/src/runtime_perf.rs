//! Shared production-path fixture. This file also compiles against 20fce70.
use super::*;
pub(super) struct NativeMemory {
    pub(super) bytes: Vec<u8>,
    pub(super) bindings: Bindings,
    pub(super) table: usize,
}
impl NativeMemory {
    fn many_rows(&mut self) {
        let t = self.table;
        self.bytes[t..t + 0x2000].fill(0);
        self.word(t - 16, 0x2000);
        self.word(t, 0x1ff0);
        self.bytes[t + 10..t + 12].copy_from_slice(&96u16.to_le_bytes());
        self.bytes[t + 0x2d] = 0x84;
        self.bytes[t + 0x2e] = 2;
        for i in 0..96 {
            let row = t + 0x40 + i * 24;
            let map = [0, 1, 10][i / 32];
            self.word(row, map * 100 + (i % 32) as u32);
            self.word(row + 8, (0x1000 + i * 8) as u32);
            self.word(t + 0x1000 + i * 8 + 4, 8);
        }
    }
    pub(super) fn word(&mut self, at: usize, value: u32) {
        self.bytes[at..at + 4].copy_from_slice(&value.to_le_bytes());
    }
    pub(super) fn ptr(&mut self, at: usize, target: usize) {
        let p = self.bytes.as_ptr() as usize + target;
        self.bytes[at..at + 8].copy_from_slice(&(p as u64).to_le_bytes());
    }
    fn rtti(&mut self, object: usize, vt: usize, col: usize, descriptor: usize, name: &[u8]) {
        self.ptr(object, vt);
        self.ptr(vt - 8, col);
        self.word(col, 1);
        self.word(col + 12, descriptor as u32);
        self.word(col + 20, col as u32);
        self.bytes[descriptor + 16..descriptor + 16 + name.len()].copy_from_slice(name);
    }
    pub(super) fn new() -> Self {
        let mut f = Self {
            bytes: vec![0; 1024 * 1024],
            bindings: Bindings {
                menu: 0x20,
                game_data: 0x28,
                event_flags: 0x30,
                repository: 0x38,
                reveal: 0x40,
                field: 0x48,
                layout: GameLayout {
                    menu_owner_offset: 0x78,
                    owner_view_offset: 0x100,
                    menu_info_offset: 0x708,
                    screen_state_offset: 0x718,
                    repository_groups: 200,
                    ..Default::default()
                },
                ..Default::default()
            },
            table: 0x88010,
        };
        f.ptr(0x20, 0x1000);
        f.ptr(0x28, 0x2000);
        f.ptr(0x2058, 0x3000);
        f.ptr(0x1078, 0x4000);
        f.ptr(0x4100, 0x5000);
        f.rtti(0x1000, 0x6000, 0x6100, 0x6200, b".?AVCSMenuManImp@CS@@");
        f.rtti(
            0x1708,
            0x7000,
            0x7100,
            0x7200,
            b".?AVLoadingScreenData@CS@@",
        );
        f.ptr(0x38, 0x9000);
        for group in [43usize, 87, 88, 141] {
            let entry = 0x10000 + group * 0x100;
            let cap = 0x20000 + group * 0x100;
            let table = 0x30010 + group * 0x1000;
            f.ptr(0x9000 + 0x88 + group * 72, entry);
            f.ptr(entry + 0x80, cap);
            f.ptr(cap + 0x80, table);
        }
        let t = f.table;
        f.word(t - 16, 0x200);
        f.word(t, 0x1f0);
        f.bytes[t + 10..t + 12].copy_from_slice(&3u16.to_le_bytes());
        f.bytes[t + 0x2d] = 0x84;
        f.bytes[t + 0x2e] = 2;
        for (i, id, event) in [(0, 0, 8), (1, 100, 9), (2, 1000, 15)] {
            let row = t + 0x40 + i * 24;
            let data = 0x100 + i * 8;
            f.word(row, id);
            f.word(row + 8, data as u32);
            f.word(t + data + 4, event);
        }
        f.ptr(0x30, 0xf0000);
        f.word(0xf001c, 8);
        f.word(0xf0020, 1);
        f.ptr(0xf0038, 0xf0100);
        f.ptr(0xf0108, 0xf0200);
        f.bytes[0xf0119] = 1;
        f.ptr(0xf0200, 0xf0100);
        f.ptr(0xf0210, 0xf0100);
        f.word(0xf0220, 1);
        f.word(0xf0228, 2);
        f.ptr(0xf0230, 0xf0300);
        f.bytes[0xf0300] = 0x81;
        f
    }
    pub(super) fn service(&self) -> GameServices {
        GameServices::with_bindings(self.bytes.as_ptr() as usize, Some(self.bindings))
    }
}

/// Time the same GameServices/update_snapshot implementation and fixture in
/// each revision. No diagnostics/counters/new fields are used here: the parent
/// runner copies this module unchanged into the 20fce70 source snapshot.
#[test]
#[ignore = "controlled same-fixture revision comparison; not game frame timings"]
fn runtime_production_read_profile() {
    let samples = std::env::var("EROVERLAY_READ_PROFILE_SAMPLES")
        .ok()
        .and_then(|s| s.parse::<usize>().ok())
        .unwrap_or(1000)
        .clamp(10, 1_000_000);
    for scenario in [
        "steady",
        "forced_mask_refresh",
        "no_param",
        "invalid_flag",
        "load_then_valid",
    ] {
        let mut fixture = NativeMemory::new();
        fixture.many_rows();
        if matches!(scenario, "no_param" | "load_then_valid") {
            fixture.bytes[0x38..0x40].fill(0);
        }
        if scenario == "invalid_flag" {
            fixture.word(0xf001c, 0);
        }
        let mut service = fixture.service();
        service.update_snapshot();
        let mut timings = Vec::with_capacity(samples);
        let mut valid = 0u64;
        let mut masks = [0u32; 3];
        for sample in 0..samples {
            if scenario == "forced_mask_refresh" {
                service.mask_refresh = None;
            }
            if scenario == "load_then_valid" && sample == samples / 2 {
                fixture.ptr(0x38, 0x9000);
            }
            std::hint::black_box(&fixture);
            let start = Instant::now();
            service.update_snapshot();
            // Include the bounded fresh read actually called by the plugin.
            if let Some(map) = service.read_map_state() {
                valid += 1;
                masks = map.active_masks;
            }
            timings.push(start.elapsed().as_nanos() as u64);
        }
        timings.sort_unstable();
        let percentile =
            |percent: usize| timings[(samples * percent).div_ceil(100).saturating_sub(1)];
        println!(
            "runtime_read_perf {{\"scenario\":\"{scenario}\",\"samples\":{samples},\"p50_ns\":{},\"p95_ns\":{},\"p99_ns\":{},\"valid\":{valid},\"masks\":[{},{},{}]}}",
            percentile(50),
            percentile(95),
            percentile(99),
            masks[0],
            masks[1],
            masks[2]
        );
        assert_eq!(
            valid,
            match scenario {
                "no_param" | "invalid_flag" => 0,
                "load_then_valid" => (samples - samples / 2) as u64,
                _ => samples as u64,
            }
        );
    }
}

#[cfg(feature = "runtime-read-fixture")]
include!("runtime_cpp_perf.rs");
