//! Native input is process-lifetime code, but capture is a short, explicit lease.
//! Window callbacks never wait for the plugin or allocate unbounded storage.
use eroverlay_abi::*;
use std::{
    ffi::c_void,
    sync::{
        Mutex,
        atomic::{AtomicBool, AtomicU32, AtomicU64, AtomicUsize, Ordering},
    },
};
use windows::{
    Win32::{
        Foundation::{HWND, LPARAM, LRESULT, POINT, RECT, WPARAM},
        Graphics::Gdi::ScreenToClient,
        System::{
            LibraryLoader::{GetModuleHandleW, GetProcAddress},
            Threading::GetCurrentProcessId,
        },
        UI::{
            Input::KeyboardAndMouse::{GetCapture, GetKeyState, ReleaseCapture, SetCapture},
            WindowsAndMessaging::*,
        },
    },
    core::{BOOL, GUID, s, w},
};

const LEASE_MS: u64 = 750;
const EMPTY: UiInput = UiInput {
    size: size_of::<UiInput>() as u32,
    kind: 0,
    code: 0,
    flags: 0,
    x: 0.0,
    y: 0.0,
    value: 0.0,
    reserved: 0,
};
static SERVICE: Service = Service::new();
// Trampolines and subclass predecessor remain valid after stop. A thread can
// already be in a detour when capture stops; NativeBridge pins the core first.
static PREVIOUS: AtomicUsize = AtomicUsize::new(0);
static STATE_ORIGINAL: [AtomicUsize; 2] = [const { AtomicUsize::new(0) }; 2];
static DATA_ORIGINAL: [AtomicUsize; 2] = [const { AtomicUsize::new(0) }; 2];
static CURSOR_ORIGINAL: AtomicUsize = AtomicUsize::new(0);
static CLIP_ORIGINAL: AtomicUsize = AtomicUsize::new(0);
static HOOK_TARGETS: [AtomicUsize; 6] = [const { AtomicUsize::new(0) }; 6];

#[link(name = "kernel32")]
unsafe extern "system" {
    fn GetTickCount64() -> u64;
}
#[link(name = "dinput8")]
unsafe extern "system" {
    fn DirectInput8Create(
        module: *mut c_void,
        version: u32,
        iid: *const GUID,
        out: *mut *mut c_void,
        outer: *mut c_void,
    ) -> i32;
}
unsafe extern "system" {
    fn MH_CreateHook(target: *mut c_void, detour: *mut c_void, out: *mut *mut c_void) -> i32;
    fn MH_EnableHook(target: *mut c_void) -> i32;
    fn MH_DisableHook(target: *mut c_void) -> i32;
}

struct Queue {
    events: [UiInput; MAX_UI_INPUT_EVENTS],
    head: usize,
    len: usize,
    reset: bool,
    high_surrogate: u16,
}
impl Queue {
    const fn new() -> Self {
        Self {
            events: [EMPTY; MAX_UI_INPUT_EVENTS],
            head: 0,
            len: 0,
            reset: true,
            high_surrogate: 0,
        }
    }
    fn clear(&mut self) {
        self.head = 0;
        self.len = 0;
        self.reset = true;
        self.high_surrogate = 0;
    }
    fn push(&mut self, event: UiInput) {
        if self.len == self.events.len() {
            self.clear();
        }
        self.events[(self.head + self.len) % self.events.len()] = event;
        self.len += 1;
    }
    fn text(&mut self, unit: u16) {
        if (0xd800..=0xdbff).contains(&unit) {
            self.high_surrogate = unit;
            return;
        }
        let high = std::mem::take(&mut self.high_surrogate);
        let code = if (0xdc00..=0xdfff).contains(&unit) {
            if high == 0 {
                return;
            }
            0x10000 + ((u32::from(high) - 0xd800) << 10) + u32::from(unit) - 0xdc00
        } else {
            u32::from(unit)
        };
        if code >= 0x20 && code != 0x7f {
            self.push(UiInput {
                kind: UI_INPUT_TEXT,
                code,
                ..EMPTY
            });
        }
    }
    fn drain(&mut self, out: &mut [UiInput]) -> usize {
        let mut written = 0;
        if self.reset && !out.is_empty() {
            out[0] = UiInput {
                kind: UI_INPUT_RESET,
                ..EMPTY
            };
            self.reset = false;
            written = 1;
        }
        while written < out.len() && self.len > 0 {
            out[written] = self.events[self.head];
            self.head = (self.head + 1) % self.events.len();
            self.len -= 1;
            written += 1;
        }
        written
    }
}

struct Service {
    running: AtomicBool,
    focused: AtomicBool,
    permitted: AtomicBool,
    capture: AtomicU32,
    expires: AtomicU64,
    window: AtomicUsize,
    lost: AtomicBool,
    mouse_buttons: AtomicU32,
    owns_mouse: AtomicBool,
    cancel_mouse: AtomicBool,
    queue: Mutex<Queue>,
}
impl Service {
    const fn new() -> Self {
        Self {
            running: AtomicBool::new(false),
            focused: AtomicBool::new(false),
            permitted: AtomicBool::new(true),
            capture: AtomicU32::new(0),
            expires: AtomicU64::new(0),
            window: AtomicUsize::new(0),
            lost: AtomicBool::new(false),
            mouse_buttons: AtomicU32::new(0),
            owns_mouse: AtomicBool::new(false),
            cancel_mouse: AtomicBool::new(false),
            queue: Mutex::new(Queue::new()),
        }
    }
    fn active_at(&self, now: u64) -> u32 {
        if self.running.load(Ordering::Acquire)
            && self.focused.load(Ordering::Relaxed)
            && self.permitted.load(Ordering::Relaxed)
            && now < self.expires.load(Ordering::Acquire)
        {
            self.capture.load(Ordering::Acquire)
        } else {
            0
        }
    }
    fn active(&self) -> u32 {
        self.active_at(unsafe { GetTickCount64() })
    }
    fn release(&self) {
        if self.capture.swap(0, Ordering::AcqRel) != 0 {
            self.lost.store(true, Ordering::Release);
        }
        if self.owns_mouse.load(Ordering::Acquire) {
            self.cancel_mouse.store(true, Ordering::Release);
        }
    }
    fn push(&self, event: UiInput) {
        if let Ok(mut queue) = self.queue.try_lock() {
            if self.lost.swap(false, Ordering::AcqRel) {
                queue.clear();
            }
            queue.push(event);
        } else {
            self.lost.store(true, Ordering::Release);
        }
    }
    fn focus(&self, focused: bool) {
        if !focused {
            self.release();
        }
        if self.focused.swap(focused, Ordering::AcqRel) != focused {
            self.push(UiInput {
                kind: UI_INPUT_FOCUS,
                code: u32::from(focused),
                ..EMPTY
            });
        }
    }
}

pub unsafe extern "C" fn poll(
    _: *mut c_void,
    out: *mut UiInput,
    capacity: usize,
    count: *mut usize,
) -> i32 {
    super::guarded(|| {
        if count.is_null() {
            return ERR_INVALID;
        }
        unsafe { *count = 0 };
        if capacity > MAX_UI_INPUT_EVENTS || (capacity != 0 && out.is_null()) {
            return ERR_INVALID;
        }
        if capacity == 0 {
            return OK;
        }
        let Ok(mut queue) = SERVICE.queue.try_lock() else {
            return ERR_BUSY;
        };
        if SERVICE.lost.swap(false, Ordering::AcqRel) {
            queue.clear();
        }
        if queue.reset {
            // Opening a panel clears its private egui input. The already-focused
            // game HWND will not emit WM_SETFOCUS again just for that panel.
            queue.push(UiInput {
                kind: UI_INPUT_FOCUS,
                code: u32::from(SERVICE.focused.load(Ordering::Acquire)),
                ..EMPTY
            });
        }
        // SAFETY: the ABI caller supplies capacity writable UiInput entries.
        let output = unsafe { std::slice::from_raw_parts_mut(out, capacity) };
        unsafe { *count = queue.drain(output) };
        OK
    })
}
pub unsafe extern "C" fn clear(_: *mut c_void) -> i32 {
    SERVICE.lost.store(true, Ordering::Release);
    OK
}
pub unsafe extern "C" fn capture(_: *mut c_void, mask: u32) -> i32 {
    if mask & !(UI_CAPTURE_MOUSE | UI_CAPTURE_KEYBOARD) != 0 {
        return ERR_INVALID;
    }
    if mask == 0 {
        SERVICE.release();
        return OK;
    }
    if !SERVICE.running.load(Ordering::Acquire)
        || !SERVICE.focused.load(Ordering::Relaxed)
        || !SERVICE.permitted.load(Ordering::Relaxed)
    {
        SERVICE.release();
        return ERR_UNAVAILABLE;
    }
    SERVICE.expires.store(
        unsafe { GetTickCount64() }.saturating_add(LEASE_MS),
        Ordering::Release,
    );
    SERVICE.capture.store(mask, Ordering::Release);
    OK
}

/// Update-thread owner of original-window discovery and cursor clipping.
pub struct NativeInput {
    saved_clip: Option<RECT>,
    discovery_ticks: u32,
}
impl NativeInput {
    /// NativeBridge has already pinned the core and initialized shared MinHook.
    pub fn start() -> Self {
        let ready = install_device_hooks();
        SERVICE.running.store(ready, Ordering::Release);
        if !ready {
            eprintln!(
                "[EROverlay] Interactive input unavailable: keyboard/mouse interception could not be installed; capture remains disabled."
            );
        }
        SERVICE.permitted.store(true, Ordering::Release);
        SERVICE.lost.store(true, Ordering::Release);
        Self {
            saved_clip: None,
            discovery_ticks: 0,
        }
    }
    pub fn update(&mut self, permitted: bool) {
        if !SERVICE.running.load(Ordering::Acquire) {
            return;
        }
        SERVICE.permitted.store(permitted, Ordering::Release);
        let mut window = HWND(SERVICE.window.load(Ordering::Acquire) as *mut c_void);
        if window.is_invalid() || !unsafe { IsWindow(Some(window)) }.as_bool() {
            SERVICE.focus(false);
            self.discovery_ticks = self.discovery_ticks.wrapping_add(1);
            if self.discovery_ticks % 60 == 1 {
                let mut candidate = Candidate::default();
                let _ = unsafe {
                    EnumWindows(
                        Some(find_window),
                        LPARAM((&mut candidate as *mut Candidate) as isize),
                    )
                };
                if !candidate.window.is_invalid() {
                    window = candidate.window;
                    bind_window(window);
                }
            }
        }
        SERVICE.focus(!window.is_invalid() && unsafe { GetForegroundWindow() } == window);
        let active = SERVICE.active();
        if active == 0 {
            SERVICE.release();
        }
        post_mouse_cancel(window);
        if active & UI_CAPTURE_MOUSE != 0 && self.saved_clip.is_none() {
            let mut clip = RECT::default();
            if unsafe { GetClipCursor(&mut clip) }.is_ok() {
                self.saved_clip = Some(clip);
                clip_cursor(None);
            }
            let mut point = POINT::default();
            if unsafe { GetCursorPos(&mut point) }.is_ok() {
                let _ = unsafe { ScreenToClient(window, &mut point) };
                SERVICE.push(pointer(point.x, point.y));
            }
        } else if active & UI_CAPTURE_MOUSE == 0
            && let Some(clip) = self.saved_clip.take()
        {
            clip_cursor(SERVICE.focused.load(Ordering::Relaxed).then_some(&clip));
        }
    }
    pub fn stop(&mut self) {
        SERVICE.running.store(false, Ordering::Release);
        SERVICE.release();
        let window = HWND(SERVICE.window.load(Ordering::Acquire) as *mut c_void);
        post_mouse_cancel(window);
        for target in &HOOK_TARGETS {
            let target = target.load(Ordering::Acquire);
            if target != 0 {
                // Disabled entries regain original code. Trampolines remain
                // allocated for any callback already returning through them.
                let status = unsafe { MH_DisableHook(target as *mut c_void) };
                if status != 0 && status != 6 {
                    eprintln!(
                        "[EROverlay] Input hook restoration deferred ({status}); capture is disabled and the pinned detour passes through."
                    );
                }
            }
        }
        if let Some(clip) = self.saved_clip.take() {
            clip_cursor(SERVICE.focused.load(Ordering::Relaxed).then_some(&clip));
        }
        // A posted cancel must run on the window thread before removing our
        // subclass. The pinned, inert callback restores itself after that cancel.
        if !SERVICE.owns_mouse.load(Ordering::Acquire) {
            restore_window(window);
        }
        SERVICE.focus(false);
    }
}

fn restore_window(window: HWND) {
    if !window.is_invalid()
        && unsafe { IsWindow(Some(window)) }.as_bool()
        && unsafe { GetWindowLongPtrW(window, GWLP_WNDPROC) } == window_proc as *const () as isize
    {
        unsafe {
            SetWindowLongPtrW(
                window,
                GWLP_WNDPROC,
                PREVIOUS.load(Ordering::Acquire) as isize,
            )
        };
        SERVICE.window.store(0, Ordering::Release);
    }
}

// ReleaseCapture is thread-local. Rendering only marks cancellation; the worker
// posts this message and the HWND owner releases its own capture without waits.
fn post_mouse_cancel(window: HWND) {
    if SERVICE.cancel_mouse.swap(false, Ordering::AcqRel)
        && !window.is_invalid()
        && unsafe { PostMessageW(Some(window), WM_CANCELMODE, WPARAM(0), LPARAM(0)) }.is_err()
    {
        SERVICE.cancel_mouse.store(true, Ordering::Release);
    }
}

fn release_mouse(window: HWND, reset: bool) {
    SERVICE.mouse_buttons.store(0, Ordering::Release);
    SERVICE.cancel_mouse.store(false, Ordering::Release);
    let owned = SERVICE.owns_mouse.swap(false, Ordering::AcqRel);
    if owned && unsafe { GetCapture() } == window {
        unsafe { ReleaseCapture() }.ok();
    }
    if reset {
        SERVICE.lost.store(true, Ordering::Release);
    }
}

fn bind_window(window: HWND) -> bool {
    let previous = unsafe { GetWindowLongPtrW(window, GWLP_WNDPROC) };
    if previous == 0 {
        return false;
    }
    PREVIOUS.store(previous as usize, Ordering::Release);
    SERVICE.window.store(window.0 as usize, Ordering::Release);
    let replaced =
        unsafe { SetWindowLongPtrW(window, GWLP_WNDPROC, window_proc as *const () as isize) };
    if replaced == 0 {
        SERVICE.window.store(0, Ordering::Release);
        return false;
    }
    PREVIOUS.store(replaced as usize, Ordering::Release);
    true
}
impl Drop for NativeInput {
    fn drop(&mut self) {
        self.stop();
    }
}

#[derive(Default)]
struct Candidate {
    window: HWND,
    area: i64,
}
unsafe extern "system" fn find_window(window: HWND, value: LPARAM) -> BOOL {
    let mut process = 0;
    unsafe { GetWindowThreadProcessId(window, Some(&mut process)) };
    if process == unsafe { GetCurrentProcessId() }
        && unsafe { IsWindowVisible(window) }.as_bool()
        && unsafe { GetWindow(window, GW_OWNER) }.is_err()
    {
        let mut rect = RECT::default();
        if unsafe { GetClientRect(window, &mut rect) }.is_ok() {
            let area = i64::from(rect.right - rect.left) * i64::from(rect.bottom - rect.top);
            // SAFETY: EnumWindows synchronously borrows its caller-owned Candidate.
            let candidate = unsafe { &mut *(value.0 as *mut Candidate) };
            if area > candidate.area {
                candidate.window = window;
                candidate.area = area;
            }
        }
    }
    BOOL(1)
}

fn pointer(x: i32, y: i32) -> UiInput {
    UiInput {
        kind: UI_INPUT_POINTER,
        x: x as f32,
        y: y as f32,
        ..EMPTY
    }
}
fn modifiers() -> u32 {
    let down = |vk| unsafe { GetKeyState(vk) } < 0;
    (if down(0x11) { MOD_CTRL } else { 0 })
        | if down(0x10) { MOD_SHIFT } else { 0 }
        | if down(0x12) { MOD_ALT } else { 0 }
        | if down(0x5b) || down(0x5c) { MOD_WIN } else { 0 }
}
unsafe extern "system" fn window_proc(window: HWND, msg: u32, w: WPARAM, l: LPARAM) -> LRESULT {
    if matches!(msg, WM_CANCELMODE | WM_KILLFOCUS | WM_NCDESTROY)
        || msg == WM_ACTIVATEAPP && w.0 == 0
    {
        release_mouse(window, true);
        if !SERVICE.running.load(Ordering::Acquire) {
            restore_window(window);
        }
    } else if msg == WM_CAPTURECHANGED
        && (SERVICE.owns_mouse.load(Ordering::Acquire)
            || SERVICE.mouse_buttons.load(Ordering::Acquire) != 0)
    {
        release_mouse(window, true);
        SERVICE.release();
    }
    let mask = SERVICE.active();
    if SERVICE.running.load(Ordering::Acquire) {
        match msg {
            WM_KILLFOCUS | WM_NCDESTROY => SERVICE.focus(false),
            WM_SETFOCUS => SERVICE.focus(true),
            WM_ACTIVATEAPP if w.0 == 0 => SERVICE.focus(false),
            WM_MOUSEMOVE if mask & UI_CAPTURE_MOUSE != 0 => {
                SERVICE.push(pointer(l.0 as i16 as i32, (l.0 >> 16) as i16 as i32));
            }
            WM_LBUTTONDOWN | WM_LBUTTONUP | WM_RBUTTONDOWN | WM_RBUTTONUP | WM_MBUTTONDOWN
            | WM_MBUTTONUP | WM_XBUTTONDOWN | WM_XBUTTONUP
                if mask & UI_CAPTURE_MOUSE != 0 =>
            {
                let code = match msg {
                    WM_LBUTTONDOWN | WM_LBUTTONUP => 0,
                    WM_RBUTTONDOWN | WM_RBUTTONUP => 1,
                    WM_MBUTTONDOWN | WM_MBUTTONUP => 2,
                    _ => 2 + ((w.0 >> 16) & 0xffff) as u32,
                };
                let down = matches!(
                    msg,
                    WM_LBUTTONDOWN | WM_RBUTTONDOWN | WM_MBUTTONDOWN | WM_XBUTTONDOWN
                );
                let bit = 1u32.checked_shl(code).unwrap_or(0);
                if down {
                    SERVICE.mouse_buttons.fetch_or(bit, Ordering::AcqRel);
                    unsafe { SetCapture(window) };
                    SERVICE
                        .owns_mouse
                        .store(unsafe { GetCapture() } == window, Ordering::Release);
                }
                SERVICE.push(UiInput {
                    kind: UI_INPUT_BUTTON,
                    code,
                    flags: modifiers() | if down { UI_INPUT_DOWN } else { 0 },
                    x: l.0 as i16 as f32,
                    y: (l.0 >> 16) as i16 as f32,
                    ..EMPTY
                });
                if !down && SERVICE.mouse_buttons.fetch_and(!bit, Ordering::AcqRel) & !bit == 0 {
                    release_mouse(window, false);
                }
            }
            WM_MOUSEWHEEL | WM_MOUSEHWHEEL if mask & UI_CAPTURE_MOUSE != 0 => {
                let delta = (w.0 >> 16) as i16 as f32 / 120.0;
                SERVICE.push(UiInput {
                    kind: UI_INPUT_WHEEL,
                    flags: modifiers(),
                    x: if msg == WM_MOUSEHWHEEL { -delta } else { 0.0 },
                    y: if msg == WM_MOUSEWHEEL { delta } else { 0.0 },
                    ..EMPTY
                });
            }
            WM_KEYDOWN | WM_KEYUP | WM_SYSKEYDOWN | WM_SYSKEYUP
                if mask & UI_CAPTURE_KEYBOARD != 0 =>
            {
                let down = matches!(msg, WM_KEYDOWN | WM_SYSKEYDOWN);
                SERVICE.push(UiInput {
                    kind: UI_INPUT_KEY,
                    code: w.0 as u32,
                    flags: modifiers()
                        | if down { UI_INPUT_DOWN } else { 0 }
                        | if down && l.0 & (1 << 30) != 0 {
                            UI_INPUT_REPEAT
                        } else {
                            0
                        },
                    ..EMPTY
                });
            }
            WM_CHAR if mask & UI_CAPTURE_KEYBOARD != 0 => {
                if let Ok(mut queue) = SERVICE.queue.try_lock() {
                    if SERVICE.lost.swap(false, Ordering::AcqRel) {
                        queue.clear();
                    }
                    queue.text(w.0 as u16);
                } else {
                    SERVICE.lost.store(true, Ordering::Release);
                }
            }
            WM_UNICHAR if w.0 == 0xffff => return LRESULT(1),
            WM_UNICHAR if mask & UI_CAPTURE_KEYBOARD != 0 => {
                if char::from_u32(w.0 as u32).is_some() && w.0 >= 0x20 {
                    SERVICE.push(UiInput {
                        kind: UI_INPUT_TEXT,
                        code: w.0 as u32,
                        ..EMPTY
                    });
                }
            }
            WM_SETCURSOR if mask & UI_CAPTURE_MOUSE != 0 && l.0 as u16 == HTCLIENT as u16 => {
                if let Ok(cursor) = unsafe { LoadCursorW(None, IDC_ARROW) } {
                    unsafe { SetCursor(Some(cursor)) };
                    return LRESULT(1);
                }
            }
            _ => {}
        }
    }
    let mouse = (WM_MOUSEFIRST..=WM_MOUSELAST).contains(&msg);
    let keyboard = (WM_KEYFIRST..=WM_KEYLAST).contains(&msg);
    if (mouse && mask & UI_CAPTURE_MOUSE != 0) || (keyboard && mask & UI_CAPTURE_KEYBOARD != 0) {
        return LRESULT(1);
    }
    let previous = PREVIOUS.load(Ordering::Acquire);
    if previous == 0 {
        unsafe { DefWindowProcW(window, msg, w, l) }
    } else {
        // SAFETY: SetWindowLongPtrW's predecessor is a WNDPROC, retained even
        // when a later third-party subclass keeps this inert callback in its chain.
        unsafe {
            CallWindowProcW(
                Some(std::mem::transmute::<
                    usize,
                    unsafe extern "system" fn(HWND, u32, WPARAM, LPARAM) -> LRESULT,
                >(previous)),
                window,
                msg,
                w,
                l,
            )
        }
    }
}

fn clip_cursor(rect: Option<&RECT>) {
    let original = CLIP_ORIGINAL.load(Ordering::Acquire);
    if original == 0 {
        unsafe { ClipCursor(rect.map(|r| r as *const RECT)) }.ok();
    } else {
        // SAFETY: MH_CreateHook returns the matching ClipCursor trampoline.
        let call: unsafe extern "system" fn(*const RECT) -> BOOL =
            unsafe { std::mem::transmute(original) };
        let _ = unsafe { call(rect.map_or(std::ptr::null(), |r| r)) };
    }
}
unsafe extern "system" fn cursor_pos(x: i32, y: i32) -> BOOL {
    if SERVICE.active() & UI_CAPTURE_MOUSE != 0 {
        return BOOL(1);
    }
    let call: unsafe extern "system" fn(i32, i32) -> BOOL =
        unsafe { std::mem::transmute(CURSOR_ORIGINAL.load(Ordering::Acquire)) };
    unsafe { call(x, y) }
}
unsafe extern "system" fn cursor_clip(rect: *const RECT) -> BOOL {
    if SERVICE.active() & UI_CAPTURE_MOUSE != 0 {
        return BOOL(1);
    }
    let call: unsafe extern "system" fn(*const RECT) -> BOOL =
        unsafe { std::mem::transmute(CLIP_ORIGINAL.load(Ordering::Acquire)) };
    unsafe { call(rect) }
}

unsafe fn method(device: *mut c_void, index: usize) -> usize {
    // SAFETY: caller owns a live DirectInput COM object of the documented IID.
    unsafe { *(*device.cast::<*const usize>()).add(index) }
}
unsafe fn release(device: *mut c_void) {
    let call: unsafe extern "system" fn(*mut c_void) -> u32 =
        unsafe { std::mem::transmute(method(device, 2)) };
    unsafe { call(device) };
}
fn install_hook(target: usize, detour: usize, original: &AtomicUsize) -> bool {
    let mut trampoline = std::ptr::null_mut();
    if unsafe {
        MH_CreateHook(
            target as *mut c_void,
            detour as *mut c_void,
            &mut trampoline,
        )
    } != 0
    {
        return false;
    }
    original.store(trampoline as usize, Ordering::Release);
    if let Some(slot) = HOOK_TARGETS
        .iter()
        .find(|slot| slot.load(Ordering::Relaxed) == 0)
    {
        slot.store(target, Ordering::Release);
    }
    // Retain inert trampoline storage on any failure or stop; no later callback
    // may return through a freed trampoline or an unmapped input implementation.
    (unsafe { MH_EnableHook(target as *mut c_void) }) == 0
}
fn install_device_hooks() -> bool {
    let iid = GUID::from_u128(0xbf798031_483a_4da2_aa99_5d64ed369700);
    let mut input = std::ptr::null_mut();
    let Ok(module) = (unsafe { GetModuleHandleW(None) }) else {
        return false;
    };
    if unsafe { DirectInput8Create(module.0, 0x800, &iid, &mut input, std::ptr::null_mut()) } < 0
        || input.is_null()
    {
        return false;
    }
    let create: unsafe extern "system" fn(
        *mut c_void,
        *const GUID,
        *mut *mut c_void,
        *mut c_void,
    ) -> i32 = unsafe { std::mem::transmute(method(input, 3)) };
    let state_detours = [
        device_state::<0> as *const () as usize,
        device_state::<1> as *const () as usize,
    ];
    let data_detours = [
        device_data::<0> as *const () as usize,
        device_data::<1> as *const () as usize,
    ];
    let mut state_target = 0;
    let mut data_target = 0;
    let mut ready = true;
    for (slot, guid) in [
        GUID::from_u128(0x6f1d2b60_d5a0_11cf_bfc7_444553540000),
        GUID::from_u128(0x6f1d2b61_d5a0_11cf_bfc7_444553540000),
    ]
    .iter()
    .enumerate()
    {
        let mut device = std::ptr::null_mut();
        if unsafe { create(input, guid, &mut device, std::ptr::null_mut()) } < 0 || device.is_null()
        {
            ready = false;
            continue;
        }
        let state = unsafe { method(device, 9) };
        let data = unsafe { method(device, 10) };
        if state != state_target {
            if install_hook(state, state_detours[slot], &STATE_ORIGINAL[slot]) {
                state_target = state;
            } else {
                ready = false;
            }
        }
        if data != data_target {
            if install_hook(data, data_detours[slot], &DATA_ORIGINAL[slot]) {
                data_target = data;
            } else {
                ready = false;
            }
        }
        unsafe { release(device) };
    }
    // Keep the factory alive: its DLL owns all retained trampoline targets.
    let _ = input;
    if let Ok(user32) = unsafe { GetModuleHandleW(w!("user32.dll")) } {
        if let Some(target) = unsafe { GetProcAddress(user32, s!("SetCursorPos")) } {
            ready &= install_hook(
                target as *const () as usize,
                cursor_pos as *const () as usize,
                &CURSOR_ORIGINAL,
            );
        } else {
            ready = false;
        }
        if let Some(target) = unsafe { GetProcAddress(user32, s!("ClipCursor")) } {
            ready &= install_hook(
                target as *const () as usize,
                cursor_clip as *const () as usize,
                &CLIP_ORIGINAL,
            );
        } else {
            ready = false;
        }
    } else {
        ready = false;
    }
    ready
}

fn state_capture(size: u32, mask: u32) -> bool {
    (size == 256 && mask & UI_CAPTURE_KEYBOARD != 0)
        || (matches!(size, 16 | 20) && mask & UI_CAPTURE_MOUSE != 0)
}
unsafe extern "system" fn device_state<const SLOT: usize>(
    device: *mut c_void,
    size: u32,
    out: *mut c_void,
) -> i32 {
    let original: unsafe extern "system" fn(*mut c_void, u32, *mut c_void) -> i32 =
        unsafe { std::mem::transmute(STATE_ORIGINAL[SLOT].load(Ordering::Acquire)) };
    let result = unsafe { original(device, size, out) };
    let mask = SERVICE.active();
    if result >= 0
        && !out.is_null()
        && state_capture(size, mask)
        && unsafe { device_capture(device, mask) }
    {
        unsafe { std::ptr::write_bytes(out.cast::<u8>(), 0, size as usize) };
    }
    result
}
unsafe extern "system" fn device_data<const SLOT: usize>(
    device: *mut c_void,
    size: u32,
    out: *mut c_void,
    count: *mut u32,
    flags: u32,
) -> i32 {
    let original: unsafe extern "system" fn(*mut c_void, u32, *mut c_void, *mut u32, u32) -> i32 =
        unsafe { std::mem::transmute(DATA_ORIGINAL[SLOT].load(Ordering::Acquire)) };
    let result = unsafe { original(device, size, out, count, flags) };
    let mask = SERVICE.active();
    if result >= 0 && mask != 0 && !count.is_null() {
        // DIDEVICEINSTANCEW: dwSize, instance/product GUID, dwDevType, two
        // MAX_PATH WCHAR names, force-feedback GUID, usage page/usage.
        if unsafe { device_capture(device, mask) } {
            // The original read consumes buffered events normally; hide only
            // their returned count. A peek is left intact for the next read.
            unsafe { *count = 0 };
        }
    }
    result
}

unsafe fn device_capture(device: *mut c_void, mask: u32) -> bool {
    let mut instance = [0u32; 275];
    instance[0] = size_of_val(&instance) as u32;
    let info: unsafe extern "system" fn(*mut c_void, *mut u32) -> i32 =
        unsafe { std::mem::transmute(method(device, 15)) };
    if unsafe { info(device, instance.as_mut_ptr()) } < 0 {
        return false;
    }
    let kind = instance[9] & 0xff;
    (kind == 0x12 && mask & UI_CAPTURE_MOUSE != 0)
        || (kind == 0x13 && mask & UI_CAPTURE_KEYBOARD != 0)
}

/// A hidden message-only window and real DirectInput/Win32 hook installation.
/// This fixture never acquires foreground focus or moves the user's cursor.
#[cfg(feature = "native-lifecycle-fixture")]
pub fn fixture() -> bool {
    fn dispatch_cancel(window: HWND) -> bool {
        let mut message = MSG::default();
        while unsafe {
            PeekMessageW(
                &mut message,
                Some(window),
                WM_CANCELMODE,
                WM_CANCELMODE,
                PM_REMOVE,
            )
        }
        .as_bool()
        {
            unsafe { DispatchMessageW(&message) };
        }
        (unsafe { GetCapture() }) != window
    }
    static FORWARDED: AtomicU32 = AtomicU32::new(0);
    unsafe extern "system" fn previous(window: HWND, msg: u32, w: WPARAM, l: LPARAM) -> LRESULT {
        if matches!(
            msg,
            WM_MOUSEMOVE | WM_LBUTTONDOWN | WM_MOUSEWHEEL | WM_KEYDOWN | WM_CHAR
        ) {
            FORWARDED.fetch_add(1, Ordering::Relaxed);
        }
        unsafe { DefWindowProcW(window, msg, w, l) }
    }
    let Ok(module) = (unsafe { GetModuleHandleW(None) }) else {
        return false;
    };
    let class = w!("EROverlay.NativeInput.Fixture");
    let description = WNDCLASSW {
        lpfnWndProc: Some(previous),
        hInstance: windows::Win32::Foundation::HINSTANCE(module.0),
        lpszClassName: class,
        ..WNDCLASSW::default()
    };
    if unsafe { RegisterClassW(&description) } == 0 {
        return false;
    }
    let result = (|| {
        let Ok(window) = (unsafe {
            CreateWindowExW(
                WINDOW_EX_STYLE(0),
                class,
                w!(""),
                WINDOW_STYLE(0),
                0,
                0,
                100,
                100,
                Some(HWND_MESSAGE),
                None,
                Some(description.hInstance),
                None,
            )
        }) else {
            return false;
        };
        let mut native = NativeInput::start();
        let passed = (|| {
            if !SERVICE.running.load(Ordering::Acquire) || !bind_window(window) {
                return false;
            }
            SERVICE.focus(true); // Controlled focus state; the hidden HWND is never activated.
            if unsafe { capture(std::ptr::null_mut(), 3) } != OK {
                return false;
            }
            FORWARDED.store(0, Ordering::Relaxed);
            for (msg, wparam, lparam) in [
                (WM_MOUSEMOVE, 0, 17 | (31 << 16)),
                (WM_LBUTTONDOWN, 0, 17 | (31 << 16)),
                (WM_MOUSEWHEEL, 120 << 16, 0),
                (WM_KEYDOWN, b'A' as usize, 0),
                (WM_CHAR, 0x738b, 0),
                (WM_CHAR, 0xd83d, 0),
                (WM_CHAR, 0xde03, 0),
                (WM_LBUTTONUP, 0, 140 | (150 << 16)),
            ] {
                let _ = unsafe {
                    SendMessageW(window, msg, Some(WPARAM(wparam)), Some(LPARAM(lparam)))
                };
                if msg == WM_LBUTTONDOWN && unsafe { GetCapture() } != window {
                    return false;
                }
            }
            let mut events = [EMPTY; MAX_UI_INPUT_EVENTS];
            let mut count = 0;
            if unsafe {
                poll(
                    std::ptr::null_mut(),
                    events.as_mut_ptr(),
                    events.len(),
                    &mut count,
                )
            } != OK
            {
                return false;
            }
            let emitted = &events[..count];
            if FORWARDED.load(Ordering::Relaxed) != 0
                || !emitted
                    .iter()
                    .any(|e| e.kind == UI_INPUT_POINTER && e.x == 17.0 && e.y == 31.0)
                || !emitted.iter().any(|e| {
                    e.kind == UI_INPUT_BUTTON && e.code == 0 && e.flags & UI_INPUT_DOWN != 0
                })
                || !emitted.iter().any(|e| {
                    e.kind == UI_INPUT_BUTTON
                        && e.code == 0
                        && e.flags & UI_INPUT_DOWN == 0
                        && e.x == 140.0
                        && e.y == 150.0
                })
                || unsafe { GetCapture() } == window
                || !emitted
                    .iter()
                    .any(|e| e.kind == UI_INPUT_WHEEL && e.y == 1.0)
                || !emitted
                    .iter()
                    .any(|e| e.kind == UI_INPUT_KEY && e.code == u32::from(b'A'))
                || !emitted
                    .iter()
                    .any(|e| e.kind == UI_INPUT_TEXT && e.code == 0x738b)
                || !emitted
                    .iter()
                    .any(|e| e.kind == UI_INPUT_TEXT && e.code == 0x1f603)
            {
                return false;
            }
            // Closing a panel during a drag is asynchronous: the render-safe
            // ABI changes only atomics; the HWND owner releases on posted cancel.
            let _ =
                unsafe { SendMessageW(window, WM_LBUTTONDOWN, Some(WPARAM(0)), Some(LPARAM(0))) };
            if unsafe { GetCapture() } != window {
                return false;
            }
            unsafe { capture(std::ptr::null_mut(), 0) };
            if unsafe { GetCapture() } != window {
                return false;
            }
            post_mouse_cancel(window);
            if !dispatch_cancel(window) || SERVICE.mouse_buttons.load(Ordering::Acquire) != 0 {
                return false;
            }
            if unsafe {
                poll(
                    std::ptr::null_mut(),
                    events.as_mut_ptr(),
                    events.len(),
                    &mut count,
                )
            } != OK
                || !events[..count].iter().any(|e| e.kind == UI_INPUT_RESET)
            {
                return false;
            }
            let _ = unsafe {
                SendMessageW(
                    window,
                    WM_KEYDOWN,
                    Some(WPARAM(b'B' as usize)),
                    Some(LPARAM(0)),
                )
            };
            if FORWARDED.load(Ordering::Relaxed) != 1 {
                return false;
            }
            if unsafe { capture(std::ptr::null_mut(), 3) } != OK {
                return false;
            }
            let _ =
                unsafe { SendMessageW(window, WM_LBUTTONDOWN, Some(WPARAM(0)), Some(LPARAM(0))) };
            let _ = unsafe { SendMessageW(window, WM_KILLFOCUS, Some(WPARAM(0)), Some(LPARAM(0))) };
            let _ = unsafe {
                SendMessageW(
                    window,
                    WM_KEYDOWN,
                    Some(WPARAM(b'C' as usize)),
                    Some(LPARAM(0)),
                )
            };
            if SERVICE.active() != 0
                || FORWARDED.load(Ordering::Relaxed) != 2
                || unsafe { GetCapture() } == window
            {
                return false;
            }
            SERVICE.focus(true);
            if unsafe { capture(std::ptr::null_mut(), 3) } != OK {
                return false;
            }
            let _ =
                unsafe { SendMessageW(window, WM_LBUTTONDOWN, Some(WPARAM(0)), Some(LPARAM(0))) };
            native.stop();
            if !dispatch_cancel(window) {
                return false;
            }
            if unsafe { GetWindowLongPtrW(window, GWLP_WNDPROC) } != previous as *const () as isize
            {
                return false;
            }
            let _ = unsafe {
                SendMessageW(
                    window,
                    WM_KEYDOWN,
                    Some(WPARAM(b'D' as usize)),
                    Some(LPARAM(0)),
                )
            };
            FORWARDED.load(Ordering::Relaxed) == 3
        })();
        native.stop();
        let _ = unsafe { DestroyWindow(window) };
        passed
    })();
    let _ = unsafe { UnregisterClassW(class, Some(description.hInstance)) };
    result
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn overflow_and_explicit_clear_reset_all_held_inputs() {
        let mut queue = Queue::new();
        for code in 0..=MAX_UI_INPUT_EVENTS as u32 {
            queue.push(UiInput {
                kind: UI_INPUT_KEY,
                code,
                flags: UI_INPUT_DOWN,
                ..EMPTY
            });
        }
        let mut events = [EMPTY; MAX_UI_INPUT_EVENTS];
        assert_eq!(queue.drain(&mut events), 2);
        assert_eq!(events[0].kind, UI_INPUT_RESET);
        assert_eq!(events[1].code, MAX_UI_INPUT_EVENTS as u32);
        queue.clear();
        assert_eq!(queue.drain(&mut events), 1);
        assert_eq!(events[0].kind, UI_INPUT_RESET);
        assert_eq!(queue.drain(&mut events), 0);
    }
    #[test]
    fn utf16_commits_unicode_without_control_or_orphan_surrogates() {
        let mut queue = Queue::new();
        for unit in [
            0xdc00,
            0xd83d,
            0xde03,
            b'\n' as u16,
            0x738b,
            0xd800,
            b'A' as u16,
        ] {
            queue.text(unit);
        }
        let mut events = [EMPTY; 16];
        assert_eq!(queue.drain(&mut events), 4);
        assert_eq!(events[1].code, 0x1f603);
        assert_eq!(events[2].code, 0x738b);
        assert_eq!(events[3].code, u32::from(b'A'));
    }
    #[test]
    fn capture_requires_focus_visibility_lease_and_running_host() {
        let service = Service::new();
        service.capture.store(3, Ordering::Relaxed);
        service.expires.store(1000, Ordering::Relaxed);
        service.running.store(true, Ordering::Relaxed);
        service.focus(true);
        assert_eq!(service.active_at(999), 3);
        assert_eq!(service.active_at(1000), 0);
        service.permitted.store(false, Ordering::Relaxed);
        assert_eq!(service.active_at(999), 0);
        service.permitted.store(true, Ordering::Relaxed);
        service.focus(false);
        service.focus(true);
        assert_eq!(service.active_at(999), 0);
        service.capture.store(3, Ordering::Relaxed);
        service.running.store(false, Ordering::Relaxed);
        assert_eq!(service.active_at(999), 0);
    }
    #[test]
    fn state_filter_keeps_controllers_and_separate_capture_masks() {
        assert!(state_capture(256, UI_CAPTURE_KEYBOARD));
        assert!(!state_capture(256, UI_CAPTURE_MOUSE));
        assert!(state_capture(20, UI_CAPTURE_MOUSE));
        assert!(state_capture(16, UI_CAPTURE_MOUSE));
        assert!(!state_capture(80, 3));
        assert!(!state_capture(272, 3));
        assert!(!state_capture(20, 0));
    }
}
