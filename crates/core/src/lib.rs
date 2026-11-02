#![allow(non_snake_case)]
mod config;
#[cfg(feature = "layer-order-fixture")]
mod layer_fixture;
mod logging;
mod painting;
mod plugin;
mod rendering;
mod staging;
mod texture;
mod ui_input;

use eroverlay_abi::*;
use eroverlay_game::GameServices;
use eroverlay_renderer::{NativeBridge, NativeFrame, PresentationHandle, Renderer};
use std::{
    cell::Cell,
    ffi::c_void,
    path::PathBuf,
    sync::{
        Arc, Mutex, OnceLock, RwLock,
        atomic::{AtomicBool, AtomicI32, AtomicU32, AtomicU64, Ordering},
    },
    thread::{self, JoinHandle},
    time::{Duration, Instant},
};

thread_local! {static STAGING:Cell<*mut staging::Staging>=const{Cell::new(std::ptr::null_mut())};}
static RUNTIME: OnceLock<Mutex<Option<Arc<Runtime>>>> = OnceLock::new();
static TRANSITION: Mutex<()> = Mutex::new(());
static DETACHING: AtomicBool = AtomicBool::new(false);

struct Input {
    epoch: u64,
    keys: std::collections::HashMap<u32, (bool, u64, bool)>,
}
struct Host {
    config: config::Config,
    log: logging::Log,
    display: eroverlay_game::DisplayReader,
    game: Mutex<GameServices>,
    textures: texture::Textures,
    input: Mutex<Input>,
    unload_chord: u32,
}
struct RenderState {
    renderer: Option<Renderer>,
    identity: (usize, usize),
    generation: u64,
}
struct Runtime {
    host: Box<Host>,
    _api: Box<HostApi>,
    plugins: Vec<plugin::Plugin>,
    render_layers: u32,
    accepting: AtomicBool,
    callbacks: RwLock<()>,
    render: Mutex<RenderState>,
    painting: Mutex<painting::Painting>,
    presentation: RwLock<Option<PresentationHandle>>,
    upper_groups: Vec<usize>,
    continuity: [AtomicU64; 6],
    renderer_generation: AtomicU64,
    display_hidden: AtomicBool,
    invalidate_presentation: AtomicBool,
    bridge: Mutex<Option<NativeBridge>>,
    native_input: Mutex<Option<ui_input::NativeInput>>,
    worker: Mutex<Option<JoinHandle<()>>>,
    updates: AtomicU64,
    renders: AtomicU64,
    meshes: AtomicU64,
    dropped: AtomicU64,
    gpu_bytes: AtomicU64,
    rejected: AtomicU32,
    error: AtomicI32,
    diagnostics: logging::Diagnostics,
}
// SAFETY: HostApi is immutable, user points at stable synchronized Host. Plugin
// objects implement callback synchronization and native bridge drains on stop.
unsafe impl Send for Runtime {}
unsafe impl Sync for Runtime {}

fn guarded(f: impl FnOnce() -> i32) -> i32 {
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(f)).unwrap_or(ERR_PANIC)
}
unsafe fn host<'a>(user: *mut c_void) -> Option<&'a Host> {
    unsafe { user.cast::<Host>().as_ref() }
}
unsafe fn bytes<'a>(slice: ByteSlice, max: usize) -> Option<&'a [u8]> {
    if slice.len > max || (slice.len != 0 && slice.data.is_null()) {
        return None;
    }
    if slice.len == 0 {
        return Some(&[]);
    }
    Some(unsafe { std::slice::from_raw_parts(slice.data, slice.len) })
}
unsafe extern "C" fn log(user: *mut c_void, _: u32, message: ByteSlice) {
    if let (Some(host), Some(b)) = (unsafe { host(user) }, unsafe { bytes(message, 4096) }) {
        host.log.write(b);
    }
}
unsafe extern "C" fn config_get(
    user: *mut c_void,
    key: ByteSlice,
    out: *mut u8,
    capacity: usize,
) -> usize {
    let Some(h) = (unsafe { host(user) }) else {
        return 0;
    };
    let Some(key) = (unsafe { bytes(key, 512) }).and_then(|v| std::str::from_utf8(v).ok()) else {
        return 0;
    };
    let Some(value) = h.config.get(key) else {
        return usize::MAX;
    };
    if !out.is_null() && capacity > 0 {
        unsafe { std::ptr::copy_nonoverlapping(value.as_ptr(), out, value.len().min(capacity)) };
    }
    value.len()
}
unsafe extern "C" fn key_pressed(user: *mut c_void, chord: u32) -> u32 {
    let Some(h) = (unsafe { host(user) }) else {
        return 0;
    };
    let Ok(mut input) = h.input.lock() else {
        return 0;
    };
    let epoch = input.epoch;
    let entry = input.keys.entry(chord).or_insert((false, u64::MAX, false));
    if entry.1 != epoch {
        use windows::Win32::UI::Input::KeyboardAndMouse::GetAsyncKeyState;
        let down = |vk: i32| unsafe { GetAsyncKeyState(vk) } < 0;
        use windows::Win32::{
            System::Threading::GetCurrentProcessId,
            UI::WindowsAndMessaging::{GetForegroundWindow, GetWindowThreadProcessId},
        };
        let mut process = 0;
        let foreground = unsafe { GetForegroundWindow() };
        if !foreground.is_invalid() {
            unsafe { GetWindowThreadProcessId(foreground, Some(&mut process)) };
        }
        let modifiers = if down(0x11) { MOD_CTRL } else { 0 }
            | if down(0x10) { MOD_SHIFT } else { 0 }
            | if down(0x12) { MOD_ALT } else { 0 }
            | if down(0x5b) || down(0x5c) { MOD_WIN } else { 0 };
        let pressed = process == unsafe { GetCurrentProcessId() }
            && chord != 0
            && (chord & 0xffff == 0 || down((chord & 0xffff) as i32))
            && modifiers == chord & (MOD_CTRL | MOD_SHIFT | MOD_ALT | MOD_WIN);
        entry.2 = pressed && !entry.0;
        entry.0 = pressed;
        entry.1 = epoch;
    }
    u32::from(entry.2)
}
unsafe extern "C" fn map_state(user: *mut c_void, out: *mut MapState) -> i32 {
    guarded(|| {
        let (Some(h), Some(out)) = (unsafe { host(user) }, unsafe { out.as_mut() }) else {
            return ERR_INVALID;
        };
        let Ok(game) = h.game.try_lock() else {
            return ERR_BUSY;
        };
        if let Some(state) = game.read_map_state() {
            *out = state;
            OK
        } else {
            ERR_UNAVAILABLE
        }
    })
}
unsafe extern "C" fn display_state(user: *mut c_void, out: *mut DisplayState) -> i32 {
    guarded(|| {
        let (Some(h), Some(out)) = (unsafe { host(user) }, unsafe { out.as_mut() }) else {
            return ERR_INVALID;
        };
        *out = DisplayState::default();
        *out = h.display.read();
        if out.valid != 0 { OK } else { ERR_UNAVAILABLE }
    })
}
unsafe extern "C" fn game_layout(user: *mut c_void, out: *mut GameLayout) -> i32 {
    guarded(|| {
        let (Some(h), Some(out)) = (unsafe { host(user) }, unsafe { out.as_mut() }) else {
            return ERR_INVALID;
        };
        let Ok(game) = h.game.try_lock() else {
            return ERR_BUSY;
        };
        if let Some(layout) = game.game_layout() {
            *out = layout;
            OK
        } else {
            ERR_UNAVAILABLE
        }
    })
}
unsafe extern "C" fn read_memory(_: *mut c_void, address: usize, out: *mut u8, len: usize) -> i32 {
    guarded(|| {
        if out.is_null() || len > MAX_FILE_BYTES {
            return ERR_INVALID;
        }
        let buffer = unsafe { std::slice::from_raw_parts_mut(out, len) };
        if eroverlay_game::memory::read_memory(address, buffer) {
            OK
        } else {
            ERR_UNAVAILABLE
        }
    })
}
unsafe extern "C" fn event_flag(user: *mut c_void, id: u32) -> u32 {
    unsafe { host(user) }
        .and_then(|h| h.game.try_lock().ok())
        .is_some_and(|mut g| g.event_flag(id)) as u32
}
unsafe extern "C" fn boss_state(user: *mut c_void, out: *mut BossState) -> i32 {
    guarded(|| {
        let Some(h) = (unsafe { host(user) }) else {
            return ERR_INVALID;
        };
        if out.is_null() || !(out as usize).is_multiple_of(align_of::<BossState>()) {
            return ERR_INVALID;
        }
        // Read only the fixed size word before accepting the whole POD buffer.
        if (unsafe { out.cast::<u32>().read() } as usize) < size_of::<BossState>() {
            return ERR_ABI;
        }
        unsafe {
            *out = BossState {
                size: size_of::<BossState>() as u32,
                ..BossState::default()
            }
        };
        let Ok(mut game) = h.game.try_lock() else {
            return ERR_BUSY;
        };
        let Some(state) = game.boss_state() else {
            return ERR_UNAVAILABLE;
        };
        unsafe { *out = state };
        OK
    })
}
unsafe extern "C" fn event_flags(
    user: *mut c_void,
    ids: *const u32,
    out: *mut u8,
    len: usize,
) -> i32 {
    guarded(|| {
        let Some(h) = (unsafe { host(user) }) else {
            return ERR_INVALID;
        };
        if len > 4096
            || (len != 0
                && (ids.is_null()
                    || out.is_null()
                    || !(ids as usize).is_multiple_of(align_of::<u32>())))
        {
            return ERR_INVALID;
        }
        if len == 0 {
            return OK;
        }
        // SAFETY: ABI caller supplies disjoint readable IDs and writable bytes.
        let (ids, out) = unsafe {
            (
                std::slice::from_raw_parts(ids, len),
                std::slice::from_raw_parts_mut(out, len),
            )
        };
        out.fill(2);
        let Ok(mut game) = h.game.try_lock() else {
            return ERR_BUSY;
        };
        if game.boss_flags(ids, out) {
            OK
        } else {
            ERR_UNAVAILABLE
        }
    })
}
unsafe extern "C" fn revive_boss(
    user: *mut c_void,
    context: u64,
    primary: u32,
    writes: *const FlagWrite,
    len: usize,
) -> i32 {
    guarded(|| {
        let Some(h) = (unsafe { host(user) }) else {
            return ERR_INVALID;
        };
        if len > 2048
            || primary == 0
            || primary == u32::MAX
            || (len != 0
                && (writes.is_null() || !(writes as usize).is_multiple_of(align_of::<FlagWrite>())))
        {
            return ERR_INVALID;
        }
        let writes = if len == 0 {
            &[]
        } else {
            // SAFETY: ABI caller supplies len readable FlagWrite entries.
            unsafe { std::slice::from_raw_parts(writes, len) }
        };
        if writes.iter().any(|write| {
            write.id == 0 || write.id == u32::MAX || write.id == primary || write.value > 1
        }) {
            return ERR_INVALID;
        }
        let Ok(mut game) = h.game.try_lock() else {
            return ERR_BUSY;
        };
        if game.revive_boss(context, primary, writes) {
            OK
        } else {
            ERR_UNAVAILABLE
        }
    })
}
unsafe extern "C" fn param_table(user: *mut c_void, id: u32) -> usize {
    unsafe { host(user) }
        .and_then(|h| h.game.try_lock().ok())
        .map_or(0, |g| g.find_param_table(id))
}
unsafe extern "C" fn request_file(user: *mut c_void, request: *const FileRequest) -> u64 {
    let (Some(h), Some(r)) = (unsafe { host(user) }, unsafe { request.as_ref() }) else {
        return 0;
    };
    if (r.size as usize) < size_of::<FileRequest>() {
        return 0;
    }
    let Some(path) = (unsafe { bytes(r.path, 32768) }).and_then(|b| std::str::from_utf8(b).ok())
    else {
        return 0;
    };
    let Some(part) = (unsafe { bytes(r.part, 1024) }).and_then(|b| std::str::from_utf8(b).ok())
    else {
        return 0;
    };
    h.game.try_lock().map_or(0, |mut g| {
        g.request_file(
            path,
            (!part.is_empty()).then_some(part),
            r.flags,
            (r.max_bytes as usize).min(MAX_FILE_BYTES),
        )
    })
}
unsafe extern "C" fn poll_file(user: *mut c_void, token: u64, out: *mut FileView) -> i32 {
    guarded(|| {
        let (Some(h), Some(out)) = (unsafe { host(user) }, unsafe { out.as_mut() }) else {
            return ERR_INVALID;
        };
        let Ok(game) = h.game.try_lock() else {
            return ERR_BUSY;
        };
        let poll = game.poll_file(token);
        *out = FileView {
            status: poll.status,
            reserved: 0,
            bytes: ByteSlice::new(poll.bytes),
        };
        OK
    })
}
unsafe extern "C" fn release_file(user: *mut c_void, token: u64) {
    if let Some(h) = unsafe { host(user) } {
        h.game
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .release_file(token);
    }
}
unsafe extern "C" fn texture_create(user: *mut c_void, data: ByteSlice) -> u64 {
    let (Some(h), Some(b)) = (unsafe { host(user) }, unsafe {
        bytes(data, MAX_FILE_BYTES)
    }) else {
        return 0;
    };
    h.textures.dds(b)
}
unsafe extern "C" fn texture_rgba(
    user: *mut c_void,
    width: u32,
    height: u32,
    data: ByteSlice,
) -> u64 {
    #[cfg(feature = "layer-order-fixture")]
    if layer_fixture::reject_rgba() {
        return 0;
    }
    let (Some(h), Some(b)) = (unsafe { host(user) }, unsafe {
        bytes(data, MAX_FILE_BYTES)
    }) else {
        return 0;
    };
    h.textures.rgba(width, height, b)
}
unsafe extern "C" fn texture_poll(user: *mut c_void, token: u64, out: *mut TextureView) -> i32 {
    let (Some(h), Some(out)) = (unsafe { host(user) }, unsafe { out.as_mut() }) else {
        return ERR_INVALID;
    };
    *out = h.textures.poll(token);
    OK
}
unsafe extern "C" fn texture_release(user: *mut c_void, token: u64) {
    if let Some(h) = unsafe { host(user) } {
        h.textures.release(token);
    }
}
unsafe extern "C" fn submit_mesh(_: *mut c_void, mesh: *const Mesh) -> i32 {
    guarded(|| {
        let Some(mesh) = (unsafe { mesh.as_ref() }) else {
            return ERR_INVALID;
        };
        STAGING.with(|s| {
            unsafe { s.get().as_mut() }.map_or(ERR_UNAVAILABLE, |s| unsafe { s.submit(mesh) })
        })
    })
}
unsafe extern "C" fn render_options(_: *mut c_void, options: *const RenderOptions) -> i32 {
    let Some(options) = (unsafe { options.as_ref() }) else {
        return ERR_INVALID;
    };
    if options.clip_rect.iter().any(|v| !v.is_finite())
        || !options.opacity.is_finite()
        || !(0.0..=1.0).contains(&options.opacity)
        || !options.corner_radius.is_finite()
        || !options.border_width.is_finite()
        || options.border_width < 0.0
    {
        return ERR_INVALID;
    }
    STAGING.with(|s| {
        unsafe { s.get().as_mut() }.map_or(ERR_UNAVAILABLE, |s| {
            s.options = *options;
            OK
        })
    })
}

impl Runtime {
    fn create(directory: PathBuf, native: bool) -> Result<Arc<Self>, i32> {
        let config = config::Config::load(&directory);
        let logger = logging::Log::new(&config);
        let game = if native {
            GameServices::discover().map_err(|e| {
                logger.write(e.as_bytes());
                ERR_UNAVAILABLE
            })?
        } else {
            GameServices::offline()
        };
        let display = game.display_reader();
        let log_enabled = logger.enabled();
        let unload_chord = parse_chord(config.get("input.unload").unwrap_or(""));
        let mut host = Box::new(Host {
            config,
            log: logger,
            display,
            game: Mutex::new(game),
            textures: texture::Textures::new(),
            input: Mutex::new(Input {
                epoch: 0,
                keys: std::collections::HashMap::new(),
            }),
            unload_chord,
        });
        let api = Box::new(HostApi {
            magic: ABI_MAGIC,
            size: size_of::<HostApi>() as u32,
            version: ABI_VERSION,
            capabilities: CAP_GAME
                | CAP_FILES
                | CAP_TEXTURES
                | CAP_MESH
                | CAP_INPUT
                | CAP_CONCURRENT
                | CAP_UI_INPUT
                | CAP_BOSS_STATE,
            user: (&mut *host as *mut Host).cast(),
            log: log_enabled.then_some(log),
            config_get: Some(config_get),
            key_pressed: Some(key_pressed),
            read_map_state: Some(map_state),
            read_display_state: Some(display_state),
            read_game_layout: Some(game_layout),
            read_memory: Some(read_memory),
            read_event_flag: Some(event_flag),
            find_param_table: Some(param_table),
            request_file: Some(request_file),
            poll_file: Some(poll_file),
            release_file: Some(release_file),
            texture_create: Some(texture_create),
            texture_poll: Some(texture_poll),
            texture_release: Some(texture_release),
            submit_mesh: Some(submit_mesh),
            texture_create_rgba: Some(texture_rgba),
            set_render_options: Some(render_options),
            ui_input_poll: Some(ui_input::poll),
            ui_input_clear: Some(ui_input::clear),
            ui_input_capture: Some(ui_input::capture),
            read_boss_state: Some(boss_state),
            read_event_flags: Some(event_flags),
            revive_boss: Some(revive_boss),
        });
        #[cfg(feature = "layer-order-fixture")]
        let api = layer_fixture::configure_api(api);
        let mut plugins = Vec::new();
        let mut rejected = 0;
        let mut paths: Vec<_> = std::fs::read_dir(directory.join("overlays"))
            .map_err(|_| ERR_UNAVAILABLE)?
            .filter_map(Result::ok)
            .map(|e| e.path())
            .filter(|p| p.extension().is_some_and(|x| x.eq_ignore_ascii_case("dll")))
            .collect();
        paths.sort();
        for path in paths {
            match plugin::Plugin::load(&path, &api) {
                Ok(plugin) => plugins.push(plugin),
                Err(e) => {
                    rejected += 1;
                    eprintln!("[EROverlay Rust] {}: {e}", path.display());
                }
            }
        }
        if plugins.is_empty() {
            return Err(ERR_UNAVAILABLE);
        }
        let render_layers = plugins
            .iter()
            .fold(0, |mask, plugin| mask | (1 << plugin.layer()));
        let painting = painting::Painting::new(plugins.len());
        let upper_groups = plugins
            .iter()
            .enumerate()
            .filter(|(_, plugin)| plugin.layer() == LAYER_ABOVE_UI)
            .map(|(index, _)| index + 1)
            .collect();
        let runtime = Arc::new(Self {
            host,
            _api: api,
            plugins,
            render_layers,
            accepting: AtomicBool::new(true),
            callbacks: RwLock::new(()),
            render: Mutex::new(RenderState {
                renderer: None,
                identity: (0, 0),
                generation: 0,
            }),
            painting: Mutex::new(painting),
            presentation: RwLock::new(None),
            upper_groups,
            continuity: std::array::from_fn(|_| AtomicU64::new(0)),
            renderer_generation: AtomicU64::new(0),
            // Unknown startup is not a confirmed global hide. Each plugin
            // decides its initial visibility; Boss has no initial cached image.
            display_hidden: AtomicBool::new(false),
            invalidate_presentation: AtomicBool::new(false),
            bridge: Mutex::new(None),
            native_input: Mutex::new(None),
            worker: Mutex::new(None),
            updates: AtomicU64::new(0),
            renders: AtomicU64::new(0),
            meshes: AtomicU64::new(0),
            dropped: AtomicU64::new(0),
            gpu_bytes: AtomicU64::new(0),
            rejected: AtomicU32::new(rejected),
            error: AtomicI32::new(OK),
            diagnostics: logging::Diagnostics::new(log_enabled),
        });
        if native {
            let bridge = unsafe {
                NativeBridge::install(native_frame, Arc::as_ptr(&runtime) as *mut c_void)
            }
            .map_err(|e| {
                eprintln!("[EROverlay Rust] native hook: {e}");
                ERR_UNAVAILABLE
            })?;
            *runtime.bridge.lock().unwrap_or_else(|e| e.into_inner()) = Some(bridge);
            if runtime.plugins.iter().any(plugin::Plugin::needs_ui_input) {
                *runtime
                    .native_input
                    .lock()
                    .unwrap_or_else(|e| e.into_inner()) = Some(ui_input::NativeInput::start());
            }
        }
        let weak = Arc::downgrade(&runtime);
        let worker = thread::Builder::new()
            .name("EROverlay update".into())
            .spawn(move || {
                let interval = Duration::from_nanos(1_000_000_000 / 60);
                while let Some(owned) = weak.upgrade() {
                    if !owned.accepting.load(Ordering::Acquire) || DETACHING.load(Ordering::Acquire)
                    {
                        break;
                    }
                    let start = Instant::now();
                    if guarded(|| {
                        owned.update();
                        OK
                    }) == ERR_PANIC
                    {
                        owned.error.store(ERR_PANIC, Ordering::Relaxed);
                        owned.accepting.store(false, Ordering::Release);
                    }
                    drop(owned);
                    thread::sleep(interval.saturating_sub(start.elapsed()));
                }
            })
            .map_err(|_| ERR_UNAVAILABLE)?;
        *runtime.worker.lock().unwrap_or_else(|e| e.into_inner()) = Some(worker);
        Ok(runtime)
    }
    fn update(&self) {
        eroverlay_game::memory::set_tracking(self.host.log.enabled());
        let total = self.diagnostics.stage(logging::Stage::Update);
        let Ok(_guard) = self.callbacks.read() else {
            return;
        };
        if !self.accepting.load(Ordering::Acquire) {
            return;
        }
        if let Ok(mut input) = self.native_input.try_lock()
            && let Some(input) = input.as_mut()
        {
            let display = self.host.display.read();
            input.update(display.valid != 0 && display.loading_or_logo == 0);
        }
        if let Ok(mut input) = self.host.input.lock() {
            input.epoch = input.epoch.wrapping_add(1);
        }
        let mut reason = u64::MAX;
        {
            let _phase = self.diagnostics.stage(logging::Stage::Game);
            if let Ok(mut game) = self.host.game.lock() {
                game.update_snapshot();
                if self.host.log.enabled() {
                    reason = game.diagnostics().reason as u64;
                }
            }
        }
        let plugins = self.diagnostics.stage(logging::Stage::UpdatePlugins);
        for plugin in &self.plugins {
            let code = plugin.update();
            if code != OK && code != ERR_BUSY {
                self.error.store(code, Ordering::Relaxed);
            }
        }
        drop(plugins);
        if self.host.unload_chord != 0
            && unsafe {
                key_pressed(
                    (&*self.host as *const Host).cast_mut().cast(),
                    self.host.unload_chord,
                )
            } != 0
        {
            self.accepting.store(false, Ordering::Release);
        }
        let tick = self.updates.fetch_add(1, Ordering::Relaxed) + 1;
        drop(total);
        if self.diagnostics.should_report(tick, reason) {
            self.host.log.write(self.diagnostics.report().as_bytes());
            let textures = self.host.textures.diagnostics();
            let gpu = self.render.try_lock().ok().map(|r| {
                (
                    r.identity,
                    r.generation,
                    r.renderer.as_ref().map(Renderer::diagnostics),
                )
            });
            self.host.log.write(format!("core-queues update={tick} renders={} dropped={} last_error={} display_map_state={reason:#x} textures={textures:?} renderer_identity_generation_state={gpu:?}",
                self.renders.load(Ordering::Relaxed), self.dropped.load(Ordering::Relaxed), self.error.load(Ordering::Relaxed)).as_bytes());
            let continuity = self
                .continuity
                .each_ref()
                .map(|c| c.load(Ordering::Relaxed));
            self.host.log.write(format!("core-continuity [gpu_busy,gate_busy,paint_busy,plugin_busy_or_duplicate,replay,known_hide]={continuity:?}").as_bytes());
            if let Ok(game) = self.host.game.try_lock() {
                let snapshot = game.diagnostics();
                let files = game.file_diagnostics();
                self.host.log.write(
                    format!(
                        "core-game reason={} flag_reason={} snapshot={snapshot:?} files={files:?}",
                        snapshot.reason.key(),
                        snapshot.flags.reason.key()
                    )
                    .as_bytes(),
                );
            }
        }
    }
    fn stop(&self) -> i32 {
        self.accepting.store(false, Ordering::Release);
        if let Some(mut input) = self
            .native_input
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .take()
        {
            input.stop();
        }
        if let Some(mut bridge) = self.bridge.lock().unwrap_or_else(|e| e.into_inner()).take() {
            bridge.stop();
            if NativeBridge::requires_module_retention() {
                self.host.log.write(b"Native hooks stopped; detour code remains pinned until process exit (STOPPED_RESIDENT=1).\n");
            }
        }
        if let Some(worker) = self.worker.lock().unwrap_or_else(|e| e.into_inner()).take() {
            let _ = worker.join();
        }
        let _guard = self.callbacks.write().unwrap_or_else(|e| e.into_inner());
        let mut render = self.render.lock().unwrap_or_else(|e| e.into_inner());
        if let Some(presentation) = self
            .presentation
            .read()
            .unwrap_or_else(|e| e.into_inner())
            .as_ref()
        {
            // A fallback may hold an Arc after leaving the callback gate. Close
            // admission before draining its independent command slots.
            presentation.close();
        }
        if render.renderer.as_mut().is_some_and(|r| !r.wait_idle(5000)) {
            return ERR_BUSY;
        }
        render.renderer = None;
        self.presentation
            .write()
            .unwrap_or_else(|e| e.into_inner())
            .take();
        self.painting
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .reset(0);
        self.gpu_bytes.store(0, Ordering::Relaxed);
        if !self
            .host
            .game
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .stop()
        {
            return ERR_BUSY;
        }
        OK
    }
}
impl Drop for Runtime {
    fn drop(&mut self) {
        self.accepting.store(false, Ordering::Release);
        if let Ok(bridge) = self.bridge.get_mut() {
            bridge.take();
        }
        // Plugin destroy callbacks still need the live host function table and
        // texture/file services. Explicitly destroy them before field teardown.
        self.plugins.clear();
    }
}
unsafe extern "C" fn native_frame(user: *mut c_void, frame: *const NativeFrame) {
    let _ = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        if let (Some(runtime), Some(frame)) = (unsafe { user.cast::<Runtime>().as_ref() }, unsafe {
            frame.as_ref()
        }) {
            runtime.render(frame);
        }
    }));
    STAGING.with(|s| s.set(std::ptr::null_mut()));
}

/// # Safety
/// options and its UTF-16 directory buffer must be readable during this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn eroverlay_start(options: *const StartOptions) -> i32 {
    unsafe { start_runtime(options, false) }
}
unsafe fn start_runtime(options: *const StartOptions, automatic: bool) -> i32 {
    guarded(|| {
        let Ok(_transition) = TRANSITION.try_lock() else {
            return ERR_BUSY;
        };
        // Serialize cancellation with the same transition that owns creation:
        // an external stop returning OK must prevent any later auto-start.
        #[cfg(windows)]
        if automatic && entry::is_external_stop_requested() {
            return ERR_UNAVAILABLE;
        }
        let Some(options) = (unsafe { options.as_ref() }) else {
            return ERR_INVALID;
        };
        if options.size as usize != size_of::<StartOptions>()
            || options.version != ABI_VERSION
            || options.directory.is_null()
            || options.directory_len == 0
            || options.directory_len > 32767
        {
            return ERR_ABI;
        }
        use std::os::windows::ffi::OsStringExt;
        let directory = PathBuf::from(std::ffi::OsString::from_wide(unsafe {
            std::slice::from_raw_parts(options.directory, options.directory_len)
        }));
        if !directory.is_absolute() {
            return ERR_INVALID;
        }
        let mut slot = RUNTIME
            .get_or_init(|| Mutex::new(None))
            .lock()
            .unwrap_or_else(|e| e.into_inner());
        if slot.is_some() {
            return ERR_BUSY;
        }
        // Removed detours can still have threads in their entry/return code.
        // NativeBridge pins that code; a fresh native session needs a new
        // process instead of reusing process-global hook state after stop.
        if options.native != 0 && NativeBridge::requires_module_retention() {
            return ERR_UNAVAILABLE;
        }
        match Runtime::create(directory, options.native != 0) {
            Ok(runtime) => {
                *slot = Some(runtime);
                OK
            }
            Err(error) => error,
        }
    })
}
/// External loaders retain their LoadLibrary reference through OK or
/// STOPPED_RESIDENT, then may release that reference. The latter keeps native
/// detour code resident until process exit. ERR_BUSY requires retaining and
/// retrying. Loaders must disable input.unload or serialize calls against
/// key-driven self-unload;
/// no DLL export may race a caller's final FreeLibrary/self-unload operation.
#[unsafe(no_mangle)]
pub extern "C" fn eroverlay_stop() -> i32 {
    // An explicit host owns its LoadLibrary reference and releases it only after
    // this returns a completed status. Bootstrap never consumes that reference.
    #[cfg(windows)]
    entry::external_stop_requested();
    stop_runtime()
}
fn stop_runtime() -> i32 {
    guarded(|| {
        let Ok(_transition) = TRANSITION.try_lock() else {
            return ERR_BUSY;
        };
        let mut slot = RUNTIME
            .get_or_init(|| Mutex::new(None))
            .lock()
            .unwrap_or_else(|e| e.into_inner());
        let Some(runtime) = slot.as_ref() else {
            return stopped_status(NativeBridge::requires_module_retention());
        };
        let result = runtime.stop();
        if result == OK {
            slot.take();
            return stopped_status(NativeBridge::requires_module_retention());
        }
        result
    })
}
fn stopped_status(resident: bool) -> i32 {
    if resident { STOPPED_RESIDENT } else { OK }
}
fn stop_completed(result: i32) -> bool {
    matches!(result, OK | STOPPED_RESIDENT)
}
/// Controlled local functions only; this export is absent from shipped builds.
#[cfg(feature = "native-lifecycle-fixture")]
#[unsafe(no_mangle)]
pub extern "C" fn eroverlay_test_native_lifecycle() -> i32 {
    unsafe extern "C" {
        fn er_rust_scene_lifecycle_check() -> bool;
    }
    guarded(|| {
        if unsafe { er_rust_scene_lifecycle_check() } {
            OK
        } else {
            ERR_UNAVAILABLE
        }
    })
}
/// Hidden Win32 message and real device-hook fixture; not shipped or injected.
#[cfg(feature = "native-lifecycle-fixture")]
#[unsafe(no_mangle)]
pub extern "C" fn eroverlay_test_ui_input() -> i32 {
    guarded(|| {
        // The native fixture pins this DLL and initializes the shared MinHook
        // before any process-lifetime input detour becomes reachable.
        if eroverlay_test_native_lifecycle() != OK {
            return ERR_UNAVAILABLE;
        }
        if ui_input::fixture() {
            OK
        } else {
            ERR_UNAVAILABLE
        }
    })
}
/// # Safety
/// out points to a writable RuntimeStats with size set to its available bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn eroverlay_stats(out: *mut RuntimeStats) -> i32 {
    guarded(|| {
        let Some(out) = (unsafe { out.as_mut() }) else {
            return ERR_INVALID;
        };
        if (out.size as usize) < size_of::<RuntimeStats>() {
            return ERR_ABI;
        }
        let slot = RUNTIME
            .get_or_init(|| Mutex::new(None))
            .lock()
            .unwrap_or_else(|e| e.into_inner());
        *out = RuntimeStats {
            size: size_of::<RuntimeStats>() as u32,
            ..RuntimeStats::default()
        };
        if let Some(r) = slot.as_ref() {
            out.state = if r.accepting.load(Ordering::Acquire) {
                1
            } else {
                2
            };
            out.plugins_loaded = r.plugins.len() as u32;
            out.plugins_rejected = r.rejected.load(Ordering::Relaxed);
            out.updates = r.updates.load(Ordering::Relaxed);
            out.renders = r.renders.load(Ordering::Relaxed);
            out.meshes = r.meshes.load(Ordering::Relaxed);
            out.dropped_frames = r.dropped.load(Ordering::Relaxed);
            out.gpu_committed_bytes = r.gpu_bytes.load(Ordering::Relaxed);
            out.last_error = r.error.load(Ordering::Relaxed);
        }
        OK
    })
}

/// Controlled render host entry used by offline D3D12 verification, never injected.
/// # Safety
/// frame is a valid borrowed D3D12 native frame for the duration of this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn eroverlay_render(frame: *const NativeFrame) -> i32 {
    guarded(|| {
        let Some(frame) = (unsafe { frame.as_ref() }) else {
            return ERR_INVALID;
        };
        let runtime = RUNTIME
            .get_or_init(|| Mutex::new(None))
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .as_ref()
            .cloned();
        if let Some(r) = runtime {
            r.render(frame);
            OK
        } else {
            ERR_UNAVAILABLE
        }
    })
}

#[cfg(windows)]
mod entry {
    use super::*;
    use windows::{
        Win32::{
            Foundation::{CloseHandle, FreeLibrary, HINSTANCE, HMODULE},
            System::{
                LibraryLoader::{
                    DisableThreadLibraryCalls, FreeLibraryAndExitThread,
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, GetModuleFileNameW, GetModuleHandleExW,
                },
                Threading::CreateThread,
            },
        },
        core::{BOOL, PCWSTR, w},
    };
    static EXTERNAL_STOP: AtomicBool = AtomicBool::new(false);

    pub(super) fn external_stop_requested() {
        EXTERNAL_STOP.store(true, Ordering::Release);
    }
    pub(super) fn is_external_stop_requested() -> bool {
        EXTERNAL_STOP.load(Ordering::Acquire)
    }
    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn GetEnvironmentVariableW(name: *const u16, value: *mut u16, size: u32) -> u32;
    }
    unsafe extern "system" fn bootstrap(module: *mut c_void) -> u32 {
        let module = HMODULE(module);
        // DllMain acquired this thread's reference before it could be scheduled.
        // All Rust locals are dropped before atomically releasing the reference
        // and exiting; returning into an unloaded DLL is never possible.
        let code = match std::panic::catch_unwind(|| bootstrap_run(module)) {
            Ok(code) => code,
            Err(_) => {
                // Even an unexpected bootstrap panic cannot release the code
                // reference while game callbacks or GPU work remain in flight.
                while !stop_completed(stop_runtime()) {
                    std::thread::sleep(Duration::from_millis(50));
                }
                1
            }
        };
        unsafe { FreeLibraryAndExitThread(module, code) }
    }
    fn bootstrap_run(module: HMODULE) -> u32 {
        if EXTERNAL_STOP.load(Ordering::Acquire) {
            return 0;
        }
        let mut path = [0u16; 32768];
        let len = unsafe { GetModuleFileNameW(Some(module), &mut path) } as usize;
        if len == 0 || len >= path.len() {
            return 1;
        }
        let Some(slash) = path[..len]
            .iter()
            .rposition(|&c| c == b'\\' as u16 || c == b'/' as u16)
        else {
            return 1;
        };
        let options = StartOptions {
            size: size_of::<StartOptions>() as u32,
            version: ABI_VERSION,
            native: 1,
            reserved: 0,
            directory: path.as_ptr(),
            directory_len: slash,
        };
        if unsafe { start_runtime(&options, true) } != OK {
            return 1;
        }
        loop {
            if DETACHING.load(Ordering::Acquire) {
                return 0;
            }
            std::thread::sleep(Duration::from_millis(50));
            let external_stop = EXTERNAL_STOP.load(Ordering::Acquire);
            let stop_requested = external_stop
                || RUNTIME.get().is_some_and(|r| {
                    r.lock().is_ok_and(|r| {
                        r.as_ref()
                            .is_some_and(|r| !r.accepting.load(Ordering::Acquire))
                    })
                });
            if stop_requested && stop_completed(stop_runtime()) {
                if !EXTERNAL_STOP.load(Ordering::Acquire) {
                    // Key-driven self-unload consumes the original injector
                    // reference while the bootstrap reference still protects us.
                    // External loaders must not race this path: disable the
                    // unload chord, retain their reference through stop()==0/1,
                    // then release only their own reference with FreeLibrary.
                    let _ = unsafe { FreeLibrary(module) };
                }
                return 0;
            }
            if RUNTIME
                .get()
                .is_some_and(|r| r.lock().is_ok_and(|r| r.is_none()))
            {
                return 0;
            }
        }
    }
    /// DllMain schedules initialization only. Scanning, I/O, plugins and hooks run
    /// after the loader lock is released. Explicit offline hosts opt out.
    #[unsafe(no_mangle)]
    pub unsafe extern "system" fn DllMain(module: HINSTANCE, reason: u32, _: *mut c_void) -> BOOL {
        if reason == 1 {
            DETACHING.store(false, Ordering::Release);
            EXTERNAL_STOP.store(false, Ordering::Release);
            let _ =
                unsafe { DisableThreadLibraryCalls(windows::Win32::Foundation::HMODULE(module.0)) };
            let mut value = [0u16; 2];
            let offline = unsafe {
                GetEnvironmentVariableW(
                    w!("EROVERLAY_RUST_OFFLINE_HOST").as_ptr(),
                    value.as_mut_ptr(),
                    value.len() as u32,
                )
            } > 0
                && value[0] == b'1' as u16;
            if !offline {
                let mut owned = HMODULE::default();
                // Acquire before CreateThread: acquiring inside bootstrap leaves
                // an unload race before that new thread has begun executing.
                // No private locks, allocation, I/O or thread waits occur here.
                if unsafe {
                    GetModuleHandleExW(
                        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                        PCWSTR(bootstrap as *const () as *const u16),
                        &mut owned,
                    )
                }
                .is_err()
                {
                    return BOOL(0);
                }
                if let Ok(thread) = unsafe {
                    CreateThread(
                        None,
                        0,
                        Some(bootstrap),
                        Some(owned.0),
                        Default::default(),
                        None,
                    )
                } {
                    let _ = unsafe { CloseHandle(thread) };
                } else {
                    // Original loader reference is still held, so this rollback
                    // only decrements our extra reference and cannot unload us.
                    let _ = unsafe { FreeLibrary(owned) };
                    return BOOL(0);
                }
            }
        } else if reason == 0 {
            DETACHING.store(true, Ordering::Release);
        }
        BOOL(1)
    }

    #[cfg(test)]
    mod tests {
        use super::*;
        #[test]
        fn stopped_resident_is_terminal_without_claiming_unload() {
            assert_eq!(stopped_status(false), OK);
            assert_eq!(stopped_status(true), STOPPED_RESIDENT);
            assert!(stop_completed(OK));
            assert!(stop_completed(STOPPED_RESIDENT));
            assert!(!stop_completed(ERR_BUSY));
            assert!(!stop_completed(ERR_PANIC));
        }
        #[test]
        fn successful_external_stop_cancels_late_automatic_start() {
            EXTERNAL_STOP.store(false, Ordering::Release);
            assert_eq!(eroverlay_stop(), OK);
            // Cancellation is checked before dereferencing options/discovery.
            assert_eq!(
                unsafe { start_runtime(std::ptr::null(), true) },
                ERR_UNAVAILABLE
            );
            // Explicit hosts can still perform subsequent ordinary starts.
            assert_eq!(
                unsafe { start_runtime(std::ptr::null(), false) },
                ERR_INVALID
            );
            EXTERNAL_STOP.store(false, Ordering::Release);
        }
    }
}
