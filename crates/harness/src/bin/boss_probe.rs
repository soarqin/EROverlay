//! Optional production-DLL probe. PARAM/EMEVD/FMG are real archived resources;
//! save state, flags, input and writes are controlled fixtures, never a live game.
#[path = "../gpu.rs"]
#[allow(dead_code)]
mod gpu;

use eroverlay_abi as abi;
use eroverlay_renderer::{NativeFrame, Renderer};
use std::{
    collections::{BTreeMap, BTreeSet, VecDeque},
    ffi::c_void,
    path::{Path, PathBuf},
    time::{Duration, Instant},
};
use windows::{
    Win32::{
        Foundation::{FreeLibrary, HMODULE},
        System::LibraryLoader::{GetProcAddress, LoadLibraryW},
    },
    core::{PCWSTR, s},
};

struct Geometry {
    texture: u64,
    vertices: Vec<abi::Vertex>,
    indices: Vec<u32>,
    clip: [f32; 4],
}
struct Upload {
    id: u64,
    width: u32,
    height: u32,
    bytes: Vec<u8>,
}
struct Fixture {
    root: String,
    params: BTreeMap<u32, Vec<u8>>,
    files: BTreeMap<String, Vec<u8>>,
    requests: BTreeMap<u64, Option<String>>,
    keys: BTreeSet<u32>,
    input: VecDeque<abi::UiInput>,
    capture: u32,
    polls: u64,
    directory_count: usize,
    last_flag_count: usize,
    flags: BTreeMap<u32, u8>,
    display: abi::DisplayState,
    state: abi::BossState,
    writes: Vec<(u32, Vec<abi::FlagWrite>)>,
    options: abi::RenderOptions,
    meshes: Vec<Geometry>,
    uploads: Vec<Upload>,
    textures: BTreeMap<u64, (u32, u32)>,
    renderer: Renderer,
    next: u64,
    upload_count: u64,
    request_count: u64,
}
unsafe fn fixture<'a>(user: *mut c_void) -> &'a mut Fixture {
    unsafe { &mut *user.cast::<Fixture>() }
}
unsafe fn text<'a>(value: abi::ByteSlice) -> &'a str {
    std::str::from_utf8(unsafe { std::slice::from_raw_parts(value.data, value.len) }).unwrap_or("")
}
unsafe extern "C" fn log(user: *mut c_void, _level: u32, value: abi::ByteSlice) {
    let value = unsafe { text(value) };
    if let Some(rest) = value.strip_prefix("boss-catalog: ") {
        unsafe { fixture(user) }.directory_count = rest
            .split_whitespace()
            .next()
            .and_then(|n| n.parse().ok())
            .unwrap_or(0);
    }
    eprint!("{value}");
}
unsafe extern "C" fn config(
    user: *mut c_void,
    key: abi::ByteSlice,
    out: *mut u8,
    capacity: usize,
) -> usize {
    let f = unsafe { fixture(user) };
    let value = match unsafe { text(key) } {
        "runtime.directory" => f.root.as_str(),
        "runtime.game_language" | "common.language" => "engUS",
        "boss.toggle_full_mode" => "=",
        "boss.toggle" => "F9",
        "boss.allow_revive" => "true",
        "boss.width" => "440",
        "boss.height" => "800",
        "boss.font_size" => "20",
        _ => return usize::MAX,
    };
    if !out.is_null() {
        unsafe { std::ptr::copy_nonoverlapping(value.as_ptr(), out, value.len().min(capacity)) };
    }
    value.len()
}
unsafe extern "C" fn key(user: *mut c_void, chord: u32) -> u32 {
    u32::from(unsafe { fixture(user) }.keys.remove(&chord))
}
unsafe extern "C" fn display(user: *mut c_void, out: *mut abi::DisplayState) -> i32 {
    unsafe { out.write(fixture(user).display) };
    abi::OK
}
unsafe extern "C" fn state(user: *mut c_void, out: *mut abi::BossState) -> i32 {
    unsafe { out.write(fixture(user).state) };
    abi::OK
}
unsafe extern "C" fn flags(user: *mut c_void, ids: *const u32, out: *mut u8, len: usize) -> i32 {
    if len > 4096 {
        return abi::ERR_INVALID;
    }
    let f = unsafe { fixture(user) };
    f.last_flag_count = len;
    for (id, out) in unsafe { std::slice::from_raw_parts(ids, len) }
        .iter()
        .zip(unsafe { std::slice::from_raw_parts_mut(out, len) })
    {
        *out = f.flags.get(id).copied().unwrap_or(1);
    }
    abi::OK
}
unsafe extern "C" fn event_flag(user: *mut c_void, id: u32) -> u32 {
    u32::from(
        unsafe { fixture(user) }
            .flags
            .get(&id)
            .copied()
            .unwrap_or(1)
            == 1,
    )
}
unsafe extern "C" fn revive(
    user: *mut c_void,
    context: u64,
    primary: u32,
    writes: *const abi::FlagWrite,
    len: usize,
) -> i32 {
    let f = unsafe { fixture(user) };
    if context != f.state.context || len > 2048 {
        return abi::ERR_UNAVAILABLE;
    }
    let writes = if len == 0 {
        Vec::new()
    } else {
        unsafe { std::slice::from_raw_parts(writes, len) }.to_vec()
    };
    f.flags.insert(primary, 0);
    f.writes.push((primary, writes));
    abi::OK
}
unsafe extern "C" fn param(user: *mut c_void, id: u32) -> usize {
    unsafe { fixture(user) }
        .params
        .get(&id)
        .map_or(0, |b| b.as_ptr() as usize + 16)
}
unsafe extern "C" fn request(user: *mut c_void, request: *const abi::FileRequest) -> u64 {
    let f = unsafe { fixture(user) };
    let request = unsafe { &*request };
    let path = unsafe { text(request.path) };
    let name = if let Some(name) = path.strip_prefix("event:/") {
        name
    } else if path.contains("item") {
        "item_dlc02.bnd"
    } else if path.contains("menu") {
        "menu_dlc02.bnd"
    } else {
        path
    };
    let name = f.files.contains_key(name).then(|| name.to_owned());
    f.next += 1;
    f.request_count += 1;
    f.requests.insert(f.next, name);
    f.next
}
unsafe extern "C" fn file(user: *mut c_void, id: u64, out: *mut abi::FileView) -> i32 {
    let f = unsafe { fixture(user) };
    let Some(request) = f.requests.get(&id) else {
        return abi::ERR_INVALID;
    };
    let view = if let Some(name) = request {
        abi::FileView {
            status: abi::FILE_READY,
            reserved: 0,
            bytes: abi::ByteSlice::new(&f.files[name]),
        }
    } else {
        abi::FileView::default()
    };
    unsafe { out.write(view) };
    abi::OK
}
unsafe extern "C" fn file_release(user: *mut c_void, id: u64) {
    unsafe { fixture(user) }.requests.remove(&id);
}
unsafe extern "C" fn rgba(
    user: *mut c_void,
    width: u32,
    height: u32,
    bytes: abi::ByteSlice,
) -> u64 {
    let f = unsafe { fixture(user) };
    if bytes.len != width as usize * height as usize * 4 || bytes.len > abi::MAX_TEXTURE_BYTES {
        return 0;
    }
    f.next += 1;
    let id = f.next;
    f.upload_count += 1;
    f.textures.insert(id, (width, height));
    f.uploads.push(Upload {
        id,
        width,
        height,
        bytes: unsafe { std::slice::from_raw_parts(bytes.data, bytes.len) }.to_vec(),
    });
    id
}
unsafe extern "C" fn texture(user: *mut c_void, id: u64, out: *mut abi::TextureView) -> i32 {
    let f = unsafe { fixture(user) };
    let Some(&(width, height)) = f.textures.get(&id) else {
        return abi::ERR_INVALID;
    };
    unsafe {
        out.write(abi::TextureView {
            status: if f.renderer.texture_ready(id) {
                abi::TEXTURE_READY
            } else {
                abi::TEXTURE_PENDING
            },
            width,
            height,
            reserved: 0,
            memory_bytes: u64::from(width) * u64::from(height) * 4,
        })
    };
    abi::OK
}
unsafe extern "C" fn texture_release(user: *mut c_void, id: u64) {
    let f = unsafe { fixture(user) };
    f.renderer.retire_texture(id);
    f.textures.remove(&id);
}
unsafe extern "C" fn submit(user: *mut c_void, mesh: *const abi::Mesh) -> i32 {
    let f = unsafe { fixture(user) };
    let mesh = unsafe { &*mesh };
    if mesh.vertex_count as usize > abi::MAX_VERTICES
        || mesh.index_count as usize > abi::MAX_INDICES
    {
        return abi::ERR_INVALID;
    }
    f.meshes.push(Geometry {
        texture: mesh.texture,
        vertices: unsafe { std::slice::from_raw_parts(mesh.vertices, mesh.vertex_count as usize) }
            .to_vec(),
        indices: unsafe { std::slice::from_raw_parts(mesh.indices, mesh.index_count as usize) }
            .to_vec(),
        clip: mesh.clip,
    });
    abi::OK
}
unsafe extern "C" fn options(user: *mut c_void, options: *const abi::RenderOptions) -> i32 {
    unsafe { fixture(user) }.options = unsafe { *options };
    abi::OK
}
unsafe extern "C" fn input_poll(
    user: *mut c_void,
    out: *mut abi::UiInput,
    capacity: usize,
    count: *mut usize,
) -> i32 {
    let f = unsafe { fixture(user) };
    f.polls += 1;
    let n = capacity.min(f.input.len());
    for index in 0..n {
        unsafe { out.add(index).write(f.input.pop_front().unwrap()) };
    }
    unsafe { count.write(n) };
    abi::OK
}
fn event(kind: u32, code: u32, flags: u32, x: f32, y: f32) -> abi::UiInput {
    abi::UiInput {
        size: size_of::<abi::UiInput>() as u32,
        kind,
        code,
        flags,
        x,
        y,
        ..Default::default()
    }
}
unsafe extern "C" fn input_clear(user: *mut c_void) -> i32 {
    let f = unsafe { fixture(user) };
    f.input.clear();
    f.input.push_back(event(abi::UI_INPUT_RESET, 0, 0, 0., 0.));
    f.input.push_back(event(abi::UI_INPUT_FOCUS, 1, 0, 0., 0.));
    abi::OK
}
unsafe extern "C" fn capture(user: *mut c_void, mask: u32) -> i32 {
    unsafe { fixture(user) }.capture = mask;
    abi::OK
}

unsafe extern "C" fn unavailable_map(_: *mut c_void, _: *mut abi::MapState) -> i32 {
    abi::ERR_UNAVAILABLE
}
unsafe extern "C" fn unavailable_memory(_: *mut c_void, _: usize, _: *mut u8, _: usize) -> i32 {
    abi::ERR_UNAVAILABLE
}
unsafe extern "C" fn unavailable_dds(_: *mut c_void, _: abi::ByteSlice) -> u64 {
    0
}

#[link(name = "kernel32")]
unsafe extern "system" {
    fn VirtualAlloc(address: *mut c_void, size: usize, kind: u32, protect: u32) -> *mut c_void;
    fn VirtualProtect(address: *mut c_void, size: usize, protect: u32, old: *mut u32) -> i32;
    fn VirtualFree(address: *mut c_void, size: usize, kind: u32) -> i32;
    fn GetSystemInfo(out: *mut c_void);
}
struct GuardedHost {
    allocation: *mut c_void,
    table: *const abi::HostApi,
}
impl GuardedHost {
    fn new(host: &abi::HostApi) -> Result<Self, String> {
        // SYSTEM_INFO is <=64 bytes on supported Windows x64; dwPageSize is at 4.
        let mut information = [0usize; 8];
        unsafe { GetSystemInfo(information.as_mut_ptr().cast()) };
        let page = unsafe {
            information
                .as_ptr()
                .cast::<u8>()
                .add(4)
                .cast::<u32>()
                .read()
        } as usize;
        if page < abi::HOST_V1_BASE_SIZE || !page.is_power_of_two() {
            return Err("unexpected system page size".into());
        }
        let allocation = unsafe { VirtualAlloc(std::ptr::null_mut(), page * 2, 0x3000, 4) };
        if allocation.is_null() {
            return Err("VirtualAlloc guard fixture failed".into());
        }
        let mut old = 0;
        if unsafe { VirtualProtect(allocation.cast::<u8>().add(page).cast(), page, 1, &mut old) }
            == 0
        {
            unsafe { VirtualFree(allocation, 0, 0x8000) };
            return Err("VirtualProtect guard fixture failed".into());
        }
        let table = unsafe { allocation.cast::<u8>().add(page - abi::HOST_V1_BASE_SIZE) };
        unsafe {
            std::ptr::copy_nonoverlapping(
                (host as *const abi::HostApi).cast::<u8>(),
                table,
                abi::HOST_V1_BASE_SIZE,
            )
        };
        Ok(Self {
            allocation,
            table: table.cast(),
        })
    }
}
impl Drop for GuardedHost {
    fn drop(&mut self) {
        unsafe { VirtualFree(self.allocation, 0, 0x8000) };
    }
}

fn short_minimap(path: &Path, host: &abi::HostApi) -> Result<(), String> {
    if !path.is_file() {
        return Err(format!(
            "{} missing for short-table compatibility check",
            path.display()
        ));
    }
    use std::os::windows::ffi::OsStrExt;
    let wide: Vec<u16> = path.as_os_str().encode_wide().chain(Some(0)).collect();
    let module = unsafe { LoadLibraryW(PCWSTR(wide.as_ptr())) }.map_err(|e| e.to_string())?;
    let result = (|| {
        let get: abi::GetPlugin = unsafe {
            std::mem::transmute(
                GetProcAddress(module, s!("getEROverlayPluginV1"))
                    .ok_or("missing Minimap export")?,
            )
        };
        let api = unsafe { *get(abi::ABI_VERSION) };
        let mut old = *host;
        old.size = abi::HOST_V1_BASE_SIZE as u32;
        old.capabilities &= !(abi::CAP_UI_INPUT | abi::CAP_BOSS_STATE);
        let guarded = GuardedHost::new(&old)?;
        let mut instance = std::ptr::null_mut();
        let code = unsafe { api.init.unwrap()(guarded.table, &mut instance) };
        if !instance.is_null() {
            unsafe { api.destroy.unwrap()(instance) };
        }
        if code != abi::OK || instance.is_null() {
            return Err(format!(
                "new Minimap rejected guarded original V1 host prefix: {code}"
            ));
        }
        println!(
            "PASS: actual new Minimap DLL init/destroy with only 176 host bytes readable; next page PAGE_NOACCESS, no appended capabilities."
        );
        Ok(())
    })();
    let _ = unsafe { FreeLibrary(module) };
    result
}

struct Plugin {
    module: HMODULE,
    api: abi::PluginApi,
    instance: *mut c_void,
}
impl Drop for Plugin {
    fn drop(&mut self) {
        unsafe {
            self.api.destroy.unwrap()(self.instance);
            let _ = FreeLibrary(self.module);
        }
    }
}
struct Probe {
    plugin: Plugin,
    fixture: Box<Fixture>,
    gpu: gpu::WarpHost,
    sequence: u64,
    cpu: Duration,
}
impl Probe {
    fn frame(&mut self) -> Result<usize, String> {
        self.sequence += 1;
        self.render_sequence(self.sequence)
    }
    fn render_sequence(&mut self, sequence: u64) -> Result<usize, String> {
        self.fixture.meshes.clear();
        let info = abi::FrameInfo {
            size: size_of::<abi::FrameInfo>() as u32,
            layer: abi::LAYER_ABOVE_UI,
            sequence,
            width: 1280.,
            height: 900.,
            pixels_per_point: 1.,
            delta_seconds: 1. / 60.,
        };
        let start = Instant::now();
        let code = unsafe { self.plugin.api.render.unwrap()(self.plugin.instance, &info) };
        self.cpu = start.elapsed();
        if code != abi::OK && code != abi::ERR_BUSY {
            return Err(format!("Boss render failed: {code}"));
        }
        let fixture = &mut *self.fixture;
        self.gpu.clear().map_err(|e| e.to_string())?;
        for upload in fixture.uploads.drain(..) {
            if !fixture
                .renderer
                .upload_rgba(upload.id, upload.width, upload.height, &upload.bytes)
            {
                return Err("font atlas rejected".into());
            }
        }
        let meshes: Vec<_> = fixture
            .meshes
            .iter()
            .map(|m| abi::Mesh {
                texture: m.texture,
                vertices: m.vertices.as_ptr(),
                vertex_count: m.vertices.len() as u32,
                indices: m.indices.as_ptr(),
                index_count: m.indices.len() as u32,
                clip: m.clip,
            })
            .collect();
        let native = self.gpu.frame(1280., 900., sequence, abi::LAYER_ABOVE_UI);
        unsafe { fixture.renderer.render(&native, &meshes, fixture.options) }
            .map_err(|e| e.to_string())?;
        self.gpu.drain().map_err(|e| e.to_string())?;
        self.gpu.check_validation()?;
        Ok(fixture.meshes.iter().map(|m| m.vertices.len()).sum())
    }
    fn update(&mut self) -> Result<Duration, String> {
        let start = Instant::now();
        let code = unsafe { self.plugin.api.update.unwrap()(self.plugin.instance) };
        if code != abi::OK {
            return Err(format!("Boss update failed: {code}"));
        }
        Ok(start.elapsed())
    }
    fn hotkey(&mut self, chord: u32) -> Result<(), String> {
        self.fixture.keys.insert(chord);
        self.update()?;
        Ok(())
    }
    fn click(&mut self, x: f32, y: f32) -> Result<usize, String> {
        self.fixture
            .input
            .push_back(event(abi::UI_INPUT_POINTER, 0, 0, x, y));
        self.fixture
            .input
            .push_back(event(abi::UI_INPUT_BUTTON, 0, abi::UI_INPUT_DOWN, x, y));
        self.frame()?;
        self.fixture
            .input
            .push_back(event(abi::UI_INPUT_BUTTON, 0, 0, x, y));
        self.frame()
    }
    fn query(&mut self, value: &str) -> Result<usize, String> {
        self.fixture.input.push_back(event(
            abi::UI_INPUT_KEY,
            0x41,
            abi::UI_INPUT_DOWN | abi::MOD_CTRL,
            0.,
            0.,
        ));
        self.fixture
            .input
            .push_back(event(abi::UI_INPUT_KEY, 0x41, abi::MOD_CTRL, 0., 0.));
        self.fixture
            .input
            .push_back(event(abi::UI_INPUT_KEY, 0x08, abi::UI_INPUT_DOWN, 0., 0.));
        self.fixture
            .input
            .push_back(event(abi::UI_INPUT_KEY, 0x08, 0, 0., 0.));
        for c in value.chars() {
            self.fixture
                .input
                .push_back(event(abi::UI_INPUT_TEXT, u32::from(c), 0, 0., 0.));
        }
        self.frame()?;
        self.frame()
    }
    fn measure(&mut self, name: &str) -> Result<(), String> {
        for _ in 0..5 {
            self.frame()?;
        }
        let uploads = self.fixture.upload_count;
        let requests = self.fixture.request_count;
        let mut samples = Vec::new();
        let mut vertices = 0;
        let mut meshes = 0;
        for _ in 0..60 {
            vertices += self.frame()?;
            meshes += self.fixture.meshes.len();
            samples.push(self.cpu.as_nanos());
        }
        samples.sort_unstable();
        println!(
            "{name}: render CPU median={}us p95={}us; mean vertices={} meshes={}; atlas replacements={} file requests={}; WARP submits/drain excluded from CPU samples",
            samples[30] / 1000,
            samples[57] / 1000,
            vertices / 60,
            meshes / 60,
            self.fixture.upload_count - uploads,
            self.fixture.request_count - requests
        );
        if self.fixture.upload_count != uploads || self.fixture.request_count != requests {
            return Err(format!(
                "{name}: steady rendering repeated atlas uploads or file requests"
            ));
        }
        Ok(())
    }
    fn screenshot(&self, name: &str) -> Result<usize, String> {
        let (width, height, rgba) = self.gpu.readback().map_err(|e| e.to_string())?;
        let coloured = rgba
            .as_chunks::<4>()
            .0
            .iter()
            .filter(|p| p[..3] != [0, 0, 0])
            .count();
        let stride = (width as usize * 3).div_ceil(4) * 4;
        let mut bytes = vec![0u8; 54 + stride * height as usize];
        let size = bytes.len() as u32;
        bytes[..2].copy_from_slice(b"BM");
        bytes[2..6].copy_from_slice(&size.to_le_bytes());
        bytes[10..14].copy_from_slice(&54u32.to_le_bytes());
        bytes[14..18].copy_from_slice(&40u32.to_le_bytes());
        bytes[18..22].copy_from_slice(&width.to_le_bytes());
        bytes[22..26].copy_from_slice(&height.to_le_bytes());
        bytes[26..28].copy_from_slice(&1u16.to_le_bytes());
        bytes[28..30].copy_from_slice(&24u16.to_le_bytes());
        for y in 0..height as usize {
            for x in 0..width as usize {
                let src = (y * width as usize + x) * 4;
                let dst = 54 + (height as usize - 1 - y) * stride + x * 3;
                bytes[dst..dst + 3].copy_from_slice(&[rgba[src + 2], rgba[src + 1], rgba[src]]);
            }
        }
        let path = Path::new(&self.fixture.root).join(format!("{name}.bmp"));
        std::fs::write(&path, bytes).map_err(|e| e.to_string())?;
        println!(
            "WARP readback {}: {width}x{height}, nonblack RGB pixels={coloured}",
            path.display()
        );
        Ok(coloured)
    }
}
fn read(path: &Path) -> Result<Vec<u8>, String> {
    std::fs::read(path).map_err(|e| format!("{}: {e}", path.display()))
}
fn main() {
    if let Err(e) = run() {
        eprintln!("Boss 离线验证失败：{e}");
        std::process::exit(1);
    }
}
fn run() -> Result<(), String> {
    let mut args = std::env::args_os().skip(1);
    let dll=PathBuf::from(args.next().ok_or("usage: boss_probe <Boss.dll> <raw-PARAM-directory> <raw-EMEVD-directory> [scratch-directory]")?).canonicalize().map_err(|e|e.to_string())?;
    let archive = PathBuf::from(args.next().ok_or("missing PARAM directory")?);
    let events = PathBuf::from(args.next().ok_or("missing EMEVD directory")?);
    let root = PathBuf::from(
        args.next()
            .unwrap_or_else(|| format!("target/boss-probe-{}", std::process::id()).into()),
    );
    std::fs::create_dir_all(root.join("configs")).map_err(|e| e.to_string())?;
    let root = root.canonicalize().map_err(|e| e.to_string())?;
    std::fs::copy(
        "configs/boss-revival.json",
        root.join("configs/boss-revival.json"),
    )
    .map_err(|e| e.to_string())?;
    let mut params = BTreeMap::new();
    for (group, name) in [
        (29, "GameAreaParam"),
        (43, "BonfireWarpParam"),
        (44, "BonfireWarpTabParam"),
        (45, "BonfireWarpSubCategoryParam"),
        (87, "WorldMapPointParam"),
        (133, "MapDefaultInfoParam"),
    ] {
        let input = read(&archive.join(format!("{name}.param")))?;
        let mut mapped = vec![0u8; 16];
        mapped[..4].copy_from_slice(&(input.len() as u32).to_le_bytes());
        mapped.extend_from_slice(&input);
        params.insert(group, mapped);
    }
    let mut files = BTreeMap::new();
    for name in ["item_dlc02.bnd", "menu_dlc02.bnd"] {
        files.insert(name.into(), read(&archive.join(name))?);
    }
    for entry in std::fs::read_dir(events).map_err(|e| e.to_string())? {
        let entry = entry.map_err(|e| e.to_string())?;
        let name = entry.file_name().to_string_lossy().into_owned();
        if name.ends_with(".emevd") {
            files.insert(name, read(&entry.path())?);
        }
    }
    let gpu = gpu::WarpHost::new_size(1280, 900).map_err(|e| e.to_string())?;
    let native: NativeFrame = gpu.frame(1280., 900., 0, abi::LAYER_ABOVE_UI);
    let renderer = unsafe { Renderer::new(&native) }.map_err(|e| e.to_string())?;
    let mut fixture = Box::new(Fixture {
        root: root.to_string_lossy().into_owned(),
        params,
        files,
        requests: BTreeMap::new(),
        keys: BTreeSet::new(),
        input: VecDeque::new(),
        capture: 0,
        polls: 0,
        directory_count: 0,
        last_flag_count: 0,
        flags: BTreeMap::new(),
        display: abi::DisplayState {
            valid: 1,
            ..Default::default()
        },
        state: abi::BossState {
            size: size_of::<abi::BossState>() as u32,
            valid: 1,
            context: 0xb055,
            raw_map_id: 0x0a000000,
            deaths: 17,
            in_game_time_ms: 3600017,
            graveyard_reached: 1,
        },
        writes: Vec::new(),
        options: abi::RenderOptions::default(),
        meshes: Vec::new(),
        uploads: Vec::new(),
        textures: BTreeMap::new(),
        renderer,
        next: 100,
        upload_count: 0,
        request_count: 0,
    });
    // SAFETY: zero is valid for nullable callbacks/pointers and scalar POD fields.
    let mut host: abi::HostApi = unsafe { std::mem::zeroed() };
    host.magic = abi::ABI_MAGIC;
    host.size = size_of::<abi::HostApi>() as u32;
    host.version = abi::ABI_VERSION;
    host.capabilities = abi::CAP_GAME
        | abi::CAP_FILES
        | abi::CAP_TEXTURES
        | abi::CAP_MESH
        | abi::CAP_INPUT
        | abi::CAP_UI_INPUT
        | abi::CAP_BOSS_STATE
        | abi::CAP_CONCURRENT;
    host.user = (&mut *fixture as *mut Fixture).cast();
    host.log = Some(log);
    host.config_get = Some(config);
    host.key_pressed = Some(key);
    host.read_display_state = Some(display);
    host.read_boss_state = Some(state);
    host.read_event_flags = Some(flags);
    host.read_event_flag = Some(event_flag);
    host.revive_boss = Some(revive);
    host.find_param_table = Some(param);
    host.request_file = Some(request);
    host.poll_file = Some(file);
    host.release_file = Some(file_release);
    host.texture_create_rgba = Some(rgba);
    host.texture_poll = Some(texture);
    host.texture_release = Some(texture_release);
    host.submit_mesh = Some(submit);
    host.set_render_options = Some(options);
    host.ui_input_poll = Some(input_poll);
    host.ui_input_clear = Some(input_clear);
    host.ui_input_capture = Some(capture);
    host.read_map_state = Some(unavailable_map);
    host.read_memory = Some(unavailable_memory);
    host.texture_create = Some(unavailable_dds);
    short_minimap(&dll.with_file_name("Minimap.dll"), &host)?;
    use std::os::windows::ffi::OsStrExt;
    let wide: Vec<u16> = dll.as_os_str().encode_wide().chain(Some(0)).collect();
    let module = unsafe { LoadLibraryW(PCWSTR(wide.as_ptr())) }.map_err(|e| e.to_string())?;
    let get: abi::GetPlugin = unsafe {
        std::mem::transmute(
            GetProcAddress(module, s!("getEROverlayPluginV1"))
                .ok_or("missing production Boss export")?,
        )
    };
    if !unsafe { get(99) }.is_null() {
        return Err("Boss accepted ABI 99".into());
    }
    let api = unsafe { *get(abi::ABI_VERSION) };
    let mut instance = std::ptr::null_mut();
    host.size = abi::HOST_V1_BASE_SIZE as u32;
    let guarded = GuardedHost::new(&host)?;
    if unsafe { api.init.unwrap()(guarded.table, &mut instance) } != abi::ERR_ABI
        || !instance.is_null()
    {
        return Err("Boss accepted old short host".into());
    }
    host.size = size_of::<abi::HostApi>() as u32;
    host.capabilities &= !abi::CAP_UI_INPUT;
    if unsafe { api.init.unwrap()(&host, &mut instance) } != abi::ERR_ABI {
        return Err("Boss accepted missing input capability".into());
    }
    host.capabilities |= abi::CAP_UI_INPUT;
    let code = unsafe { api.init.unwrap()(&host, &mut instance) };
    if code != abi::OK || instance.is_null() {
        return Err(format!("Boss init failed {code}"));
    }
    let plugin = Plugin {
        module,
        api,
        instance,
    };
    let mut probe = Probe {
        plugin,
        fixture,
        gpu,
        sequence: 0,
        cpu: Duration::ZERO,
    };
    let deadline = Instant::now() + Duration::from_secs(35);
    while probe.fixture.directory_count == 0 {
        probe.update()?;
        if Instant::now() >= deadline {
            return Err("real archive catalogue did not finish in 35 seconds".into());
        }
        std::thread::sleep(Duration::from_millis(2));
    }
    std::thread::sleep(Duration::from_millis(510));
    probe.update()?;
    println!(
        "Production Boss.dll loaded; old short table/capability rejected. Real resource catalogue={} rows, batch flags={}, file requests={}; save/flags/input/writes are controlled fixtures.",
        probe.fixture.directory_count, probe.fixture.last_flag_count, probe.fixture.request_count
    );
    if probe.fixture.directory_count != 211 || probe.fixture.last_flag_count != 211 {
        return Err("unexpectedly small production catalogue".into());
    }
    probe.measure("compact")?;
    probe.hotkey(abi::parse_chord("="))?;
    for _ in 0..5 {
        probe.frame()?;
    }
    if probe.fixture.capture != 3 {
        return Err("full list did not capture input".into());
    }
    probe.measure("full real catalogue")?;
    let mut update_samples = Vec::new();
    let requests_before = probe.fixture.request_count;
    for _ in 0..10 {
        std::thread::sleep(Duration::from_millis(510));
        update_samples.push(probe.update()?.as_nanos());
    }
    update_samples.sort_unstable();
    println!(
        "Full 500ms update with controlled batch flag reader: median={}us max={}us, ids={}, file requests={}",
        update_samples[5] / 1000,
        update_samples[9] / 1000,
        probe.fixture.last_flag_count,
        probe.fixture.request_count - requests_before
    );
    probe.frame()?;
    if probe.screenshot("boss-full")? < 1000 {
        return Err("full list produced no readback pixels".into());
    }
    let polls = probe.fixture.polls;
    let repeated = probe.render_sequence(probe.sequence)?;
    if probe.fixture.polls != polls || repeated != 0 {
        return Err("duplicate native sequence advanced egui/input".into());
    }
    // The captured default 1280x900 layout places this text box here. Keep this
    // deliberate click away from the reload/settings controls above it.
    let (x, y) = (1000., 255.);
    probe.click(x, y)?;
    let before = probe.query("")?;
    let after = probe.query("zzzzzznonexistent")?;
    if before <= after + 100 {
        return Err("could not verify actual search text edit through POD events".into());
    }
    println!(
        "Search input verified at ({x},{y}): all={before} vertices, no-match={after}; real pointer press/release, Ctrl+A, Backspace and Unicode text events."
    );
    probe.screenshot("boss-search")?;
    probe.query("")?;
    probe.click(885., 185.)?;
    for _ in 0..12 {
        probe.frame()?;
    }
    probe.screenshot("boss-settings")?;
    probe.click(925., 215.)?;
    probe.update()?;
    if !root.join("configs/boss-ui.json").is_file() {
        return Err("settings slider did not persist through update".into());
    }
    let saved =
        std::fs::read_to_string(root.join("configs/boss-ui.json")).map_err(|e| e.to_string())?;
    let opacity = saved
        .lines()
        .find(|line| line.contains("\"opacity\""))
        .and_then(|line| line.split_once(':'))
        .and_then(|(_, v)| v.trim().trim_end_matches(',').parse::<f32>().ok())
        .ok_or("saved opacity missing")?;
    if (opacity - 0.95).abs() < 0.05 {
        return Err("settings click did not change opacity".into());
    }
    println!(
        "Settings pointer interaction persisted configs/boss-ui.json on update, outside render."
    );
    probe.click(885., 185.)?;
    for _ in 0..12 {
        probe.frame()?;
    }
    probe.click(850., 377.)?;
    for _ in 0..6 {
        probe.frame()?;
    }
    probe.screenshot("boss-revival-confirm")?;
    if !probe.fixture.writes.is_empty() {
        return Err("revival mutated before confirmation".into());
    }
    probe.click(380., 524.)?;
    probe.update()?;
    if !probe.fixture.writes.is_empty() {
        return Err("Cancel triggered revival".into());
    }
    for _ in 0..4 {
        probe.frame()?;
    }
    probe.click(1000., 255.)?;
    probe.query("Erdtree Burial Watchdog")?;
    for _ in 0..4 {
        probe.frame()?;
    }
    probe.click(850., 377.)?;
    for _ in 0..6 {
        probe.frame()?;
    }
    probe.screenshot("boss-revival-reopened")?;
    probe.click(460., 524.)?;
    if !probe.fixture.writes.is_empty() {
        return Err("revival performed writes during render".into());
    }
    probe.update()?;
    if probe.fixture.writes.len() != 1 {
        return Err("Revive confirmation did not issue exactly one update-side request".into());
    }
    println!(
        "Cancel preserved flags; explicit Revive button queued exactly one update-side controlled request: primary={}, auxiliary writes={}",
        probe.fixture.writes[0].0,
        probe.fixture.writes[0].1.len()
    );
    probe.frame()?;
    probe.screenshot("boss-revived")?;
    probe
        .fixture
        .input
        .push_back(event(abi::UI_INPUT_POINTER, 0, 0, 1100., 600.));
    probe
        .fixture
        .input
        .push_back(event(abi::UI_INPUT_WHEEL, 0, 0, 0., -6.));
    probe.frame()?;
    println!(
        "Wheel delivered through production input adapter; capture={}",
        probe.fixture.capture
    );
    probe.fixture.display.full_screen_map = 1;
    probe.update()?;
    if probe.frame()? != 0 || probe.fixture.capture != 0 {
        return Err("fullscreen map did not hide/release Boss input".into());
    }
    probe.fixture.display.full_screen_map = 0;
    probe.update()?;
    probe.frame()?;
    if probe.fixture.capture != 3 {
        return Err("map close did not restore full list input".into());
    }
    probe.fixture.display.loading_or_logo = 1;
    probe.update()?;
    probe.measure("hidden loading")?;
    if probe.fixture.capture != 0 {
        return Err("hidden loading UI captured input".into());
    }
    if probe.screenshot("boss-hidden")? != 0 {
        return Err("hidden Boss left pixels on a cleared target".into());
    }
    probe.fixture.display.loading_or_logo = 0;
    probe.update()?;
    let code = unsafe { probe.plugin.api.renderer_reset.unwrap()(probe.plugin.instance, 2) };
    if code != abi::OK || probe.fixture.capture != 0 {
        return Err("renderer reset failed/reused capture".into());
    }
    let mut drawn = 0;
    for _ in 0..8 {
        drawn = probe.frame()?;
    }
    if drawn == 0 {
        return Err("renderer reset lost production geometry".into());
    }
    probe.hotkey(abi::parse_chord("F9"))?;
    if probe.frame()? != 0 || probe.fixture.capture != 0 {
        return Err("hide shortcut retained input/geometry".into());
    }
    probe.gpu.drain().map_err(|e| e.to_string())?;
    if !probe.fixture.renderer.wait_idle(5000) {
        return Err("renderer drain timed out".into());
    }
    println!(
        "PASS: actual Boss DLL + real archived parsers + WARP pixels/mesh/font rendering; search/settings persistence/cancel/explicit revival confirmation/wheel/full-map/loading/hide/reset/repeated-sequence contracts. Save/flags/writes are controlled fixtures; no live game/FPS claim. Scratch {}",
        root.display()
    );
    Ok(())
}
