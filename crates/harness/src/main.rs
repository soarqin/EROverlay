use eroverlay_abi::*;
use eroverlay_renderer::NativeFrame;
use std::{path::PathBuf, time::Duration};
mod gpu;
use windows::{
    Win32::{
        Foundation::FreeLibrary,
        System::LibraryLoader::{GetProcAddress, LoadLibraryW},
    },
    core::PCWSTR,
};

#[repr(C)]
#[derive(Default, Debug)]
struct FixtureRenderCounts {
    resets: u32,
    renders: u32,
    ready_draws: u32,
    texture_creations: u32,
    generation: u64,
}
type FixtureCounts = unsafe extern "C" fn(*mut FixtureRenderCounts) -> i32;

fn main() {
    if let Err(e) = run() {
        eprintln!("{e}");
        std::process::exit(1);
    }
}
fn run() -> Result<(), String> {
    // Single-threaded startup; inherited only by this controlled offline process.
    unsafe { std::env::set_var("EROVERLAY_RUST_OFFLINE_HOST", "1") };
    use std::os::windows::ffi::OsStrExt;
    let directory = std::env::args_os()
        .nth(1)
        .map(PathBuf::from)
        .ok_or("usage: eroverlay-harness <absolute-package-directory> [cycles] [observe-seconds] [upper-fixture-dll-in-overlays]")?
        .canonicalize()
        .map_err(|e| e.to_string())?;
    let cycles = std::env::args()
        .nth(2)
        .and_then(|v| v.parse::<usize>().ok())
        .unwrap_or(3);
    let observe = Duration::from_secs(
        std::env::args()
            .nth(3)
            .and_then(|v| v.parse::<u64>().ok())
            .unwrap_or(0)
            .min(30),
    );
    let fixture_path = std::env::args_os().nth(4).map(PathBuf::from);
    let has_boss = directory.join("overlays/Boss.dll").is_file();
    let upper_consumer = fixture_path.is_some() || has_boss;
    let expected_plugins = std::fs::read_dir(directory.join("overlays"))
        .map_err(|e| e.to_string())?
        .filter_map(Result::ok)
        .filter(|entry| {
            entry
                .path()
                .extension()
                .is_some_and(|v| v.eq_ignore_ascii_case("dll"))
        })
        .count() as u32;
    if fixture_path.is_some() {
        // Single-threaded controlled startup. The fixture must already reside
        // in the package's overlays directory so the real loader discovers it.
        unsafe { std::env::set_var("EROVERLAY_ABI_FIXTURE_MODE", "upper") };
    }
    let wide: Vec<u16> = directory.as_os_str().encode_wide().collect();
    let gpu = gpu::WarpHost::new().map_err(|e| format!("WARP initialization failed: {e}"))?;
    for cycle in 0..cycles {
        let fixture = if let Some(path) = fixture_path.as_ref() {
            let path = path.canonicalize().map_err(|e| e.to_string())?;
            if path.parent() != Some(directory.join("overlays").as_path()) {
                return Err(
                    "upper fixture must reside in this package's overlays directory".into(),
                );
            }
            let path: Vec<u16> = path.as_os_str().encode_wide().chain(Some(0)).collect();
            let module =
                unsafe { LoadLibraryW(PCWSTR(path.as_ptr())) }.map_err(|e| e.to_string())?;
            let counts: FixtureCounts = unsafe {
                std::mem::transmute(
                    GetProcAddress(module, windows::core::s!("fixture_render_counts"))
                        .ok_or("missing fixture render counters")?,
                )
            };
            Some((module, counts))
        } else {
            None
        };
        let path: Vec<u16> = directory
            .join("EROverlay.dll")
            .as_os_str()
            .encode_wide()
            .chain(Some(0))
            .collect();
        // SAFETY: retained loaded module owns all symbol addresses below.
        let module = unsafe { LoadLibraryW(PCWSTR(path.as_ptr())) }.map_err(|e| e.to_string())?;
        unsafe {
            let start: unsafe extern "C" fn(*const StartOptions) -> i32 = std::mem::transmute(
                GetProcAddress(module, windows::core::s!("eroverlay_start"))
                    .ok_or("missing start export")?,
            );
            let stop: unsafe extern "C" fn() -> i32 = std::mem::transmute(
                GetProcAddress(module, windows::core::s!("eroverlay_stop"))
                    .ok_or("missing stop export")?,
            );
            let stats: unsafe extern "C" fn(*mut RuntimeStats) -> i32 = std::mem::transmute(
                GetProcAddress(module, windows::core::s!("eroverlay_stats"))
                    .ok_or("missing stats export")?,
            );
            let render: unsafe extern "C" fn(*const NativeFrame) -> i32 = std::mem::transmute(
                GetProcAddress(module, windows::core::s!("eroverlay_render"))
                    .ok_or("missing render export")?,
            );
            if render(std::ptr::null()) != ERR_INVALID {
                return Err("null frame accepted".into());
            }
            let mut options = StartOptions {
                size: size_of::<StartOptions>() as u32,
                version: ABI_VERSION,
                native: 0,
                reserved: 0,
                directory: wide.as_ptr(),
                directory_len: wide.len(),
            };
            options.version = 99;
            if start(&options) != ERR_ABI {
                return Err("unknown ABI was accepted".into());
            }
            options.version = ABI_VERSION;
            if start(std::ptr::null()) != ERR_INVALID {
                return Err("null options accepted".into());
            }
            let code = start(&options);
            if code != OK {
                return Err(format!("start failed: {code}"));
            }
            if start(&options) != ERR_BUSY {
                return Err("duplicate start accepted".into());
            }
            std::thread::sleep(Duration::from_millis(100));
            let mut out = RuntimeStats {
                size: size_of::<RuntimeStats>() as u32,
                ..RuntimeStats::default()
            };
            if stats(&mut out) != OK
                || out.plugins_loaded != expected_plugins
                || out.plugins_rejected != 0
                || out.updates == 0
            {
                return Err(format!(
                    "runtime did not load all {expected_plugins} plugins and update: {out:?}"
                ));
            }
            // Call the actual loaded core/Minimap pipeline with a borrowed WARP
            // RGB10A2 target. Offline Minimap remains correctly hidden without
            // a game snapshot; backend initialization/uploads still execute.
            let mut sequence = 1;
            for (width, height) in [(64.0, 64.0), (128.0, 96.0)] {
                let before = out.renders;
                let deadline = std::time::Instant::now() + Duration::from_secs(3);
                loop {
                    sequence += 1;
                    let frame = gpu.frame(width, height, sequence, LAYER_BELOW_UI);
                    if render(&frame) != OK {
                        return Err("controlled core render entry failed".into());
                    }
                    if stats(&mut out) != OK || out.last_error != OK {
                        return Err(format!("core GPU render failed: {out:?}"));
                    }
                    if out.renders > before && out.gpu_committed_bytes > 0 {
                        break;
                    }
                    if std::time::Instant::now() >= deadline {
                        return Err(format!("core render never progressed: {out:?}"));
                    }
                    std::thread::sleep(Duration::from_millis(1));
                }
                gpu.drain().map_err(|e| e.to_string())?;
                // A different upper viewport in the same native sequence must
                // retain the renderer/textures. With the optional actual upper
                // plugin, also verify its generation/reset callback directly.
                for phase in 0..8 {
                    let before = out.renders;
                    let committed = out.gpu_committed_bytes;
                    let (upper_width, upper_height) = if phase % 2 == 0 {
                        (192.0, 128.0)
                    } else {
                        (32.0, 32.0)
                    };
                    let upper = gpu.frame(upper_width, upper_height, sequence, LAYER_ABOVE_UI);
                    if render(&upper) != OK
                        || stats(&mut out) != OK
                        || out.last_error != OK
                        || out.renders != before + u64::from(upper_consumer)
                        || (!upper_consumer && out.gpu_committed_bytes != committed)
                    {
                        return Err(format!(
                            "above-UI layer changed core renderer state unexpectedly: before_renders={before}, before_bytes={committed}, after={out:?}"
                        ));
                    }
                    check_fixture(fixture.as_ref().map(|(_, counts)| *counts), false)?;
                    let before_lower = out.renders;
                    let deadline = std::time::Instant::now() + Duration::from_secs(3);
                    while out.renders <= before_lower {
                        sequence += 1;
                        let lower = gpu.frame(width, height, sequence, LAYER_BELOW_UI);
                        if render(&lower) != OK || stats(&mut out) != OK || out.last_error != OK {
                            return Err(format!(
                                "below-UI render after unused layer failed: {out:?}"
                            ));
                        }
                        if std::time::Instant::now() >= deadline {
                            return Err("below-UI core render did not progress".into());
                        }
                        std::thread::sleep(Duration::from_millis(1));
                    }
                }
            }
            if fixture.is_some() {
                // Wait only in this standalone host until real worker-prepared
                // texture data has reached the production renderer and drawn.
                let deadline = std::time::Instant::now() + Duration::from_secs(3);
                loop {
                    sequence += 1;
                    let frame = gpu.frame(96.0, 64.0, sequence, LAYER_ABOVE_UI);
                    if render(&frame) != OK || stats(&mut out) != OK || out.last_error != OK {
                        return Err(format!("upper fixture render failed: {out:?}"));
                    }
                    if check_fixture(fixture.as_ref().map(|(_, counts)| *counts), true)? {
                        break;
                    }
                    if std::time::Instant::now() >= deadline {
                        return Err("upper fixture texture never reached a real GPU draw".into());
                    }
                    std::thread::sleep(Duration::from_millis(1));
                }
                gpu.drain().map_err(|e| e.to_string())?;
                // All four slot targets are now allocated at a fixed 24x24
                // group size. More viewport changes must not recreate textures
                // or retain fresh staging allocations after completed draws.
                let mut stable_committed = None;
                for phase in 0..12 {
                    sequence += 1;
                    let (width, height) = if phase % 2 == 0 {
                        (32.0, 32.0)
                    } else {
                        (192.0, 128.0)
                    };
                    let frame = gpu.frame(width, height, sequence, LAYER_ABOVE_UI);
                    if render(&frame) != OK || stats(&mut out) != OK || out.last_error != OK {
                        return Err(format!("resized upper fixture render failed: {out:?}"));
                    }
                    check_fixture(fixture.as_ref().map(|(_, counts)| *counts), true)?;
                    if phase >= 4 {
                        if let Some(committed) = stable_committed {
                            if out.gpu_committed_bytes != committed {
                                return Err(format!(
                                    "viewport-only change recreated GPU storage: before={committed}, after={out:?}"
                                ));
                            }
                        } else {
                            stable_committed = Some(out.gpu_committed_bytes);
                        }
                    }
                    gpu.drain().map_err(|e| e.to_string())?;
                }
            }
            // Optional game-paced production-path observation, long enough for
            // the configured low-frequency logger to publish render/queue data.
            // This remains an offline hidden Minimap, not a gameplay benchmark.
            let observe_until = std::time::Instant::now() + observe;
            while std::time::Instant::now() < observe_until {
                sequence += 1;
                let lower = gpu.frame(128.0, 96.0, sequence, LAYER_BELOW_UI);
                let unused = gpu.frame(32.0, 32.0, sequence, LAYER_ABOVE_UI);
                if render(&lower) != OK
                    || render(&unused) != OK
                    || stats(&mut out) != OK
                    || out.last_error != OK
                {
                    return Err(format!("production observation failed: {out:?}"));
                }
                std::thread::sleep(Duration::from_millis(16));
            }
            // Hold a real fixture draw behind the queue gate. A hidden Minimap
            // without a consumer draw has no owned work to wait for at stop.
            gpu.drain().map_err(|e| e.to_string())?;
            let gate = SignalOnDrop(gpu.hold_queue().map_err(|e| e.to_string())?);
            let before = out.renders;
            let before_meshes = out.meshes;
            let deadline = std::time::Instant::now() + Duration::from_secs(3);
            while out.renders <= before || (fixture.is_some() && out.meshes <= before_meshes) {
                sequence += 1;
                let frame = gpu.frame(
                    96.0,
                    64.0,
                    sequence,
                    if fixture.is_some() {
                        LAYER_ABOVE_UI
                    } else {
                        LAYER_BELOW_UI
                    },
                );
                if render(&frame) != OK || stats(&mut out) != OK || out.last_error != OK {
                    return Err(format!("held GPU render failed: {out:?}"));
                }
                if std::time::Instant::now() >= deadline {
                    return Err("held render did not progress".into());
                }
                std::thread::sleep(Duration::from_millis(1));
            }
            if gate.0.GetCompletedValue() != 0 {
                return Err("GPU gate opened before shutdown".into());
            }
            let resume = gate.0.clone();
            let signaler = std::thread::spawn(move || {
                std::thread::sleep(Duration::from_millis(80));
                resume.Signal(1)
            });
            let render_count = out.renders;
            let committed = out.gpu_committed_bytes;
            let result = stop();
            let gate_completed_at_stop = gate.0.GetCompletedValue() >= 1;
            signaler
                .join()
                .map_err(|_| "GPU gate signaler panicked")?
                .map_err(|e| e.to_string())?;
            if result != OK {
                return Err(format!("stop busy; module MUST remain loaded: {result}"));
            }
            if fixture.is_some() && !gate_completed_at_stop {
                return Err("stop returned before its in-flight GPU fence retired".into());
            }
            if stop() != OK {
                return Err("second stop failed".into());
            }
            if stats(&mut out) != OK || out.state != 0 {
                return Err("runtime remained active after stop".into());
            }
            let stopped = gpu.frame(64.0, 64.0, sequence + 1, LAYER_BELOW_UI);
            if render(&stopped) != ERR_UNAVAILABLE {
                return Err("render accepted after stop".into());
            }
            gpu.drain().map_err(|e| e.to_string())?;
            gpu.check_validation()?;
            FreeLibrary(module).map_err(|e| e.to_string())?;
            if let Some((module, counts)) = fixture {
                check_fixture(Some(counts), true)?;
                FreeLibrary(module).map_err(|e| e.to_string())?;
            }
            println!(
                "cycle {}: real core and {expected_plugins} plugin DLLs load/update, RGB10A2 WARP alternating-layer viewport/resize and unload passed (renders={render_count}, committed={committed}; Boss={has_boss}, upper_fixture={}, GPU-retained shutdown={}; offline game overlays hidden)",
                cycle + 1,
                fixture_path.is_some(),
                fixture_path.is_some()
            );
        }
    }
    Ok(())
}

fn check_fixture(counts: Option<FixtureCounts>, require_draw: bool) -> Result<bool, String> {
    let Some(query) = counts else {
        return Ok(false);
    };
    let mut counts = FixtureRenderCounts::default();
    if unsafe { query(&mut counts) } != OK
        || counts.resets != 1
        || counts.generation != 1
        || counts.texture_creations != 1
    {
        return Err(format!(
            "viewport change reset the fixture renderer/texture: {counts:?}"
        ));
    }
    Ok(!require_draw || counts.ready_draws != 0)
}

struct SignalOnDrop(windows::Win32::Graphics::Direct3D12::ID3D12Fence);
impl Drop for SignalOnDrop {
    fn drop(&mut self) {
        let _ = unsafe { self.0.Signal(1) };
    }
}
