//! Stable C ABI. All pointers are borrowed for the duration of the call unless
//! explicitly documented. The allocating DLL always destroys its own storage.
#![deny(unsafe_op_in_unsafe_fn)]
use core::ffi::c_void;
mod input;
pub use input::parse_chord;

pub const ABI_VERSION: u32 = 1;
pub const ABI_MAGIC: u64 = 0x4552_5255_5354_3031;
pub const CAP_GAME: u64 = 1;
pub const CAP_FILES: u64 = 2;
pub const CAP_TEXTURES: u64 = 4;
pub const CAP_MESH: u64 = 8;
pub const CAP_INPUT: u64 = 16;
pub const CAP_CONCURRENT: u64 = 32;
pub const LAYER_ABOVE_UI: u32 = 0;
pub const LAYER_BELOW_UI: u32 = 1;
pub const OK: i32 = 0;
/// Core stop completed, but native detour code is pinned until process exit.
/// The caller may release its own LoadLibrary reference; the DLL stays resident.
pub const STOPPED_RESIDENT: i32 = 1;
pub const ERR_INVALID: i32 = -1;
pub const ERR_ABI: i32 = -2;
pub const ERR_UNAVAILABLE: i32 = -3;
pub const ERR_BUSY: i32 = -4;
pub const ERR_PANIC: i32 = -5;
pub const FILE_PENDING: u32 = 0;
pub const FILE_READY: u32 = 1;
pub const FILE_FAILED: u32 = 2;
pub const FILE_UNSUPPORTED: u32 = 3;
pub const TEXTURE_PENDING: u32 = 0;
pub const TEXTURE_READY: u32 = 1;
pub const TEXTURE_FAILED: u32 = 2;
pub const MOD_CTRL: u32 = 1 << 16;
pub const MOD_SHIFT: u32 = 1 << 17;
pub const MOD_ALT: u32 = 1 << 18;
pub const MOD_WIN: u32 = 1 << 19;
pub const MAX_VERTICES: usize = 262_144;
pub const MAX_INDICES: usize = 786_432;
pub const MAX_MESHES: usize = 4096;
pub const MAX_FILE_BYTES: usize = 256 * 1024 * 1024;
/// Maximum input payload for one texture; queue admission can still defer it.
pub const MAX_TEXTURE_BYTES: usize = 32 * 1024 * 1024;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct MapState {
    pub generation: u64,
    pub raw_map_id: u32,
    pub map_id: i32,
    pub x: f32,
    pub y: f32,
    pub underground: i32,
    pub ori_deg: f32,
    pub active_masks: [u32; 3],
    pub death_valid: u32,
    pub death_x: f32,
    pub death_y: f32,
    pub death_map_id: i32,
    pub view_model: usize,
    pub camera_yaw_cos: f32,
    pub camera_yaw_sin: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct DisplayState {
    pub valid: u32,
    pub loading_or_logo: u32,
    pub full_screen_map: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct GameLayout {
    pub size: u32,
    pub menu_owner_offset: u32,
    pub owner_view_offset: u32,
    pub menu_info_offset: u32,
    pub screen_state_offset: u32,
    pub grace_stride: u32,
    pub grace_normal_offset: u32,
    pub alternate_icons: u32,
    pub map_mask: u32,
    pub repository_groups: u32,
}

/// The DLL consumes this buffer synchronously and must not retain its pointer.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct ByteSlice {
    pub data: *const u8,
    pub len: usize,
}
impl ByteSlice {
    pub fn new(bytes: &[u8]) -> Self {
        Self {
            data: bytes.as_ptr(),
            len: bytes.len(),
        }
    }
    pub fn empty() -> Self {
        Self {
            data: core::ptr::null(),
            len: 0,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct FileRequest {
    pub size: u32,
    pub flags: u32,
    pub path: ByteSlice,
    /// Empty selects the entire file; otherwise one named TPF member.
    pub part: ByteSlice,
    pub max_bytes: u64,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct FileView {
    pub status: u32,
    pub reserved: u32,
    /// Remains valid until release_file(token); only update may consume it.
    pub bytes: ByteSlice,
}
impl Default for FileView {
    fn default() -> Self {
        Self {
            status: FILE_FAILED,
            reserved: 0,
            bytes: ByteSlice::empty(),
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct TextureView {
    pub status: u32,
    pub width: u32,
    pub height: u32,
    pub reserved: u32,
    pub memory_bytes: u64,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Vertex {
    pub pos: [f32; 2],
    pub uv: [f32; 2],
    /// Premultiplied sRGBA, byte order R G B A, as used by epaint.
    pub color: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Mesh {
    pub texture: u64,
    pub vertices: *const Vertex,
    pub vertex_count: u32,
    pub indices: *const u32,
    pub index_count: u32,
    /// Physical pixels, min-x/min-y/max-x/max-y.
    pub clip: [f32; 4],
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct FrameInfo {
    pub size: u32,
    pub layer: u32,
    pub sequence: u64,
    pub width: f32,
    pub height: f32,
    pub pixels_per_point: f32,
    pub delta_seconds: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct RenderOptions {
    pub clip_rect: [f32; 4],
    pub corner_radius: f32,
    pub circle: u32,
    pub opacity: f32,
    pub reserved: u32,
    pub border_width: f32,
    pub border_color: u32,
}
impl Default for RenderOptions {
    fn default() -> Self {
        Self {
            clip_rect: [0.0, 0.0, 16_384.0, 16_384.0],
            corner_radius: 0.0,
            circle: 0,
            opacity: 1.0,
            reserved: 0,
            border_width: 0.0,
            border_color: 0,
        }
    }
}

/// Core-owned immutable function table. user is opaque and valid through destroy.
/// init/update-only: config, input, map, memory, flags, params, files, texture_create.
/// read_display_state is also allowed on render and must never block.
/// render-only: submit_mesh. texture_poll/release are safe from either callback.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct HostApi {
    pub magic: u64,
    pub size: u32,
    pub version: u32,
    pub capabilities: u64,
    pub user: *mut c_void,
    pub log: Option<unsafe extern "C" fn(*mut c_void, u32, ByteSlice)>,
    /// Returns required UTF-8 byte count. Writes at most capacity (no NUL).
    pub config_get: Option<unsafe extern "C" fn(*mut c_void, ByteSlice, *mut u8, usize) -> usize>,
    pub key_pressed: Option<unsafe extern "C" fn(*mut c_void, u32) -> u32>,
    pub read_map_state: Option<unsafe extern "C" fn(*mut c_void, *mut MapState) -> i32>,
    pub read_display_state: Option<unsafe extern "C" fn(*mut c_void, *mut DisplayState) -> i32>,
    pub read_game_layout: Option<unsafe extern "C" fn(*mut c_void, *mut GameLayout) -> i32>,
    pub read_memory: Option<unsafe extern "C" fn(*mut c_void, usize, *mut u8, usize) -> i32>,
    pub read_event_flag: Option<unsafe extern "C" fn(*mut c_void, u32) -> u32>,
    pub find_param_table: Option<unsafe extern "C" fn(*mut c_void, u32) -> usize>,
    /// Zero means not accepted (including temporary service/queue pressure).
    /// Actual accepted-request failures are reported by poll_file.
    pub request_file: Option<unsafe extern "C" fn(*mut c_void, *const FileRequest) -> u64>,
    pub poll_file: Option<unsafe extern "C" fn(*mut c_void, u64, *mut FileView) -> i32>,
    pub release_file: Option<unsafe extern "C" fn(*mut c_void, u64)>,
    /// Copies up to MAX_TEXTURE_BYTES of DDS before returning. Zero means not
    /// accepted, including temporary pressure; accepted failures use texture_poll.
    pub texture_create: Option<unsafe extern "C" fn(*mut c_void, ByteSlice) -> u64>,
    pub texture_poll: Option<unsafe extern "C" fn(*mut c_void, u64, *mut TextureView) -> i32>,
    pub texture_release: Option<unsafe extern "C" fn(*mut c_void, u64)>,
    /// Synchronously copies into bounded core-owned staging. Does not retain pointers.
    pub submit_mesh: Option<unsafe extern "C" fn(*mut c_void, *const Mesh) -> i32>,
    /// Copies straight-alpha RGBA8 pixels, used for egui's font atlas. Render calls are bounded
    /// queue operations; GPU upload is scheduled after plugin callbacks return.
    pub texture_create_rgba: Option<unsafe extern "C" fn(*mut c_void, u32, u32, ByteSlice) -> u64>,
    pub set_render_options: Option<unsafe extern "C" fn(*mut c_void, *const RenderOptions) -> i32>,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct PluginApi {
    pub magic: u64,
    pub size: u32,
    pub version: u32,
    pub required_capabilities: u64,
    pub capabilities: u64,
    pub layer: u32,
    pub reserved: u32,
    /// Name is a static UTF-8 slice owned by the plugin DLL.
    pub name: ByteSlice,
    pub init: Option<unsafe extern "C" fn(*const HostApi, *mut *mut c_void) -> i32>,
    pub update: Option<unsafe extern "C" fn(*mut c_void) -> i32>,
    pub render: Option<unsafe extern "C" fn(*mut c_void, *const FrameInfo) -> i32>,
    pub renderer_reset: Option<unsafe extern "C" fn(*mut c_void, u64) -> i32>,
    /// Caller excludes all callbacks and calls exactly once for successful init.
    pub destroy: Option<unsafe extern "C" fn(*mut c_void)>,
}
pub type GetPlugin = unsafe extern "C" fn(u32) -> *const PluginApi;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct StartOptions {
    pub size: u32,
    pub version: u32,
    /// 1 installs native hooks. 0 is an explicit offline lifecycle host.
    pub native: u32,
    pub reserved: u32,
    /// Absolute UTF-16 directory, without NUL. Core copies this string.
    pub directory: *const u16,
    pub directory_len: usize,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct RuntimeStats {
    pub size: u32,
    pub state: u32,
    pub plugins_loaded: u32,
    pub plugins_rejected: u32,
    pub updates: u64,
    pub renders: u64,
    pub meshes: u64,
    pub dropped_frames: u64,
    /// Last sampled committed renderer resources (including in-flight staging
    /// and geometry), not DXGI process residency or total game VRAM.
    pub gpu_committed_bytes: u64,
    pub last_error: i32,
    pub reserved: u32,
}

pub fn valid_header(magic: u64, size: u32, version: u32, required: usize) -> bool {
    magic == ABI_MAGIC && version == ABI_VERSION && size as usize >= required
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::mem::offset_of;
    #[test]
    fn c_layout_and_version_gates() {
        assert_eq!(size_of::<Vertex>(), 20);
        assert_eq!(offset_of!(HostApi, magic), 0);
        assert_eq!(offset_of!(HostApi, size), 8);
        assert_eq!(offset_of!(PluginApi, version), 12);
        assert!(!valid_header(ABI_MAGIC, 16, 2, size_of::<PluginApi>()));
        assert!(!valid_header(ABI_MAGIC, 16, 1, size_of::<PluginApi>()));
        assert!(valid_header(
            ABI_MAGIC,
            size_of::<PluginApi>() as u32,
            1,
            size_of::<PluginApi>()
        ));
    }
}
