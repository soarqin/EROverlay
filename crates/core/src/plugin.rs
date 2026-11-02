use eroverlay_abi::*;
use std::{
    ffi::c_void,
    path::Path,
    sync::{
        Mutex,
        atomic::{AtomicBool, AtomicU64, Ordering},
    },
};
use windows::{
    Win32::{
        Foundation::{FreeLibrary, HMODULE},
        System::LibraryLoader::{
            GetProcAddress, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR,
            LoadLibraryExW,
        },
    },
    core::PCWSTR,
};

pub struct Plugin {
    module: HMODULE,
    api: PluginApi,
    instance: *mut c_void,
    gate: Mutex<()>,
    rendered_sequence: AtomicU64,
    has_rendered: AtomicBool,
}
// SAFETY: callbacks obey the C ABI threading contract. Nonconcurrent plugins
// use gate; concurrent plugins explicitly advertise CAP_CONCURRENT.
unsafe impl Send for Plugin {}
unsafe impl Sync for Plugin {}

impl Plugin {
    pub fn load(path: &Path, host: &HostApi) -> Result<Self, String> {
        use std::os::windows::ffi::OsStrExt;
        let path: Vec<u16> = path.as_os_str().encode_wide().chain(Some(0)).collect();
        // SAFETY: NUL-terminated absolute path, no ambient DLL search directory.
        let module = unsafe {
            LoadLibraryExW(
                PCWSTR(path.as_ptr()),
                None,
                LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR,
            )
        }
        .map_err(|e| e.to_string())?;
        let result = (|| {
            // SAFETY: lookup is against the module retained by this function.
            let symbol =
                unsafe { GetProcAddress(module, windows::core::s!("getEROverlayPluginV1")) }
                    .ok_or("missing getEROverlayPluginV1 (new Rust ABI required)")?;
            // SAFETY: the named symbol has the documented C ABI signature.
            let get: GetPlugin = unsafe { std::mem::transmute(symbol) };
            let table = unsafe { get(ABI_VERSION) };
            if table.is_null() || !(table as usize).is_multiple_of(align_of::<PluginApi>()) {
                return Err("null or misaligned plugin API".into());
            }
            // Read only the fixed header before size-gating the remaining table.
            #[repr(C)]
            struct Header {
                magic: u64,
                size: u32,
                version: u32,
            }
            let h = unsafe { &*table.cast::<Header>() };
            if !valid_header(h.magic, h.size, h.version, size_of::<PluginApi>()) {
                return Err("unsupported plugin ABI size/version/magic".into());
            }
            let api = unsafe { *table };
            if api.required_capabilities & !host.capabilities != 0 || api.layer > LAYER_BELOW_UI {
                return Err("unsupported plugin capabilities/layer".into());
            }
            let init = api.init.ok_or("missing init callback")?;
            if api.destroy.is_none() {
                return Err("missing destroy callback".into());
            }
            let mut instance = std::ptr::null_mut();
            let status = unsafe { init(host, &mut instance) };
            if status != OK || instance.is_null() {
                if !instance.is_null() {
                    unsafe { api.destroy.unwrap()(instance) };
                }
                return Err(format!("plugin initialization failed: {status}"));
            }
            Ok(Self {
                module,
                api,
                instance,
                gate: Mutex::new(()),
                rendered_sequence: AtomicU64::new(0),
                has_rendered: AtomicBool::new(false),
            })
        })();
        if result.is_err() {
            let _ = unsafe { FreeLibrary(module) };
        }
        result
    }
    pub fn layer(&self) -> u32 {
        self.api.layer
    }
    pub fn needs_ui_input(&self) -> bool {
        self.api.required_capabilities & CAP_UI_INPUT != 0
    }
    pub fn update(&self) -> i32 {
        let _guard = (self.api.capabilities & CAP_CONCURRENT == 0)
            .then(|| self.gate.lock().unwrap_or_else(|e| e.into_inner()));
        self.api.update.map_or(OK, |f| unsafe { f(self.instance) })
    }
    pub fn render(&self, frame: &FrameInfo) -> i32 {
        if self.has_rendered.load(Ordering::Relaxed)
            && self.rendered_sequence.load(Ordering::Relaxed) == frame.sequence
        {
            return ERR_BUSY;
        }
        let _guard = if self.api.capabilities & CAP_CONCURRENT == 0 {
            match self.gate.try_lock() {
                Ok(g) => Some(g),
                Err(_) => return ERR_BUSY,
            }
        } else {
            None
        };
        // Runtime serializes render calls. A plugin owns one layer/context, so
        // repeated native callbacks must not advance its UI/input frame again.
        self.rendered_sequence
            .store(frame.sequence, Ordering::Relaxed);
        self.has_rendered.store(true, Ordering::Relaxed);
        self.api
            .render
            .map_or(OK, |f| unsafe { f(self.instance, frame) })
    }
    pub fn reset(&self, generation: u64) {
        self.has_rendered.store(false, Ordering::Relaxed);
        if let Some(f) = self.api.renderer_reset {
            unsafe { f(self.instance, generation) };
        }
    }
}
impl Drop for Plugin {
    fn drop(&mut self) {
        // SAFETY: runtime excludes update/render before dropping this object.
        unsafe {
            self.api.destroy.unwrap()(self.instance);
            let _ = FreeLibrary(self.module);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::mem::ManuallyDrop;

    unsafe extern "C" fn count_render(instance: *mut c_void, _: *const FrameInfo) -> i32 {
        unsafe { &*instance.cast::<AtomicU64>() }.fetch_add(1, Ordering::Relaxed);
        OK
    }

    fn fixture(count: &AtomicU64) -> ManuallyDrop<Plugin> {
        fixture_callback((count as *const AtomicU64).cast_mut().cast(), count_render)
    }
    fn fixture_callback(
        instance: *mut c_void,
        render: unsafe extern "C" fn(*mut c_void, *const FrameInfo) -> i32,
    ) -> ManuallyDrop<Plugin> {
        // Callback dispatch only: no fake module is passed to FreeLibrary.
        ManuallyDrop::new(Plugin {
            module: HMODULE::default(),
            api: PluginApi {
                magic: ABI_MAGIC,
                size: size_of::<PluginApi>() as u32,
                version: ABI_VERSION,
                required_capabilities: 0,
                capabilities: 0,
                layer: LAYER_BELOW_UI,
                reserved: 0,
                name: ByteSlice::new(b"frame-counter"),
                init: None,
                update: None,
                render: Some(render),
                renderer_reset: None,
                destroy: None,
            },
            instance,
            gate: Mutex::new(()),
            rendered_sequence: AtomicU64::new(0),
            has_rendered: AtomicBool::new(false),
        })
    }

    #[test]
    fn one_plugin_ui_frame_per_native_sequence_and_renderer_generation() {
        let count = AtomicU64::new(0);
        let plugin = fixture(&count);
        let mut frame = FrameInfo::default();
        assert_eq!(plugin.render(&frame), OK);
        frame.layer = LAYER_ABOVE_UI;
        assert_eq!(plugin.render(&frame), ERR_BUSY);
        assert_eq!(count.load(Ordering::Relaxed), 1);
        frame.sequence = 1;
        assert_eq!(plugin.render(&frame), OK);
        assert_eq!(plugin.render(&frame), ERR_BUSY);
        plugin.reset(2);
        assert_eq!(plugin.render(&frame), OK);
        assert_eq!(count.load(Ordering::Relaxed), 3);
    }

    #[test]
    fn busy_dispatch_does_not_consume_the_plugins_frame() {
        let count = AtomicU64::new(0);
        let plugin = fixture(&count);
        let frame = FrameInfo {
            sequence: 7,
            ..FrameInfo::default()
        };
        let gate = plugin.gate.lock().unwrap();
        assert_eq!(plugin.render(&frame), ERR_BUSY);
        drop(gate);
        assert_eq!(plugin.render(&frame), OK);
        assert_eq!(plugin.render(&frame), ERR_BUSY);
        assert_eq!(count.load(Ordering::Relaxed), 1);
    }

    #[test]
    fn copied_paint_is_committed_only_after_a_complete_successful_callback() {
        use std::sync::atomic::AtomicI32;
        unsafe extern "C" fn paint(user: *mut c_void, _: *const FrameInfo) -> i32 {
            let code = unsafe { &*user.cast::<AtomicI32>() }.load(Ordering::Relaxed);
            if code == ERR_UNAVAILABLE {
                return OK;
            } // Explicitly empty/hidden frame.
            let vertices = [Vertex::default(); 3];
            let indices = [0, 1, 2];
            let mesh = Mesh {
                texture: if code == OK { 7 } else { 99 },
                vertices: vertices.as_ptr(),
                vertex_count: 3,
                indices: indices.as_ptr(),
                index_count: 3,
                clip: [0., 0., 100., 100.],
            };
            assert_eq!(
                unsafe { crate::submit_mesh(std::ptr::null_mut(), &mesh) },
                OK
            );
            code
        }
        let code = AtomicI32::new(OK);
        let plugin = fixture_callback((&code as *const AtomicI32).cast_mut().cast(), paint);
        let mut packets = crate::painting::Painting::new(1);
        let mut frame = FrameInfo {
            width: 100.,
            height: 100.,
            sequence: 1,
            ..Default::default()
        };
        assert_eq!(packets.prepare(0, &plugin, &frame), OK);
        // The callback's stack vertices have already expired. Only copied data survives.
        assert_eq!(packets.packets[0].staging.meshes()[0].texture, 7);
        code.store(ERR_BUSY, Ordering::Relaxed);
        frame.sequence += 1;
        assert_eq!(packets.prepare(0, &plugin, &frame), ERR_BUSY);
        assert!(packets.packets[0].valid);
        assert!(!packets.packets[0].fresh);
        assert_eq!(packets.packets[0].staging.meshes()[0].texture, 7);
        assert_eq!(packets.prepare(0, &plugin, &frame), ERR_BUSY);
        frame.width = 120.;
        assert_eq!(packets.prepare(0, &plugin, &frame), ERR_BUSY);
        assert!(!packets.packets[0].valid);
        code.store(ERR_INVALID, Ordering::Relaxed);
        frame.sequence += 1;
        assert_eq!(packets.prepare(0, &plugin, &frame), ERR_INVALID);
        assert!(!packets.packets[0].valid);
        code.store(ERR_UNAVAILABLE, Ordering::Relaxed);
        frame.sequence += 1;
        assert_eq!(packets.prepare(0, &plugin, &frame), OK);
        assert!(packets.packets[0].valid);
        assert!(!packets.packets[0].staging.has_paint());
        packets.reset(2);
        assert!(!packets.packets[0].valid);
    }
}
