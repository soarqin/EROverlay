//! DX12 UI_SCENE renderer. Game pointers are borrowed for one native callback;
//! only device/queue COM identities are retained. No window or Present hook.
#![deny(unsafe_op_in_unsafe_fn)]
mod gpu;
pub mod scene_scan;
pub use gpu::{PreparedTexture, RenderStats, Renderer, RendererDiagnostics};
use std::ffi::c_void;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct NativeFrame {
    pub device: *mut c_void,
    pub queue: *mut c_void,
    pub target: usize,
    pub format: u32,
    pub width: f32,
    pub height: f32,
    pub layer: u32,
    pub sequence: u64,
}
pub type FrameCallback = unsafe extern "C" fn(*mut c_void, *const NativeFrame);

unsafe extern "C" {
    fn er_rust_scene_install(
        callback: FrameCallback,
        user: *mut c_void,
        module: usize,
        addresses: *const scene_scan::Addresses,
    ) -> *mut c_void;
    fn er_rust_scene_stop(bridge: *mut c_void);
    fn er_rust_scene_module_retained() -> bool;
}

/// Stops and drains synchronous native callbacks before freeing their context.
/// Once native hooks can be reached, their module is pinned until process exit.
pub struct NativeBridge(*mut c_void);
// The native bridge excludes its callbacks during stop; it owns no Rust state.
unsafe impl Send for NativeBridge {}
impl NativeBridge {
    /// Native detour entry/return code cannot be proved quiescent by the callback
    /// gate. A true result permanently forbids claiming a complete DLL unload.
    /// It also covers partial-install rollback and remains true after stop.
    pub fn requires_module_retention() -> bool {
        unsafe { er_rust_scene_module_retained() }
    }
    /// # Safety
    /// `user` must remain live until this bridge is dropped. Callback must not
    /// unwind, call stop itself, or retain borrowed frame pointers.
    pub unsafe fn install(
        callback: FrameCallback,
        user: *mut c_void,
    ) -> windows::core::Result<Self> {
        // Snapshot/scanning runs once before hooks and outside render callbacks.
        // Discard the temporary image before enabling either native detour.
        let module = eroverlay_game::memory::main_module();
        let addresses = (|| {
            let image = eroverlay_game::memory::image_snapshot(module)?;
            scene_scan::resolve(&eroverlay_game::scan::PeImage::new(&image, module))
                .map_err(String::from)
        })()
        .map_err(|message| {
            windows::core::Error::new(windows::core::HRESULT(0x80004005u32 as i32), message)
        })?;
        let bridge = unsafe { er_rust_scene_install(callback, user, module, &addresses) };
        if bridge.is_null() {
            Err(windows::core::Error::from_hresult(windows::core::HRESULT(
                0x80004005u32 as i32,
            )))
        } else {
            Ok(Self(bridge))
        }
    }
    pub fn stop(&mut self) {
        if !self.0.is_null() {
            unsafe { er_rust_scene_stop(self.0) };
            self.0 = std::ptr::null_mut();
        }
    }
}
impl Drop for NativeBridge {
    fn drop(&mut self) {
        self.stop();
    }
}

#[cfg(test)]
mod tests {
    unsafe extern "C" {
        fn er_rust_scene_lifecycle_check() -> bool;
    }
    #[test]
    fn native_hook_teardown_and_exact_layer_boundary() {
        assert!(unsafe { er_rust_scene_lifecycle_check() });
        assert!(super::NativeBridge::requires_module_retention());
    }
    #[test]
    fn native_frame_layout() {
        assert_eq!(size_of::<super::NativeFrame>(), 48);
        assert_eq!(std::mem::offset_of!(super::NativeFrame, sequence), 40);
    }
}
