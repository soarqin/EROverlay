//! Controlled malformed DLLs for the loader harness. Never ship in overlays/.
#![allow(non_snake_case)]
use eroverlay_abi::*;
use std::sync::atomic::{AtomicU32, Ordering};
static INITIALIZED: AtomicU32 = AtomicU32::new(0);
static DESTROYED: AtomicU32 = AtomicU32::new(0);
static LIVE: AtomicU32 = AtomicU32::new(0);
static RESETS: AtomicU32 = AtomicU32::new(0);
static RENDERS: AtomicU32 = AtomicU32::new(0);
static READY_DRAWS: AtomicU32 = AtomicU32::new(0);
static TEXTURE_CREATIONS: AtomicU32 = AtomicU32::new(0);
static GENERATION: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
#[repr(C)]
#[derive(Default)]
pub struct FixtureCounts {
    pub init: u32,
    pub destroy: u32,
    pub live: u32,
}
#[unsafe(no_mangle)]
/// # Safety
/// `out` must point to a writable `FixtureCounts` for the duration of this call.
pub unsafe extern "C" fn fixture_counts(out: *mut FixtureCounts) -> i32 {
    let Some(out) = (unsafe { out.as_mut() }) else {
        return ERR_INVALID;
    };
    *out = FixtureCounts {
        init: INITIALIZED.load(Ordering::Relaxed),
        destroy: DESTROYED.load(Ordering::Relaxed),
        live: LIVE.load(Ordering::Relaxed),
    };
    OK
}
/// Separate fixture-only export; leaves the negative-loader fixture layout intact.
#[repr(C)]
#[derive(Default)]
pub struct FixtureRenderCounts {
    pub resets: u32,
    pub renders: u32,
    pub ready_draws: u32,
    pub texture_creations: u32,
    pub generation: u64,
}
#[unsafe(no_mangle)]
/// # Safety
/// `out` must point to a writable `FixtureRenderCounts` during this call.
pub unsafe extern "C" fn fixture_render_counts(out: *mut FixtureRenderCounts) -> i32 {
    let Some(out) = (unsafe { out.as_mut() }) else {
        return ERR_INVALID;
    };
    *out = FixtureRenderCounts {
        resets: RESETS.load(Ordering::Relaxed),
        renders: RENDERS.load(Ordering::Relaxed),
        ready_draws: READY_DRAWS.load(Ordering::Relaxed),
        texture_creations: TEXTURE_CREATIONS.load(Ordering::Relaxed),
        generation: GENERATION.load(Ordering::Relaxed),
    };
    OK
}
#[cfg(not(feature = "missing-export"))]
mod api {
    use super::*;
    use std::{ffi::c_void, sync::OnceLock};
    #[repr(C)]
    struct Header {
        magic: u64,
        size: u32,
        version: u32,
    }
    static SHORT: Header = Header {
        magic: ABI_MAGIC,
        size: size_of::<Header>() as u32,
        version: ABI_VERSION,
    };
    struct Export(PluginApi);
    // SAFETY: once initialized this function table and its static name are immutable.
    unsafe impl Send for Export {}
    unsafe impl Sync for Export {}
    static EXPORT: OnceLock<Export> = OnceLock::new();
    struct Instance {
        magic: u32,
        host: HostApi,
        texture: u64,
    }
    unsafe extern "C" fn init(host: *const HostApi, out: *mut *mut c_void) -> i32 {
        let Some(out) = (unsafe { out.as_mut() }) else {
            return ERR_INVALID;
        };
        let Some(host) = (unsafe { host.as_ref() }) else {
            return ERR_INVALID;
        };
        INITIALIZED.fetch_add(1, Ordering::Relaxed);
        LIVE.fetch_add(1, Ordering::Relaxed);
        *out = Box::into_raw(Box::new(Instance {
            magic: 0x0123_abcd,
            host: *host,
            texture: 0,
        }))
        .cast();
        if std::env::var("EROVERLAY_ABI_FIXTURE_MODE").as_deref() == Ok("init_failure") {
            ERR_UNAVAILABLE
        } else {
            OK
        }
    }
    unsafe extern "C" fn destroy(instance: *mut c_void) {
        if instance.is_null() {
            return;
        }
        // SAFETY: the harness transfers each successfully allocated instance once.
        let instance = unsafe { Box::from_raw(instance.cast::<Instance>()) };
        if instance.texture != 0
            && let Some(release) = instance.host.texture_release
        {
            unsafe { release(instance.host.user, instance.texture) };
        }
        if instance.magic == 0x0123_abcd {
            DESTROYED.fetch_add(1, Ordering::Relaxed);
            LIVE.fetch_sub(1, Ordering::Relaxed);
        }
    }
    unsafe extern "C" fn reset(instance: *mut c_void, generation: u64) -> i32 {
        let Some(instance) = (unsafe { instance.cast::<Instance>().as_mut() }) else {
            return ERR_INVALID;
        };
        RESETS.fetch_add(1, Ordering::Relaxed);
        GENERATION.store(generation, Ordering::Relaxed);
        if instance.texture != 0
            && let Some(release) = instance.host.texture_release
        {
            unsafe { release(instance.host.user, instance.texture) };
        }
        instance.texture = unsafe {
            instance.host.texture_create_rgba.unwrap()(
                instance.host.user,
                1,
                1,
                ByteSlice::new(&[255; 4]),
            )
        };
        if instance.texture == 0 {
            return ERR_UNAVAILABLE;
        }
        TEXTURE_CREATIONS.fetch_add(1, Ordering::Relaxed);
        OK
    }
    unsafe extern "C" fn render(instance: *mut c_void, frame: *const FrameInfo) -> i32 {
        let (Some(instance), Some(frame)) =
            (unsafe { instance.cast::<Instance>().as_ref() }, unsafe {
                frame.as_ref()
            })
        else {
            return ERR_INVALID;
        };
        RENDERS.fetch_add(1, Ordering::Relaxed);
        let mut texture = TextureView::default();
        if unsafe {
            instance.host.texture_poll.unwrap()(instance.host.user, instance.texture, &mut texture)
        } != OK
        {
            return ERR_UNAVAILABLE;
        }
        if texture.status != TEXTURE_READY {
            return OK;
        }
        let clip = [0.0, 0.0, frame.width.min(24.0), frame.height.min(24.0)];
        let vertices = [
            Vertex {
                pos: [0.0, 0.0],
                uv: [0.0, 0.0],
                color: u32::MAX,
            },
            Vertex {
                pos: [clip[2], 0.0],
                uv: [1.0, 0.0],
                color: u32::MAX,
            },
            Vertex {
                pos: [0.0, clip[3]],
                uv: [0.0, 1.0],
                color: u32::MAX,
            },
        ];
        let indices = [0, 1, 2];
        let mesh = Mesh {
            texture: instance.texture,
            vertices: vertices.as_ptr(),
            vertex_count: vertices.len() as u32,
            indices: indices.as_ptr(),
            index_count: indices.len() as u32,
            clip,
        };
        let options = RenderOptions {
            clip_rect: clip,
            ..RenderOptions::default()
        };
        let code =
            unsafe { instance.host.set_render_options.unwrap()(instance.host.user, &options) };
        if code != OK {
            return code;
        }
        let code = unsafe { instance.host.submit_mesh.unwrap()(instance.host.user, &mesh) };
        if code == OK {
            READY_DRAWS.fetch_add(1, Ordering::Relaxed);
        }
        code
    }
    #[unsafe(no_mangle)]
    pub extern "C" fn getEROverlayPluginV1(_: u32) -> *const PluginApi {
        std::panic::catch_unwind(|| {
            let mode =
                std::env::var("EROVERLAY_ABI_FIXTURE_MODE").unwrap_or_else(|_| "null".into());
            if mode == "null" {
                return std::ptr::null();
            }
            if mode == "short" {
                return (&SHORT as *const Header).cast();
            }
            if mode == "misaligned" {
                return (&SHORT as *const Header as usize + 1) as *const PluginApi;
            }
            &EXPORT
                .get_or_init(|| {
                    let mut api = PluginApi {
                        magic: ABI_MAGIC,
                        size: size_of::<PluginApi>() as u32,
                        version: ABI_VERSION,
                        required_capabilities: 0,
                        capabilities: 0,
                        layer: LAYER_ABOVE_UI,
                        reserved: 0,
                        name: ByteSlice::new(b"ABI negative fixture"),
                        init: Some(init),
                        update: None,
                        render: None,
                        renderer_reset: None,
                        destroy: Some(destroy),
                    };
                    match mode.as_str() {
                        "upper" => {
                            api.render = Some(render);
                            api.renderer_reset = Some(reset);
                        }
                        "version" => api.version = 99,
                        "magic" => api.magic = 0,
                        "capability" => api.required_capabilities = 1 << 63,
                        "missing_init" => api.init = None,
                        "missing_destroy" => api.destroy = None,
                        _ => {}
                    }
                    Export(api)
                })
                .0 as *const PluginApi
        })
        .unwrap_or(std::ptr::null())
    }
}
