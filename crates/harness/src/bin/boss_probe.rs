//! Optional production-DLL probe. PARAM/EMEVD/FMG/MSB are real archived resources;
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
struct FileToken {
    name: Option<String>,
    ready_epoch: u64,
    busy_once: bool,
}
struct Fixture {
    root: String,
    params: BTreeMap<u32, Vec<u8>>,
    files: BTreeMap<String, Vec<u8>>,
    scenes: Option<PathBuf>,
    scene_reads: BTreeSet<String>,
    invalid_scene_aliases: BTreeSet<String>,
    invalid_resource_requests: BTreeSet<String>,
    first_invalid_resource_request: Option<String>,
    requests: BTreeMap<u64, FileToken>,
    update_epoch: u64,
    pending_polls: u64,
    busy_polls: u64,
    max_pending_requests: usize,
    keys: BTreeSet<u32>,
    input: VecDeque<abi::UiInput>,
    capture: u32,
    polls: u64,
    directory_count: usize,
    revival_denials: u64,
    last_flag_ids: Vec<u32>,
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
    state_mode: u8,
    flag_reads: u64,
    param_queries: u64,
    params_ready_epoch: u64,
    files_ready_epoch: u64,
}
unsafe fn fixture<'a>(user: *mut c_void) -> &'a mut Fixture {
    unsafe { &mut *user.cast::<Fixture>() }
}
unsafe fn text<'a>(value: abi::ByteSlice) -> &'a str {
    std::str::from_utf8(unsafe { std::slice::from_raw_parts(value.data, value.len) }).unwrap_or("")
}
unsafe extern "C" fn log(user: *mut c_void, _level: u32, value: abi::ByteSlice) {
    let value = unsafe { text(value) };
    if value.starts_with("boss-revival: context or flags unavailable") {
        unsafe { fixture(user) }.revival_denials += 1;
    }
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
    let f = unsafe { fixture(user) };
    let mut value = f.state;
    if f.state_mode != 0 {
        value.valid = 0;
    }
    unsafe { out.write(value) };
    if f.state_mode == 1 {
        abi::ERR_UNAVAILABLE
    } else {
        abi::OK
    }
}
unsafe extern "C" fn flags(user: *mut c_void, ids: *const u32, out: *mut u8, len: usize) -> i32 {
    if len > 4096 || (len != 0 && (ids.is_null() || out.is_null() || !ids.is_aligned())) {
        return abi::ERR_INVALID;
    }
    let f = unsafe { fixture(user) };
    f.flag_reads += 1;
    if len == 0 {
        f.last_flag_ids.clear();
        return abi::OK;
    }
    f.last_flag_ids = unsafe { std::slice::from_raw_parts(ids, len) }.to_vec();
    for (id, out) in unsafe { std::slice::from_raw_parts(ids, len) }
        .iter()
        .zip(unsafe { std::slice::from_raw_parts_mut(out, len) })
    {
        *out = f.flags.get(id).copied().unwrap_or(1);
    }
    abi::OK
}
unsafe extern "C" fn event_flag(user: *mut c_void, id: u32) -> u32 {
    let f = unsafe { fixture(user) };
    f.flag_reads += 1;
    u32::from(f.flags.get(&id).copied().unwrap_or(1) == 1)
}
unsafe extern "C" fn revive(
    user: *mut c_void,
    context: u64,
    primary: u32,
    writes: *const abi::FlagWrite,
    len: usize,
) -> i32 {
    let f = unsafe { fixture(user) };
    if primary == 0
        || primary == u32::MAX
        || len > 2048
        || (len != 0 && (writes.is_null() || !writes.is_aligned()))
    {
        return abi::ERR_INVALID;
    }
    if context != f.state.context
        || f.state.valid == 0
        || f.display.valid == 0
        || f.display.loading_or_logo != 0
        || f.display.full_screen_map != 0
        || f.flags.get(&primary).copied().unwrap_or(1) != 1
    {
        return abi::ERR_UNAVAILABLE;
    }
    let writes = if len == 0 {
        Vec::new()
    } else {
        unsafe { std::slice::from_raw_parts(writes, len) }.to_vec()
    };
    // Controlled flag-address availability stands in for native prepare_writes.
    // Validate the complete request before changing a single fixture bit.
    let mut targets = BTreeMap::new();
    for write in &writes {
        if write.id == 0 || write.id == u32::MAX || write.id == primary || write.value > 1 {
            return abi::ERR_INVALID;
        }
        if targets
            .insert(write.id, write.value)
            .is_some_and(|old| old != write.value)
        {
            return abi::ERR_INVALID;
        }
        if f.flags.get(&write.id).copied().unwrap_or(1) > 1 {
            return abi::ERR_UNAVAILABLE;
        }
    }
    for write in &writes {
        f.flags.insert(write.id, write.value as u8);
    }
    // Match the real core's confirmation witness: it must be true, and is
    // cleared last. A false canonical flag must never waive this check.
    f.flags.insert(primary, 0);
    f.writes.push((primary, writes));
    abi::OK
}
unsafe extern "C" fn param(user: *mut c_void, id: u32) -> usize {
    let f = unsafe { fixture(user) };
    f.param_queries += 1;
    if f.update_epoch < f.params_ready_epoch {
        return 0;
    }
    f.params.get(&id).map_or(0, |b| b.as_ptr() as usize + 16)
}
unsafe extern "C" fn request(user: *mut c_void, request: *const abi::FileRequest) -> u64 {
    let f = unsafe { fixture(user) };
    let request = unsafe { &*request };
    let physical_path = unsafe { text(request.path) };
    f.request_count += 1;
    let Some(path) = physical_path
        .strip_suffix(".dcx")
        .filter(|_| request.flags == 0x40)
    else {
        let invalid = format!("path={physical_path}, flags={:#x}", request.flags);
        f.first_invalid_resource_request
            .get_or_insert_with(|| invalid.clone());
        f.invalid_resource_requests.insert(invalid);
        return 0;
    };
    if f.update_epoch < f.files_ready_epoch {
        return 0;
    }
    let scene = path
        .strip_prefix("mapstudio:/")
        .or_else(|| path.strip_prefix("mapstudio_dlc2:/"));
    // Independent archived-EXE expectation: areas 31 and 60 never select the
    // DLC2 mount. Do not let a permissive fixture conceal a production error.
    let invalid_scene_alias = path.starts_with("mapstudio_dlc2:/")
        && matches!(scene, Some("m31_00_00_00.msb" | "m60_38_54_00.msb"));
    if invalid_scene_alias {
        f.invalid_scene_aliases.insert(physical_path.to_owned());
    }
    if let Some(name) = scene
        && !invalid_scene_alias
        && !name.contains(['/', '\\'])
        && name.starts_with('m')
        && name.ends_with(".msb")
        && !f.files.contains_key(path)
        && let Some(root) = &f.scenes
    {
        // Strip the checked physical .dcx suffix only at the provider boundary:
        // these archives contain bytes after native VFS decompression.
        // For area 61, it models the same archived scene under both native
        // aliases without claiming to reproduce the live switch/Mod priority.
        if let Ok(bytes) = std::fs::read(root.join(name)) {
            f.scene_reads.insert(name.to_owned());
            f.files.insert(path.to_owned(), bytes);
        }
    }
    let name = if scene.is_some() {
        path
    } else if let Some(name) = path.strip_prefix("event:/") {
        name
    } else if path.contains("item") {
        "item_dlc02.bnd"
    } else if path.contains("menu") {
        "menu_dlc02.bnd"
    } else {
        path
    };
    let name = (!invalid_scene_alias && f.files.contains_key(name)).then(|| name.to_owned());
    f.next += 1;
    f.requests.insert(
        f.next,
        FileToken {
            name,
            ready_epoch: f.update_epoch + 1,
            busy_once: f.next.is_multiple_of(17),
        },
    );
    f.max_pending_requests = f.max_pending_requests.max(f.requests.len());
    f.next
}
unsafe extern "C" fn file(user: *mut c_void, id: u64, out: *mut abi::FileView) -> i32 {
    let f = unsafe { fixture(user) };
    let Some(request) = f.requests.get_mut(&id) else {
        return abi::ERR_INVALID;
    };
    if f.update_epoch < request.ready_epoch {
        f.pending_polls += 1;
        unsafe {
            out.write(abi::FileView {
                status: abi::FILE_PENDING,
                ..Default::default()
            })
        };
        return abi::OK;
    }
    if request.busy_once {
        request.busy_once = false;
        f.busy_polls += 1;
        return abi::ERR_BUSY;
    }
    let view = if let Some(name) = &request.name {
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
        // Match the core order: native file pump before this tick's plugin
        // update. A request submitted this tick cannot already be FILE_READY.
        self.fixture.update_epoch += 1;
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
    fn refresh_flags(&mut self, changes: &[(u32, u8)]) -> Result<Vec<u32>, String> {
        self.fixture.flags.clear();
        self.fixture.flags.extend(changes.iter().copied());
        std::thread::sleep(Duration::from_millis(510));
        self.update()?;
        for _ in 0..12 {
            self.frame()?;
        }
        Ok(self.geometry())
    }
    fn geometry(&self) -> Vec<u32> {
        let mut out = Vec::new();
        for mesh in &self.fixture.meshes {
            out.extend(mesh.clip.map(f32::to_bits));
            out.push(mesh.vertices.len() as u32);
            for vertex in &mesh.vertices {
                out.extend(vertex.pos.map(f32::to_bits));
                out.extend(vertex.uv.map(f32::to_bits));
                out.push(vertex.color);
            }
            out.extend_from_slice(&mesh.indices);
        }
        out
    }
    fn completion_counts(&mut self) -> Result<(), String> {
        // Compare the actual compact label geometry against 207/206/205 killed
        // reference states. Warm all digits first so atlas growth cannot be
        // mistaken for a count change; no internal Boss ABI is added for tests.
        self.refresh_flags(&[(2050470800, 0), (2050480860, 0)])?;
        self.refresh_flags(&[(2050470800, 0)])?;
        let all = self.refresh_flags(&[])?;
        let one = self.refresh_flags(&[(2050470800, 0)])?;
        let two = self.refresh_flags(&[(2050470800, 0), (2050480860, 0)])?;
        if all.is_empty() || all == one || one == two || all == two {
            return Err("compact 207/206/205 reference geometries were not distinct".into());
        }
        for (name, changes, expected) in [
            (
                "Lansseax early kill OR late",
                vec![(1041520800, 0), (1037510800, 1)],
                &all,
            ),
            (
                "Lansseax retreat is not a kill",
                vec![(1041520800, 0), (1037510800, 0), (1037510810, 1)],
                &one,
            ),
            (
                "Adula early is not late completion",
                vec![(1034420800, 0), (1034500800, 1)],
                &one,
            ),
            (
                "Patches late is not initial completion",
                vec![(31000800, 0), (31000850, 1)],
                &one,
            ),
            (
                "Hinterland first remains independent",
                vec![(2050470800, 0), (2050480860, 1)],
                &one,
            ),
            (
                "Hinterland second remains independent",
                vec![(2050470800, 1), (2050480860, 0)],
                &one,
            ),
            (
                "Hinterland both unfinished",
                vec![(2050470800, 0), (2050480860, 0)],
                &two,
            ),
        ] {
            if self.refresh_flags(&changes)? != *expected {
                return Err(format!("real DLL compact count mismatch: {name}"));
            }
            println!("PASS: real DLL compact geometry: {name}");
        }
        self.refresh_flags(&[])?;
        Ok(())
    }
    fn wait_catalog_resources(&mut self) -> Result<(), String> {
        let start = Instant::now();
        let deadline = start + Duration::from_secs(35);
        let epoch = self.fixture.update_epoch;
        let requests = self.fixture.request_count;
        let pending = self.fixture.pending_polls;
        let busy = self.fixture.busy_polls;
        self.fixture.max_pending_requests = self.fixture.requests.len();
        let interval = Duration::from_nanos(1_000_000_000 / 60);
        let mut max_update = Duration::ZERO;
        while self.fixture.directory_count == 0 {
            let submissions = self.fixture.request_count;
            let elapsed = self.update()?;
            max_update = max_update.max(elapsed);
            if self.fixture.request_count - submissions > 32 {
                return Err("one update exceeded the 32-request submission budget".into());
            }
            if self.fixture.state_mode != 0
                && self.fixture.update_epoch > self.fixture.files_ready_epoch + 40
                && self.fixture.request_count == requests
            {
                return Err(
                    "static catalogue still requires a valid save after PARAM/VFS are available"
                        .into(),
                );
            }
            if !self.fixture.invalid_resource_requests.is_empty() {
                return Err(format!(
                    "production raw resource request violates physical .dcx / 0x40 contract: first={:?}, invalid requests={}, update epoch={}",
                    self.fixture.first_invalid_resource_request,
                    self.fixture.invalid_resource_requests.len(),
                    self.fixture.update_epoch
                ));
            }
            if !self.fixture.invalid_scene_aliases.is_empty() {
                return Err(format!(
                    "production requested a DLC2 alias for a base-only scene: {:?}",
                    self.fixture.invalid_scene_aliases
                ));
            }
            if Instant::now() >= deadline {
                return Err("real archive catalogue did not finish in 35 seconds".into());
            }
            std::thread::sleep(interval.saturating_sub(elapsed));
        }
        println!(
            "Catalogue initialization at 60Hz: elapsed={}ms updates={} requests={} peak active tokens={} pending polls={} busy polls={} max single update={}us; physical .dcx names verified, provider returns native-decompressed archive bytes no earlier than the next update",
            start.elapsed().as_millis(),
            self.fixture.update_epoch - epoch,
            self.fixture.request_count - requests,
            self.fixture.max_pending_requests,
            self.fixture.pending_polls - pending,
            self.fixture.busy_polls - busy,
            max_update.as_micros()
        );
        if self.fixture.pending_polls == pending || self.fixture.busy_polls == busy {
            return Err(
                "catalogue did not exercise next-tick pending and transient busy polls".into(),
            );
        }
        if self.fixture.max_pending_requests > 16 {
            return Err("Boss exceeded its 16 active-token budget".into());
        }
        Ok(())
    }
    fn wait_catalog(&mut self) -> Result<(), String> {
        self.wait_catalog_resources()?;
        std::thread::sleep(Duration::from_millis(510));
        self.update()?;
        self.verify_catalog_flags()
    }
    fn preload_before_save(&mut self) -> Result<(), String> {
        self.fixture.state_mode = 1;
        self.fixture.display.loading_or_logo = 1;
        self.fixture.params_ready_epoch = self.fixture.update_epoch + 2;
        self.fixture.files_ready_epoch = self.fixture.update_epoch + 50;
        let flags_before = self.fixture.flag_reads;
        let writes_before = self.fixture.writes.len();
        self.wait_catalog_resources()?;
        if self.fixture.directory_count != 207
            || self.fixture.flag_reads != flags_before
            || self.fixture.writes.len() != writes_before
        {
            return Err(
                "menu preparation did not produce 207 rows without save reads/writes".into(),
            );
        }
        // A Host may return OK with valid=0 instead of ERR_UNAVAILABLE.
        self.fixture.state_mode = 2;
        std::thread::sleep(Duration::from_millis(510));
        self.update()?;
        if self.fixture.flag_reads != flags_before || self.fixture.writes.len() != writes_before {
            return Err("OK/valid=0 save state permitted menu flag reads/writes".into());
        }
        let resource_requests = self.fixture.request_count;
        let queries = self.fixture.param_queries;
        self.fixture.state_mode = 0;
        self.fixture.display.loading_or_logo = 0;
        std::thread::sleep(Duration::from_millis(510));
        let elapsed = self.update()?;
        self.verify_catalog_flags()?;
        if self.fixture.request_count != resource_requests {
            return Err("first valid save restarted static resource preparation".into());
        }
        println!(
            "PASS: menu catalogue Ready with save always unavailable, late PARAM/VFS, zero flag reads/writes; OK/valid=0 also rejected. First valid save update={}us, extra file requests=0, extra PARAM pointer queries={}; this is a controlled startup, not a live-game completion-time claim.",
            elapsed.as_micros(),
            self.fixture.param_queries - queries
        );
        Ok(())
    }
    fn verify_catalog_flags(&self) -> Result<(), String> {
        let ids = &self.fixture.last_flag_ids;
        if !self.fixture.invalid_scene_aliases.is_empty() {
            return Err(format!(
                "production requested a DLC2 alias for a base-only scene: {:?}",
                self.fixture.invalid_scene_aliases
            ));
        }
        if self.fixture.directory_count != 207
            || ids.len() != 208
            || ids.iter().copied().collect::<BTreeSet<_>>().len() != 208
            || ![
                1034420800, 1041520800, 1037510800, 31000800, 2050470800, 2050480860,
            ]
            .iter()
            .all(|id| ids.contains(id))
            || [1034500800, 31000850, 1037510810, 1038540800]
                .iter()
                .any(|id| ids.contains(id))
            || self.fixture.scene_reads.is_empty()
        {
            return Err(format!(
                "expected real-scene 207 catalogue / 208 unique completion flags; got rows={}, ids={}, real scenes={}. Supply EROVERLAY_BOSS_SCENE_DIR with decompressed archived MSBs",
                self.fixture.directory_count,
                ids.len(),
                self.fixture.scene_reads.len()
            ));
        }
        Ok(())
    }
    fn fresh_full(&mut self, host: &abi::HostApi) -> Result<(), String> {
        // Isolate negative confirmations from prior result banners/layout. This
        // is a fresh production plugin instance, not a private testing entry.
        unsafe { self.plugin.api.destroy.unwrap()(self.plugin.instance) };
        self.plugin.instance = std::ptr::null_mut();
        self.fixture.directory_count = 0;
        let code = unsafe { self.plugin.api.init.unwrap()(host, &mut self.plugin.instance) };
        if code != abi::OK || self.plugin.instance.is_null() {
            return Err(format!("fresh Boss init failed: {code}"));
        }
        self.wait_catalog()?;
        self.hotkey(abi::parse_chord("="))?;
        for _ in 0..12 {
            self.frame()?;
        }
        self.click(1000., 255.)?;
        let no_match = self.query("zzzzzznonexistent")?;
        let matched = self.query("Ancient Dragon Lansseax")?;
        if matched <= no_match + 100 {
            return Err("real canonical Lansseax search did not produce a row".into());
        }
        Ok(())
    }
    fn open_confirmation(&mut self) -> Result<(), String> {
        let before = self.frame()?;
        self.click(850., 377.)?;
        let mut after = 0;
        for _ in 0..6 {
            after = self.frame()?;
        }
        if after <= before + 500 {
            return Err("actual canonical row did not open its confirmation modal".into());
        }
        Ok(())
    }
    fn reject_changed_confirmations(&mut self, host: &abi::HostApi) -> Result<(), String> {
        for changed_context in [false, true] {
            self.fixture.flags.clear();
            self.fixture.flags.insert(1041520800, 0);
            self.fixture.flags.insert(1037510800, 1);
            self.fresh_full(host)?;
            self.open_confirmation()?;
            let modal = self.frame()?;
            self.screenshot(if changed_context {
                "boss-context-confirm"
            } else {
                "boss-unknown-confirm"
            })?;
            let writes = self.fixture.writes.len();
            let denials = self.fixture.revival_denials;
            self.click(460., 524.)?;
            let mut after = 0;
            for _ in 0..6 {
                after = self.frame()?;
            }
            if modal <= after + 500 {
                return Err("negative case did not dismiss the actual confirmation".into());
            }
            let before = self.fixture.flags.clone();
            let context = self.fixture.state.context;
            if changed_context {
                self.fixture.state.context ^= 0x100;
            } else {
                self.fixture.flags.insert(1041520800, 2);
                self.fixture.flags.insert(1037510800, 2);
            }
            let unchanged = self.fixture.flags.clone();
            self.update()?;
            self.frame()?;
            if self.fixture.writes.len() != writes
                || self.fixture.flags != unchanged
                || self.fixture.revival_denials != denials + 1
            {
                return Err("changed context / unknown completion accepted a revival".into());
            }
            self.screenshot(if changed_context {
                "boss-context-rejected"
            } else {
                "boss-unknown-rejected"
            })?;
            self.fixture.state.context = context;
            self.fixture.flags = before;
            println!(
                "PASS: actual DLL confirmation rejected {} without controlled writes",
                if changed_context {
                    "changed save context"
                } else {
                    "unknown completion flags"
                }
            );
        }
        Ok(())
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
fn check_controlled_revive(fixture: &mut Fixture) -> Result<(), String> {
    let valid = abi::FlagWrite {
        id: 1041520820,
        value: 0,
    };
    for (name, context, primary, extras, values) in [
        (
            "false canonical despite true early witness",
            fixture.state.context,
            1041520800,
            vec![valid],
            vec![(1041520800, 0), (1037510800, 1)],
        ),
        (
            "unknown primary",
            fixture.state.context,
            1041520800,
            vec![valid],
            vec![(1041520800, 2)],
        ),
        (
            "unknown auxiliary target",
            fixture.state.context,
            1041520800,
            vec![valid],
            vec![(valid.id, 2)],
        ),
        (
            "changed context",
            fixture.state.context ^ 1,
            1041520800,
            vec![valid],
            vec![],
        ),
        (
            "invalid target",
            fixture.state.context,
            1041520800,
            vec![abi::FlagWrite { id: 0, value: 0 }],
            vec![],
        ),
        (
            "invalid value",
            fixture.state.context,
            1041520800,
            vec![abi::FlagWrite {
                id: valid.id,
                value: 2,
            }],
            vec![],
        ),
        (
            "conflicting duplicate",
            fixture.state.context,
            1041520800,
            vec![
                valid,
                abi::FlagWrite {
                    id: valid.id,
                    value: 1,
                },
            ],
            vec![],
        ),
        (
            "primary repeated in extras",
            fixture.state.context,
            1041520800,
            vec![abi::FlagWrite {
                id: 1041520800,
                value: 0,
            }],
            vec![],
        ),
    ] {
        fixture.flags.clear();
        fixture.flags.extend(values);
        let before = fixture.flags.clone();
        let writes = fixture.writes.len();
        let result = unsafe {
            revive(
                (fixture as *mut Fixture).cast(),
                context,
                primary,
                extras.as_ptr(),
                extras.len(),
            )
        };
        if result == abi::OK || fixture.flags != before || fixture.writes.len() != writes {
            return Err(format!(
                "controlled native-contract fixture accepted {name}"
            ));
        }
    }
    fixture.flags.clear();
    println!(
        "PASS: controlled revival host rejects false/unknown primary, unknown targets, stale context, invalid IDs/values and conflicts before any write; this fixture is not the native game writer."
    );
    Ok(())
}
fn main() {
    if let Err(e) = run() {
        eprintln!("Boss 离线验证失败：{e}");
        std::process::exit(1);
    }
}
fn run() -> Result<(), String> {
    let mut args = std::env::args_os().skip(1);
    let dll=PathBuf::from(args.next().ok_or("usage: boss_probe <Boss.dll> <raw-PARAM-directory> <raw-EMEVD-directory> [scratch-directory] [raw-MSB-directory]; alternatively EROVERLAY_BOSS_SCENE_DIR")?).canonicalize().map_err(|e|e.to_string())?;
    let archive = PathBuf::from(args.next().ok_or("missing PARAM directory")?);
    let events = PathBuf::from(args.next().ok_or("missing EMEVD directory")?);
    let root = PathBuf::from(
        args.next()
            .unwrap_or_else(|| format!("target/boss-probe-{}", std::process::id()).into()),
    );
    let scenes = args
        .next()
        .or_else(|| std::env::var_os("EROVERLAY_BOSS_SCENE_DIR"))
        .map(PathBuf::from)
        .map(|p| {
            p.canonicalize()
                .map_err(|e| format!("MSB directory {}: {e}", p.display()))
        })
        .transpose()?;
    std::fs::create_dir_all(root.join("configs")).map_err(|e| e.to_string())?;
    let root = root.canonicalize().map_err(|e| e.to_string())?;
    for name in ["boss-revival.json", "boss-encounters.json"] {
        std::fs::copy(
            Path::new("configs").join(name),
            root.join("configs").join(name),
        )
        .map_err(|e| e.to_string())?;
    }
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
        scenes,
        scene_reads: BTreeSet::new(),
        invalid_scene_aliases: BTreeSet::new(),
        invalid_resource_requests: BTreeSet::new(),
        first_invalid_resource_request: None,
        requests: BTreeMap::new(),
        update_epoch: 0,
        pending_polls: 0,
        busy_polls: 0,
        max_pending_requests: 0,
        keys: BTreeSet::new(),
        input: VecDeque::new(),
        capture: 0,
        polls: 0,
        directory_count: 0,
        revival_denials: 0,
        last_flag_ids: Vec::new(),
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
        state_mode: 0,
        flag_reads: 0,
        param_queries: 0,
        params_ready_epoch: 0,
        files_ready_epoch: 0,
    });
    check_controlled_revive(&mut fixture)?;
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
    probe.preload_before_save()?;
    println!(
        "Production Boss.dll loaded; old short table/capability rejected. Real resource catalogue={} rows, unique completion flags={}, file requests={}, real scenes={:?}; save/flags/input/writes are controlled fixtures. Base-only scenes reject DLC2 and no invalid alias was requested; area 61 may serve both aliases from archived bytes. The live switch/mount priority is not simulated.",
        probe.fixture.directory_count,
        probe.fixture.last_flag_ids.len(),
        probe.fixture.request_count,
        probe.fixture.scene_reads
    );
    probe.measure("compact")?;
    probe.completion_counts()?;
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
        probe.fixture.last_flag_ids.len(),
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
    probe.query("Ancient Dragon Lansseax")?;
    probe.refresh_flags(&[(1041520800, 0), (1037510800, 1)])?;
    for _ in 0..4 {
        probe.frame()?;
    }
    probe.open_confirmation()?;
    probe.screenshot("boss-revival-reopened")?;
    probe.click(460., 524.)?;
    if !probe.fixture.writes.is_empty() {
        return Err("revival performed writes during render".into());
    }
    probe.update()?;
    if probe.fixture.writes.len() != 1 {
        return Err("Revive confirmation did not issue exactly one update-side request".into());
    }
    let (primary, extras) = &probe.fixture.writes[0];
    let actual: Vec<_> = extras.iter().map(|w| (w.id, w.value)).collect();
    // Exactly the original canonical recipe + canonical(false), with its
    // already-reviewed early-kill clear moved to the core's true witness slot.
    let expected = vec![
        (1037510810, 0),
        (1041520820, 0),
        (1041522810, 0),
        (1041520800, 0),
    ];
    if *primary != 1037510800
        || actual != expected
        || [1037510800, 1037510810, 1041520820, 1041522810, 1041520800]
            .iter()
            .any(|id| probe.fixture.flags.get(id) != Some(&0))
    {
        return Err(format!(
            "early-only Lansseax revival changed the reviewed target/value set or failed to clear it: primary={primary}, extras={actual:?}"
        ));
    }
    println!(
        "PASS: Cancel preserved flags; actual Lansseax search + early-only completion allowed one explicit update-side revival with a true early witness and the exact original five cleared targets: primary={}, auxiliary writes={}",
        probe.fixture.writes[0].0,
        probe.fixture.writes[0].1.len()
    );
    probe.frame()?;
    probe.screenshot("boss-revived")?;
    probe.reject_changed_confirmations(&host)?;
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
        "PASS: actual Boss DLL + real archived PARAM/EMEVD/FMG/MSB parsers + WARP pixels/mesh/font rendering; 207 rows/208 unique completion flags, OR/count boundaries, strict controlled revival, search/settings persistence/cancel/explicit early-witness confirmation/unknown/context rejection/wheel/full-map/loading/hide/reset/repeated-sequence contracts. Save/flags/writes are controlled fixtures; no live game/FPS or mount-priority claim. Scratch {}",
        root.display()
    );
    Ok(())
}
