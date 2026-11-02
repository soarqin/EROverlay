//! Real core + Boss + Minimap overlap test. Only game data and the native UI
//! compositor are controlled; DLL lifecycle, meshes, atlases and renderer are real.
use eroverlay_abi::*;
use eroverlay_renderer::NativeFrame;
use std::{
    ffi::c_void,
    os::windows::ffi::OsStrExt,
    path::{Path, PathBuf},
    sync::atomic::{AtomicBool, AtomicU32, Ordering},
    time::{Duration, Instant},
};
use windows::{
    Win32::{
        Foundation::FreeLibrary,
        Graphics::Direct3D12::ID3D12Fence,
        System::LibraryLoader::{GetProcAddress, LoadLibraryW},
    },
    core::PCWSTR,
};
#[path = "../layer_order_gpu.rs"]
mod gpu;

type Start = unsafe extern "C" fn(*const StartOptions) -> i32;
type Stop = unsafe extern "C" fn() -> i32;
type Stats = unsafe extern "C" fn(*mut RuntimeStats) -> i32;
type Render = unsafe extern "C" fn(*const NativeFrame) -> i32;
type Register = unsafe extern "C" fn(*const HostApi) -> i32;
type Drive = unsafe extern "C" fn(
    *const NativeFrame,
    *mut c_void,
    *const NativeFrame,
    *mut c_void,
    Option<unsafe extern "C" fn(*mut c_void)>,
    *mut c_void,
    u32,
) -> i32;
type Busy = unsafe extern "C" fn(*const NativeFrame, u32) -> i32;

#[derive(Default)]
struct Control {
    toggle: AtomicBool,
    capture: AtomicU32,
}
struct NativeCommand<'a> {
    gpu: &'a mut gpu::WarpHost,
    width: f32,
    height: f32,
}
unsafe extern "C" fn native_command(user: *mut c_void) {
    // SAFETY: the native driver invokes this synchronously, on this test thread.
    let command = unsafe { &mut *user.cast::<NativeCommand<'_>>() };
    if let Err(e) = command.gpu.composite_async(command.width, command.height) {
        command.gpu.command_error = Some(e.to_string());
    }
}
struct OpenGate(ID3D12Fence);
impl Drop for OpenGate {
    fn drop(&mut self) {
        unsafe {
            let _ = self.0.Signal(1);
        }
    }
}

fn main() {
    if let Err(e) = run() {
        eprintln!("层级探针未通过：{e}");
        std::process::exit(1);
    }
}
fn run() -> Result<(), String> {
    // The real core must not install native hooks or inspect a running game.
    unsafe { std::env::set_var("EROVERLAY_RUST_OFFLINE_HOST", "1") };
    let mut args = std::env::args_os().skip(1);
    let core = PathBuf::from(
        args.next()
            .ok_or("usage: layer_order_probe <fixture-core.dll> <package> <output-directory>")?,
    )
    .canonicalize()
    .map_err(err)?;
    let package = PathBuf::from(args.next().ok_or("missing package")?)
        .canonicalize()
        .map_err(err)?;
    let output = PathBuf::from(args.next().ok_or("missing output directory")?);
    std::fs::create_dir_all(&output).map_err(err)?;
    let output = output.canonicalize().map_err(err)?;
    let directory = output.join("controlled-package");
    if directory.exists() {
        return Err("请为每次探针指定新的输出目录，以保留独立证据".into());
    }
    std::fs::create_dir_all(directory.join("overlays")).map_err(err)?;
    std::fs::create_dir_all(directory.join("configs")).map_err(err)?;
    for name in ["Boss.dll", "Minimap.dll"] {
        std::fs::copy(
            package.join("overlays").join(name),
            directory.join("overlays").join(name),
        )
        .map_err(err)?;
    }
    for entry in std::fs::read_dir(package.join("configs")).map_err(err)? {
        let entry = entry.map_err(err)?;
        if entry.file_type().map_err(err)?.is_file() && entry.file_name() != "boss-ui.json" {
            std::fs::copy(
                entry.path(),
                directory.join("configs").join(entry.file_name()),
            )
            .map_err(err)?;
        }
    }
    let control = Box::<Control>::default();
    let api = host(&control);
    let path: Vec<u16> = core.as_os_str().encode_wide().chain(Some(0)).collect();
    let module = unsafe { LoadLibraryW(PCWSTR(path.as_ptr())) }.map_err(err)?;
    // SAFETY: the retained module owns every symbol until successful stop.
    unsafe {
        macro_rules! symbol {
            ($name:literal, $ty:ty) => {
                std::mem::transmute::<unsafe extern "system" fn() -> isize, $ty>(
                    GetProcAddress(module, windows::core::s!($name))
                        .ok_or(concat!("missing ", $name))?,
                )
            };
        }
        let start = symbol!("eroverlay_start", Start);
        let stop = symbol!("eroverlay_stop", Stop);
        let stats = symbol!("eroverlay_stats", Stats);
        let render = symbol!("eroverlay_render", Render);
        let register = symbol!("eroverlay_test_layer_host", Register);
        let drive = symbol!("eroverlay_test_layer_drive", Drive);
        let busy = symbol!("eroverlay_test_layer_busy", Busy);
        check(register(&api), "register controlled game host")?;
        let wide: Vec<u16> = directory.as_os_str().encode_wide().collect();
        check(
            start(&StartOptions {
                size: size_of::<StartOptions>() as u32,
                version: ABI_VERSION,
                native: 0,
                reserved: 0,
                directory: wide.as_ptr(),
                directory_len: wide.len(),
            }),
            "start real core",
        )?;
        let mut gpu = gpu::WarpHost::new_size(1280, 900).map_err(err)?;
        let mut seq = 100;
        std::thread::sleep(Duration::from_millis(120));
        let mut out = read_stats(stats)?;
        if out.plugins_loaded != 2 || out.plugins_rejected != 0 {
            return Err(format!("expected real Boss+Minimap, got {out:?}"));
        }
        // Font uploads, renderer-reset snapshots and egui layout all settle first.
        for _ in 0..24 {
            step(&mut gpu, drive, &mut seq, [1280., 900.], 0, true)?;
            std::thread::sleep(Duration::from_millis(17));
        }
        let (_, _, lower) = gpu.readback().map_err(err)?;
        bmp(&output.join("01-minimap-reference.bmp"), 1280, 900, &lower)?;
        control.toggle.store(true, Ordering::Release);
        std::thread::sleep(Duration::from_millis(80));
        for _ in 0..24 {
            step(&mut gpu, drive, &mut seq, [1280., 900.], 0, true)?;
            std::thread::sleep(Duration::from_millis(17));
        }
        let (_, _, mut reference) = gpu.readback().map_err(err)?;
        bmp(&output.join("02-normal-overlap.bmp"), 1280, 900, &reference)?;
        let mut roi = overlap(&lower, &reference);
        if roi.len() < 500 {
            return Err(format!(
                "real Minimap red border / opaque Boss overlap not established: {} pixels",
                roi.len()
            ));
        }
        println!(
            "real DLL overlap established: {} interior red-border pixels hidden by Boss; capture={}",
            roi.len(),
            control.capture.load(Ordering::Acquire)
        );
        assert_overlap(&reference, &reference, &roi)?;
        out = read_stats(stats)?;
        if out.meshes == 0 || out.last_error != OK {
            return Err(format!("real plugin meshes missing or core error: {out:?}"));
        }
        let initial_drops = out.dropped_frames;
        let mut worst = Duration::ZERO;
        for _ in 0..120 {
            let time = Instant::now();
            step(&mut gpu, drive, &mut seq, [1280., 900.], 0, true)?;
            worst = worst.max(time.elapsed());
            let (_, _, pixels) = gpu.readback().map_err(err)?;
            assert_overlap(&pixels, &reference, &roi)?;
        }
        println!(
            "120 sequential actual double-layer frames: overlap readback stable; max test step including GPU drain={:.3}ms",
            worst.as_secs_f64() * 1000.
        );

        // Three real upper submissions consume three slots behind a queue gate.
        // Clear is enqueued AFTER them. The following native dual-layer frame
        // must preserve Boss: the one remaining slot belongs to the upper stage.
        gpu.drain().map_err(err)?;
        let before = read_stats(stats)?;
        let gate = OpenGate(gpu.hold_queue().map_err(err)?);
        for _ in 0..3 {
            seq += 1;
            check(
                render(&gpu.frame(1280., 900., seq, LAYER_ABOVE_UI)),
                "occupy upper slot",
            )?;
        }
        step(&mut gpu, drive, &mut seq, [1280., 900.], 0, false)?;
        let held = read_stats(stats)?;
        if gate.0.GetCompletedValue() != 0 || held.dropped_frames <= before.dropped_frames {
            return Err(format!(
                "three-slot gate did not cause the intended nonblocking lower drop: before={before:?}, held={held:?}"
            ));
        }
        drop(gate);
        let (_, _, pressure) = gpu.readback().map_err(err)?;
        assert_overlap(&pressure, &reference, &roi)?;
        bmp(
            &output.join("03-three-slots-held.bmp"),
            1280,
            900,
            &pressure,
        )?;
        println!(
            "three busy slots + native dual-layer frame: Boss remains above overlap; drops +{}",
            held.dropped_frames - before.dropped_frames
        );

        // Multiple queued frames deliberately exercise nonblocking saturation;
        // no callback drains the GPU. A fresh frame after release must recover.
        let gate = OpenGate(gpu.hold_queue().map_err(err)?);
        for _ in 0..8 {
            step(&mut gpu, drive, &mut seq, [1280., 900.], 0, false)?;
        }
        drop(gate);
        gpu.drain().map_err(err)?;
        step(&mut gpu, drive, &mut seq, [1280., 900.], 0, true)?;
        let (_, _, recovered) = gpu.readback().map_err(err)?;
        assert_overlap(&recovered, &reference, &roi)?;
        println!("8 asynchronous queued frames: released gate and recovered full overlap");

        for scenario in 0..=4 {
            let composites = gpu.composites;
            step(&mut gpu, drive, &mut seq, [1280., 900.], scenario, true)?;
            let (_, _, pixels) = gpu.readback().map_err(err)?;
            assert_overlap(&pixels, &reference, &roi)?;
            let red = (100..550)
                .flat_map(|y| (660..700).map(move |x| (y * 1280 + x) * 4))
                .filter(|&i| pixels[i] > 220 && pixels[i + 1] < 30 && pixels[i + 2] < 30)
                .count();
            if gpu.composites - composites != if scenario == 3 { 2 } else { 1 }
                || if scenario == 0 || scenario == 3 {
                    red < 17000
                } else {
                    red != 0
                }
            {
                return Err(format!(
                    "native boundary scenario {scenario}: unexpected native command count or lower-stage red coverage ({red})"
                ));
            }
        }
        println!(
            "native driver scenarios 0–4: stale/missing/duplicate composite boundaries preserve the upper stage"
        );
        // A repeated callback carries no additional egui geometry. Preserve the
        // target (as the game does), rather than clearing an already-drawn frame.
        let before = read_stats(stats)?;
        check(
            render(&gpu.frame(1280., 900., seq, LAYER_BELOW_UI)),
            "duplicate lower",
        )?;
        check(
            render(&gpu.frame(1280., 900., seq, LAYER_ABOVE_UI)),
            "duplicate upper",
        )?;
        let duplicate = read_stats(stats)?;
        if duplicate.meshes != before.meshes {
            return Err("duplicate native sequence advanced plugin geometry".into());
        }
        for size in [[1240., 880.], [1280., 900.], [1220., 800.], [1280., 900.]] {
            step(&mut gpu, drive, &mut seq, size, 0, true)?;
        }
        for _ in 0..3 {
            step(&mut gpu, drive, &mut seq, [1280., 900.], 0, true)?;
        }
        let (_, _, resized) = gpu.readback().map_err(err)?;
        // egui keeps a window's clamped position when a viewport grows again.
        // Re-establish the actual overlap instead of asserting stale coordinates.
        roi = overlap(&lower, &resized);
        if roi.len() < 500 {
            return Err("viewport resize lost real Boss/Minimap overlap".into());
        }
        reference = resized;
        println!(
            "duplicate sequence emits no new meshes; same-device viewport resizes recover overlap"
        );

        let before = read_stats(stats)?;
        for which in 0..2 {
            seq += 1;
            check(
                busy(&gpu.frame(1280., 900., seq, LAYER_ABOVE_UI), which),
                "controlled core render/gate lock",
            )?;
        }
        let skipped = read_stats(stats)?;
        if skipped.meshes != before.meshes || skipped.dropped_frames <= before.dropped_frames {
            return Err("controlled lock contention did not skip work".into());
        }
        step(&mut gpu, drive, &mut seq, [1280., 900.], 0, true)?;
        let (_, _, recovered) = gpu.readback().map_err(err)?;
        assert_overlap(&recovered, &reference, &roi)?;
        println!("core render try_lock and callback-gate contention skip and recover");

        // Only the test host waits. Core must retain its renderer on timeout.
        gpu.drain().map_err(err)?;
        let gate = OpenGate(gpu.hold_queue().map_err(err)?);
        step(&mut gpu, drive, &mut seq, [1280., 900.], 0, false)?;
        let final_stats = read_stats(stats)?;
        let started = Instant::now();
        let code = stop();
        if code != ERR_BUSY {
            return Err(format!(
                "GPU-busy stop should retain resources with ERR_BUSY, got {code}"
            ));
        }
        let retained = read_stats(stats)?;
        if retained.state != 2 || retained.gpu_committed_bytes == 0 {
            return Err(format!("busy stop lost owned renderer: {retained:?}"));
        }
        println!(
            "GPU-busy stop returned ERR_BUSY after {:.3}s with {} bytes retained",
            started.elapsed().as_secs_f64(),
            retained.gpu_committed_bytes
        );
        drop(gate);
        gpu.drain().map_err(err)?;
        check(stop(), "stop after retirement")?;
        if read_stats(stats)?.state != 0 {
            return Err("runtime remains after drained stop".into());
        }
        gpu.check_validation()?;
        FreeLibrary(module).map_err(err)?;
        println!(
            "PASS: actual core + Boss.dll + Minimap.dll; plugins=2, nonempty meshes={}, drops={} (normal baseline {}); RGB10A2/RGBA8 same-queue composite/readback; validation clean; drained stop OK",
            final_stats.meshes, final_stats.dropped_frames, initial_drops
        );
        println!(
            "Evidence limits: controlled game/save/resource inputs; real plugin UI, core ownership, native queue adapter and WARP pixels. This does not measure live-game FPS or prove injected-game visuals."
        );
    }
    Ok(())
}
fn err(e: impl std::fmt::Display) -> String {
    e.to_string()
}
fn check(code: i32, what: &str) -> Result<(), String> {
    if code == OK {
        Ok(())
    } else {
        Err(format!("{what}: {code}"))
    }
}
unsafe fn read_stats(stats: Stats) -> Result<RuntimeStats, String> {
    let mut out = RuntimeStats {
        size: size_of::<RuntimeStats>() as u32,
        ..Default::default()
    };
    check(unsafe { stats(&mut out) }, "stats")?;
    if out.last_error != OK {
        return Err(format!("runtime error: {out:?}"));
    }
    Ok(out)
}
fn step(
    gpu: &mut gpu::WarpHost,
    drive: Drive,
    seq: &mut u64,
    size: [f32; 2],
    scenario: u32,
    drain: bool,
) -> Result<(), String> {
    *seq += 1;
    gpu.clear_async().map_err(err)?;
    let above = gpu.frame(size[0], size[1], *seq, LAYER_ABOVE_UI);
    let below = gpu.frame(size[0], size[1], *seq, LAYER_BELOW_UI);
    let above_resource = gpu.resource(LAYER_ABOVE_UI);
    let below_resource = gpu.resource(LAYER_BELOW_UI);
    let mut command = NativeCommand {
        gpu,
        width: size[0],
        height: size[1],
    };
    check(
        unsafe {
            drive(
                &above,
                above_resource,
                &below,
                below_resource,
                Some(native_command),
                std::ptr::from_mut(&mut command).cast(),
                scenario,
            )
        },
        "native queue drive",
    )?;
    if let Some(e) = command.gpu.command_error.take() {
        return Err(e);
    }
    if drain {
        command.gpu.drain().map_err(err)?;
    }
    Ok(())
}
fn overlap(lower: &[u8], full: &[u8]) -> Vec<usize> {
    lower
        .as_chunks::<4>()
        .0
        .iter()
        .zip(full.as_chunks::<4>().0)
        .enumerate()
        .filter_map(|(index, (a, b))| {
            let (x, y) = (index % 1280, index / 1280);
            (x > 870
                && x < 1250
                && y > 100
                && y < 550
                && a[0] > 220
                && a[1] < 30
                && a[2] < 30
                && b[0] < 130
                && b[1] < 130
                && b[2] < 130
                && b[3] == 255)
                .then_some(index)
        })
        .collect()
}
fn assert_overlap(pixels: &[u8], reference: &[u8], roi: &[usize]) -> Result<(), String> {
    let bad = roi
        .iter()
        .filter(|&&index| {
            let p = &pixels[index * 4..][..4];
            let r = &reference[index * 4..][..4];
            p[3] != 255 || p[..3].iter().zip(r).any(|(&a, &b)| a.abs_diff(b) > 12)
        })
        .count();
    if bad > roi.len() / 100 {
        Err(format!(
            "upper Boss pixel coverage differs: {bad}/{} overlap pixels",
            roi.len()
        ))
    } else {
        Ok(())
    }
}
fn bmp(path: &Path, width: u32, height: u32, rgba: &[u8]) -> Result<(), String> {
    let len = 54 + rgba.len() as u32;
    let mut bytes = Vec::with_capacity(len as usize);
    bytes.extend_from_slice(b"BM");
    bytes.extend_from_slice(&len.to_le_bytes());
    bytes.extend_from_slice(&[0; 4]);
    bytes.extend_from_slice(&54u32.to_le_bytes());
    bytes.extend_from_slice(&40u32.to_le_bytes());
    bytes.extend_from_slice(&width.to_le_bytes());
    bytes.extend_from_slice(&(-(height as i32)).to_le_bytes());
    bytes.extend_from_slice(&1u16.to_le_bytes());
    bytes.extend_from_slice(&32u16.to_le_bytes());
    bytes.extend_from_slice(&[0; 24]);
    for p in rgba.as_chunks::<4>().0 {
        bytes.extend_from_slice(&[p[2], p[1], p[0], p[3]]);
    }
    std::fs::write(path, bytes).map_err(err)
}

fn host(control: &Control) -> HostApi {
    // Zero is valid for every nullable callback and integer field; core forwards
    // only the game/input fields and retains its actual texture/mesh callbacks.
    let mut api: HostApi = unsafe { std::mem::zeroed() };
    api.magic = ABI_MAGIC;
    api.size = size_of::<HostApi>() as u32;
    api.version = ABI_VERSION;
    api.user = std::ptr::from_ref(control).cast_mut().cast();
    api.log = Some(log);
    api.config_get = Some(config_get);
    api.key_pressed = Some(key);
    api.read_display_state = Some(display);
    api.read_map_state = Some(map);
    api.read_boss_state = Some(boss);
    api.read_event_flags = Some(flags);
    api.find_param_table = Some(param);
    api.read_memory = Some(memory);
    api.request_file = Some(request);
    api.poll_file = Some(poll);
    api.release_file = Some(release);
    api.ui_input_poll = Some(input);
    api.ui_input_clear = Some(clear);
    api.ui_input_capture = Some(capture);
    api
}
unsafe extern "C" fn log(_: *mut c_void, _: u32, text: ByteSlice) {
    if text.len < 16384 && !text.data.is_null() {
        println!(
            "plugin: {}",
            String::from_utf8_lossy(unsafe { std::slice::from_raw_parts(text.data, text.len) })
                .trim_end()
        );
    }
}
unsafe extern "C" fn config_get(
    _: *mut c_void,
    key: ByteSlice,
    out: *mut u8,
    capacity: usize,
) -> usize {
    let key = unsafe { std::slice::from_raw_parts(key.data, key.len) };
    let value: &[u8] = match key {
        b"minimap.border.width" => b"80",
        b"minimap.border.color" => b"255,0,0,255",
        b"minimap.presets.order" => b"compact",
        b"minimap.preset.compact.width" => b"70%",
        b"minimap.preset.compact.height" => b"80%",
        b"minimap.preset.compact.shape" => b"rect",
        b"minimap.preset.compact.rotate" => b"false",
        b"minimap.preset.compact.opacity" => b"100%",
        b"minimap.preset.compact.margin_right" => b"10",
        b"minimap.preset.compact.margin_top" => b"10",
        b"minimap.preset.compact.margin_left" | b"minimap.preset.compact.margin_bottom" => b"",
        b"boss.width" => b"440",
        b"boss.height" => b"600",
        b"boss.opacity" => b"1",
        b"boss.font_size" => b"20",
        b"boss.toggle_full_mode" => b"=",
        b"common.language" | b"runtime.game_language" => b"engUS",
        _ => return usize::MAX,
    };
    if !out.is_null() {
        unsafe { std::ptr::copy_nonoverlapping(value.as_ptr(), out, value.len().min(capacity)) };
    }
    value.len()
}
unsafe extern "C" fn key(user: *mut c_void, chord: u32) -> u32 {
    u32::from(
        chord == parse_chord("=")
            && unsafe { &*user.cast::<Control>() }
                .toggle
                .swap(false, Ordering::AcqRel),
    )
}
unsafe extern "C" fn display(_: *mut c_void, out: *mut DisplayState) -> i32 {
    unsafe {
        out.write(DisplayState {
            valid: 1,
            ..Default::default()
        })
    };
    OK
}
unsafe extern "C" fn map(_: *mut c_void, out: *mut MapState) -> i32 {
    unsafe {
        out.write(MapState {
            generation: 1,
            raw_map_id: 0x3c263600,
            map_id: 0,
            ..Default::default()
        })
    };
    OK
}
unsafe extern "C" fn boss(_: *mut c_void, out: *mut BossState) -> i32 {
    unsafe {
        out.write(BossState {
            size: size_of::<BossState>() as u32,
            valid: 1,
            context: 1,
            raw_map_id: 0x3c263600,
            ..Default::default()
        })
    };
    OK
}
unsafe extern "C" fn flags(_: *mut c_void, _: *const u32, out: *mut u8, len: usize) -> i32 {
    unsafe { std::ptr::write_bytes(out, 2, len) };
    ERR_UNAVAILABLE
}
unsafe extern "C" fn param(_: *mut c_void, _: u32) -> usize {
    0
}
unsafe extern "C" fn memory(_: *mut c_void, _: usize, _: *mut u8, _: usize) -> i32 {
    ERR_UNAVAILABLE
}
unsafe extern "C" fn request(_: *mut c_void, _: *const FileRequest) -> u64 {
    0
}
unsafe extern "C" fn poll(_: *mut c_void, _: u64, _: *mut FileView) -> i32 {
    ERR_UNAVAILABLE
}
unsafe extern "C" fn release(_: *mut c_void, _: u64) {}
unsafe extern "C" fn input(_: *mut c_void, _: *mut UiInput, _: usize, count: *mut usize) -> i32 {
    unsafe { count.write(0) };
    OK
}
unsafe extern "C" fn clear(_: *mut c_void) -> i32 {
    OK
}
unsafe extern "C" fn capture(user: *mut c_void, mask: u32) -> i32 {
    unsafe { &*user.cast::<Control>() }
        .capture
        .store(mask, Ordering::Release);
    OK
}
