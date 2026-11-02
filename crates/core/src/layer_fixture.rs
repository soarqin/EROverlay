//! Explicit offline test core only. Game inputs may be controlled; plugin loading,
//! mesh/texture ownership, native queue callbacks and Runtime::render stay real.
use super::*;

#[derive(Clone, Copy)]
struct Forward(HostApi);
// SAFETY: the fixture caller owns a synchronized HostApi/user until stop returns.
// Registration is forbidden while a Runtime exists; the table is copied once.
unsafe impl Send for Forward {}
static FORWARD: Mutex<Option<Forward>> = Mutex::new(None);
static TRANSFERS_PAUSED: AtomicBool = AtomicBool::new(false);
static REJECT_RGBA: AtomicU32 = AtomicU32::new(0);

pub(super) fn transfers_paused() -> bool {
    TRANSFERS_PAUSED.load(Ordering::Acquire)
}
pub(super) fn reject_rgba() -> bool {
    REJECT_RGBA
        .try_update(Ordering::AcqRel, Ordering::Acquire, |n| n.checked_sub(1))
        .is_ok()
}

fn table() -> Option<Forward> {
    *FORWARD.lock().unwrap_or_else(|e| e.into_inner())
}

macro_rules! forward {
    ($name:ident($($arg:ident: $ty:ty),*)) => {
        unsafe extern "C" fn $name(_user: *mut c_void, $($arg: $ty),*) {
            if let Some(Forward(api)) = table()
                && let Some(f) = api.$name
            {
                unsafe { f(api.user, $($arg),*) };
            }
        }
    };
    ($name:ident($($arg:ident: $ty:ty),*) -> $result:ty, $fallback:expr) => {
        unsafe extern "C" fn $name(_user: *mut c_void, $($arg: $ty),*) -> $result {
            let Some(Forward(api)) = table() else { return $fallback; };
            api.$name.map_or($fallback, |f| unsafe { f(api.user, $($arg),*) })
        }
    };
}
forward!(log(level: u32, value: ByteSlice));
forward!(key_pressed(chord: u32) -> u32, 0);
forward!(read_map_state(out: *mut MapState) -> i32, ERR_UNAVAILABLE);
forward!(
    read_display_state(out: *mut DisplayState) -> i32,
    ERR_UNAVAILABLE
);
forward!(
    read_game_layout(out: *mut GameLayout) -> i32,
    ERR_UNAVAILABLE
);
forward!(
    read_memory(address: usize, out: *mut u8, len: usize) -> i32,
    ERR_UNAVAILABLE
);
forward!(read_event_flag(id: u32) -> u32, 0);
forward!(find_param_table(group: u32) -> usize, 0);
forward!(request_file(request: *const FileRequest) -> u64, 0);
forward!(
    poll_file(token: u64, out: *mut FileView) -> i32,
    ERR_UNAVAILABLE
);
forward!(release_file(token: u64));
forward!(
    ui_input_poll(out: *mut UiInput, capacity: usize, count: *mut usize) -> i32,
    ERR_UNAVAILABLE
);
forward!(ui_input_clear() -> i32, OK);
forward!(ui_input_capture(mask: u32) -> i32, OK);
forward!(read_boss_state(out: *mut BossState) -> i32, ERR_UNAVAILABLE);
forward!(
    read_event_flags(ids: *const u32, out: *mut u8, len: usize) -> i32,
    ERR_UNAVAILABLE
);
forward!(
    revive_boss(context: u64, primary: u32, extras: *const FlagWrite, len: usize) -> i32,
    ERR_UNAVAILABLE
);

unsafe extern "C" fn config_get(
    user: *mut c_void,
    key: ByteSlice,
    out: *mut u8,
    capacity: usize,
) -> usize {
    if let Some(Forward(api)) = table()
        && let Some(f) = api.config_get
    {
        let value = unsafe { f(api.user, key, out, capacity) };
        if value != usize::MAX {
            return value;
        }
    }
    unsafe { super::config_get(user, key, out, capacity) }
}

pub(super) fn configure_api(mut api: Box<HostApi>) -> Box<HostApi> {
    if table().is_some() {
        api.log = Some(log);
        api.config_get = Some(config_get);
        api.key_pressed = Some(key_pressed);
        api.read_map_state = Some(read_map_state);
        api.read_display_state = Some(read_display_state);
        api.read_game_layout = Some(read_game_layout);
        api.read_memory = Some(read_memory);
        api.read_event_flag = Some(read_event_flag);
        api.find_param_table = Some(find_param_table);
        api.request_file = Some(request_file);
        api.poll_file = Some(poll_file);
        api.release_file = Some(release_file);
        api.ui_input_poll = Some(ui_input_poll);
        api.ui_input_clear = Some(ui_input_clear);
        api.ui_input_capture = Some(ui_input_capture);
        api.read_boss_state = Some(read_boss_state);
        api.read_event_flags = Some(read_event_flags);
        api.revive_boss = Some(revive_boss);
    }
    api
}

/// # Safety
/// The copied callbacks and their synchronized user outlive the offline runtime.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn eroverlay_test_layer_host(api: *const HostApi) -> i32 {
    guarded(|| {
        let Ok(_transition) = TRANSITION.try_lock() else {
            return ERR_BUSY;
        };
        if RUNTIME
            .get_or_init(|| Mutex::new(None))
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .is_some()
        {
            return ERR_BUSY;
        }
        if api.is_null() || !api.is_aligned() {
            return ERR_INVALID;
        }
        let header = unsafe { &*api };
        if !valid_header(
            header.magic,
            header.size,
            header.version,
            size_of::<HostApi>(),
        ) {
            return ERR_ABI;
        }
        *FORWARD.lock().unwrap_or_else(|e| e.into_inner()) = Some(Forward(*header));
        TRANSFERS_PAUSED.store(false, Ordering::Release);
        REJECT_RGBA.store(0, Ordering::Release);
        OK
    })
}

type NativeCommand = unsafe extern "C" fn(*mut c_void);
/// # Safety
/// Frames, resources and synchronous native-command callback are valid for the call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn eroverlay_test_layer_drive(
    above: *const NativeFrame,
    above_resource: *mut c_void,
    below: *const NativeFrame,
    below_resource: *mut c_void,
    native_command: Option<NativeCommand>,
    native_user: *mut c_void,
    scenario: u32,
) -> i32 {
    guarded(|| {
        let (Some(above), Some(below), Some(command)) = (
            unsafe { above.as_ref() },
            unsafe { below.as_ref() },
            native_command,
        ) else {
            return ERR_INVALID;
        };
        let runtime = RUNTIME
            .get_or_init(|| Mutex::new(None))
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .clone();
        let Some(runtime) = runtime else {
            return ERR_UNAVAILABLE;
        };
        if unsafe {
            eroverlay_renderer::drive_scene_fixture(
                native_frame,
                Arc::as_ptr(&runtime).cast_mut().cast(),
                above,
                above_resource,
                below,
                below_resource,
                command,
                native_user,
                scenario,
            )
        } {
            OK
        } else {
            ERR_UNAVAILABLE
        }
    })
}

/// Exercise the ordinary nonblocking renderer/gate skip paths without a race.
/// # Safety
/// `frame` is a borrowed valid NativeFrame; caller does not concurrently stop.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn eroverlay_test_layer_busy(frame: *const NativeFrame, gate: u32) -> i32 {
    guarded(|| {
        let Some(frame) = (unsafe { frame.as_ref() }) else {
            return ERR_INVALID;
        };
        let runtime = RUNTIME
            .get_or_init(|| Mutex::new(None))
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .clone();
        let Some(runtime) = runtime else {
            return ERR_UNAVAILABLE;
        };
        if gate == 0 {
            let _busy = runtime.render.lock().unwrap_or_else(|e| e.into_inner());
            runtime.render(frame);
        } else {
            let _busy = runtime.callbacks.write().unwrap_or_else(|e| e.into_inner());
            runtime.render(frame);
        }
        OK
    })
}

struct DriveContext {
    runtime: Arc<Runtime>,
    mode: u32,
    failed: AtomicBool,
}

fn render_while_publisher_busy(context: &DriveContext, frame: &NativeFrame) {
    // The acknowledged second thread owns the real maintenance lock for the
    // entire affected callback; it never submits GPU work or changes paint.
    let handle = context
        .runtime
        .presentation
        .read()
        .unwrap_or_else(|e| e.into_inner())
        .clone();
    let Some(handle) = handle else {
        context.failed.store(true, Ordering::Release);
        return;
    };
    let held = AtomicBool::new(false);
    thread::scope(|scope| {
        let (ack_tx, ack_rx) = std::sync::mpsc::sync_channel(1);
        let (release_tx, release_rx) = std::sync::mpsc::sync_channel(1);
        let held = &held;
        let worker = scope.spawn(move || {
            handle.with_maintenance_lock_for_fixture(|| {
                held.store(true, Ordering::Release);
                let _ = ack_tx.send(thread::current().id());
                let _ = release_rx.recv_timeout(Duration::from_secs(2));
                held.store(false, Ordering::Release);
            });
        });
        let other_cpu = ack_rx.recv_timeout(Duration::from_secs(2));
        if other_cpu.is_ok_and(|id| id != thread::current().id()) && held.load(Ordering::Acquire) {
            if context.mode == 8 {
                let _busy = context
                    .runtime
                    .render
                    .lock()
                    .unwrap_or_else(|e| e.into_inner());
                context.runtime.render(frame);
            } else {
                context.runtime.render(frame);
            }
            if !held.load(Ordering::Acquire) {
                context.failed.store(true, Ordering::Release);
            }
        } else {
            context.failed.store(true, Ordering::Release);
        }
        let _ = release_tx.send(());
        if worker.join().is_err() {
            context.failed.store(true, Ordering::Release);
        }
    });
}
unsafe extern "C" fn busy_frame(user: *mut c_void, frame: *const NativeFrame) {
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let context = unsafe { &*user.cast::<DriveContext>() };
        let Some(frame) = (unsafe { frame.as_ref() }) else {
            return;
        };
        let target_layer = if context.mode <= 2 || context.mode >= 5 {
            LAYER_ABOVE_UI
        } else {
            LAYER_BELOW_UI
        };
        if frame.layer == target_layer && context.mode != 0 {
            if context.mode >= 7 {
                render_while_publisher_busy(context, frame);
            } else if context.mode == 6 {
                let handle = context
                    .runtime
                    .presentation
                    .read()
                    .unwrap_or_else(|e| e.into_inner())
                    .clone();
                if let Some(handle) = handle {
                    handle.with_maintenance_lock_for_fixture(|| context.runtime.render(frame));
                } else {
                    context.failed.store(true, Ordering::Release);
                }
            } else if context.mode == 5 {
                let _busy = context
                    .runtime
                    .painting
                    .lock()
                    .unwrap_or_else(|e| e.into_inner());
                context.runtime.render(frame);
            } else if context.mode % 2 == 1 {
                let _busy = context
                    .runtime
                    .render
                    .lock()
                    .unwrap_or_else(|e| e.into_inner());
                context.runtime.render(frame);
            } else {
                let _busy = context
                    .runtime
                    .callbacks
                    .write()
                    .unwrap_or_else(|e| e.into_inner());
                context.runtime.render(frame);
            }
        } else {
            context.runtime.render(frame);
        }
    }));
    if result.is_err() {
        unsafe { &*user.cast::<DriveContext>() }
            .failed
            .store(true, Ordering::Release);
    }
    STAGING.with(|s| s.set(std::ptr::null_mut()));
}

/// Full native L/UI/H sequence with deterministic contention only at the selected callback.
/// # Safety
/// Same borrowed resources and synchronous callback contract as `eroverlay_test_layer_drive`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn eroverlay_test_layer_drive_busy(
    above: *const NativeFrame,
    above_resource: *mut c_void,
    below: *const NativeFrame,
    below_resource: *mut c_void,
    native_command: Option<NativeCommand>,
    native_user: *mut c_void,
    scenario: u32,
    mode: u32,
) -> i32 {
    guarded(|| {
        let (Some(above), Some(below), Some(command)) = (
            unsafe { above.as_ref() },
            unsafe { below.as_ref() },
            native_command,
        ) else {
            return ERR_INVALID;
        };
        if mode > 8 {
            return ERR_INVALID;
        }
        let Some(runtime) = RUNTIME
            .get_or_init(|| Mutex::new(None))
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .clone()
        else {
            return ERR_UNAVAILABLE;
        };
        let mut context = DriveContext {
            runtime,
            mode,
            failed: AtomicBool::new(false),
        };
        if unsafe {
            eroverlay_renderer::drive_scene_fixture(
                busy_frame,
                (&mut context as *mut DriveContext).cast(),
                above,
                above_resource,
                below,
                below_resource,
                command,
                native_user,
                scenario,
            )
        } && !context.failed.load(Ordering::Acquire)
        {
            OK
        } else {
            ERR_UNAVAILABLE
        }
    })
}

/// # Safety
/// Offline fixture only; changes transfer scheduling without changing texture state.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn eroverlay_test_layer_texture_gate(paused: u32) -> i32 {
    TRANSFERS_PAUSED.store(paused != 0, Ordering::Release);
    OK
}

/// One or more real create calls can be refused, matching registry contention.
/// # Safety
/// Offline fixture only; no handle or texture state is fabricated.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn eroverlay_test_layer_texture_reject(count: u32) -> i32 {
    REJECT_RGBA.store(count.min(1024), Ordering::Release);
    OK
}

/// # Safety
/// `out` points to at least five writable u64 values.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn eroverlay_test_layer_texture_stats(out: *mut u64, capacity: usize) -> i32 {
    guarded(|| {
        if out.is_null() || !out.is_aligned() || capacity < 5 {
            return ERR_INVALID;
        }
        let Some(runtime) = RUNTIME
            .get_or_init(|| Mutex::new(None))
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .clone()
        else {
            return ERR_UNAVAILABLE;
        };
        let Some(d) = runtime.host.textures.diagnostics() else {
            return ERR_BUSY;
        };
        let values = [
            d.rgba_requests,
            d.prepared as u64,
            d.uploading as u64,
            d.ready as u64,
            d.source_bytes,
        ];
        unsafe { std::ptr::copy_nonoverlapping(values.as_ptr(), out, values.len()) };
        OK
    })
}
