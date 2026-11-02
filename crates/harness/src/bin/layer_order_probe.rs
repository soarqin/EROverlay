//! Real core + Boss + Minimap overlap test. Only game data and the native UI
//! compositor are controlled; DLL lifecycle, meshes, atlases and renderer are real.
use eroverlay_abi::*;
use eroverlay_renderer::NativeFrame;
use std::{
    collections::VecDeque,
    ffi::c_void,
    os::windows::ffi::OsStrExt,
    path::{Path, PathBuf},
    sync::{
        Mutex,
        atomic::{AtomicBool, AtomicI32, AtomicU32, AtomicU64, Ordering},
    },
    time::{Duration, Instant},
};
use windows::{
    Win32::{
        Foundation::{FreeLibrary, HMODULE},
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
type DriveBusy = unsafe extern "C" fn(
    *const NativeFrame,
    *mut c_void,
    *const NativeFrame,
    *mut c_void,
    Option<unsafe extern "C" fn(*mut c_void)>,
    *mut c_void,
    u32,
    u32,
) -> i32;
type TextureGate = unsafe extern "C" fn(u32) -> i32;
type TextureReject = unsafe extern "C" fn(u32) -> i32;
type TextureStats = unsafe extern "C" fn(*mut u64, usize) -> i32;
type BossFault = unsafe extern "C" fn(u32) -> i32;

#[derive(Default)]
struct Control {
    toggle: AtomicBool,
    toggle_shown: AtomicBool,
    capture: AtomicU32,
    display_mode: AtomicU32,
    state_mode: AtomicU32,
    deaths: AtomicU32,
    igt: AtomicU32,
    context: AtomicU64,
    inputs: Mutex<VecDeque<UiInput>>,
    input_polls: AtomicU64,
    text_events: AtomicU64,
    pause_update: AtomicBool,
    update_entered: AtomicBool,
    state_error: AtomicI32,
    state_reads: AtomicU64,
}
#[derive(Clone, Copy)]
struct Fixture {
    drive: Drive,
    stats: Stats,
    drive_busy: Option<DriveBusy>,
    texture_gate: Option<TextureGate>,
    texture_stats: Option<TextureStats>,
    texture_reject: Option<TextureReject>,
    boss_fault: Option<BossFault>,
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
struct RuntimeLifetime {
    stop: Stop,
    core: HMODULE,
    boss: HMODULE,
    closed: bool,
}
impl RuntimeLifetime {
    unsafe fn close_modules(&mut self) -> Result<(), String> {
        self.closed = true;
        unsafe {
            FreeLibrary(self.boss).map_err(err)?;
            FreeLibrary(self.core).map_err(err)?;
        }
        Ok(())
    }
}
impl Drop for RuntimeLifetime {
    fn drop(&mut self) {
        if self.closed {
            return;
        }
        // Declared before GPU: the GPU host drains first, then this stops the
        // actual worker before the controlled HostApi/user storage is dropped.
        let code = unsafe { (self.stop)() };
        if code == OK {
            let _ = unsafe { self.close_modules() };
        } else {
            eprintln!("探针异常收尾：core stop返回{code}，保留DLL引用至进程结束。");
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
    let case = args
        .next()
        .map(|v| v.to_string_lossy().into_owned())
        .unwrap_or_else(|| "all".into());
    if !["all", "basic", "duplicate", "atlas", "snapshot"].contains(&case.as_str()) {
        return Err("case must be all, basic, duplicate, atlas or snapshot".into());
    }
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
        macro_rules! optional {
            ($module:expr,$name:literal,$ty:ty) => {
                GetProcAddress($module, windows::core::s!($name))
                    .map(|f| std::mem::transmute::<unsafe extern "system" fn() -> isize, $ty>(f))
            };
        }
        let boss_path: Vec<u16> = directory
            .join("overlays/Boss.dll")
            .as_os_str()
            .encode_wide()
            .chain(Some(0))
            .collect();
        let boss_module = LoadLibraryW(PCWSTR(boss_path.as_ptr())).map_err(err)?;
        let fixture = Fixture {
            drive,
            stats,
            drive_busy: optional!(module, "eroverlay_test_layer_drive_busy", DriveBusy),
            texture_gate: optional!(module, "eroverlay_test_layer_texture_gate", TextureGate),
            texture_stats: optional!(module, "eroverlay_test_layer_texture_stats", TextureStats),
            texture_reject: optional!(module, "eroverlay_test_layer_texture_reject", TextureReject),
            boss_fault: optional!(boss_module, "eroverlay_test_boss_fault", BossFault),
        };
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
        let mut lifetime = RuntimeLifetime {
            stop,
            core: module,
            boss: boss_module,
            closed: false,
        };
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
        if case != "basic" {
            continuity(
                &mut gpu, fixture, &control, &mut seq, &reference, &lower, &roi, &output, &case,
            )?;
            if case != "all" {
                gpu.drain().map_err(err)?;
                check(stop(), "stop selected continuity case")?;
                gpu.check_validation()?;
                lifetime.close_modules()?;
                println!("PASS: selected continuously captured case {case}");
                return Ok(());
            }
        }

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
        if skipped.meshes != before.meshes {
            return Err("legacy raw lock contention submitted fresh geometry".into());
        }
        gpu.drain().map_err(err)?;
        gpu.check_validation()?;
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
        lifetime.close_modules()?;
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

#[derive(Clone, Copy, Debug)]
enum Coverage {
    Boss,
    Lower,
    Hidden,
    Closing,
    Overload,
}
struct View<'a> {
    reference: &'a [u8],
    lower: &'a [u8],
    roi: &'a [usize],
    output: &'a Path,
}
impl View<'_> {
    fn check(
        &self,
        capture: &gpu::Capture,
        label: &str,
        sequence: u64,
        expected: Coverage,
    ) -> Result<usize, String> {
        use std::io::Write;
        let (width, height, pixels) = gpu::WarpHost::finish_capture(capture).map_err(err)?;
        let bad = self
            .roi
            .iter()
            .filter(|&&index| {
                let p = &pixels[index * 4..][..4];
                match expected {
                    Coverage::Boss | Coverage::Lower | Coverage::Overload => {
                        let source = if matches!(expected, Coverage::Boss | Coverage::Overload) {
                            self.reference
                        } else {
                            self.lower
                        };
                        let r = &source[index * 4..][..4];
                        p[3] != 255 || p[..3].iter().zip(r).any(|(&a, &b)| a.abs_diff(b) > 12)
                    }
                    Coverage::Hidden => p[3] != 255 || p[0] > 12 || p[1] > 16 || p[2] > 21,
                    // egui intentionally fades a Window on its explicit close
                    // frame. This is separate from visible-frame continuity; its
                    // very next frame must be fully cleared (including when busy).
                    // RGB is intentionally animated, including widget hover/text
                    // colors; no opaque-coverage claim is made during this explicit
                    // transition. The next stable frame has the strict RGB oracle.
                    Coverage::Closing => p[3] != 255,
                }
            })
            .count();
        let mut trace = std::fs::OpenOptions::new()
            .create(true)
            .append(true)
            .open(self.output.join("presented-frames.log"))
            .map_err(err)?;
        let opaque_bad = self
            .roi
            .iter()
            .filter(|&&index| {
                let p = &pixels[index * 4..][..4];
                let r = &self.reference[index * 4..][..4];
                p[..3].iter().zip(r).any(|(&a, &b)| a.abs_diff(b) > 12)
            })
            .count();
        let mean_red = self
            .roi
            .iter()
            .map(|&index| u64::from(pixels[index * 4]))
            .sum::<u64>() as f64
            / self.roi.len() as f64;
        writeln!(
            trace,
            "sequence={sequence} case={label} expected={expected:?} bad_pixels={bad}/{} opaque_bad={opaque_bad} mean_red={mean_red:.3}",
            self.roi.len()
        )
        .map_err(err)?;
        if bad > self.roi.len() / 100 && !matches!(expected, Coverage::Overload) {
            let path = self.output.join(format!("FAIL-{label}-{sequence}.bmp"));
            bmp(&path, width, height, &pixels)?;
            return Err(format!(
                "continuity {label}: presented sequence {sequence} lost {expected:?} coverage ({bad}/{} pixels), {}",
                self.roi.len(),
                path.display()
            ));
        }
        Ok(opaque_bad)
    }
}
struct Continuity<'a> {
    gpu: &'a mut gpu::WarpHost,
    fixture: Fixture,
    control: &'a Control,
    seq: &'a mut u64,
    view: View<'a>,
}
impl Continuity<'_> {
    fn frame(&mut self, label: &str, expected: Coverage, mode: u32) -> Result<(), String> {
        *self.seq += 1;
        self.frame_at(label, *self.seq, expected, mode, [1280., 900.])
    }
    fn frame_at(
        &mut self,
        label: &str,
        sequence: u64,
        expected: Coverage,
        mode: u32,
        size: [f32; 2],
    ) -> Result<(), String> {
        let capture = present(self.gpu, self.fixture, sequence, mode, size)?;
        self.gpu.drain().map_err(err)?;
        self.view
            .check(&capture, label, sequence, expected)
            .map(|_| ())
    }
    fn events(&self, events: impl IntoIterator<Item = UiInput>) {
        self.control
            .inputs
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .extend(events);
    }
    fn sample_state(&mut self, label: &str) -> Result<(), String> {
        let before = self.control.state_reads.load(Ordering::Acquire);
        let until = Instant::now() + Duration::from_secs(2);
        while self.control.state_reads.load(Ordering::Acquire) == before {
            self.frame(label, Coverage::Boss, 0)?;
            if Instant::now() > until {
                return Err(format!("fixture BossState was not sampled: {label}"));
            }
            std::thread::sleep(Duration::from_millis(5));
        }
        self.frame(label, Coverage::Boss, 0)
    }
    fn duplicate(&mut self) -> Result<(), String> {
        self.frame("duplicate-prior-valid", Coverage::Boss, 0)?;
        let polls = self.control.input_polls.load(Ordering::Acquire);
        for _ in 0..4 {
            self.frame_at(
                "cleared-duplicate",
                *self.seq,
                Coverage::Boss,
                0,
                [1280., 900.],
            )?;
        }
        if self.control.input_polls.load(Ordering::Acquire) != polls {
            return Err("duplicate callbacks advanced real Boss input/egui".into());
        }
        let old = *self.seq;
        self.frame("interleave-new", Coverage::Boss, 0)?;
        self.frame_at("interleave-old", old, Coverage::Boss, 0, [1280., 900.])?;
        self.frame("interleave-forward", Coverage::Boss, 0)?;
        println!(
            "continuity PASS: every cleared duplicate/interleaved presented frame remains opaque; duplicate input polls unchanged"
        );
        Ok(())
    }
    fn atlas(&mut self) -> Result<(), String> {
        let before = texture_stats(self.fixture)?;
        if let Some(gate) = self.fixture.texture_gate {
            check(unsafe { gate(1) }, "hold actual texture transfer")?;
        }
        self.events([
            event(UI_INPUT_POINTER, 0, 0, 1000., 255.),
            event(UI_INPUT_BUTTON, 0, UI_INPUT_DOWN, 1000., 255.),
        ]);
        self.frame("atlas-focus-down", Coverage::Boss, 0)?;
        self.events([event(UI_INPUT_BUTTON, 0, 0, 1000., 255.)]);
        self.frame("atlas-focus-up", Coverage::Boss, 0)?;
        let consumed = self.control.text_events.load(Ordering::Acquire);
        self.events(
            "龙龘"
                .chars()
                .map(|c| event(UI_INPUT_TEXT, c as u32, 0, 0., 0.)),
        );
        let held: Result<(), String> = (|| {
            for _ in 0..6 {
                self.frame("atlas-new-glyph-pending", Coverage::Boss, 0)?;
            }
            if self.control.text_events.load(Ordering::Acquire) < consumed + 2 {
                return Err("real Boss did not consume fixture Unicode text".into());
            }
            let after = texture_stats(self.fixture)?;
            if let (Some(before), Some(after)) = (before, after) {
                if after[0] <= before[0] {
                    return Err(format!(
                        "new glyph input did not request a real atlas: before={before:?}, after={after:?}"
                    ));
                }
                println!("real atlas held pending: counters before={before:?}, held={after:?}");
            }
            Ok(())
        })();
        if let Some(gate) = self.fixture.texture_gate {
            check(unsafe { gate(0) }, "release actual texture transfer")?;
        }
        held?;
        for _ in 0..10 {
            self.frame("atlas-ready-transition", Coverage::Boss, 0)?;
            std::thread::sleep(Duration::from_millis(2));
        }
        println!(
            "continuity PASS: fresh glyphs and actual pending→ready atlas transition never expose Minimap"
        );
        self.rejected_atlas()?;
        Ok(())
    }
    fn rejected_atlas(&mut self) -> Result<(), String> {
        let reject = self
            .fixture
            .texture_reject
            .ok_or("missing real texture admission rejection fixture")?;
        let gate = self
            .fixture
            .texture_gate
            .ok_or("missing real texture transfer gate")?;
        let before = texture_stats(self.fixture)?.ok_or("missing real texture counters")?;
        check(unsafe { reject(2) }, "reject two real atlas admissions")?;
        check(unsafe { gate(1) }, "hold retried atlas transfer")?;
        self.events(
            "测试汉字"
                .chars()
                .map(|c| event(UI_INPUT_TEXT, c as u32, 0, 0., 0.)),
        );
        let started = Instant::now();
        let result: Result<(), String> = (|| {
            let meshes = unsafe { read_stats(self.fixture.stats) }?.meshes;
            // Only L holds the renderer lock: H still executes the real Boss
            // callback. A rejected dirty atlas must return BUSY, not fresh
            // meshes combining new UVs with an old admitted atlas handle.
            self.frame("atlas-admission-rejected", Coverage::Boss, 3)?;
            let after = unsafe { read_stats(self.fixture.stats) }?.meshes;
            if after != meshes {
                return Err(format!(
                    "rejected dirty atlas emitted fresh geometry: {meshes}→{after}"
                ));
            }
            if texture_stats(self.fixture)?.unwrap()[0] != before[0] {
                return Err("rejected atlas unexpectedly obtained a registered handle".into());
            }
            loop {
                self.frame("atlas-admission-retry", Coverage::Boss, 3)?;
                let stats = texture_stats(self.fixture)?.unwrap();
                if stats[0] > before[0] {
                    if stats[1] == 0 {
                        return Err(format!(
                            "accepted atlas did not remain genuinely prepared behind transfer gate: {stats:?}"
                        ));
                    }
                    println!(
                        "real atlas rejected twice then admitted after {:.3}s: {before:?}→{stats:?}",
                        started.elapsed().as_secs_f64()
                    );
                    break;
                }
                if started.elapsed() > Duration::from_secs(3) {
                    return Err("real rejected atlas did not retry within three seconds".into());
                }
                std::thread::sleep(Duration::from_millis(17));
            }
            Ok(())
        })();
        check(unsafe { gate(0) }, "release retried atlas transfer")?;
        check(unsafe { reject(0) }, "clear rejection control")?;
        result?;
        let sequence = *self.seq;
        let polls = self.control.input_polls.load(Ordering::Acquire);
        let until = Instant::now() + Duration::from_secs(2);
        loop {
            // Both callbacks carry an already-consumed sequence on freshly
            // cleared targets. No UI/egui advance can incidentally drive upload.
            self.frame_at(
                "atlas-upload-during-duplicate-only",
                sequence,
                Coverage::Boss,
                0,
                [1280., 900.],
            )?;
            let stats = texture_stats(self.fixture)?.unwrap();
            if stats[1] == 0 && stats[2] == 0 && stats[3] >= before[3] && stats[4] == 0 {
                println!(
                    "duplicate-only upload maintenance reached real GPU ready without UI advance: {stats:?}"
                );
                break;
            }
            if Instant::now() > until {
                return Err(format!(
                    "duplicate-only upload maintenance stalled: {stats:?}"
                ));
            }
            std::thread::sleep(Duration::from_millis(2));
        }
        if self.control.input_polls.load(Ordering::Acquire) != polls {
            return Err("upload-only duplicate callbacks advanced real Boss UI/input".into());
        }
        for _ in 0..10 {
            self.frame("atlas-retried-gpu-ready", Coverage::Boss, 0)?;
            std::thread::sleep(Duration::from_millis(2));
        }
        println!(
            "continuity PASS: actual atlas admission rejection→backoff→retry→prepared→GPU ready preserves complete Boss pixels and rejects incompatible fresh geometry"
        );
        Ok(())
    }
    fn snapshot(&mut self) -> Result<(), String> {
        let fault=self.fixture.boss_fault.ok_or("snapshot case requires a continuity-fixture Boss.dll; production DLL must not expose test faults")?;
        for mask in [1, 2, 3, 1, 2, 3] {
            check(unsafe { fault(mask) }, "arm real Boss mutex contention")?;
            self.frame(&format!("boss-lock-{mask}"), Coverage::Boss, 0)?;
        }
        println!(
            "continuity PASS: actual Boss snapshot/UI mutex contention preserves every presented frame"
        );
        Ok(())
    }
    fn busy(&mut self) -> Result<(), String> {
        if self.fixture.drive_busy.is_none() {
            return Err("full continuity suite requires the native drive_busy fixture".into());
        }
        for mode in [1, 2, 3, 4, 5, 1, 2, 5] {
            self.frame(&format!("core-stage-lock-{mode}"), Coverage::Boss, mode)?;
        }
        self.gpu.drain().map_err(err)?;
        let gate = OpenGate(self.gpu.hold_queue().map_err(err)?);
        let mut captures = Vec::new();
        for _ in 0..6 {
            *self.seq += 1;
            captures.push((
                *self.seq,
                present(self.gpu, self.fixture, *self.seq, 0, [1280., 900.])?,
            ));
        }
        drop(gate);
        self.gpu.drain().map_err(err)?;
        for (sequence, capture) in &captures {
            self.view
                .check(capture, "multiframe-inflight", *sequence, Coverage::Boss)?;
        }
        println!(
            "continuity PASS: six async presented frames, including all-four geometry slots busy, each checked independently"
        );
        Ok(())
    }
    fn visibility(&mut self) -> Result<(), String> {
        for mode in [1, 2, 3] {
            self.control.display_mode.store(mode, Ordering::Release);
            for busy in [0, 1, 2, 5] {
                self.frame(
                    &format!("display-unknown-{mode}-core-{busy}"),
                    Coverage::Boss,
                    busy,
                )?;
                if self.control.capture.load(Ordering::Acquire) != 0 {
                    return Err("Display Unknown + core contention retained capture".into());
                }
                if let Some(fault) = self.fixture.boss_fault {
                    check(unsafe { fault(2) }, "arm Unknown plus UI lock")?;
                    self.frame(
                        &format!("display-unknown-{mode}-core-{busy}-ui-busy"),
                        Coverage::Boss,
                        busy,
                    )?;
                    if self.control.capture.load(Ordering::Acquire) != 0 {
                        return Err("Display Unknown + core/UI contention retained capture".into());
                    }
                }
            }
            self.control.display_mode.store(0, Ordering::Release);
            self.frame("display-unknown-return-visible", Coverage::Boss, 0)?;
        }
        self.control.state_mode.store(1, Ordering::Release);
        self.sample_state("save-valid-zero")?;
        self.control.state_mode.store(0, Ordering::Release);
        self.control.state_error.store(ERR_BUSY, Ordering::Release);
        self.sample_state("save-read-busy")?;
        self.control.state_error.store(0, Ordering::Release);
        self.control.deaths.store(123456789, Ordering::Release);
        self.control.igt.store(123456789, Ordering::Release);
        self.sample_state("save-valid-new-digits")?;
        self.control.update_entered.store(false, Ordering::Release);
        self.control.pause_update.store(true, Ordering::Release);
        let deadline = Instant::now() + Duration::from_secs(2);
        while !self.control.update_entered.load(Ordering::Acquire) {
            self.frame("worker-await-update-guard", Coverage::Boss, 0)?;
            if Instant::now() > deadline {
                self.control.pause_update.store(false, Ordering::Release);
                return Err("controlled update callback never entered".into());
            }
            std::thread::sleep(Duration::from_millis(5));
        }
        let held: Result<(), String> = (|| {
            for _ in 0..6 {
                self.frame("worker-holds-update-guard", Coverage::Boss, 0)?;
            }
            Ok(())
        })();
        self.control.pause_update.store(false, Ordering::Release);
        held?;
        println!(
            "continuity PASS: Display unknown, invalid/unavailable save state, and real update-mutex hold do not erase an already-visible Boss"
        );
        Ok(())
    }
    fn hides(&mut self) -> Result<(), String> {
        for mode in [4, 5] {
            self.control.display_mode.store(mode, Ordering::Release);
            self.frame(&format!("explicit-game-hide-{mode}"), Coverage::Hidden, 0)?;
            self.frame(
                &format!("explicit-game-hide-beats-busy-{mode}"),
                Coverage::Hidden,
                1,
            )?;
            if self.control.capture.load(Ordering::Acquire) != 0 {
                return Err("true loading/full-map hide retained input capture".into());
            }
            for unknown in [1, 2, 3] {
                self.control.display_mode.store(unknown, Ordering::Release);
                for busy in [0, 1, 2, 5] {
                    self.frame(
                        &format!("known-hide-{mode}-then-unknown-{unknown}-busy-{busy}"),
                        Coverage::Hidden,
                        busy,
                    )?;
                    if self.control.capture.load(Ordering::Acquire) != 0 {
                        return Err(
                            "KnownHidden→Unknown retained capture or resurrected paint".into()
                        );
                    }
                }
            }
            self.control.display_mode.store(0, Ordering::Release);
            self.frame("explicit-game-hide-reopen", Coverage::Boss, 0)?;
        }
        self.control.toggle_shown.store(true, Ordering::Release);
        await_key(&self.control.toggle_shown)?;
        if let Some(fault) = self.fixture.boss_fault {
            check(unsafe { fault(2) }, "hide while the real UI mutex is busy")?;
        }
        self.frame("user-hide", Coverage::Lower, 0)?;
        self.frame("user-hide-beats-busy", Coverage::Lower, 2)?;
        if self.control.capture.load(Ordering::Acquire) != 0 {
            return Err("user hide retained input capture".into());
        }
        self.control.toggle_shown.store(true, Ordering::Release);
        await_key(&self.control.toggle_shown)?;
        if let Some(fault) = self.fixture.boss_fault {
            check(unsafe { fault(0) }, "clear any unconsumed hidden UI fault")?;
        }
        self.frame("user-show", Coverage::Boss, 0)?;
        self.control.toggle_shown.store(true, Ordering::Release);
        await_key(&self.control.toggle_shown)?;
        self.frame("fresh-empty-presentation-mutex-busy", Coverage::Lower, 6)?;
        self.frame("fresh-empty-then-painting-mutex-busy", Coverage::Lower, 5)?;
        if self.control.capture.load(Ordering::Acquire) != 0 {
            return Err("deferred fresh-empty invalidation retained input capture".into());
        }
        self.control.toggle_shown.store(true, Ordering::Release);
        await_key(&self.control.toggle_shown)?;
        self.frame("fresh-empty-reshow", Coverage::Boss, 0)?;
        self.events([event(UI_INPUT_KEY, 0x1b, UI_INPUT_DOWN, 0., 0.)]);
        self.frame("user-close-escape-intentional-fade", Coverage::Closing, 0)?;
        if self.control.capture.load(Ordering::Acquire) != 0 {
            return Err("explicit close did not release input capture".into());
        }
        self.events([event(UI_INPUT_KEY, 0x1b, 0, 0., 0.)]);
        self.frame("closed-panel-stays-empty", Coverage::Lower, 1)?;
        self.control.toggle.store(true, Ordering::Release);
        await_key(&self.control.toggle)?;
        // Only an explicit Window reopen permits a bounded fade-in. Once the
        // first opaque image is reached, the ordinary strict oracle applies to
        // every subsequent presentation, including the rest of this interval.
        let texture_before = texture_stats(self.fixture)?;
        let mut first_opaque = None;
        let mut last_red = 255.;
        for index in 0..10 {
            *self.seq += 1;
            let capture = present(self.gpu, self.fixture, *self.seq, 0, [1280., 900.])?;
            self.gpu.drain().map_err(err)?;
            let (_, _, pixels) = gpu::WarpHost::finish_capture(&capture).map_err(err)?;
            let red = self
                .view
                .roi
                .iter()
                .map(|&i| u64::from(pixels[i * 4]))
                .sum::<u64>() as f64
                / self.view.roi.len() as f64;
            if red > last_red + 2. {
                return Err(format!(
                    "explicit reopen fade regressed: frame {index}, mean red {last_red:.3}→{red:.3}"
                ));
            }
            last_red = red;
            let bad = self.view.check(
                &capture,
                "closed-panel-reopen-intentional-fade",
                *self.seq,
                if first_opaque.is_some() {
                    Coverage::Boss
                } else {
                    Coverage::Closing
                },
            )?;
            if bad <= self.view.roi.len() / 100 {
                first_opaque.get_or_insert(index + 1);
            }
        }
        self.frame("closed-panel-reopen-opaque", Coverage::Boss, 0)?;
        println!(
            "explicit reopen: first opaque frame={first_opaque:?}, fixed transition bound=11 presentations; real texture counters {texture_before:?}→{:?}",
            texture_stats(self.fixture)?
        );
        println!(
            "continuity PASS: real hides/close clear retained paint immediately, beat busy fallback, release input, and reopen"
        );
        Ok(())
    }
    fn overload(&mut self) -> Result<(), String> {
        self.gpu.drain().map_err(err)?;
        let gate = OpenGate(self.gpu.hold_queue().map_err(err)?);
        let mut captures = Vec::new();
        // Deliberately exceed both bounded pools while preventing *any* GPU
        // completion. This is artificial overload, not ordinary lock contention.
        for _ in 0..24 {
            *self.seq += 1;
            captures.push((
                *self.seq,
                present(self.gpu, self.fixture, *self.seq, 0, [1280., 900.])?,
            ));
        }
        drop(gate);
        self.gpu.drain().map_err(err)?;
        let mut affected = 0;
        for (sequence, capture) in &captures {
            if self.view.check(
                capture,
                "artificial-pool-overload",
                *sequence,
                Coverage::Overload,
            )? > self.view.roi.len() / 100
            {
                affected += 1;
            }
        }
        self.frame("artificial-pool-overload-recovery", Coverage::Boss, 0)?;
        self.gpu.check_validation()?;
        println!(
            "artificial overload boundary: 24 queued presentations with GPU held; {affected}/24 differ from opaque Boss; after drain the next frame recovers. This interval is excluded from ordinary continuity PASS."
        );
        Ok(())
    }
}
fn check(code: i32, what: &str) -> Result<(), String> {
    if code == OK {
        Ok(())
    } else {
        Err(format!("{what}: {code}"))
    }
}
#[allow(clippy::too_many_arguments)]
fn continuity(
    gpu: &mut gpu::WarpHost,
    fixture: Fixture,
    control: &Control,
    seq: &mut u64,
    reference: &[u8],
    lower: &[u8],
    roi: &[usize],
    output: &Path,
    case: &str,
) -> Result<(), String> {
    let mut test = Continuity {
        gpu,
        fixture,
        control,
        seq,
        view: View {
            reference,
            lower,
            roi,
            output,
        },
    };
    match case {
        "duplicate" => return test.duplicate(),
        "atlas" => return test.atlas(),
        "snapshot" => return test.snapshot(),
        _ => {}
    }
    test.atlas()?;
    test.duplicate()?;
    if fixture.boss_fault.is_some() {
        test.snapshot()?;
    } else {
        println!(
            "internal Boss locks not injected: production Boss has no fixture export; run separately with continuity-fixture Boss"
        );
    }
    test.busy()?;
    test.visibility()?;
    test.hides()?;
    for size in [[1280., 880.], [1280., 900.], [1280., 860.], [1280., 900.]] {
        *test.seq += 1;
        test.frame_at("viewport-every-present", *test.seq, Coverage::Boss, 0, size)?;
    }
    println!("continuity PASS: viewport changes checked on each presented frame");
    test.overload()?;
    Ok(())
}
fn event(kind: u32, code: u32, flags: u32, x: f32, y: f32) -> UiInput {
    UiInput {
        size: size_of::<UiInput>() as u32,
        kind,
        code,
        flags,
        x,
        y,
        ..Default::default()
    }
}
fn await_key(key: &AtomicBool) -> Result<(), String> {
    let until = Instant::now() + Duration::from_millis(800);
    while key.load(Ordering::Acquire) {
        if Instant::now() > until {
            return Err("fixture hotkey was not acknowledged on update".into());
        }
        std::thread::sleep(Duration::from_millis(1));
    }
    Ok(())
}
fn texture_stats(fixture: Fixture) -> Result<Option<[u64; 5]>, String> {
    let Some(read) = fixture.texture_stats else {
        return Ok(None);
    };
    let mut values = [0; 5];
    check(
        unsafe { read(values.as_mut_ptr(), values.len()) },
        "actual texture counters",
    )?;
    Ok(Some(values))
}
fn present(
    gpu: &mut gpu::WarpHost,
    fixture: Fixture,
    sequence: u64,
    mode: u32,
    size: [f32; 2],
) -> Result<gpu::Capture, String> {
    gpu.clear_async().map_err(err)?;
    let above = gpu.frame(size[0], size[1], sequence, LAYER_ABOVE_UI);
    let below = gpu.frame(size[0], size[1], sequence, LAYER_BELOW_UI);
    let above_resource = gpu.resource(LAYER_ABOVE_UI);
    let below_resource = gpu.resource(LAYER_BELOW_UI);
    let mut command = NativeCommand {
        gpu,
        width: size[0],
        height: size[1],
    };
    let user = std::ptr::from_mut(&mut command).cast();
    let result = if mode == 0 {
        unsafe {
            (fixture.drive)(
                &above,
                above_resource,
                &below,
                below_resource,
                Some(native_command),
                user,
                0,
            )
        }
    } else {
        let drive = fixture
            .drive_busy
            .ok_or("missing actual native drive_busy")?;
        unsafe {
            drive(
                &above,
                above_resource,
                &below,
                below_resource,
                Some(native_command),
                user,
                0,
                mode,
            )
        }
    };
    check(result, "continuous native L/U/H")?;
    if let Some(error) = command.gpu.command_error.take() {
        return Err(error);
    }
    command.gpu.capture_async().map_err(err)
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
        b"boss.toggle" => b"F11",
        b"common.language" | b"runtime.game_language" => b"engUS",
        _ => return usize::MAX,
    };
    if !out.is_null() {
        unsafe { std::ptr::copy_nonoverlapping(value.as_ptr(), out, value.len().min(capacity)) };
    }
    value.len()
}
unsafe extern "C" fn key(user: *mut c_void, chord: u32) -> u32 {
    let c = unsafe { &*user.cast::<Control>() };
    u32::from(if chord == parse_chord("=") {
        c.toggle.swap(false, Ordering::AcqRel)
    } else if chord == parse_chord("F11") {
        c.toggle_shown.swap(false, Ordering::AcqRel)
    } else {
        false
    })
}
unsafe extern "C" fn display(user: *mut c_void, out: *mut DisplayState) -> i32 {
    let mode = unsafe { &*user.cast::<Control>() }
        .display_mode
        .load(Ordering::Acquire);
    unsafe {
        out.write(DisplayState {
            valid: u32::from(mode == 0 || mode >= 4),
            loading_or_logo: u32::from(mode == 4),
            full_screen_map: u32::from(mode == 5),
        })
    };
    match mode {
        2 => ERR_BUSY,
        3 => ERR_UNAVAILABLE,
        _ => OK,
    }
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
unsafe extern "C" fn boss(user: *mut c_void, out: *mut BossState) -> i32 {
    let c = unsafe { &*user.cast::<Control>() };
    c.state_reads.fetch_add(1, Ordering::Relaxed);
    if c.pause_update.load(Ordering::Acquire) {
        c.update_entered.store(true, Ordering::Release);
        let start = Instant::now();
        // Update-only bounded fake game read; render must use the last snapshot.
        while c.pause_update.load(Ordering::Acquire)
            && start.elapsed() < Duration::from_millis(1500)
        {
            std::thread::sleep(Duration::from_millis(1));
        }
    }
    let error = c.state_error.load(Ordering::Acquire);
    if error != 0 {
        return error;
    }
    unsafe {
        out.write(BossState {
            size: size_of::<BossState>() as u32,
            valid: u32::from(c.state_mode.load(Ordering::Acquire) == 0),
            context: c.context.load(Ordering::Acquire).max(1),
            raw_map_id: 0x3c263600,
            deaths: c.deaths.load(Ordering::Acquire),
            in_game_time_ms: c.igt.load(Ordering::Acquire),
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
unsafe extern "C" fn input(
    user: *mut c_void,
    out: *mut UiInput,
    capacity: usize,
    count: *mut usize,
) -> i32 {
    unsafe { count.write(0) };
    let c = unsafe { &*user.cast::<Control>() };
    c.input_polls.fetch_add(1, Ordering::Relaxed);
    let Ok(mut input) = c.inputs.try_lock() else {
        return ERR_BUSY;
    };
    let n = input.len().min(capacity);
    for i in 0..n {
        let event = input.pop_front().unwrap();
        if event.kind == UI_INPUT_TEXT {
            c.text_events.fetch_add(1, Ordering::Relaxed);
        }
        unsafe { out.add(i).write(event) };
    }
    unsafe { count.write(n) };
    OK
}
unsafe extern "C" fn clear(user: *mut c_void) -> i32 {
    if let Ok(mut input) = unsafe { &*user.cast::<Control>() }.inputs.try_lock() {
        input.clear();
    }
    OK
}
unsafe extern "C" fn capture(user: *mut c_void, mask: u32) -> i32 {
    unsafe { &*user.cast::<Control>() }
        .capture
        .store(mask, Ordering::Release);
    OK
}
