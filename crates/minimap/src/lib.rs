#![deny(unsafe_op_in_unsafe_fn)]
#![allow(non_snake_case)]
mod data;
mod geometry;
mod gfx;
mod goblins;
mod host;
#[cfg(test)]
mod probe;
mod resources;
mod settings;
mod ui;

use eroverlay_abi as abi;
use host::Host;
use std::{
    collections::HashMap,
    ffi::c_void,
    panic::{AssertUnwindSafe, catch_unwind},
    sync::{
        Arc, Mutex,
        atomic::{AtomicU32, Ordering},
    },
};

struct UpdateState {
    settings: Arc<settings::Settings>,
    keys: [u32; 4],
    preset: usize,
    shown: bool,
    graces: bool,
    landmarks: bool,
    data: data::Data,
    resources: resources::Resources,
}
struct Instance {
    host: Host,
    update: Mutex<UpdateState>,
    renderer: Mutex<ui::Renderer>,
    snapshot: Mutex<Option<Arc<ui::Snapshot>>>,
    width: AtomicU32,
    height: AtomicU32,
}

fn update_controls(state: &mut UpdateState, pressed: [bool; 4], game_visible: bool) {
    if !game_visible {
        return;
    }
    if state.keys[0] != state.keys[1] && pressed[0] {
        state.shown = !state.shown;
    }
    if state.shown && pressed[1] {
        state.preset = (state.preset + 1) % state.settings.presets.len();
    }
    if state.shown && state.settings.presets[state.preset].zoom >= 0.0001 {
        if pressed[2] {
            state.graces = !state.graces;
        }
        if pressed[3] {
            state.landmarks = !state.landmarks;
        }
    }
}

fn load_settings(host: Host) -> settings::Settings {
    let mut values = HashMap::new();
    for key in [
        "controls.toggle",
        "controls.cycle",
        "controls.toggle_graces",
        "controls.toggle_landmarks",
        "map.full_map",
        "markers.graces",
        "markers.landmarks",
        "markers.death",
        "markers.beacons",
        "markers.others",
        "border.color",
        "border.width",
        "presets.order",
    ] {
        if let Some(value) = host.config(key) {
            values.insert(key.into(), value);
        }
    }
    let order = values
        .get("presets.order")
        .map(String::as_str)
        .map(str::trim)
        .filter(|order| !order.is_empty())
        .unwrap_or("compact,large")
        .to_owned();
    for name in order.split(',').map(str::trim).take(128) {
        if name.is_empty()
            || name.len() > 64
            || !name
                .bytes()
                .all(|b| b.is_ascii_alphanumeric() || b == b'_' || b == b'-')
        {
            continue;
        }
        for field in [
            "width",
            "height",
            "zoom",
            "opacity",
            "position",
            "margin_left",
            "margin_right",
            "margin_top",
            "margin_bottom",
            "rotate",
            "shape",
            "rounding",
            "map_scale",
            "decoration_scale",
            "player_scale",
            "compass_scale",
        ] {
            let key = format!("preset.{name}.{field}");
            if let Some(value) = host.config(&key) {
                values.insert(key, value);
            }
        }
    }
    settings::Settings::load(&values)
}

fn guard(action: impl FnOnce() -> i32) -> i32 {
    catch_unwind(AssertUnwindSafe(action)).unwrap_or(abi::ERR_PANIC)
}

unsafe extern "C" fn init(host: *const abi::HostApi, out: *mut *mut c_void) -> i32 {
    guard(|| {
        if host.is_null() || out.is_null() {
            return abi::ERR_INVALID;
        }
        // SAFETY: caller supplies at least the ABI header. Read size before copying table.
        let magic = unsafe { std::ptr::addr_of!((*host).magic).read_unaligned() };
        let size = unsafe { std::ptr::addr_of!((*host).size).read_unaligned() };
        let version = unsafe { std::ptr::addr_of!((*host).version).read_unaligned() };
        if !abi::valid_header(magic, size, version, size_of::<abi::HostApi>()) {
            return abi::ERR_ABI;
        }
        let host = Host(unsafe { host.read_unaligned() });
        let required =
            abi::CAP_GAME | abi::CAP_FILES | abi::CAP_TEXTURES | abi::CAP_MESH | abi::CAP_INPUT;
        if host.0.capabilities & required != required
            || host.0.read_map_state.is_none()
            || host.0.read_display_state.is_none()
            || host.0.read_memory.is_none()
            || host.0.request_file.is_none()
            || host.0.poll_file.is_none()
            || host.0.release_file.is_none()
            || host.0.texture_create.is_none()
            || host.0.texture_poll.is_none()
            || host.0.texture_release.is_none()
            || host.0.submit_mesh.is_none()
            || host.0.set_render_options.is_none()
            || host.0.texture_create_rgba.is_none()
        {
            return abi::ERR_ABI;
        }
        let settings = Arc::new(load_settings(host));
        let keys = std::array::from_fn(|i| host::chord(&settings.keys[i]));
        let mut renderer = ui::Renderer::new();
        renderer.load_fonts(host);
        let instance = Instance {
            host,
            update: Mutex::new(UpdateState {
                graces: settings.graces,
                landmarks: settings.landmarks,
                settings,
                keys,
                preset: 0,
                shown: true,
                data: data::Data::new(host),
                resources: resources::Resources::new(host),
            }),
            renderer: Mutex::new(renderer),
            snapshot: Mutex::new(None),
            width: AtomicU32::new(1920.0f32.to_bits()),
            height: AtomicU32::new(1080.0f32.to_bits()),
        };
        unsafe { out.write(Box::into_raw(Box::new(instance)).cast()) };
        host.log("Minimap Rust/egui initialized; native UI_SCENE below game UI\n");
        abi::OK
    })
}
unsafe extern "C" fn update(instance: *mut c_void) -> i32 {
    guard(|| {
        if instance.is_null() {
            return abi::ERR_INVALID;
        }
        let instance = unsafe { &*instance.cast::<Instance>() };
        let Ok(mut state) = instance.update.try_lock() else {
            return abi::ERR_BUSY;
        };
        let pressed = state.keys.map(|key| instance.host.key(key));
        update_controls(&mut state, pressed, instance.host.visible());
        let settings = state.settings.clone();
        let others = settings.others;
        let preset = &settings.presets[state.preset];
        let screen = [
            f32::from_bits(instance.width.load(Ordering::Relaxed)),
            f32::from_bits(instance.height.load(Ordering::Relaxed)),
        ];
        let rect = ui::map_rect(preset, screen);
        let bounds = rect.map_or([0.0; 2], |r| [r.width(), r.height()]);
        let shown = state.shown && preset.zoom >= 0.0001 && rect.is_some();
        let mut data = if shown && preset.opacity > 0.0 {
            state.data.update(others)
        } else {
            data::MapData::default()
        };
        let scale = (preset.zoom * preset.map_scale).clamp(0.01, 16.0);
        // Cull the cached marker catalog on update, keeping native ordering intact.
        if data.valid {
            let angle = if preset.rotate {
                data.state.camera_yaw_sin.atan2(data.state.camera_yaw_cos)
            } else {
                0.0
            };
            let (c, s) = (angle.cos(), angle.sin());
            let visible = |m: &data::Decoration| {
                let x = (m.x - data.state.x) * scale;
                let y = (m.y - data.state.y) * scale;
                (x * c - y * s).abs() <= bounds[0] * 0.5 + 200.0
                    && (x * s + y * c).abs() <= bounds[1] * 0.5 + 200.0
            };
            data.decorations = Arc::new(
                data.decorations
                    .iter()
                    .filter(|m| visible(m))
                    .cloned()
                    .collect(),
            );
        }
        let mut budget = resources::UploadBudget::new();
        state.data.prepare_runtime(&mut data, &mut budget);
        let graces = state.graces;
        let landmarks = state.landmarks;
        let visuals = state.resources.update(
            resources::ViewRequest {
                data: &data,
                bounds,
                shown,
                preset,
                settings: &settings,
                graces,
                landmarks,
            },
            &mut budget,
        );
        let snapshot = Arc::new(ui::Snapshot {
            shown,
            preset: state.preset,
            settings,
            data,
            visuals,
            graces: state.graces,
            landmarks: state.landmarks,
        });
        match instance.snapshot.lock() {
            Ok(mut slot) => *slot = Some(snapshot),
            Err(_) => return abi::ERR_PANIC,
        }
        abi::OK
    })
}
unsafe extern "C" fn render(instance: *mut c_void, frame: *const abi::FrameInfo) -> i32 {
    guard(|| {
        if instance.is_null() || frame.is_null() {
            return abi::ERR_INVALID;
        }
        // SAFETY: the caller supplies the fixed size header. A short frame
        // must be rejected before reading fields beyond that header.
        if unsafe { std::ptr::addr_of!((*frame).size).read_unaligned() }
            < size_of::<abi::FrameInfo>() as u32
        {
            return abi::ERR_INVALID;
        }
        let instance = unsafe { &*instance.cast::<Instance>() };
        let frame = unsafe { frame.read_unaligned() };
        if frame.layer != abi::LAYER_BELOW_UI
            || !frame.width.is_finite()
            || !frame.height.is_finite()
            || frame.width <= 0.0
            || frame.height <= 0.0
        {
            return abi::ERR_INVALID;
        }
        instance
            .width
            .store(frame.width.to_bits(), Ordering::Relaxed);
        instance
            .height
            .store(frame.height.to_bits(), Ordering::Relaxed);
        let Ok(slot) = instance.snapshot.try_lock() else {
            return abi::OK;
        };
        let snapshot = slot.clone();
        drop(slot);
        let Some(snapshot) = snapshot else {
            return abi::OK;
        };
        let Ok(mut renderer) = instance.renderer.try_lock() else {
            return abi::OK;
        };
        renderer.render(instance.host, &snapshot, &frame)
    })
}
unsafe extern "C" fn renderer_reset(instance: *mut c_void, _generation: u64) -> i32 {
    guard(|| {
        if instance.is_null() {
            return abi::ERR_INVALID;
        }
        let instance = unsafe { &*instance.cast::<Instance>() };
        let (Ok(mut state), Ok(mut renderer)) =
            (instance.update.try_lock(), instance.renderer.try_lock())
        else {
            return abi::ERR_BUSY;
        };
        state.resources.reset();
        state.data.reset();
        renderer.reset(instance.host);
        if let Ok(mut snapshot) = instance.snapshot.lock() {
            *snapshot = None;
        }
        abi::OK
    })
}
unsafe extern "C" fn destroy(instance: *mut c_void) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if !instance.is_null() {
            let instance = unsafe { Box::from_raw(instance.cast::<Instance>()) };
            if let Ok(mut renderer) = instance.renderer.lock() {
                renderer.reset(instance.host);
            }
            drop(instance);
        }
    }));
}

struct Export(abi::PluginApi);
// SAFETY: immutable DLL-static function table/name; contains no mutable storage.
unsafe impl Sync for Export {}
static PLUGIN: Export = Export(abi::PluginApi {
    magic: abi::ABI_MAGIC,
    size: size_of::<abi::PluginApi>() as u32,
    version: abi::ABI_VERSION,
    required_capabilities: abi::CAP_GAME
        | abi::CAP_FILES
        | abi::CAP_TEXTURES
        | abi::CAP_MESH
        | abi::CAP_INPUT,
    capabilities: abi::CAP_CONCURRENT,
    layer: abi::LAYER_BELOW_UI,
    reserved: 0,
    name: abi::ByteSlice {
        data: b"Minimap".as_ptr(),
        len: 7,
    },
    init: Some(init),
    update: Some(update),
    render: Some(render),
    renderer_reset: Some(renderer_reset),
    destroy: Some(destroy),
});

/// # Safety
/// The returned table is immutable and valid only while this DLL remains loaded.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn getEROverlayPluginV1(version: u32) -> *const abi::PluginApi {
    if version == abi::ABI_VERSION {
        &PLUGIN.0
    } else {
        std::ptr::null()
    }
}

#[cfg(test)]
mod lifecycle_tests {
    use super::*;
    #[test]
    fn host_configuration_preserves_empty_order_and_mixed_case_pixel_units() {
        struct Config {
            order: Option<&'static str>,
        }
        unsafe extern "C" fn config_get(
            user: *mut c_void,
            key: abi::ByteSlice,
            out: *mut u8,
            capacity: usize,
        ) -> usize {
            let config = unsafe { &*user.cast::<Config>() };
            let key = unsafe { std::slice::from_raw_parts(key.data, key.len) };
            let value = match key {
                b"minimap.presets.order" => config.order,
                b"minimap.preset.compact.width" => Some("45%"),
                b"minimap.preset.compact.margin_left" => Some("10PX"),
                b"minimap.preset.compact.margin_top" => Some("5pX"),
                b"minimap.preset.compact.shape" => Some("rounded"),
                b"minimap.preset.compact.rounding" => Some("12Px"),
                b"minimap.preset.large.zoom" => Some("200%"),
                _ => None,
            };
            let Some(value) = value else {
                return usize::MAX;
            };
            if !out.is_null() {
                unsafe {
                    std::ptr::copy_nonoverlapping(value.as_ptr(), out, value.len().min(capacity))
                };
            }
            value.len()
        }
        for order in [None, Some(""), Some("   "), Some("compact,large")] {
            let mut config = Config { order };
            // SAFETY: nullable callbacks and zero integer fields are valid.
            let mut api: abi::HostApi = unsafe { std::mem::zeroed() };
            api.user = std::ptr::from_mut(&mut config).cast();
            api.config_get = Some(config_get);
            let loaded = load_settings(Host(api));
            assert_eq!(loaded.presets[0].name, "compact");
            assert_eq!(loaded.presets[0].width, 0.45, "order={order:?}");
            assert_eq!(loaded.presets[1].name, "large");
            assert_eq!(loaded.presets[1].zoom, 2.0, "order={order:?}");
            let compact = &loaded.presets[0];
            assert_eq!(compact.shape, settings::Shape::Rounded);
            assert_eq!(compact.rounding.value, 12.0);
            assert!(!compact.rounding.percent);
            let rect = ui::map_rect(compact, [1920.0, 1080.0]).unwrap();
            assert_eq!(rect.min, egui::Pos2::new(10.0, 5.0));
            assert_eq!(rect.width(), 486.0);
        }
    }
    #[test]
    fn hidden_and_loading_controls_preserve_existing_behavior() {
        // SAFETY: nullable C callback fields and zero integer fields are valid.
        let host = Host(unsafe { std::mem::zeroed() });
        let settings = Arc::new(settings::Settings::load(&HashMap::new()));
        let keys = std::array::from_fn(|i| host::chord(&settings.keys[i]));
        let mut state = UpdateState {
            settings,
            keys,
            preset: 0,
            shown: true,
            graces: true,
            landmarks: true,
            data: data::Data::new(host),
            resources: resources::Resources::new(host),
        };
        update_controls(&mut state, [true; 4], false);
        assert_eq!(state.preset, 0);
        assert!(state.graces && state.landmarks);
        update_controls(&mut state, [true, true, false, false], true);
        assert_eq!(state.preset, 1);
        update_controls(&mut state, [true, true, false, false], true);
        assert_eq!(state.preset, 2);
        update_controls(&mut state, [false, false, true, true], true);
        assert!(state.graces && state.landmarks);
        update_controls(&mut state, [true, true, false, false], true);
        assert_eq!(state.preset, 0);
        update_controls(&mut state, [false, false, true, true], true);
        assert!(!state.graces && !state.landmarks);
    }
    #[test]
    fn init_rejects_a_host_without_required_texture_polling() {
        unsafe extern "C" fn map(_: *mut c_void, _: *mut abi::MapState) -> i32 {
            abi::ERR_UNAVAILABLE
        }
        unsafe extern "C" fn display(_: *mut c_void, _: *mut abi::DisplayState) -> i32 {
            abi::ERR_UNAVAILABLE
        }
        unsafe extern "C" fn read(_: *mut c_void, _: usize, _: *mut u8, _: usize) -> i32 {
            abi::ERR_UNAVAILABLE
        }
        unsafe extern "C" fn request(_: *mut c_void, _: *const abi::FileRequest) -> u64 {
            0
        }
        unsafe extern "C" fn poll(_: *mut c_void, _: u64, _: *mut abi::FileView) -> i32 {
            abi::ERR_UNAVAILABLE
        }
        unsafe extern "C" fn release(_: *mut c_void, _: u64) {}
        unsafe extern "C" fn texture(_: *mut c_void, _: abi::ByteSlice) -> u64 {
            0
        }
        unsafe extern "C" fn submit(_: *mut c_void, _: *const abi::Mesh) -> i32 {
            abi::ERR_UNAVAILABLE
        }
        unsafe extern "C" fn options(_: *mut c_void, _: *const abi::RenderOptions) -> i32 {
            abi::ERR_UNAVAILABLE
        }
        unsafe extern "C" fn rgba(_: *mut c_void, _: u32, _: u32, _: abi::ByteSlice) -> u64 {
            0
        }
        // SAFETY: nullable callbacks/pointers and zero integers are valid.
        let mut host: abi::HostApi = unsafe { std::mem::zeroed() };
        host.magic = abi::ABI_MAGIC;
        host.size = size_of::<abi::HostApi>() as u32;
        host.version = abi::ABI_VERSION;
        host.capabilities = PLUGIN.0.required_capabilities;
        host.read_map_state = Some(map);
        host.read_display_state = Some(display);
        host.read_memory = Some(read);
        host.request_file = Some(request);
        host.poll_file = Some(poll);
        host.release_file = Some(release);
        host.texture_create = Some(texture);
        host.texture_release = Some(release);
        host.submit_mesh = Some(submit);
        host.set_render_options = Some(options);
        host.texture_create_rgba = Some(rgba);
        let mut instance = std::ptr::null_mut();
        assert_eq!(unsafe { init(&host, &mut instance) }, abi::ERR_ABI);
        assert!(instance.is_null());
    }
    #[cfg(windows)]
    #[test]
    fn render_rejects_short_frame_header_at_an_inaccessible_page_boundary() {
        #[link(name = "kernel32")]
        unsafe extern "system" {
            fn VirtualAlloc(
                address: *mut c_void,
                size: usize,
                kind: u32,
                protect: u32,
            ) -> *mut c_void;
            fn VirtualProtect(
                address: *mut c_void,
                size: usize,
                protect: u32,
                old: *mut u32,
            ) -> i32;
            fn VirtualFree(address: *mut c_void, size: usize, kind: u32) -> i32;
        }
        struct Allocation(*mut c_void);
        impl Drop for Allocation {
            fn drop(&mut self) {
                // SAFETY: only this test owns the reservation.
                assert_ne!(unsafe { VirtualFree(self.0, 0, 0x8000) }, 0);
            }
        }
        const CHUNK: usize = 64 * 1024;
        // SAFETY: allocation-granularity multiples keep protection boundaries
        // page aligned; only fixture-owned memory is changed.
        let allocation =
            Allocation(unsafe { VirtualAlloc(std::ptr::null_mut(), CHUNK * 2, 0x3000, 4) });
        assert!(!allocation.0.is_null());
        let boundary = unsafe { allocation.0.cast::<u8>().add(CHUNK) };
        let header = unsafe { boundary.sub(size_of::<u32>()).cast::<u32>() };
        unsafe { header.write(size_of::<u32>() as u32) };
        let mut old = 0;
        assert_ne!(
            unsafe { VirtualProtect(boundary.cast(), CHUNK, 1, &mut old) },
            0
        );
        // A real, initialized plugin instance excludes invalid-instance pointers
        // as the reason this short-frame call is rejected.
        let host = Host(unsafe { std::mem::zeroed() });
        let settings = Arc::new(settings::Settings::load(&HashMap::new()));
        let mut instance = Instance {
            host,
            update: Mutex::new(UpdateState {
                keys: std::array::from_fn(|i| host::chord(&settings.keys[i])),
                preset: 0,
                shown: true,
                graces: settings.graces,
                landmarks: settings.landmarks,
                settings,
                data: data::Data::new(host),
                resources: resources::Resources::new(host),
            }),
            renderer: Mutex::new(ui::Renderer::new()),
            snapshot: Mutex::new(None),
            width: AtomicU32::new(1920.0f32.to_bits()),
            height: AtomicU32::new(1080.0f32.to_bits()),
        };
        assert_eq!(
            unsafe { render(std::ptr::from_mut(&mut instance).cast(), header.cast()) },
            abi::ERR_INVALID
        );
        assert_eq!(instance.width.load(Ordering::Relaxed), 1920.0f32.to_bits());
    }
    #[test]
    fn export_version_and_null_arguments_fail_safely() {
        unsafe {
            assert!(getEROverlayPluginV1(99).is_null());
            assert!(!getEROverlayPluginV1(1).is_null());
            assert_eq!(
                init(std::ptr::null(), std::ptr::null_mut()),
                abi::ERR_INVALID
            );
            assert_eq!(update(std::ptr::null_mut()), abi::ERR_INVALID);
            assert_eq!(
                render(std::ptr::null_mut(), std::ptr::null()),
                abi::ERR_INVALID
            );
            assert_eq!(renderer_reset(std::ptr::null_mut(), 0), abi::ERR_INVALID);
            destroy(std::ptr::null_mut());
        }
    }
}
