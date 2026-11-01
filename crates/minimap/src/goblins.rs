//! Read-only consumer of the existing public MapForGoblins ABI.
use crate::{
    gfx::{self, IconLayer, Reader, Recipe},
    host::{Host, Texture},
    resources::{Region, UploadBudget},
};
use std::sync::Arc;
use std::{
    collections::{HashMap, HashSet},
    ffi::c_void,
    time::{Duration, Instant},
};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Point {
    pub id: u64,
    pub icon_id: u32,
    pub raw_map_id: u32,
    pub x: f32,
    pub z: f32,
    pub draw_order: u32,
    pub reserved: u32,
}
#[repr(C)]
struct Icon {
    lossless2: *const u8,
    lossless2_size: u32,
    source_icon_id: u32,
    matrix: *const u8,
    matrix_size: u32,
    reserved: u32,
}
#[repr(C)]
#[derive(Clone, Copy)]
struct Api {
    size: u32,
    abi_version: u32,
    point_size: u32,
    icon_size: u32,
    icon_count: u32,
    reserved: u32,
    view_points: Option<unsafe extern "C" fn(u32, *mut *const Point, *mut u32) -> i32>,
    get_icon: Option<unsafe extern "C" fn(u32, *mut Icon) -> i32>,
}

#[cfg(windows)]
#[link(name = "kernel32")]
unsafe extern "system" {
    fn GetModuleHandleExW(flags: u32, name: *const u16, module: *mut *mut c_void) -> i32;
    fn GetProcAddress(module: *mut c_void, name: *const u8) -> *mut c_void;
    fn FreeLibrary(module: *mut c_void) -> i32;
}

pub struct RuntimeIcon {
    pub recipe: Recipe,
    pub region: Region,
    pub texture: Option<Arc<Texture>>,
    pub pending: u64,
    pub(crate) rgba: Vec<u8>,
    pub(crate) touched: Instant,
    pub(crate) retry: Instant,
}
pub type RuntimeVisuals = HashMap<u32, (Recipe, Arc<Texture>)>;
pub struct Goblins {
    host: Host,
    module: *mut c_void,
    api: Option<Api>,
    retry: Instant,
    pub icons: HashMap<u32, RuntimeIcon>,
    pub visuals: Arc<RuntimeVisuals>,
}
// SAFETY: provider module is retained, all its calls happen only on update.
unsafe impl Send for Goblins {}
impl Goblins {
    pub fn new(host: Host) -> Self {
        Self {
            host,
            module: std::ptr::null_mut(),
            api: None,
            retry: Instant::now(),
            icons: HashMap::new(),
            visuals: Arc::new(HashMap::new()),
        }
    }
    #[cfg(windows)]
    fn connect(&mut self) {
        if !self.module.is_null() || Instant::now() < self.retry {
            return;
        }
        self.retry = Instant::now() + Duration::from_secs(1);
        let name: Vec<u16> = "MapForGoblins.dll\0".encode_utf16().collect();
        let mut module = std::ptr::null_mut();
        // SAFETY: wide name is NUL terminated, module receives a retained provider reference.
        if unsafe { GetModuleHandleExW(0, name.as_ptr(), &mut module) } == 0 {
            return;
        }
        self.module = module;
        let get = unsafe { GetProcAddress(module, c"MfgGetMapAPI".as_ptr().cast()) };
        if get.is_null() {
            self.host.log("minimap-goblins: missing official map API\n");
            return;
        }
        // SAFETY: named export has the public mfg_map_api.h signature.
        let get: unsafe extern "C" fn(u32) -> *const Api = unsafe { std::mem::transmute(get) };
        let api = unsafe { get(1) };
        if api.is_null() {
            return;
        }
        // SAFETY: the public provider always exposes this fixed size/version header.
        let size = unsafe { std::ptr::addr_of!((*api).size).read_unaligned() };
        let version = unsafe { std::ptr::addr_of!((*api).abi_version).read_unaligned() };
        if size < size_of::<Api>() as u32 || version != 1 {
            return;
        }
        let api = unsafe { api.read_unaligned() };
        if api.size < size_of::<Api>() as u32
            || api.abi_version != 1
            || api.point_size != size_of::<Point>() as u32
            || api.icon_size != size_of::<Icon>() as u32
            || api.icon_count == 0
            || api.icon_count > 512
            || api.view_points.is_none()
            || api.get_icon.is_none()
        {
            self.host.log("minimap-goblins: incompatible map API\n");
            return;
        }
        let mut icons = HashMap::new();
        let mut remaining = 64 * 1024 * 1024;
        for index in 0..api.icon_count {
            let mut icon = Icon {
                lossless2: std::ptr::null(),
                lossless2_size: 0,
                source_icon_id: 0,
                matrix: std::ptr::null(),
                matrix_size: 0,
                reserved: 0,
            };
            if unsafe { api.get_icon.unwrap()(index, &mut icon) } != 0
                || icon.lossless2.is_null()
                || icon.matrix.is_null()
                || icon.lossless2_size == 0
                || icon.lossless2_size > 1024 * 1024
                || icon.matrix_size == 0
                || icon.matrix_size > 64
            {
                return;
            }
            // SAFETY: validated official API returns immutable byte ranges valid while module retained.
            let lossless =
                unsafe { std::slice::from_raw_parts(icon.lossless2, icon.lossless2_size as usize) };
            let matrix =
                unsafe { std::slice::from_raw_parts(icon.matrix, icon.matrix_size as usize) };
            let Some((recipe, width, height, rgba)) =
                decode(icon.source_icon_id, lossless, matrix, remaining)
            else {
                return;
            };
            remaining -= rgba.len();
            if icons
                .insert(
                    icon.source_icon_id,
                    RuntimeIcon {
                        recipe,
                        region: Region {
                            atlas: 0,
                            x: 0,
                            y: 0,
                            width,
                            height,
                        },
                        texture: None,
                        pending: 0,
                        rgba,
                        touched: Instant::now(),
                        retry: Instant::now(),
                    },
                )
                .is_some()
            {
                return;
            }
        }
        self.icons = icons;
        self.api = Some(api);
        self.host
            .log("minimap-goblins: connected official read-only map API\n");
    }
    #[cfg(not(windows))]
    fn connect(&mut self) {}
    pub fn update(&mut self, demand: &HashSet<u32>, budget: &mut UploadBudget) {
        let mut changed = false;
        let now = Instant::now();
        for (id, icon) in &mut self.icons {
            let wanted = demand.contains(id);
            if wanted {
                icon.touched = now;
            } else if now.duration_since(icon.touched) >= Duration::from_secs(10) {
                changed |= icon.texture.take().is_some();
                self.host.release_texture(icon.pending);
                icon.pending = 0;
            }
            if icon.pending != 0 {
                match Texture::poll(self.host, icon.pending) {
                    Some(Ok(texture)) => {
                        icon.texture = Some(Arc::new(texture));
                        icon.pending = 0;
                        changed = true;
                    }
                    Some(Err(())) => {
                        self.host.release_texture(icon.pending);
                        icon.pending = 0;
                        icon.retry = now + Duration::from_secs(1);
                    }
                    None => {}
                }
            } else if icon.texture.is_none()
                && wanted
                && now >= icon.retry
                && budget.accepts(icon.rgba.len())
            {
                icon.pending = self
                    .host
                    .rgba(icon.region.width, icon.region.height, &icon.rgba);
                if icon.pending != 0 {
                    budget.record(icon.rgba.len());
                } else {
                    icon.retry = now + Duration::from_millis(250);
                }
            }
        }
        if changed {
            self.visuals = Arc::new(
                self.icons
                    .iter()
                    .filter_map(|(id, icon)| {
                        Some((*id, (icon.recipe.clone(), icon.texture.as_ref()?.clone())))
                    })
                    .collect(),
            );
        }
    }
    pub fn points(&mut self, layer: u32) -> Vec<Point> {
        self.connect();
        let Some(api) = self.api else {
            return Vec::new();
        };
        let mut points = std::ptr::null();
        let mut count = 0;
        // SAFETY: retained provider and validated callbacks. View is synchronously copied once.
        if unsafe { api.view_points.unwrap()(layer, &mut points, &mut count) } != 0
            || count > 32768
            || (count != 0 && points.is_null())
        {
            return Vec::new();
        }
        if count == 0 {
            return Vec::new();
        }
        unsafe { std::slice::from_raw_parts(points, count as usize) }
            .iter()
            .copied()
            .filter(|p| p.x.is_finite() && p.z.is_finite())
            .collect()
    }
    pub fn reset(&mut self) {
        for icon in self.icons.values_mut() {
            icon.texture = None;
            self.host.release_texture(icon.pending);
            icon.pending = 0;
            icon.retry = Instant::now();
        }
        self.visuals = Arc::new(HashMap::new());
    }
}
impl Drop for Goblins {
    fn drop(&mut self) {
        for icon in self.icons.values_mut() {
            self.host.release_texture(icon.pending);
            icon.pending = 0;
        }
        self.icons.clear();
        #[cfg(windows)]
        if !self.module.is_null() {
            unsafe { FreeLibrary(self.module) };
        }
    }
}

fn decode(
    id: u32,
    bytes: &[u8],
    matrix: &[u8],
    budget: usize,
) -> Option<(Recipe, u32, u32, Vec<u8>)> {
    let mut r = Reader::new(bytes);
    r.u16()?;
    let kind = r.u8()?;
    if kind != 3 && kind != 5 {
        return None;
    }
    let width = u32::from(r.u16()?);
    let height = u32::from(r.u16()?);
    if width == 0 || height == 0 || width > 1024 || height > 1024 {
        return None;
    }
    let palette = if kind == 3 {
        usize::from(r.u8()?) + 1
    } else {
        0
    };
    let size = width as usize * height as usize * 4;
    if size > budget {
        return None;
    }
    let stride = if kind == 3 {
        (width as usize).div_ceil(4) * 4
    } else {
        width as usize * 4
    };
    let raw_size = palette * 4 + stride * height as usize;
    let raw = miniz_oxide::inflate::decompress_to_vec_zlib_with_limit(
        r.take(bytes.len() - r.at)?,
        raw_size,
    )
    .ok()?;
    if raw.len() != raw_size {
        return None;
    }
    let mut rgba = Vec::with_capacity(size);
    for y in 0..height as usize {
        for x in 0..width as usize {
            let p = if kind == 3 {
                let index = usize::from(raw[palette * 4 + y * stride + x]);
                if index >= palette {
                    return None;
                }
                &raw[index * 4..index * 4 + 4]
            } else {
                &raw[y * stride + x * 4..y * stride + x * 4 + 4]
            };
            let alpha = p[if kind == 3 { 3 } else { 0 }];
            for channel in 0..3 {
                let value = u32::from(p[if kind == 3 { channel } else { channel + 1 }]);
                rgba.push(if alpha == 0 {
                    0
                } else {
                    ((value * 255 + u32::from(alpha) / 2) / u32::from(alpha)).min(255) as u8
                });
            }
            rgba.push(alpha);
        }
    }
    let matrix = gfx::parse_matrix(matrix)?;
    for p in [
        [0.0, 0.0],
        [width as f32, 0.0],
        [0.0, height as f32],
        [width as f32, height as f32],
    ] {
        if gfx::transform(matrix, p)
            .iter()
            .any(|v| !v.is_finite() || v.abs() > 4096.0)
        {
            return None;
        }
    }
    let layer = IconLayer {
        image: format!("mfg:{id}"),
        width,
        height,
        matrix,
        ..Default::default()
    };
    Some((
        Recipe {
            layers: vec![layer],
        },
        width,
        height,
        rgba,
    ))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn official_abi_sizes() {
        assert_eq!(size_of::<Point>(), 32);
        assert_eq!(size_of::<Icon>(), 32);
        assert_eq!(size_of::<Api>(), 40);
    }
    #[test]
    fn runtime_icon_bounds_and_premultiplication() {
        let mut raw = vec![0, 0, 5, 1, 0, 1, 0];
        raw.extend(miniz_oxide::deflate::compress_to_vec_zlib(
            &[128, 64, 32, 0],
            6,
        ));
        let (_, w, h, rgba) = decode(7, &raw, &[0], 4).unwrap();
        assert_eq!((w, h), (1, 1));
        assert_eq!(rgba, [128, 64, 0, 128]);
        assert!(decode(7, &raw, &[0], 3).is_none());
    }

    #[test]
    fn runtime_uploads_follow_visible_demand_and_retire_idle_pending_work() {
        struct Sink {
            uploads: usize,
            releases: usize,
        }
        unsafe extern "C" fn upload(
            user: *mut c_void,
            _: u32,
            _: u32,
            _: eroverlay_abi::ByteSlice,
        ) -> u64 {
            let sink = unsafe { &mut *user.cast::<Sink>() };
            sink.uploads += 1;
            sink.uploads as u64
        }
        unsafe extern "C" fn release(user: *mut c_void, _: u64) {
            unsafe { &mut *user.cast::<Sink>() }.releases += 1;
        }
        let mut sink = Sink {
            uploads: 0,
            releases: 0,
        };
        // SAFETY: HostApi contains integers, raw pointers and nullable callbacks only.
        let mut api: eroverlay_abi::HostApi = unsafe { std::mem::zeroed() };
        api.user = (&mut sink as *mut Sink).cast();
        api.texture_create_rgba = Some(upload);
        api.texture_release = Some(release);
        let mut goblins = Goblins::new(Host(api));
        for id in [7, 8] {
            goblins.icons.insert(
                id,
                RuntimeIcon {
                    recipe: Recipe { layers: Vec::new() },
                    region: Region {
                        atlas: 0,
                        x: 0,
                        y: 0,
                        width: 1,
                        height: 1,
                    },
                    texture: None,
                    pending: 0,
                    rgba: vec![255; 4],
                    touched: Instant::now(),
                    retry: Instant::now(),
                },
            );
        }
        goblins.update(&HashSet::new(), &mut UploadBudget::new());
        assert_eq!(sink.uploads, 0);
        goblins.update(&HashSet::from([7]), &mut UploadBudget::new());
        assert_eq!(sink.uploads, 1);
        assert_eq!(goblins.icons[&7].pending, 1);
        assert_eq!(goblins.icons[&8].pending, 0);
        goblins.icons.get_mut(&7).unwrap().touched = Instant::now() - Duration::from_secs(11);
        goblins.update(&HashSet::new(), &mut UploadBudget::new());
        assert_eq!(sink.uploads, 1);
        assert_eq!(sink.releases, 1);
        assert_eq!(goblins.icons[&7].pending, 0);
    }
    #[test]
    fn rejected_runtime_uploads_back_off_and_pending_tokens_are_polled_once() {
        struct Sink {
            accepted: bool,
            uploads: usize,
            polls: usize,
            releases: usize,
            status: u32,
        }
        unsafe extern "C" fn upload(
            user: *mut c_void,
            _: u32,
            _: u32,
            _: crate::abi::ByteSlice,
        ) -> u64 {
            let sink = unsafe { &mut *user.cast::<Sink>() };
            sink.uploads += 1;
            if sink.accepted {
                sink.uploads as u64
            } else {
                0
            }
        }
        unsafe extern "C" fn poll(
            user: *mut c_void,
            _: u64,
            out: *mut crate::abi::TextureView,
        ) -> i32 {
            let sink = unsafe { &mut *user.cast::<Sink>() };
            sink.polls += 1;
            unsafe {
                out.write(crate::abi::TextureView {
                    status: sink.status,
                    width: 1,
                    height: 1,
                    memory_bytes: 4,
                    ..Default::default()
                })
            };
            crate::abi::OK
        }
        unsafe extern "C" fn release(user: *mut c_void, _: u64) {
            unsafe { &mut *user.cast::<Sink>() }.releases += 1;
        }
        let mut sink = Sink {
            accepted: false,
            uploads: 0,
            polls: 0,
            releases: 0,
            status: crate::abi::TEXTURE_PENDING,
        };
        // SAFETY: HostApi contains integers, raw pointers and nullable callbacks only.
        let mut api: crate::abi::HostApi = unsafe { std::mem::zeroed() };
        api.user = std::ptr::from_mut(&mut sink).cast();
        api.texture_create_rgba = Some(upload);
        api.texture_poll = Some(poll);
        api.texture_release = Some(release);
        let mut goblins = Goblins::new(Host(api));
        goblins.icons.insert(
            7,
            RuntimeIcon {
                recipe: Recipe { layers: Vec::new() },
                region: Region {
                    width: 1,
                    height: 1,
                    ..Default::default()
                },
                texture: None,
                pending: 0,
                rgba: vec![255; 4],
                touched: Instant::now(),
                retry: Instant::now(),
            },
        );
        let demand = HashSet::from([7]);
        let rejected_at = Instant::now();
        goblins.update(&demand, &mut UploadBudget::new());
        assert_eq!(sink.uploads, 1);
        assert!(goblins.icons[&7].retry.duration_since(rejected_at) >= Duration::from_millis(250));
        goblins.icons.get_mut(&7).unwrap().retry = Instant::now() + Duration::from_secs(60);
        for _ in 0..100 {
            goblins.update(&demand, &mut UploadBudget::new());
        }
        assert_eq!(
            sink.uploads, 1,
            "full queues must not be retried every update"
        );
        assert_eq!(sink.polls, 0);
        sink.accepted = true;
        goblins.icons.get_mut(&7).unwrap().retry = Instant::now() - Duration::from_millis(1);
        goblins.update(&demand, &mut UploadBudget::new());
        assert_eq!(sink.uploads, 2);
        for _ in 0..100 {
            goblins.update(&demand, &mut UploadBudget::new());
        }
        assert_eq!(sink.uploads, 2);
        assert_eq!(sink.polls, 100, "one poll per pending token and update");
        sink.status = crate::abi::TEXTURE_FAILED;
        let failed_at = Instant::now();
        goblins.update(&demand, &mut UploadBudget::new());
        assert_eq!(sink.polls, 101);
        assert_eq!(sink.releases, 1);
        assert_eq!(goblins.icons[&7].pending, 0);
        assert!(goblins.icons[&7].retry.duration_since(failed_at) >= Duration::from_secs(1));
        goblins.icons.get_mut(&7).unwrap().retry = Instant::now() + Duration::from_secs(60);
        for _ in 0..100 {
            goblins.update(&demand, &mut UploadBudget::new());
        }
        assert_eq!(
            sink.uploads, 2,
            "failed tokens must retain their retry delay"
        );
        assert_eq!(sink.polls, 101);
        goblins.icons.get_mut(&7).unwrap().retry = Instant::now() - Duration::from_millis(1);
        sink.status = crate::abi::TEXTURE_READY;
        goblins.update(&demand, &mut UploadBudget::new());
        goblins.update(&demand, &mut UploadBudget::new());
        assert_eq!(sink.uploads, 3);
        assert_eq!(sink.polls, 102);
        assert!(goblins.visuals.contains_key(&7));
        for _ in 0..100 {
            goblins.update(&demand, &mut UploadBudget::new());
        }
        assert_eq!(sink.uploads, 3);
        assert_eq!(sink.polls, 102, "ready textures need no polling");
        goblins.reset();
        assert_eq!(sink.releases, 2);
    }
}
