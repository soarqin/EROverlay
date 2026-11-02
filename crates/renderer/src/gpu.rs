use crate::NativeFrame;
use eroverlay_abi::{
    LAYER_BELOW_UI, MAX_INDICES, MAX_MESHES, MAX_VERTICES, Mesh, RenderOptions, Vertex,
};
use std::{
    collections::{HashMap, VecDeque},
    ffi::c_void,
    mem::{ManuallyDrop, size_of},
    ptr,
};
use windows::{
    Win32::{
        Foundation::{CloseHandle, RECT, WAIT_OBJECT_0},
        Graphics::{Direct3D::Fxc::*, Direct3D::*, Direct3D12::*, Dxgi::Common::*},
        System::Threading::{CreateEventW, WaitForSingleObject},
    },
    core::{Error, HRESULT, Interface, PCSTR, Result, s},
};

const SLOT_COUNT: usize = 4;
const DESCRIPTORS: u32 = 2048;
const MAX_TEXTURE_BYTES: usize = 64 * 1024 * 1024;
const MAX_SOURCE_BYTES: usize = 32 * 1024 * 1024;
const MAX_TEXTURES: usize = 769; // 768 user handles plus built-in white texture 0.
const MAX_GPU_TEXTURE_BYTES: u64 = 512 * 1024 * 1024;
const UPLOAD_BUDGET: usize = 4 * 1024 * 1024;
const UPLOAD_IMAGES: usize = 4;
const INFLIGHT_BYTES: u64 = 16 * 1024 * 1024;
const INFLIGHT_IMAGES: usize = 8;
const INITIAL_VERTEX_BYTES: usize = 8192 * size_of::<Vertex>();
const INITIAL_INDEX_BYTES: usize = 16384 * size_of::<u32>();
#[cfg(test)]
thread_local! {
    static WAIT_EVENT_CREATES: std::cell::Cell<u64> = const { std::cell::Cell::new(0) };
}
const SHADER: &str = r#"
cbuffer Params : register(b0) { float2 screen; float2 origin; float4 clip_rect; float4 style; float4 border_color; float4 border_style; };
struct In { float2 pos : POSITION; float2 uv : TEXCOORD0; float4 color : COLOR0; };
struct Out { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 color : COLOR0; };
Out vs(In v) { Out o; o.pos=float4((v.pos-origin) / screen * float2(2,-2)+float2(-1,1),0,1); o.uv=v.uv; o.color=v.color; return o; }
SamplerState smp : register(s0); Texture2D tex : register(t0);
float4 ps(Out i):SV_Target { float4 sample=tex.Sample(smp,i.uv); if(style.w>0.5) sample.rgb*=sample.a; return i.color * sample; }
Out fullscreen(uint id:SV_VertexID) { Out o; o.uv=float2((id<<1)&2,id&2); o.pos=float4(o.uv*float2(2,-2)+float2(-1,1),0,1); o.color=1; return o; }
float4 composite(Out i):SV_Target {
  float2 center=(clip_rect.xy+clip_rect.zw)*0.5, halfsize=(clip_rect.zw-clip_rect.xy)*0.5;
  float2 p=abs(i.pos.xy-center);
  float distance;
  if(style.y>0.5) distance=(length((i.pos.xy-center)/max(halfsize,0.001))-1)*min(halfsize.x,halfsize.y);
  else { float radius=min(style.x,min(halfsize.x,halfsize.y)); float2 q=p-halfsize+radius; distance=length(max(q,0))+min(max(q.x,q.y),0)-radius; }
  float coverage=saturate(0.5-distance);
  float border=border_style.x>0 ? saturate(distance+border_style.x+0.5) : 0;
  float4 map=tex.Sample(smp,i.uv)*style.z;
  return (border_color*border + map*(1-border_color.a*border)) * coverage;
}
"#;

fn invalid() -> Error {
    Error::from_hresult(HRESULT(0x80070057u32 as i32))
}

#[derive(Clone, Copy, Debug, Default)]
pub struct RenderStats {
    pub draw_calls: u32,
    pub uploaded_bytes: u64,
    pub resident_bytes: u64,
    pub dropped: bool,
}
#[derive(Debug)]
pub struct RendererDiagnostics {
    pub pending: usize,
    pub queued_source_bytes: usize,
    pub inflight_images: usize,
    pub inflight_bytes: u64,
    pub texture_bytes: u64,
    pub submitted_fence: u64,
    pub completed_fence: u64,
    pub upload_block: &'static str,
}

struct UploadBuffer {
    resource: ID3D12Resource,
    mapped: *mut u8,
    capacity: usize,
    bytes: u64,
}
impl UploadBuffer {
    unsafe fn new(device: &ID3D12Device, bytes: usize) -> Result<Self> {
        let resource = unsafe {
            buffer(
                device,
                bytes,
                D3D12_HEAP_TYPE_UPLOAD,
                D3D12_RESOURCE_STATE_GENERIC_READ,
            )?
        };
        let mut mapped = ptr::null_mut();
        unsafe {
            resource.Map(
                0,
                Some(&D3D12_RANGE { Begin: 0, End: 0 }),
                Some(&mut mapped),
            )?;
        }
        let allocation_bytes =
            unsafe { device.GetResourceAllocationInfo(0, &[resource.GetDesc()]) }.SizeInBytes;
        Ok(Self {
            resource,
            mapped: mapped.cast(),
            capacity: bytes,
            bytes: allocation_bytes,
        })
    }
    unsafe fn ensure(&mut self, device: &ID3D12Device, needed: usize, limit: usize) -> Result<()> {
        if needed <= self.capacity {
            return Ok(());
        }
        let mut capacity = self.capacity;
        while capacity < needed {
            capacity = (capacity * 2).min(limit);
        }
        *self = unsafe { Self::new(device, capacity)? };
        Ok(())
    }
}
impl Drop for UploadBuffer {
    fn drop(&mut self) {
        unsafe { self.resource.Unmap(0, None) }
    }
}

struct Offscreen {
    resource: ID3D12Resource,
    width: u32,
    height: u32,
    bytes: u64,
}
struct Slot {
    allocator: ID3D12CommandAllocator,
    list: ID3D12GraphicsCommandList,
    vertices: UploadBuffer,
    indices: UploadBuffer,
    uploads: Vec<ID3D12Resource>,
    staging_bytes: u64,
    exclusive_upload: bool,
    timing_pending: bool,
    offscreen: Option<Offscreen>,
    fence: u64,
}
/// Worker-prepared compressed/mip data. Parsing never runs in render().
#[derive(Debug)]
pub struct PreparedTexture {
    pub width: u32,
    pub height: u32,
    image: eroverlay_game::assets::DdsImage,
    bytes: Vec<u8>,
    straight_alpha: bool,
}
impl PreparedTexture {
    pub fn from_dds(bytes: Vec<u8>) -> eroverlay_game::assets::Result<Self> {
        use eroverlay_game::assets::{AssetError, parse_dds};
        if bytes.len() > MAX_SOURCE_BYTES {
            return Err(AssetError::Budget);
        }
        let image = parse_dds(&bytes)?;
        let premultiplied = bytes.get(84..88) == Some(b"DX10")
            && u32::from_le_bytes(bytes[144..148].try_into().unwrap()) & 7 == 2;
        Ok(Self {
            width: image.width,
            height: image.height,
            image,
            bytes,
            straight_alpha: !premultiplied,
        })
    }
    pub fn byte_len(&self) -> usize {
        self.bytes.len()
    }
    fn payload_bytes(&self) -> usize {
        self.image
            .levels
            .iter()
            .map(|level| level.rows as usize * level.row_bytes as usize)
            .sum()
    }
    pub fn mip_count(&self) -> usize {
        self.image.levels.len()
    }
    fn desc(&self) -> D3D12_RESOURCE_DESC {
        let mut desc = rgba_desc(self.width, self.height, D3D12_RESOURCE_FLAG_NONE);
        desc.Format = DXGI_FORMAT(self.image.format as i32);
        desc.MipLevels = self.image.levels.len() as u16;
        desc
    }
}
struct Texture {
    prepared: PreparedTexture,
    dirty: bool,
    gpu: Option<(ID3D12Resource, u32, u64, bool)>,
}
struct Retired {
    _resource: ID3D12Resource,
    descriptor: u32,
    fence: u64,
    bytes: u64,
}

/// Owned by one render thread. Texture staging is fed by core's bounded update
/// queue; it never calls into the updater, scans the game, or reads a file.
struct GpuTiming {
    heap: ID3D12QueryHeap,
    readback: ID3D12Resource,
    frequency: u64,
}

pub struct Renderer {
    device: ID3D12Device,
    queue: ID3D12CommandQueue,
    fence: ID3D12Fence,
    fence_value: u64,
    srv: ID3D12DescriptorHeap,
    rtv: ID3D12DescriptorHeap,
    srv_stride: usize,
    rtv_stride: usize,
    root: ID3D12RootSignature,
    mesh_pipeline: ID3D12PipelineState,
    composite_rgba: [ID3D12PipelineState; 2],
    composite_hdr: [ID3D12PipelineState; 2],
    slots: Vec<Slot>,
    next_slot: usize,
    above_slot_reserve: usize,
    textures: HashMap<u64, Texture>,
    free_descriptors: Vec<u32>,
    retired: Vec<Retired>,
    cpu_texture_bytes: usize,
    dirty_count: usize,
    pending_textures: VecDeque<u64>,
    upload_sequence: Option<u64>,
    frame_upload_bytes: usize,
    frame_upload_images: usize,
    upload_block: &'static str,
    failed: bool,
    timing: Option<GpuTiming>,
    resident_bytes: u64,
}
// Core transfers exclusive ownership only while its nonblocking render gate is
// held. Raw mapped pointers refer to this renderer's owned upload resources.
unsafe impl Send for Renderer {}

impl Renderer {
    /// # Safety
    /// Frame must contain live DIRECT queue and device COM pointers from one
    /// device; its RTV is borrowed for the current synchronous native callback.
    pub unsafe fn new(frame: &NativeFrame) -> Result<Self> {
        if frame.device.is_null() || frame.queue.is_null() {
            return Err(invalid());
        }
        // from_raw_borrowed does not AddRef; clone takes our independent refs.
        let device = unsafe { ID3D12Device::from_raw_borrowed(&frame.device) }
            .ok_or_else(invalid)?
            .clone();
        let queue = unsafe { ID3D12CommandQueue::from_raw_borrowed(&frame.queue) }
            .ok_or_else(invalid)?
            .clone();
        if unsafe { queue.GetDesc() }.Type != D3D12_COMMAND_LIST_TYPE_DIRECT {
            return Err(invalid());
        }
        let mut queue_device: Option<ID3D12Device> = None;
        unsafe {
            queue.GetDevice(&mut queue_device)?;
        }
        if queue_device.as_ref() != Some(&device) {
            return Err(invalid());
        }
        let fence = unsafe { device.CreateFence(0, D3D12_FENCE_FLAG_NONE)? };
        let srv = unsafe {
            device.CreateDescriptorHeap(&D3D12_DESCRIPTOR_HEAP_DESC {
                Type: D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                NumDescriptors: DESCRIPTORS,
                Flags: D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
                NodeMask: 0,
            })?
        };
        let rtv = unsafe {
            device.CreateDescriptorHeap(&D3D12_DESCRIPTOR_HEAP_DESC {
                Type: D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                NumDescriptors: SLOT_COUNT as u32,
                ..Default::default()
            })?
        };
        let srv_stride = unsafe {
            device.GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
        } as usize;
        let rtv_stride =
            unsafe { device.GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV) }
                as usize;
        let root = unsafe { root_signature(&device)? };
        let vs = unsafe { compile("vs", "vs_5_0")? };
        let ps = unsafe { compile("ps", "ps_5_0")? };
        let fs = unsafe { compile("fullscreen", "vs_5_0")? };
        let composite = unsafe { compile("composite", "ps_5_0")? };
        let mesh_pipeline = unsafe {
            pipeline(
                &device,
                &root,
                &vs,
                &ps,
                DXGI_FORMAT_R8G8B8A8_UNORM,
                false,
                false,
            )?
        };
        let composite_rgba = unsafe {
            [
                pipeline(
                    &device,
                    &root,
                    &fs,
                    &composite,
                    DXGI_FORMAT_R8G8B8A8_UNORM,
                    true,
                    false,
                )?,
                pipeline(
                    &device,
                    &root,
                    &fs,
                    &composite,
                    DXGI_FORMAT_R8G8B8A8_UNORM,
                    true,
                    true,
                )?,
            ]
        };
        let composite_hdr = unsafe {
            [
                pipeline(
                    &device,
                    &root,
                    &fs,
                    &composite,
                    DXGI_FORMAT_R10G10B10A2_UNORM,
                    true,
                    false,
                )?,
                pipeline(
                    &device,
                    &root,
                    &fs,
                    &composite,
                    DXGI_FORMAT_R10G10B10A2_UNORM,
                    true,
                    true,
                )?,
            ]
        };
        let mut slots = Vec::with_capacity(SLOT_COUNT);
        for _ in 0..SLOT_COUNT {
            let allocator: ID3D12CommandAllocator =
                unsafe { device.CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT)? };
            let list: ID3D12GraphicsCommandList = unsafe {
                device.CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, &allocator, None)?
            };
            unsafe {
                list.Close()?;
            }
            slots.push(Slot {
                allocator,
                list,
                vertices: unsafe { UploadBuffer::new(&device, INITIAL_VERTEX_BYTES)? },
                indices: unsafe { UploadBuffer::new(&device, INITIAL_INDEX_BYTES)? },
                uploads: Vec::with_capacity(UPLOAD_IMAGES),
                staging_bytes: 0,
                exclusive_upload: false,
                timing_pending: false,
                offscreen: None,
                fence: 0,
            });
        }
        let mut renderer = Self {
            device,
            queue,
            fence,
            fence_value: 0,
            srv,
            rtv,
            srv_stride,
            rtv_stride,
            root,
            mesh_pipeline,
            composite_rgba,
            composite_hdr,
            slots,
            next_slot: 0,
            above_slot_reserve: 0,
            textures: HashMap::with_capacity(1024),
            free_descriptors: ((SLOT_COUNT as u32)..DESCRIPTORS).rev().collect(),
            retired: Vec::with_capacity(DESCRIPTORS as usize),
            cpu_texture_bytes: 0,
            dirty_count: 0,
            pending_textures: VecDeque::with_capacity(1024),
            upload_sequence: None,
            frame_upload_bytes: 0,
            frame_upload_images: 0,
            upload_block: "none",
            failed: false,
            timing: None,
            resident_bytes: 0,
        };
        renderer.upload_rgba(0, 1, 1, &[255, 255, 255, 255]);
        Ok(renderer)
    }

    pub fn matches_device(&self, frame: &NativeFrame) -> bool {
        self.device.as_raw() == frame.device && self.queue.as_raw() == frame.queue
    }

    /// Keep lower-layer work from consuming the last slots needed by loaded
    /// upper-layer plugin groups. This never waits for GPU work or grows the pool.
    pub fn reserve_above_slots(&mut self, groups: usize) {
        // More upper groups remain best effort: even an idle pool must leave
        // one lower opportunity instead of starving it for the entire session.
        self.above_slot_reserve = groups.min(SLOT_COUNT - 1);
    }

    /// Uploads premultiplied sRGBA pixels. Copies only on a dirty texture update.
    /// The core decodes/premultiplies straight DDS/RGBA on its worker thread.
    /// Replacement keeps the old GPU
    /// allocation and descriptor live until its last submitted frame completes.
    pub fn upload_rgba(&mut self, id: u64, width: u32, height: u32, rgba: &[u8]) -> bool {
        if width == 0
            || height == 0
            || width > 16384
            || height > 16384
            || (width as usize)
                .checked_mul(height as usize)
                .and_then(|n| n.checked_mul(4))
                != Some(rgba.len())
            || rgba.len() > MAX_SOURCE_BYTES
        {
            return false;
        }
        self.upload_rgba_owned(id, width, height, rgba.to_vec())
    }
    /// Moves a core-owned decoded image without copying its pixels on render.
    pub fn upload_rgba_owned(&mut self, id: u64, width: u32, height: u32, rgba: Vec<u8>) -> bool {
        let Some(bytes) = (width as usize)
            .checked_mul(height as usize)
            .and_then(|n| n.checked_mul(4))
        else {
            return false;
        };
        if width == 0
            || height == 0
            || width > 16384
            || height > 16384
            || bytes != rgba.len()
            || bytes > MAX_SOURCE_BYTES
        {
            return false;
        }
        let image = eroverlay_game::assets::DdsImage {
            width,
            height,
            format: 28,
            levels: vec![eroverlay_game::assets::DdsLevel {
                offset: 0,
                rows: height,
                row_bytes: width * 4,
            }],
        };
        self.upload_prepared(
            id,
            PreparedTexture {
                width,
                height,
                image,
                bytes: rgba,
                straight_alpha: false,
            },
        )
    }
    /// Moves worker-validated DDS/RGBA pixels without parsing, decoding or copying.
    pub fn upload_prepared(&mut self, id: u64, prepared: PreparedTexture) -> bool {
        let bytes = prepared.bytes.len();
        if prepared.width != prepared.image.width || prepared.height != prepared.image.height {
            return false;
        }
        let old = self.textures.get(&id).map_or(0, |t| t.prepared.bytes.len());
        if self.cpu_texture_bytes - old + bytes > MAX_TEXTURE_BYTES
            || (!self.textures.contains_key(&id) && self.textures.len() >= MAX_TEXTURES)
        {
            return false;
        }
        if self.textures.get(&id).is_none_or(|t| !t.dirty) {
            self.dirty_count += 1;
            self.pending_textures.push_back(id);
        }
        match self.textures.entry(id) {
            std::collections::hash_map::Entry::Occupied(mut entry) => {
                let texture = entry.get_mut();
                texture.prepared = prepared;
                texture.dirty = true;
            }
            std::collections::hash_map::Entry::Vacant(entry) => {
                entry.insert(Texture {
                    prepared,
                    dirty: true,
                    gpu: None,
                });
            }
        }
        self.cpu_texture_bytes = self.cpu_texture_bytes - old + bytes;
        true
    }
    pub fn texture_ready(&self, id: u64) -> bool {
        self.textures
            .get(&id)
            .is_some_and(|t| t.gpu.is_some() && !t.dirty)
    }
    pub fn texture_bytes(&self, id: u64) -> u64 {
        self.textures.get(&id).map_or(0, |t| {
            t.gpu
                .as_ref()
                .map_or(t.prepared.bytes.len() as u64, |g| g.2)
        })
    }
    pub fn retire_texture(&mut self, id: u64) {
        if let Some(texture) = self.textures.remove(&id) {
            self.cpu_texture_bytes -= texture.prepared.bytes.len();
            if texture.dirty {
                self.dirty_count -= 1;
                self.pending_textures.retain(|&queued| queued != id);
            }
            if let Some((resource, descriptor, bytes, _)) = texture.gpu {
                self.retired.push(Retired {
                    _resource: resource,
                    descriptor,
                    fence: self.fence_value,
                    bytes,
                });
            }
        }
    }

    /// # Safety
    /// All mesh pointers must reference aligned live arrays for this call.
    /// Indices must be in range. Core validates and copies foreign plugin data
    /// at submit_mesh, before constructing these borrowed views.
    pub unsafe fn render(
        &mut self,
        frame: &NativeFrame,
        meshes: &[Mesh],
        options: RenderOptions,
    ) -> Result<RenderStats> {
        if self.failed {
            return Err(invalid());
        }
        let result = unsafe { self.render_inner(frame, meshes, options) };
        if result.is_err() {
            self.failed = true;
        }
        result
    }
    unsafe fn render_inner(
        &mut self,
        frame: &NativeFrame,
        meshes: &[Mesh],
        options: RenderOptions,
    ) -> Result<RenderStats> {
        self.upload_block = "none";
        if !self.matches_device(frame)
            || frame.target == 0
            || !frame.width.is_finite()
            || !frame.height.is_finite()
            || frame.width <= 0.0
            || frame.height <= 0.0
            || frame.width > 16384.0
            || frame.height > 16384.0
            || (frame.format != 24 && frame.format != 28)
            || meshes.len() > MAX_MESHES
        {
            return Err(invalid());
        }
        let mut vertices = 0usize;
        let mut indices = 0usize;
        for mesh in meshes {
            vertices = vertices
                .checked_add(mesh.vertex_count as usize)
                .ok_or_else(invalid)?;
            indices = indices
                .checked_add(mesh.index_count as usize)
                .ok_or_else(invalid)?;
            if mesh.vertex_count > 0
                && (mesh.vertices.is_null()
                    || !(mesh.vertices as usize).is_multiple_of(align_of::<Vertex>()))
                || mesh.index_count > 0
                    && (mesh.indices.is_null()
                        || !(mesh.indices as usize).is_multiple_of(align_of::<u32>()))
                || mesh.clip.iter().any(|v| !v.is_finite())
            {
                return Err(invalid());
            }
        }
        if vertices > MAX_VERTICES
            || indices > MAX_INDICES
            || !options.opacity.is_finite()
            || !(0.0..=1.0).contains(&options.opacity)
            || options.clip_rect.iter().any(|v| !v.is_finite())
            || !options.corner_radius.is_finite()
            || options.corner_radius < 0.0
            || !options.border_width.is_finite()
            || options.border_width < 0.0
        {
            return Err(invalid());
        }
        // Both native layers (and plugin groups) share one game-queue sequence.
        // A second render() call for the same sequence must not refill uploads.
        if self.upload_sequence != Some(frame.sequence) {
            self.upload_sequence = Some(frame.sequence);
            self.frame_upload_bytes = 0;
            self.frame_upload_images = 0;
        }
        let dirty = self.dirty_count > 0;
        let left = options.clip_rect[0].clamp(0.0, frame.width).floor();
        let top = options.clip_rect[1].clamp(0.0, frame.height).floor();
        let right = options.clip_rect[2].clamp(0.0, frame.width).ceil();
        let bottom = options.clip_rect[3].clamp(0.0, frame.height).ceil();
        let border_visible = options.border_width > 0.0 && (options.border_color >> 24) > 0;
        let visible = ((indices > 0 && options.opacity > 0.0) || border_visible)
            && right > left
            && bottom > top;
        let maintenance =
            !self.retired.is_empty() || self.slots.iter().any(|slot| !slot.uploads.is_empty());
        let completed = if maintenance {
            Some(unsafe { self.fence.GetCompletedValue() })
        } else {
            None
        };
        if let Some(completed) = completed {
            if completed == u64::MAX {
                return Err(Error::from_hresult(HRESULT(0x887a0005u32 as i32)));
            }
            for slot in &mut self.slots {
                if slot.fence <= completed {
                    slot.uploads.clear();
                    slot.staging_bytes = 0;
                    slot.exclusive_upload = false;
                }
            }
            let free = &mut self.free_descriptors;
            self.retired.retain(|r| {
                if r.fence <= completed {
                    free.push(r.descriptor);
                    self.resident_bytes -= r.bytes;
                    false
                } else {
                    true
                }
            });
        }
        if !visible && !dirty {
            return Ok(RenderStats {
                resident_bytes: self.committed_bytes(),
                ..Default::default()
            });
        }
        let completed = completed.unwrap_or_else(|| unsafe { self.fence.GetCompletedValue() });
        if completed == u64::MAX {
            return Err(Error::from_hresult(HRESULT(0x887a0005u32 as i32)));
        }
        let mut inflight_images: usize = self.slots.iter().map(|slot| slot.uploads.len()).sum();
        let mut inflight_bytes: u64 = self.slots.iter().map(|slot| slot.staging_bytes).sum();
        let inflight_exclusive = self.slots.iter().any(|slot| slot.exclusive_upload);
        if !visible
            && (self.frame_upload_images >= UPLOAD_IMAGES
                || self.frame_upload_bytes >= UPLOAD_BUDGET
                || inflight_images >= INFLIGHT_IMAGES
                || inflight_bytes >= INFLIGHT_BYTES
                || inflight_exclusive)
        {
            self.upload_block = if inflight_exclusive {
                "exclusive_inflight"
            } else if inflight_images >= INFLIGHT_IMAGES || inflight_bytes >= INFLIGHT_BYTES {
                "inflight_budget"
            } else {
                "frame_budget"
            };
            return Ok(RenderStats {
                resident_bytes: self.committed_bytes(),
                ..Default::default()
            });
        }
        // A lower group can otherwise consume the last free slot and leave the
        // upper group invisible in the same native queue. Preserve the normal
        // nonblocking drop policy, but give the upper stage its own opportunity.
        if frame.layer == LAYER_BELOW_UI
            && self.above_slot_reserve > 0
            && self
                .slots
                .iter()
                .filter(|slot| slot.fence <= completed)
                .count()
                <= self.above_slot_reserve
        {
            self.upload_block = "reserved_above_slots";
            return Ok(RenderStats {
                dropped: true,
                resident_bytes: self.committed_bytes(),
                ..Default::default()
            });
        }
        let Some(index) = (0..SLOT_COUNT)
            .map(|n| (self.next_slot + n) % SLOT_COUNT)
            .find(|&i| self.slots[i].fence <= completed)
        else {
            self.upload_block = "busy_slots";
            return Ok(RenderStats {
                dropped: true,
                resident_bytes: self.committed_bytes(),
                ..Default::default()
            });
        };
        self.next_slot = (index + 1) % SLOT_COUNT;
        let slot = &mut self.slots[index];
        slot.uploads.clear();
        slot.staging_bytes = 0;
        slot.exclusive_upload = false;
        slot.timing_pending = false;
        unsafe {
            slot.vertices.ensure(
                &self.device,
                vertices * size_of::<Vertex>(),
                MAX_VERTICES * size_of::<Vertex>(),
            )?;
            slot.indices
                .ensure(&self.device, indices * 4, MAX_INDICES * 4)?;
            slot.allocator.Reset()?;
            slot.list.Reset(&slot.allocator, None)?;
        }
        if let Some(timing) = &self.timing {
            unsafe {
                slot.list
                    .EndQuery(&timing.heap, D3D12_QUERY_TYPE_TIMESTAMP, index as u32 * 2);
            }
        }
        let mut stats = RenderStats::default();
        let cpu_base = unsafe { self.srv.GetCPUDescriptorHandleForHeapStart() }.ptr;
        let gpu_base = unsafe { self.srv.GetGPUDescriptorHandleForHeapStart() }.ptr;
        // Work queue contains dirty IDs only, with an upload count/byte budget.
        // Stable frames never scan the texture table.
        if dirty {
            while let Some(id) = self.pending_textures.pop_front() {
                let Some(texture) = self.textures.get_mut(&id) else {
                    continue;
                };
                if !texture.dirty {
                    continue;
                }
                let bytes = texture.prepared.payload_bytes();
                let source_bytes = texture.prepared.byte_len();
                let staging_bytes =
                    unsafe { upload_allocation_bytes(&self.device, &texture.prepared)? };
                let exclusive = source_bytes > UPLOAD_BUDGET || staging_bytes > INFLIGHT_BYTES;
                if inflight_exclusive
                    || self.frame_upload_images >= UPLOAD_IMAGES
                    || inflight_images >= INFLIGHT_IMAGES
                    || (self.frame_upload_images > 0
                        && self.frame_upload_bytes + source_bytes > UPLOAD_BUDGET)
                    || (exclusive && (inflight_images > 0 || self.frame_upload_images > 0))
                    || (!exclusive && inflight_bytes + staging_bytes > INFLIGHT_BYTES)
                {
                    self.upload_block = if inflight_exclusive {
                        "exclusive_inflight"
                    } else if exclusive && inflight_images > 0 {
                        "large_requires_idle"
                    } else if inflight_images >= INFLIGHT_IMAGES
                        || inflight_bytes + staging_bytes > INFLIGHT_BYTES
                    {
                        "inflight_budget"
                    } else {
                        "frame_budget"
                    };
                    self.pending_textures.push_front(id);
                    break;
                }
                let allocation_bytes = unsafe {
                    self.device
                        .GetResourceAllocationInfo(0, &[texture.prepared.desc()])
                }
                .SizeInBytes;
                if self.resident_bytes + allocation_bytes > MAX_GPU_TEXTURE_BYTES {
                    self.upload_block = "gpu_texture_budget";
                    self.pending_textures.push_front(id);
                    break;
                }
                let Some(descriptor) = self.free_descriptors.pop() else {
                    self.upload_block = "descriptors";
                    self.pending_textures.push_front(id);
                    break;
                };
                let (resource, upload) =
                    match unsafe { upload_texture(&self.device, &slot.list, &texture.prepared) } {
                        Ok(pair) => pair,
                        Err(error) => {
                            self.free_descriptors.push(descriptor);
                            return Err(error);
                        }
                    };
                unsafe {
                    create_srv(
                        &self.device,
                        &resource,
                        D3D12_CPU_DESCRIPTOR_HANDLE {
                            ptr: cpu_base + descriptor as usize * self.srv_stride,
                        },
                    );
                }
                if let Some((old, old_descriptor, bytes, _)) = texture.gpu.replace((
                    resource,
                    descriptor,
                    allocation_bytes,
                    texture.prepared.straight_alpha,
                )) {
                    self.retired.push(Retired {
                        _resource: old,
                        descriptor: old_descriptor,
                        fence: self.fence_value,
                        bytes,
                    });
                }
                self.resident_bytes += allocation_bytes;
                slot.staging_bytes += staging_bytes;
                slot.uploads.push(upload);
                slot.exclusive_upload = exclusive;
                inflight_images += 1;
                inflight_bytes += staging_bytes;
                self.frame_upload_images += 1;
                self.frame_upload_bytes += source_bytes;
                self.cpu_texture_bytes -= texture.prepared.bytes.len();
                texture.prepared.bytes = Vec::new();
                texture.dirty = false;
                self.dirty_count -= 1;
                stats.uploaded_bytes += bytes as u64;
                if exclusive
                    || self.frame_upload_bytes >= UPLOAD_BUDGET
                    || self.frame_upload_images >= UPLOAD_IMAGES
                {
                    break;
                }
            }
        }
        if !visible && stats.uploaded_bytes == 0 {
            // A pending upload may be blocked by a fence-owned staging budget.
            // Closing an unsubmitted list does not consume a slot or GPU work.
            unsafe {
                slot.list.Close()?;
            }
            return Ok(RenderStats {
                resident_bytes: self.committed_bytes(),
                ..Default::default()
            });
        }
        if visible {
            let width = (right - left) as u32;
            let height = (bottom - top) as u32;
            if slot
                .offscreen
                .as_ref()
                .is_none_or(|o| o.width != width || o.height != height)
            {
                let resource = unsafe {
                    texture(
                        &self.device,
                        width,
                        height,
                        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                    )?
                };
                unsafe {
                    self.device.CreateRenderTargetView(
                        &resource,
                        None,
                        D3D12_CPU_DESCRIPTOR_HANDLE {
                            ptr: self.rtv.GetCPUDescriptorHandleForHeapStart().ptr
                                + index * self.rtv_stride,
                        },
                    );
                    create_srv(
                        &self.device,
                        &resource,
                        D3D12_CPU_DESCRIPTOR_HANDLE {
                            ptr: cpu_base + index * self.srv_stride,
                        },
                    );
                }
                let bytes = unsafe {
                    self.device
                        .GetResourceAllocationInfo(0, &[resource.GetDesc()])
                }
                .SizeInBytes;
                slot.offscreen = Some(Offscreen {
                    resource,
                    width,
                    height,
                    bytes,
                });
            }
            let offscreen = slot.offscreen.as_ref().unwrap();
            let offscreen_rtv = D3D12_CPU_DESCRIPTOR_HANDLE {
                ptr: unsafe { self.rtv.GetCPUDescriptorHandleForHeapStart() }.ptr
                    + index * self.rtv_stride,
            };
            let list = &slot.list;
            unsafe {
                barrier(
                    list,
                    &offscreen.resource,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_RENDER_TARGET,
                );
                list.ClearRenderTargetView(offscreen_rtv, &[0.0; 4], None);
                list.OMSetRenderTargets(1, Some(&offscreen_rtv), false, None);
                list.SetDescriptorHeaps(&[Some(self.srv.clone())]);
                list.SetGraphicsRootSignature(&self.root);
                list.SetPipelineState(&self.mesh_pipeline);
                list.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                list.RSSetViewports(&[D3D12_VIEWPORT {
                    Width: width as f32,
                    Height: height as f32,
                    MaxDepth: 1.0,
                    ..Default::default()
                }]);
            }
            let params = [
                width as f32,
                height as f32,
                left,
                top,
                options.clip_rect[0],
                options.clip_rect[1],
                options.clip_rect[2],
                options.clip_rect[3],
                options.corner_radius,
                options.circle as f32,
                options.opacity,
                0.0,
                (options.border_color & 255) as f32 / 255.0,
                ((options.border_color >> 8) & 255) as f32 / 255.0,
                ((options.border_color >> 16) & 255) as f32 / 255.0,
                (options.border_color >> 24) as f32 / 255.0,
                options.border_width,
                0.0,
                0.0,
                0.0,
            ];
            unsafe {
                list.SetGraphicsRoot32BitConstants(0, 20, params.as_ptr().cast(), 0);
            }
            let vertex_view = D3D12_VERTEX_BUFFER_VIEW {
                BufferLocation: unsafe { slot.vertices.resource.GetGPUVirtualAddress() },
                SizeInBytes: (vertices * size_of::<Vertex>()) as u32,
                StrideInBytes: size_of::<Vertex>() as u32,
            };
            let index_view = D3D12_INDEX_BUFFER_VIEW {
                BufferLocation: unsafe { slot.indices.resource.GetGPUVirtualAddress() },
                SizeInBytes: (indices * 4) as u32,
                Format: DXGI_FORMAT_R32_UINT,
            };
            unsafe {
                list.IASetVertexBuffers(0, Some(&[vertex_view]));
                list.IASetIndexBuffer(Some(&index_view));
            }
            let mut vertex_offset = 0;
            let mut index_offset = 0;
            for mesh in meshes {
                if mesh.vertex_count > 0 {
                    unsafe {
                        ptr::copy_nonoverlapping(
                            mesh.vertices.cast::<u8>(),
                            slot.vertices
                                .mapped
                                .add(vertex_offset * size_of::<Vertex>()),
                            mesh.vertex_count as usize * size_of::<Vertex>(),
                        );
                    }
                }
                if mesh.index_count > 0 {
                    unsafe {
                        ptr::copy_nonoverlapping(
                            mesh.indices.cast::<u8>(),
                            slot.indices.mapped.add(index_offset * 4),
                            mesh.index_count as usize * 4,
                        );
                    }
                }
                if let Some((_, descriptor, _, straight_alpha)) = self
                    .textures
                    .get(&mesh.texture)
                    .and_then(|t| t.gpu.as_ref())
                {
                    let clip = RECT {
                        left: (mesh.clip[0] - left).clamp(0.0, width as f32).floor() as i32,
                        top: (mesh.clip[1] - top).clamp(0.0, height as f32).floor() as i32,
                        right: (mesh.clip[2] - left).clamp(0.0, width as f32).ceil() as i32,
                        bottom: (mesh.clip[3] - top).clamp(0.0, height as f32).ceil() as i32,
                    };
                    if clip.right > clip.left && clip.bottom > clip.top && mesh.index_count > 0 {
                        unsafe {
                            list.SetGraphicsRoot32BitConstant(
                                0,
                                if *straight_alpha { 1.0f32.to_bits() } else { 0 },
                                11,
                            );
                            list.RSSetScissorRects(&[clip]);
                            list.SetGraphicsRootDescriptorTable(
                                1,
                                D3D12_GPU_DESCRIPTOR_HANDLE {
                                    ptr: gpu_base + (*descriptor as usize * self.srv_stride) as u64,
                                },
                            );
                            list.DrawIndexedInstanced(
                                mesh.index_count,
                                1,
                                index_offset as u32,
                                vertex_offset as i32,
                                0,
                            );
                        }
                        stats.draw_calls += 1;
                    }
                }
                vertex_offset += mesh.vertex_count as usize;
                index_offset += mesh.index_count as usize;
            }
            unsafe {
                barrier(
                    list,
                    &offscreen.resource,
                    D3D12_RESOURCE_STATE_RENDER_TARGET,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                );
                list.OMSetRenderTargets(
                    1,
                    Some(&D3D12_CPU_DESCRIPTOR_HANDLE { ptr: frame.target }),
                    false,
                    None,
                );
            }
            let layer = usize::from(frame.layer == LAYER_BELOW_UI);
            unsafe {
                list.RSSetViewports(&[D3D12_VIEWPORT {
                    TopLeftX: left,
                    TopLeftY: top,
                    Width: width as f32,
                    Height: height as f32,
                    MaxDepth: 1.0,
                    ..Default::default()
                }]);
            }
            unsafe {
                list.SetPipelineState(if frame.format == 24 {
                    &self.composite_hdr[layer]
                } else {
                    &self.composite_rgba[layer]
                });
                list.RSSetScissorRects(&[RECT {
                    left: left as i32,
                    top: top as i32,
                    right: right as i32,
                    bottom: bottom as i32,
                }]);
                list.SetGraphicsRootDescriptorTable(
                    1,
                    D3D12_GPU_DESCRIPTOR_HANDLE {
                        ptr: gpu_base + (index * self.srv_stride) as u64,
                    },
                );
                list.DrawInstanced(3, 1, 0, 0);
            }
            stats.draw_calls += 1;
        }
        if let Some(timing) = &self.timing {
            unsafe {
                slot.list.EndQuery(
                    &timing.heap,
                    D3D12_QUERY_TYPE_TIMESTAMP,
                    index as u32 * 2 + 1,
                );
                slot.list.ResolveQueryData(
                    &timing.heap,
                    D3D12_QUERY_TYPE_TIMESTAMP,
                    index as u32 * 2,
                    2,
                    &timing.readback,
                    (index * 16) as u64,
                );
                slot.timing_pending = true;
            }
        }
        unsafe {
            slot.list.Close()?;
            let command: ID3D12CommandList = slot.list.cast()?;
            self.queue.ExecuteCommandLists(&[Some(command)]);
        }
        self.fence_value += 1;
        // Mark the slot before signal: if signaling fails the allocator must
        // never be reset/released while untracked submitted work can use it.
        slot.fence = self.fence_value;
        unsafe {
            self.queue.Signal(&self.fence, self.fence_value)?;
        }
        stats.resident_bytes = self.committed_bytes();
        Ok(stats)
    }

    /// Committed resource bytes, including in-flight retirements, mapped geometry
    /// buffers, upload staging and group targets. This is not DXGI process VRAM.
    pub fn committed_bytes(&self) -> u64 {
        self.resident_bytes
            + if self.timing.is_some() { 65536 } else { 0 }
            + self
                .slots
                .iter()
                .map(|s| {
                    s.vertices.bytes
                        + s.indices.bytes
                        + s.staging_bytes
                        + s.offscreen.as_ref().map_or(0, |o| o.bytes)
                })
                .sum::<u64>()
    }
    /// Only called by the diagnostic worker under the owner's render try-lock.
    /// Four slot counters and a fence query; no wait, heap walk or allocation.
    pub fn diagnostics(&self) -> RendererDiagnostics {
        RendererDiagnostics {
            pending: self.dirty_count,
            queued_source_bytes: self.cpu_texture_bytes,
            inflight_images: self.slots.iter().map(|s| s.uploads.len()).sum(),
            inflight_bytes: self.slots.iter().map(|s| s.staging_bytes).sum(),
            texture_bytes: self.resident_bytes,
            submitted_fence: self.fence_value,
            completed_fence: unsafe { self.fence.GetCompletedValue() },
            upload_block: self.upload_block,
        }
    }
    /// Optional diagnostic timestamps. Leave disabled for normal gameplay.
    pub fn enable_gpu_timing(&mut self) -> Result<()> {
        if self.timing.is_some() {
            return Ok(());
        }
        let mut heap = None;
        unsafe {
            self.device.CreateQueryHeap(
                &D3D12_QUERY_HEAP_DESC {
                    Type: D3D12_QUERY_HEAP_TYPE_TIMESTAMP,
                    Count: SLOT_COUNT as u32 * 2,
                    NodeMask: 0,
                },
                &mut heap,
            )?;
        }
        let heap = heap.ok_or_else(invalid)?;
        let readback = unsafe {
            buffer(
                &self.device,
                SLOT_COUNT * 16,
                D3D12_HEAP_TYPE_READBACK,
                D3D12_RESOURCE_STATE_COPY_DEST,
            )?
        };
        let frequency = unsafe { self.queue.GetTimestampFrequency()? };
        self.timing = Some(GpuTiming {
            heap,
            readback,
            frequency,
        });
        Ok(())
    }
    /// Returns completed GPU durations in nanoseconds without waiting. Caller
    /// supplies storage so diagnostics need not allocate on the render thread.
    pub fn take_gpu_samples(&mut self, out: &mut [u64]) -> Result<usize> {
        let Some(timing) = &self.timing else {
            return Ok(0);
        };
        let completed = unsafe { self.fence.GetCompletedValue() };
        let mut mapped = ptr::null_mut();
        unsafe {
            timing.readback.Map(
                0,
                Some(&D3D12_RANGE {
                    Begin: 0,
                    End: SLOT_COUNT * 16,
                }),
                Some(&mut mapped),
            )?;
        }
        let mut count = 0;
        for (index, slot) in self.slots.iter_mut().enumerate() {
            if count == out.len() {
                break;
            }
            if slot.timing_pending && slot.fence <= completed {
                let begin = unsafe { mapped.cast::<u64>().add(index * 2).read() };
                let end = unsafe { mapped.cast::<u64>().add(index * 2 + 1).read() };
                out[count] = ((end.saturating_sub(begin) as u128 * 1_000_000_000)
                    / (timing.frequency as u128)) as u64;
                slot.timing_pending = false;
                count += 1;
            }
        }
        unsafe {
            timing
                .readback
                .Unmap(0, Some(&D3D12_RANGE { Begin: 0, End: 0 }));
        }
        Ok(count)
    }

    /// Only shutdown/harness uses a CPU wait. Normal frames skip a busy slot.
    pub fn wait_idle(&self, timeout_ms: u32) -> bool {
        unsafe {
            if self.fence.GetCompletedValue() >= self.fence_value {
                return true;
            }
            if timeout_ms == 0 {
                return false;
            }
            let Ok(event) = CreateEventW(None, false, false, None) else {
                return false;
            };
            #[cfg(test)]
            WAIT_EVENT_CREATES.with(|count| count.set(count.get() + 1));
            let armed = self
                .fence
                .SetEventOnCompletion(self.fence_value, event)
                .is_ok();
            let completed = armed && WaitForSingleObject(event, timeout_ms) == WAIT_OBJECT_0;
            let _ = CloseHandle(event);
            completed
        }
    }
}
impl Drop for Renderer {
    fn drop(&mut self) {
        if !self.wait_idle(5000) {
            // A wedged GPU must not turn unload into release of live command
            // storage. These bounded allocations live until process exit.
            std::mem::forget(std::mem::take(&mut self.slots));
            std::mem::forget(std::mem::take(&mut self.textures));
            std::mem::forget(std::mem::take(&mut self.retired));
            std::mem::forget(self.timing.take());
            std::mem::forget(self.srv.clone());
            std::mem::forget(self.rtv.clone());
            std::mem::forget(self.root.clone());
            std::mem::forget(self.mesh_pipeline.clone());
            for p in self.composite_rgba.iter().chain(self.composite_hdr.iter()) {
                std::mem::forget(p.clone());
            }
        }
    }
}

unsafe fn buffer(
    device: &ID3D12Device,
    bytes: usize,
    heap: D3D12_HEAP_TYPE,
    state: D3D12_RESOURCE_STATES,
) -> Result<ID3D12Resource> {
    let desc = buffer_desc(bytes as u64);
    let properties = D3D12_HEAP_PROPERTIES {
        Type: heap,
        CreationNodeMask: 1,
        VisibleNodeMask: 1,
        ..Default::default()
    };
    let mut resource = None;
    unsafe {
        device.CreateCommittedResource(
            &properties,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            state,
            None,
            &mut resource,
        )?;
    }
    resource.ok_or_else(invalid)
}
fn buffer_desc(bytes: u64) -> D3D12_RESOURCE_DESC {
    D3D12_RESOURCE_DESC {
        Dimension: D3D12_RESOURCE_DIMENSION_BUFFER,
        Width: bytes,
        Height: 1,
        DepthOrArraySize: 1,
        MipLevels: 1,
        SampleDesc: DXGI_SAMPLE_DESC {
            Count: 1,
            Quality: 0,
        },
        Layout: D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        ..Default::default()
    }
}
unsafe fn upload_allocation_bytes(device: &ID3D12Device, data: &PreparedTexture) -> Result<u64> {
    let mut total = 0;
    unsafe {
        device.GetCopyableFootprints(
            &data.desc(),
            0,
            data.mip_count() as u32,
            0,
            None,
            None,
            None,
            Some(&mut total),
        );
    }
    if total == 0 || total > 64 * 1024 * 1024 {
        return Err(invalid());
    }
    // Include committed-resource alignment, not just source pixels or rows.
    Ok(unsafe { device.GetResourceAllocationInfo(0, &[buffer_desc(total)]) }.SizeInBytes)
}
unsafe fn texture(
    device: &ID3D12Device,
    width: u32,
    height: u32,
    flags: D3D12_RESOURCE_FLAGS,
    state: D3D12_RESOURCE_STATES,
) -> Result<ID3D12Resource> {
    let desc = rgba_desc(width, height, flags);
    let properties = D3D12_HEAP_PROPERTIES {
        Type: D3D12_HEAP_TYPE_DEFAULT,
        CreationNodeMask: 1,
        VisibleNodeMask: 1,
        ..Default::default()
    };
    let mut resource = None;
    unsafe {
        device.CreateCommittedResource(
            &properties,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            state,
            None,
            &mut resource,
        )?;
    }
    resource.ok_or_else(invalid)
}
fn rgba_desc(width: u32, height: u32, flags: D3D12_RESOURCE_FLAGS) -> D3D12_RESOURCE_DESC {
    D3D12_RESOURCE_DESC {
        Dimension: D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        Width: width as u64,
        Height: height,
        DepthOrArraySize: 1,
        MipLevels: 1,
        Format: DXGI_FORMAT_R8G8B8A8_UNORM,
        SampleDesc: DXGI_SAMPLE_DESC {
            Count: 1,
            Quality: 0,
        },
        Flags: flags,
        ..Default::default()
    }
}
unsafe fn create_srv(
    device: &ID3D12Device,
    resource: &ID3D12Resource,
    handle: D3D12_CPU_DESCRIPTOR_HANDLE,
) {
    let resource_desc = unsafe { resource.GetDesc() };
    let desc = D3D12_SHADER_RESOURCE_VIEW_DESC {
        Format: resource_desc.Format,
        ViewDimension: D3D12_SRV_DIMENSION_TEXTURE2D,
        Shader4ComponentMapping: D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
        Anonymous: D3D12_SHADER_RESOURCE_VIEW_DESC_0 {
            Texture2D: D3D12_TEX2D_SRV {
                MipLevels: resource_desc.MipLevels as u32,
                ..Default::default()
            },
        },
    };
    unsafe {
        device.CreateShaderResourceView(resource, Some(&desc), handle);
    }
}
unsafe fn upload_texture(
    device: &ID3D12Device,
    list: &ID3D12GraphicsCommandList,
    data: &PreparedTexture,
) -> Result<(ID3D12Resource, ID3D12Resource)> {
    let desc = data.desc();
    let count = data.image.levels.len();
    if count == 0 || count > 16 {
        return Err(invalid());
    }
    let mut footprints = [D3D12_PLACED_SUBRESOURCE_FOOTPRINT::default(); 16];
    let mut rows = [0u32; 16];
    let mut sizes = [0u64; 16];
    let mut total = 0u64;
    unsafe {
        device.GetCopyableFootprints(
            &desc,
            0,
            count as u32,
            0,
            Some(footprints.as_mut_ptr()),
            Some(rows.as_mut_ptr()),
            Some(sizes.as_mut_ptr()),
            Some(&mut total),
        );
    }
    if total == 0 || total > 64 * 1024 * 1024 {
        return Err(invalid());
    }
    for (index, level) in data.image.levels.iter().enumerate() {
        if rows[index] != level.rows
            || sizes[index] != level.row_bytes as u64
            || data
                .bytes
                .get(level.offset..level.offset + level.rows as usize * level.row_bytes as usize)
                .is_none()
        {
            return Err(invalid());
        }
    }
    let mut resource = None;
    let heap = D3D12_HEAP_PROPERTIES {
        Type: D3D12_HEAP_TYPE_DEFAULT,
        CreationNodeMask: 1,
        VisibleNodeMask: 1,
        ..Default::default()
    };
    unsafe {
        device.CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            None,
            &mut resource,
        )?;
    }
    let resource: ID3D12Resource = resource.ok_or_else(invalid)?;
    let upload = unsafe {
        buffer(
            device,
            total as usize,
            D3D12_HEAP_TYPE_UPLOAD,
            D3D12_RESOURCE_STATE_GENERIC_READ,
        )?
    };
    let mut mapped: *mut c_void = ptr::null_mut();
    unsafe {
        upload.Map(
            0,
            Some(&D3D12_RANGE { Begin: 0, End: 0 }),
            Some(&mut mapped),
        )?;
    }
    for (index, level) in data.image.levels.iter().enumerate() {
        for row in 0..level.rows as usize {
            unsafe {
                ptr::copy_nonoverlapping(
                    data.bytes
                        .as_ptr()
                        .add(level.offset + row * level.row_bytes as usize),
                    mapped.cast::<u8>().add(
                        footprints[index].Offset as usize
                            + row * footprints[index].Footprint.RowPitch as usize,
                    ),
                    level.row_bytes as usize,
                );
            }
        }
    }
    unsafe {
        upload.Unmap(0, None);
    }
    for (index, footprint) in footprints[..count].iter().enumerate() {
        let mut source = D3D12_TEXTURE_COPY_LOCATION {
            pResource: ManuallyDrop::new(Some(upload.clone())),
            Type: D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
            Anonymous: D3D12_TEXTURE_COPY_LOCATION_0 {
                PlacedFootprint: *footprint,
            },
        };
        let mut destination = D3D12_TEXTURE_COPY_LOCATION {
            pResource: ManuallyDrop::new(Some(resource.clone())),
            Type: D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
            Anonymous: D3D12_TEXTURE_COPY_LOCATION_0 {
                SubresourceIndex: index as u32,
            },
        };
        unsafe {
            list.CopyTextureRegion(&destination, 0, 0, 0, &source, None);
            ManuallyDrop::drop(&mut source.pResource);
            ManuallyDrop::drop(&mut destination.pResource);
        }
    }
    unsafe {
        barrier(
            list,
            &resource,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        );
    }
    Ok((resource, upload))
}
unsafe fn barrier(
    list: &ID3D12GraphicsCommandList,
    resource: &ID3D12Resource,
    before: D3D12_RESOURCE_STATES,
    after: D3D12_RESOURCE_STATES,
) {
    let transition = D3D12_RESOURCE_TRANSITION_BARRIER {
        pResource: ManuallyDrop::new(Some(resource.clone())),
        Subresource: D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        StateBefore: before,
        StateAfter: after,
    };
    let mut barrier = D3D12_RESOURCE_BARRIER {
        Type: D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        Anonymous: D3D12_RESOURCE_BARRIER_0 {
            Transition: ManuallyDrop::new(transition),
        },
        ..Default::default()
    };
    unsafe {
        list.ResourceBarrier(std::slice::from_ref(&barrier));
        ManuallyDrop::drop(&mut (*barrier.Anonymous.Transition).pResource);
    }
}
unsafe fn compile(entry: &str, target: &str) -> Result<ID3DBlob> {
    let entry = std::ffi::CString::new(entry).map_err(|_| invalid())?;
    let target = std::ffi::CString::new(target).map_err(|_| invalid())?;
    let mut code = None;
    let mut errors = None;
    unsafe {
        D3DCompile(
            SHADER.as_ptr().cast(),
            SHADER.len(),
            None,
            None,
            None,
            PCSTR(entry.as_ptr().cast()),
            PCSTR(target.as_ptr().cast()),
            D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0,
            &mut code,
            Some(&mut errors),
        )?;
    }
    code.ok_or_else(invalid)
}
unsafe fn root_signature(device: &ID3D12Device) -> Result<ID3D12RootSignature> {
    let range = D3D12_DESCRIPTOR_RANGE {
        RangeType: D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
        NumDescriptors: 1,
        BaseShaderRegister: 0,
        RegisterSpace: 0,
        OffsetInDescriptorsFromTableStart: 0,
    };
    let parameters = [
        D3D12_ROOT_PARAMETER {
            ParameterType: D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
            Anonymous: D3D12_ROOT_PARAMETER_0 {
                Constants: D3D12_ROOT_CONSTANTS {
                    ShaderRegister: 0,
                    RegisterSpace: 0,
                    Num32BitValues: 20,
                },
            },
            ShaderVisibility: D3D12_SHADER_VISIBILITY_ALL,
        },
        D3D12_ROOT_PARAMETER {
            ParameterType: D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
            Anonymous: D3D12_ROOT_PARAMETER_0 {
                DescriptorTable: D3D12_ROOT_DESCRIPTOR_TABLE {
                    NumDescriptorRanges: 1,
                    pDescriptorRanges: &range,
                },
            },
            ShaderVisibility: D3D12_SHADER_VISIBILITY_PIXEL,
        },
    ];
    let sampler = D3D12_STATIC_SAMPLER_DESC {
        Filter: D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        AddressU: D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        AddressV: D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        AddressW: D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        ComparisonFunc: D3D12_COMPARISON_FUNC_ALWAYS,
        MaxLOD: f32::MAX,
        ShaderVisibility: D3D12_SHADER_VISIBILITY_PIXEL,
        ..Default::default()
    };
    let desc = D3D12_ROOT_SIGNATURE_DESC {
        NumParameters: parameters.len() as u32,
        pParameters: parameters.as_ptr(),
        NumStaticSamplers: 1,
        pStaticSamplers: &sampler,
        Flags: D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
    };
    let mut blob = None;
    let mut errors = None;
    unsafe {
        D3D12SerializeRootSignature(
            &desc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &mut blob,
            Some(&mut errors),
        )?;
    }
    let blob = blob.ok_or_else(invalid)?;
    unsafe {
        device.CreateRootSignature(
            0,
            std::slice::from_raw_parts(blob.GetBufferPointer().cast(), blob.GetBufferSize()),
        )
    }
}
unsafe fn pipeline(
    device: &ID3D12Device,
    root: &ID3D12RootSignature,
    vs: &ID3DBlob,
    ps: &ID3DBlob,
    format: DXGI_FORMAT,
    fullscreen: bool,
    below: bool,
) -> Result<ID3D12PipelineState> {
    let elements = [
        D3D12_INPUT_ELEMENT_DESC {
            SemanticName: s!("POSITION"),
            Format: DXGI_FORMAT_R32G32_FLOAT,
            InputSlotClass: D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            ..Default::default()
        },
        D3D12_INPUT_ELEMENT_DESC {
            SemanticName: s!("TEXCOORD"),
            Format: DXGI_FORMAT_R32G32_FLOAT,
            AlignedByteOffset: 8,
            InputSlotClass: D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            ..Default::default()
        },
        D3D12_INPUT_ELEMENT_DESC {
            SemanticName: s!("COLOR"),
            Format: DXGI_FORMAT_R8G8B8A8_UNORM,
            AlignedByteOffset: 16,
            InputSlotClass: D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            ..Default::default()
        },
    ];
    let blend = D3D12_RENDER_TARGET_BLEND_DESC {
        BlendEnable: true.into(),
        SrcBlend: if below {
            D3D12_BLEND_DEST_ALPHA
        } else {
            D3D12_BLEND_ONE
        },
        DestBlend: if below {
            D3D12_BLEND_ONE
        } else {
            D3D12_BLEND_INV_SRC_ALPHA
        },
        BlendOp: D3D12_BLEND_OP_ADD,
        SrcBlendAlpha: if below {
            D3D12_BLEND_ZERO
        } else {
            D3D12_BLEND_ONE
        },
        DestBlendAlpha: D3D12_BLEND_INV_SRC_ALPHA,
        BlendOpAlpha: D3D12_BLEND_OP_ADD,
        LogicOp: D3D12_LOGIC_OP_NOOP,
        RenderTargetWriteMask: D3D12_COLOR_WRITE_ENABLE_ALL.0 as u8,
        ..Default::default()
    };
    let mut desc = D3D12_GRAPHICS_PIPELINE_STATE_DESC {
        pRootSignature: ManuallyDrop::new(Some(root.clone())),
        VS: D3D12_SHADER_BYTECODE {
            pShaderBytecode: unsafe { vs.GetBufferPointer() },
            BytecodeLength: unsafe { vs.GetBufferSize() },
        },
        PS: D3D12_SHADER_BYTECODE {
            pShaderBytecode: unsafe { ps.GetBufferPointer() },
            BytecodeLength: unsafe { ps.GetBufferSize() },
        },
        SampleMask: u32::MAX,
        RasterizerState: D3D12_RASTERIZER_DESC {
            FillMode: D3D12_FILL_MODE_SOLID,
            CullMode: D3D12_CULL_MODE_NONE,
            DepthClipEnable: true.into(),
            ..Default::default()
        },
        DepthStencilState: D3D12_DEPTH_STENCIL_DESC {
            DepthFunc: D3D12_COMPARISON_FUNC_ALWAYS,
            ..Default::default()
        },
        PrimitiveTopologyType: D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
        NumRenderTargets: 1,
        SampleDesc: DXGI_SAMPLE_DESC {
            Count: 1,
            Quality: 0,
        },
        ..Default::default()
    };
    desc.BlendState.RenderTarget[0] = blend;
    desc.RTVFormats[0] = format;
    if !fullscreen {
        desc.InputLayout = D3D12_INPUT_LAYOUT_DESC {
            pInputElementDescs: elements.as_ptr(),
            NumElements: elements.len() as u32,
        };
    }
    let result = unsafe { device.CreateGraphicsPipelineState(&desc) };
    unsafe {
        ManuallyDrop::drop(&mut desc.pRootSignature);
    }
    result
}

#[cfg(test)]
mod tests {
    use super::*;
    use windows::Win32::Graphics::Dxgi::{CreateDXGIFactory2, IDXGIAdapter, IDXGIFactory4};

    unsafe fn context(
        format: DXGI_FORMAT,
    ) -> Result<(
        ID3D12Device,
        ID3D12CommandQueue,
        ID3D12Resource,
        ID3D12DescriptorHeap,
        NativeFrame,
    )> {
        static DEBUG: std::sync::Once = std::sync::Once::new();
        DEBUG.call_once(|| unsafe {
            let mut debug = None;
            if D3D12GetDebugInterface::<ID3D12Debug>(&mut debug).is_ok() {
                debug.unwrap().EnableDebugLayer();
                eprintln!("D3D12 debug layer enabled for WARP pixel/lifecycle checks");
            } else {
                eprintln!("D3D12 debug layer unavailable; WARP pixel/lifecycle checks still run");
            }
        });
        let factory: IDXGIFactory4 = unsafe { CreateDXGIFactory2(Default::default())? };
        let adapter: IDXGIAdapter = unsafe { factory.EnumWarpAdapter()? };
        let mut device: Option<ID3D12Device> = None;
        unsafe {
            D3D12CreateDevice(&adapter, D3D_FEATURE_LEVEL_11_0, &mut device)?;
        }
        let device = device.ok_or_else(invalid)?;
        let queue = unsafe {
            device.CreateCommandQueue::<ID3D12CommandQueue>(&D3D12_COMMAND_QUEUE_DESC {
                Type: D3D12_COMMAND_LIST_TYPE_DIRECT,
                ..Default::default()
            })?
        };
        let rtv: ID3D12DescriptorHeap = unsafe {
            device.CreateDescriptorHeap(&D3D12_DESCRIPTOR_HEAP_DESC {
                Type: D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                NumDescriptors: 1,
                ..Default::default()
            })?
        };
        let properties = D3D12_HEAP_PROPERTIES {
            Type: D3D12_HEAP_TYPE_DEFAULT,
            CreationNodeMask: 1,
            VisibleNodeMask: 1,
            ..Default::default()
        };
        let desc = D3D12_RESOURCE_DESC {
            Dimension: D3D12_RESOURCE_DIMENSION_TEXTURE2D,
            Width: 64,
            Height: 64,
            DepthOrArraySize: 1,
            MipLevels: 1,
            Format: format,
            SampleDesc: DXGI_SAMPLE_DESC {
                Count: 1,
                Quality: 0,
            },
            Flags: D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
            ..Default::default()
        };
        let mut target = None;
        unsafe {
            device.CreateCommittedResource(
                &properties,
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                None,
                &mut target,
            )?;
        }
        let target = target.ok_or_else(invalid)?;
        let handle = unsafe { rtv.GetCPUDescriptorHandleForHeapStart() };
        unsafe {
            device.CreateRenderTargetView(&target, None, handle);
        }
        let frame = NativeFrame {
            device: device.as_raw(),
            queue: queue.as_raw(),
            target: handle.ptr,
            format: format.0 as u32,
            width: 64.0,
            height: 64.0,
            layer: LAYER_BELOW_UI,
            sequence: 1,
        };
        Ok((device, queue, target, rtv, frame))
    }

    unsafe fn no_debug_errors(device: &ID3D12Device) {
        let Ok(info) = device.cast::<ID3D12InfoQueue>() else {
            return;
        };
        for index in 0..unsafe { info.GetNumStoredMessages() } {
            let mut bytes = 0;
            unsafe {
                info.GetMessage(index, None, &mut bytes).unwrap();
            }
            let mut storage = vec![0u64; bytes.div_ceil(8)];
            let message = storage.as_mut_ptr().cast::<D3D12_MESSAGE>();
            unsafe {
                info.GetMessage(index, Some(message), &mut bytes).unwrap();
            }
            let message = unsafe { &*message };
            assert!(
                message.Severity != D3D12_MESSAGE_SEVERITY_CORRUPTION
                    && message.Severity != D3D12_MESSAGE_SEVERITY_ERROR,
                "D3D12 validation: {}",
                unsafe { std::ffi::CStr::from_ptr(message.pDescription.cast()) }.to_string_lossy()
            );
        }
    }

    unsafe fn run_list(
        device: &ID3D12Device,
        queue: &ID3D12CommandQueue,
        record: impl FnOnce(&ID3D12GraphicsCommandList),
    ) -> Result<()> {
        let allocator: ID3D12CommandAllocator =
            unsafe { device.CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT)? };
        let list: ID3D12GraphicsCommandList = unsafe {
            device.CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, &allocator, None)?
        };
        record(&list);
        unsafe {
            list.Close()?;
            queue.ExecuteCommandLists(&[Some(list.cast()?)]);
        }
        let fence: ID3D12Fence = unsafe { device.CreateFence(0, D3D12_FENCE_FLAG_NONE)? };
        let event = unsafe { CreateEventW(None, false, false, None)? };
        unsafe {
            queue.Signal(&fence, 1)?;
            fence.SetEventOnCompletion(1, event)?;
            assert_eq!(WaitForSingleObject(event, 10000), WAIT_OBJECT_0);
            let _ = CloseHandle(event);
        }
        Ok(())
    }

    unsafe fn readback(
        device: &ID3D12Device,
        queue: &ID3D12CommandQueue,
        target: &ID3D12Resource,
        format: DXGI_FORMAT,
    ) -> Result<Vec<u8>> {
        let readback = unsafe {
            buffer(
                device,
                64 * 256,
                D3D12_HEAP_TYPE_READBACK,
                D3D12_RESOURCE_STATE_COPY_DEST,
            )?
        };
        unsafe {
            run_list(device, queue, |list| {
                barrier(
                    list,
                    target,
                    D3D12_RESOURCE_STATE_RENDER_TARGET,
                    D3D12_RESOURCE_STATE_COPY_SOURCE,
                );
                let mut source = D3D12_TEXTURE_COPY_LOCATION {
                    pResource: ManuallyDrop::new(Some(target.clone())),
                    Type: D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
                    Anonymous: D3D12_TEXTURE_COPY_LOCATION_0 {
                        SubresourceIndex: 0,
                    },
                };
                let mut dest = D3D12_TEXTURE_COPY_LOCATION {
                    pResource: ManuallyDrop::new(Some(readback.clone())),
                    Type: D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
                    Anonymous: D3D12_TEXTURE_COPY_LOCATION_0 {
                        PlacedFootprint: D3D12_PLACED_SUBRESOURCE_FOOTPRINT {
                            Offset: 0,
                            Footprint: D3D12_SUBRESOURCE_FOOTPRINT {
                                Format: format,
                                Width: 64,
                                Height: 64,
                                Depth: 1,
                                RowPitch: 256,
                            },
                        },
                    },
                };
                list.CopyTextureRegion(&dest, 0, 0, 0, &source, None);
                barrier(
                    list,
                    target,
                    D3D12_RESOURCE_STATE_COPY_SOURCE,
                    D3D12_RESOURCE_STATE_RENDER_TARGET,
                );
                ManuallyDrop::drop(&mut source.pResource);
                ManuallyDrop::drop(&mut dest.pResource);
            })?;
        }
        let mut mapped = ptr::null_mut();
        unsafe {
            readback.Map(
                0,
                Some(&D3D12_RANGE {
                    Begin: 0,
                    End: 64 * 256,
                }),
                Some(&mut mapped),
            )?;
        }
        let result = unsafe { std::slice::from_raw_parts(mapped.cast::<u8>(), 64 * 256) }.to_vec();
        unsafe {
            readback.Unmap(0, Some(&D3D12_RANGE { Begin: 0, End: 0 }));
        }
        Ok(result)
    }

    #[test]
    fn warp_real_pixels_group_opacity_circle_and_native_transmittance() -> Result<()> {
        unsafe {
            let (device, queue, target, _rtv, frame) = context(DXGI_FORMAT_R8G8B8A8_UNORM)?;
            let mut renderer = Renderer::new(&frame)?;
            run_list(&device, &queue, |list| {
                list.ClearRenderTargetView(
                    D3D12_CPU_DESCRIPTOR_HANDLE { ptr: frame.target },
                    &[0.2, 0.1, 0.0, 0.75],
                    None,
                )
            })?;
            let vertices = [
                Vertex {
                    pos: [8.0, 8.0],
                    uv: [0.0, 0.0],
                    color: 0x80000080,
                },
                Vertex {
                    pos: [56.0, 8.0],
                    uv: [1.0, 0.0],
                    color: 0x80000080,
                },
                Vertex {
                    pos: [56.0, 56.0],
                    uv: [1.0, 1.0],
                    color: 0x80000080,
                },
                Vertex {
                    pos: [8.0, 56.0],
                    uv: [0.0, 1.0],
                    color: 0x80000080,
                },
            ];
            let indices = [0, 1, 2, 0, 2, 3];
            let mesh = Mesh {
                texture: 0,
                vertices: vertices.as_ptr(),
                vertex_count: 4,
                indices: indices.as_ptr(),
                index_count: 6,
                clip: [8.0, 8.0, 56.0, 56.0],
            };
            let options = RenderOptions {
                clip_rect: [8.0, 8.0, 56.0, 56.0],
                circle: 1,
                opacity: 0.5,
                ..Default::default()
            };
            let stats = renderer.render(&frame, &[mesh, mesh], options)?;
            assert_eq!(stats.draw_calls, 3);
            assert!(!stats.dropped);
            assert!(renderer.wait_idle(10000));
            let pixels = readback(&device, &queue, &target, DXGI_FORMAT_R8G8B8A8_UNORM)?;
            let center = &pixels[32 * 256 + 32 * 4..32 * 256 + 32 * 4 + 4];
            let corner = &pixels[9 * 256 + 9 * 4..9 * 256 + 9 * 4 + 4];
            for (actual, expected) in center.iter().zip([123u8, 26, 0, 119]) {
                assert!(actual.abs_diff(expected) <= 2, "center={center:?}");
            }
            for (actual, expected) in corner.iter().zip([51u8, 26, 0, 191]) {
                assert!(actual.abs_diff(expected) <= 1, "corner={corner:?}");
            }
            // Once uploads are complete, hidden frames perform no GPU work.
            assert_eq!(renderer.render(&frame, &[], options)?.draw_calls, 0);
            assert_eq!(renderer.render(&frame, &[], options)?.uploaded_bytes, 0);
            run_list(&device, &queue, |list| {
                list.ClearRenderTargetView(
                    D3D12_CPU_DESCRIPTOR_HANDLE { ptr: frame.target },
                    &[0.2, 0.1, 0.0, 0.75],
                    None,
                )
            })?;
            let border = RenderOptions {
                opacity: 0.0,
                border_width: 3.0,
                border_color: 0xff0000ff,
                ..options
            };
            assert_eq!(renderer.render(&frame, &[], border)?.draw_calls, 1);
            assert!(renderer.wait_idle(10000));
            let pixels = readback(&device, &queue, &target, DXGI_FORMAT_R8G8B8A8_UNORM)?;
            let edge = &pixels[9 * 256 + 32 * 4..9 * 256 + 32 * 4 + 4];
            let center = &pixels[32 * 256 + 32 * 4..32 * 256 + 32 * 4 + 4];
            for (actual, expected) in edge.iter().zip([242u8, 26, 0, 0]) {
                assert!(actual.abs_diff(expected) <= 2, "border={edge:?}");
            }
            for (actual, expected) in center.iter().zip([51u8, 26, 0, 191]) {
                assert!(
                    actual.abs_diff(expected) <= 1,
                    "border-only center={center:?}"
                );
            }
            renderer.upload_rgba(5, 2, 2, &[255; 16]);
            let replacement = renderer.render(&frame, &[], options)?;
            assert_eq!(replacement.uploaded_bytes, 16);
            renderer.retire_texture(5);
            assert!(!renderer.texture_ready(5));
            assert!(renderer.wait_idle(10000));
            no_debug_errors(&device);
        }
        Ok(())
    }

    #[test]
    fn warp_real_rgb10a2_above_ui_and_busy_slots() -> Result<()> {
        unsafe {
            let (device, queue, target, _rtv, mut frame) = context(DXGI_FORMAT_R10G10B10A2_UNORM)?;
            frame.layer = 0;
            let mut renderer = Renderer::new(&frame)?;
            run_list(&device, &queue, |list| {
                list.ClearRenderTargetView(
                    D3D12_CPU_DESCRIPTOR_HANDLE { ptr: frame.target },
                    &[0.2, 0.1, 0.0, 1.0],
                    None,
                )
            })?;
            let vertices = [
                Vertex {
                    pos: [8.0, 8.0],
                    uv: [0.0, 0.0],
                    color: 0x80000080,
                },
                Vertex {
                    pos: [56.0, 8.0],
                    uv: [1.0, 0.0],
                    color: 0x80000080,
                },
                Vertex {
                    pos: [56.0, 56.0],
                    uv: [1.0, 1.0],
                    color: 0x80000080,
                },
                Vertex {
                    pos: [8.0, 56.0],
                    uv: [0.0, 1.0],
                    color: 0x80000080,
                },
            ];
            let indices = [0, 1, 2, 0, 2, 3];
            let mesh = Mesh {
                texture: 0,
                vertices: vertices.as_ptr(),
                vertex_count: 4,
                indices: indices.as_ptr(),
                index_count: 6,
                clip: [8.0, 8.0, 56.0, 56.0],
            };
            let stats = renderer.render(&frame, &[mesh], RenderOptions::default())?;
            assert_eq!(stats.draw_calls, 2);
            assert!(renderer.wait_idle(10000));
            let pixels = readback(&device, &queue, &target, DXGI_FORMAT_R10G10B10A2_UNORM)?;
            let offset = 32 * 256 + 32 * 4;
            let color = u32::from_le_bytes(pixels[offset..offset + 4].try_into().unwrap());
            assert!((color & 1023).abs_diff(615) <= 3, "red={}", color & 1023);
            assert!(((color >> 10) & 1023).abs_diff(51) <= 2);
            assert_eq!(color >> 30, 3);
            // No fence/CPU wait is inserted by render when the ring is full.
            let gate = hold_uploads(&device, &queue)?;
            for _ in 0..SLOT_COUNT {
                assert!(
                    !renderer
                        .render(&frame, &[mesh], RenderOptions::default())?
                        .dropped
                );
            }
            assert!(
                renderer
                    .render(&frame, &[mesh], RenderOptions::default())?
                    .dropped
            );
            let events = WAIT_EVENT_CREATES.with(|count| count.get());
            for _ in 0..1000 {
                assert!(!renderer.wait_idle(0));
            }
            assert_eq!(WAIT_EVENT_CREATES.with(|count| count.get()), events);
            assert!(!renderer.wait_idle(1));
            assert_eq!(WAIT_EVENT_CREATES.with(|count| count.get()), events + 1);
            drop(gate);
            assert!(renderer.wait_idle(10000));
            let events = WAIT_EVENT_CREATES.with(|count| count.get());
            assert!(renderer.wait_idle(0));
            assert_eq!(WAIT_EVENT_CREATES.with(|count| count.get()), events);
            no_debug_errors(&device);
        }
        Ok(())
    }

    #[test]
    fn warp_lower_cannot_take_upper_layers_last_free_slot() -> Result<()> {
        unsafe {
            let (device, queue, target, _rtv, mut frame) = context(DXGI_FORMAT_R10G10B10A2_UNORM)?;
            let mut renderer = Renderer::new(&frame)?;
            renderer.render(&frame, &[], RenderOptions::default())?;
            assert!(renderer.wait_idle(10000));
            let indices = [0, 1, 2, 0, 2, 3];
            for reserved in [0, 1] {
                renderer.reserve_above_slots(reserved);
                run_list(&device, &queue, |list| {
                    list.ClearRenderTargetView(
                        D3D12_CPU_DESCRIPTOR_HANDLE { ptr: frame.target },
                        &[0.0, 0.0, 0.0, 1.0],
                        None,
                    );
                })?;
                let gate = hold_uploads(&device, &queue)?;
                let mut vertices = [
                    Vertex {
                        pos: [8.0, 8.0],
                        uv: [0.0, 0.0],
                        color: 0,
                    },
                    Vertex {
                        pos: [56.0, 8.0],
                        uv: [1.0, 0.0],
                        color: 0,
                    },
                    Vertex {
                        pos: [56.0, 56.0],
                        uv: [1.0, 1.0],
                        color: 0,
                    },
                    Vertex {
                        pos: [8.0, 56.0],
                        uv: [0.0, 1.0],
                        color: 0,
                    },
                ];
                let mesh = Mesh {
                    texture: 0,
                    vertices: vertices.as_ptr(),
                    vertex_count: 4,
                    indices: indices.as_ptr(),
                    index_count: 6,
                    clip: [8.0, 8.0, 56.0, 56.0],
                };
                frame.layer = 0;
                for _ in 0..SLOT_COUNT - 1 {
                    assert!(
                        !renderer
                            .render(&frame, &[mesh], RenderOptions::default())?
                            .dropped
                    );
                }
                for vertex in &mut vertices {
                    vertex.color = 0xff0000ff;
                }
                frame.layer = LAYER_BELOW_UI;
                let below = renderer.render(&frame, &[mesh], RenderOptions::default())?;
                for vertex in &mut vertices {
                    vertex.color = 0xff00ff00;
                }
                frame.layer = 0;
                let above = renderer.render(&frame, &[mesh], RenderOptions::default())?;
                assert_eq!(below.dropped, reserved == 1);
                assert_eq!(above.dropped, reserved == 0);
                drop(gate);
                assert!(renderer.wait_idle(10000));
                let pixels = readback(&device, &queue, &target, DXGI_FORMAT_R10G10B10A2_UNORM)?;
                let offset = 32 * 256 + 32 * 4;
                let color = u32::from_le_bytes(pixels[offset..offset + 4].try_into().unwrap());
                let red = color & 1023;
                let green = (color >> 10) & 1023;
                assert_eq!(
                    (red, green),
                    if reserved == 0 { (1023, 0) } else { (0, 1023) }
                );
                println!(
                    "WARP reserved={reserved}: lower_dropped={}, upper_dropped={}, overlap RGB10=({red},{green},0)",
                    below.dropped, above.dropped
                );
            }
            // Loading more upper plugins than the pool must not permanently
            // suppress a visible lower group, even when all slots are idle.
            renderer.reserve_above_slots(usize::MAX);
            frame.layer = LAYER_BELOW_UI;
            run_list(&device, &queue, |list| {
                list.ClearRenderTargetView(
                    D3D12_CPU_DESCRIPTOR_HANDLE { ptr: frame.target },
                    &[0.0, 0.0, 0.0, 1.0],
                    None,
                );
            })?;
            let lower = renderer.render(
                &frame,
                &[],
                RenderOptions {
                    border_width: 2.0,
                    border_color: 0xffffffff,
                    ..Default::default()
                },
            )?;
            assert!(!lower.dropped);
            assert_eq!(lower.draw_calls, 1);
            assert!(renderer.wait_idle(10000));
            let pixels = readback(&device, &queue, &target, DXGI_FORMAT_R10G10B10A2_UNORM)?;
            let offset = 32 * 256;
            let edge = u32::from_le_bytes(pixels[offset..offset + 4].try_into().unwrap());
            assert_eq!(edge & 0x3fff_ffff, 0x3fff_ffff);
            no_debug_errors(&device);
        }
        Ok(())
    }

    #[cfg(feature = "native-scene-fixture")]
    #[test]
    fn warp_native_queue_fixture_uses_production_frame_adapter() -> Result<()> {
        struct Observation(Vec<(u32, u64)>);
        unsafe extern "C" fn frame(user: *mut c_void, frame: *const NativeFrame) {
            let observed = unsafe { &mut *user.cast::<Observation>() };
            let frame = unsafe { &*frame };
            observed.0.push((frame.layer, frame.sequence));
        }
        unsafe extern "C" fn native(user: *mut c_void) {
            unsafe { &mut *user.cast::<Observation>() }.0.push((2, 0));
        }
        let _guard = crate::NATIVE_SCENE_TEST_LOCK.lock().unwrap();
        unsafe {
            let (device, _queue, target, _rtv, mut native_frame) =
                context(DXGI_FORMAT_R10G10B10A2_UNORM)?;
            native_frame.sequence = 17;
            for scenario in 0..=4 {
                let mut observed = Observation(Vec::new());
                let user = std::ptr::from_mut(&mut observed).cast();
                assert!(crate::drive_scene_fixture(
                    frame,
                    user,
                    &native_frame,
                    target.as_raw(),
                    &native_frame,
                    target.as_raw(),
                    native,
                    user,
                    scenario,
                ));
                assert_eq!(
                    observed.0,
                    match scenario {
                        0 => vec![(LAYER_BELOW_UI, 17), (2, 0), (0, 17)],
                        3 => vec![(2, 0), (LAYER_BELOW_UI, 17), (2, 0), (0, 17)],
                        _ => vec![(2, 0), (0, 17)],
                    }
                );
            }
            no_debug_errors(&device);
        }
        Ok(())
    }

    #[test]
    fn texture_queue_budget_and_retirement_are_bounded() -> Result<()> {
        unsafe {
            let (device, _queue, _target, _rtv, mut frame) = context(DXGI_FORMAT_R8G8B8A8_UNORM)?;
            let mut renderer = Renderer::new(&frame)?;
            renderer.enable_gpu_timing()?;
            for id in 1..=9 {
                assert!(renderer.upload_rgba_owned(id, 1, 1, vec![255; 4]));
            }
            assert_eq!(
                renderer
                    .render(&frame, &[], RenderOptions::default())?
                    .uploaded_bytes,
                4 * 4
            );
            assert!(renderer.wait_idle(10000));
            let mut samples = [0; 4];
            assert_eq!(renderer.take_gpu_samples(&mut samples)?, 1);
            assert!(samples[0] > 0);
            frame.sequence += 1;
            assert_eq!(
                renderer
                    .render(&frame, &[], RenderOptions::default())?
                    .uploaded_bytes,
                4 * 4
            );
            assert!(renderer.wait_idle(10000));
            frame.sequence += 1;
            assert_eq!(
                renderer
                    .render(&frame, &[], RenderOptions::default())?
                    .uploaded_bytes,
                2 * 4
            );
            assert!(renderer.wait_idle(10000));
            for id in 100..1100 {
                assert!(renderer.upload_rgba_owned(id, 1, 1, vec![255; 4]));
                renderer.retire_texture(id);
            }
            assert_eq!(renderer.pending_textures.len(), 0);
            assert_eq!(renderer.dirty_count, 0);
            assert!(!renderer.upload_rgba_owned(100, 0, 1, vec![]));
            assert!(!renderer.upload_rgba_owned(100, 1, 1, vec![255; 3]));
            no_debug_errors(&device);
        }
        Ok(())
    }

    struct OpenGateOnDrop(ID3D12Fence);
    impl Drop for OpenGateOnDrop {
        fn drop(&mut self) {
            unsafe {
                let _ = self.0.Signal(1);
            }
        }
    }
    unsafe fn hold_uploads(
        device: &ID3D12Device,
        queue: &ID3D12CommandQueue,
    ) -> Result<OpenGateOnDrop> {
        let gate = unsafe { device.CreateFence(0, D3D12_FENCE_FLAG_NONE)? };
        unsafe {
            queue.Wait(&gate, 1)?;
        }
        Ok(OpenGateOnDrop(gate))
    }
    unsafe fn warm_uploads(renderer: &mut Renderer, frame: &mut NativeFrame) -> Result<()> {
        unsafe {
            renderer.render(frame, &[], RenderOptions::default())?;
        }
        assert!(renderer.wait_idle(10000));
        frame.sequence += 1;
        unsafe {
            renderer.render(frame, &[], RenderOptions::default())?;
        }
        assert!(renderer.slots.iter().all(|slot| slot.uploads.is_empty()));
        Ok(())
    }
    fn inflight(renderer: &Renderer) -> (usize, u64) {
        (
            renderer.slots.iter().map(|slot| slot.uploads.len()).sum(),
            renderer.slots.iter().map(|slot| slot.staging_bytes).sum(),
        )
    }

    #[test]
    fn warp_uploads_share_sequence_and_inflight_image_budgets() -> Result<()> {
        unsafe {
            let (device, queue, _target, _rtv, mut frame) = context(DXGI_FORMAT_R8G8B8A8_UNORM)?;
            let mut renderer = Renderer::new(&frame)?;
            warm_uploads(&mut renderer, &mut frame)?;
            let gate = hold_uploads(&device, &queue)?;
            for id in 1..=12 {
                assert!(renderer.upload_rgba_owned(id, 1, 1, vec![255; 4]));
            }
            assert_eq!(
                renderer
                    .render(&frame, &[], RenderOptions::default())?
                    .uploaded_bytes,
                16
            );
            let first_fence = renderer.fence_value;
            frame.layer = 1;
            assert_eq!(
                renderer
                    .render(&frame, &[], RenderOptions::default())?
                    .uploaded_bytes,
                0
            );
            assert_eq!(renderer.fence_value, first_fence);
            assert_eq!(inflight(&renderer).0, 4);
            frame.sequence += 1;
            assert_eq!(
                renderer
                    .render(&frame, &[], RenderOptions::default())?
                    .uploaded_bytes,
                16
            );
            assert_eq!(inflight(&renderer).0, INFLIGHT_IMAGES);
            let second_fence = renderer.fence_value;
            frame.sequence += 1;
            assert_eq!(
                renderer
                    .render(&frame, &[], RenderOptions::default())?
                    .uploaded_bytes,
                0
            );
            assert_eq!(renderer.fence_value, second_fence);
            assert!(renderer.fence.GetCompletedValue() < first_fence);
            assert_eq!(
                renderer
                    .slots
                    .iter()
                    .filter(|slot| slot.fence > renderer.fence.GetCompletedValue())
                    .count(),
                2
            );
            drop(gate);
            assert!(renderer.wait_idle(10000));
            frame.sequence += 1;
            assert_eq!(
                renderer
                    .render(&frame, &[], RenderOptions::default())?
                    .uploaded_bytes,
                16
            );
            assert!(renderer.wait_idle(10000));
            frame.sequence += 1;
            renderer.render(&frame, &[], RenderOptions::default())?;
            assert_eq!(inflight(&renderer), (0, 0));

            // A byte budget is shared across layers even below four images.
            let gate = hold_uploads(&device, &queue)?;
            for id in 20..23 {
                assert!(renderer.upload_rgba_owned(id, 1024, 512, vec![255; 2 * 1024 * 1024]));
            }
            assert_eq!(
                renderer
                    .render(&frame, &[], RenderOptions::default())?
                    .uploaded_bytes,
                UPLOAD_BUDGET as u64
            );
            assert_eq!(renderer.frame_upload_images, 2);
            let before = renderer.fence_value;
            frame.layer = 0;
            assert_eq!(
                renderer
                    .render(&frame, &[], RenderOptions::default())?
                    .uploaded_bytes,
                0
            );
            assert_eq!(renderer.fence_value, before);
            frame.sequence += 1;
            assert_eq!(
                renderer
                    .render(&frame, &[], RenderOptions::default())?
                    .uploaded_bytes,
                2 * 1024 * 1024
            );
            drop(gate);
            assert!(renderer.wait_idle(10000));
            no_debug_errors(&device);
        }
        Ok(())
    }

    #[test]
    fn warp_padded_uploads_obey_aggregate_committed_staging_budget() -> Result<()> {
        unsafe {
            let (device, queue, _target, _rtv, mut frame) = context(DXGI_FORMAT_R8G8B8A8_UNORM)?;
            let mut renderer = Renderer::new(&frame)?;
            warm_uploads(&mut renderer, &mut frame)?;
            let gate = hold_uploads(&device, &queue)?;
            // Each source is only 64 KiB, but 256-byte GPU row alignment makes
            // its committed upload buffer 4 MiB. Source-byte caps cannot bound it.
            for id in 1..=4 {
                assert!(renderer.upload_rgba_owned(id, 1, 16384, vec![255; 65536]));
            }
            assert!(renderer.upload_rgba_owned(5, 1, 1, vec![255; 4]));
            assert_eq!(
                renderer
                    .render(&frame, &[], RenderOptions::default())?
                    .uploaded_bytes,
                4 * 65536
            );
            assert_eq!(inflight(&renderer), (4, INFLIGHT_BYTES));
            let before = renderer.fence_value;
            let held_bytes = renderer.committed_bytes();
            frame.sequence += 1;
            assert_eq!(
                renderer
                    .render(&frame, &[], RenderOptions::default())?
                    .uploaded_bytes,
                0
            );
            assert_eq!(renderer.fence_value, before);
            assert_eq!(renderer.committed_bytes(), held_bytes);
            let blocked = renderer.diagnostics();
            assert_eq!(blocked.upload_block, "inflight_budget");
            assert_eq!(
                (
                    blocked.pending,
                    blocked.inflight_images,
                    blocked.inflight_bytes
                ),
                (1, 4, INFLIGHT_BYTES)
            );
            assert!(blocked.completed_fence < blocked.submitted_fence);
            assert_eq!(
                renderer
                    .slots
                    .iter()
                    .filter(|slot| slot.fence > renderer.fence.GetCompletedValue())
                    .count(),
                1
            );
            drop(gate);
            assert!(renderer.wait_idle(10000));
            frame.sequence += 1;
            assert_eq!(
                renderer
                    .render(&frame, &[], RenderOptions::default())?
                    .uploaded_bytes,
                4
            );
            assert_eq!(inflight(&renderer), (1, 65536));
            assert!(renderer.committed_bytes() <= held_bytes - INFLIGHT_BYTES + 131072);
            assert!(renderer.wait_idle(10000));
            frame.sequence += 1;
            renderer.render(&frame, &[], RenderOptions::default())?;
            assert_eq!(inflight(&renderer), (0, 0));
            no_debug_errors(&device);
        }
        Ok(())
    }

    #[test]
    fn warp_oversized_upload_requires_idle_and_remains_alone() -> Result<()> {
        unsafe {
            let (device, queue, _target, _rtv, mut frame) = context(DXGI_FORMAT_R8G8B8A8_UNORM)?;
            let mut renderer = Renderer::new(&frame)?;
            warm_uploads(&mut renderer, &mut frame)?;
            let gate = hold_uploads(&device, &queue)?;
            assert!(renderer.upload_rgba_owned(1, 1, 1, vec![255; 4]));
            assert!(renderer.upload_rgba_owned(2, 3072, 2048, vec![255; 24 * 1024 * 1024]));
            assert!(renderer.upload_rgba_owned(3, 1, 1, vec![255; 4]));
            assert_eq!(
                renderer
                    .render(&frame, &[], RenderOptions::default())?
                    .uploaded_bytes,
                4
            );
            frame.sequence += 1;
            let before = renderer.fence_value;
            assert_eq!(
                renderer
                    .render(&frame, &[], RenderOptions::default())?
                    .uploaded_bytes,
                0
            );
            assert_eq!(renderer.fence_value, before);
            assert!(!renderer.texture_ready(2));
            drop(gate);
            assert!(renderer.wait_idle(10000));

            let gate = hold_uploads(&device, &queue)?;
            frame.sequence += 1;
            assert_eq!(
                renderer
                    .render(&frame, &[], RenderOptions::default())?
                    .uploaded_bytes,
                24 * 1024 * 1024
            );
            assert_eq!(inflight(&renderer), (1, 24 * 1024 * 1024));
            assert!(renderer.slots.iter().any(|slot| slot.exclusive_upload));
            let before = renderer.fence_value;
            frame.sequence += 1;
            assert_eq!(
                renderer
                    .render(&frame, &[], RenderOptions::default())?
                    .uploaded_bytes,
                0
            );
            assert_eq!(renderer.fence_value, before);
            assert_eq!(inflight(&renderer).0, 1);
            drop(gate);
            assert!(renderer.wait_idle(10000));
            frame.sequence += 1;
            assert_eq!(
                renderer
                    .render(&frame, &[], RenderOptions::default())?
                    .uploaded_bytes,
                4
            );
            assert!(renderer.wait_idle(10000));
            frame.sequence += 1;
            renderer.render(&frame, &[], RenderOptions::default())?;
            assert_eq!(inflight(&renderer), (0, 0));
            assert!(matches!(
                PreparedTexture::from_dds(vec![0; MAX_SOURCE_BYTES + 1]),
                Err(eroverlay_game::assets::AssetError::Budget)
            ));
            no_debug_errors(&device);
        }
        Ok(())
    }

    fn dds_fixture(
        size: u32,
        mips: u32,
        fourcc: &[u8; 4],
        blocks: impl Fn(u32) -> Vec<u8>,
    ) -> Vec<u8> {
        let mut bytes = vec![0u8; 128];
        bytes[..4].copy_from_slice(b"DDS ");
        for (offset, value) in [
            (4, 124u32),
            (12, size),
            (16, size),
            (28, mips),
            (76, 32),
            (80, 4),
        ] {
            bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
        }
        bytes[84..88].copy_from_slice(fourcc);
        let mut width = size;
        for mip in 0..mips {
            let block = blocks(mip);
            for _ in 0..width.div_ceil(4).pow(2) {
                bytes.extend_from_slice(&block);
            }
            width = (width / 2).max(1);
        }
        bytes
    }

    #[test]
    fn warp_compressed_mips_memory_and_straight_alpha() -> Result<()> {
        unsafe {
            let (device, queue, target, _rtv, mut frame) = context(DXGI_FORMAT_R8G8B8A8_UNORM)?;
            frame.layer = 0;
            let mut renderer = Renderer::new(&frame)?;
            let bytes = dds_fixture(512, 10, b"DXT1", |mip| {
                if mip == 0 {
                    vec![0, 248, 0, 0, 0, 0, 0, 0]
                } else {
                    vec![224, 7, 0, 0, 0, 0, 0, 0]
                }
            });
            let prepared = PreparedTexture::from_dds(bytes).unwrap();
            assert_eq!(prepared.mip_count(), 10);
            let old_compressed_bytes = device
                .GetResourceAllocationInfo(0, &[prepared.desc()])
                .SizeInBytes;
            let rgba_bytes = device
                .GetResourceAllocationInfo(0, &[rgba_desc(512, 512, D3D12_RESOURCE_FLAG_NONE)])
                .SizeInBytes;
            assert!(renderer.upload_prepared(42, prepared));
            let vertices = [
                Vertex {
                    pos: [8.0, 8.0],
                    uv: [0.0, 0.0],
                    color: u32::MAX,
                },
                Vertex {
                    pos: [56.0, 8.0],
                    uv: [1.0, 0.0],
                    color: u32::MAX,
                },
                Vertex {
                    pos: [56.0, 56.0],
                    uv: [1.0, 1.0],
                    color: u32::MAX,
                },
                Vertex {
                    pos: [8.0, 56.0],
                    uv: [0.0, 1.0],
                    color: u32::MAX,
                },
            ];
            let indices = [0, 1, 2, 0, 2, 3];
            let mut mesh = Mesh {
                texture: 42,
                vertices: vertices.as_ptr(),
                vertex_count: 4,
                indices: indices.as_ptr(),
                index_count: 6,
                clip: [8.0, 8.0, 56.0, 56.0],
            };
            run_list(&device, &queue, |list| {
                list.ClearRenderTargetView(
                    D3D12_CPU_DESCRIPTOR_HANDLE { ptr: frame.target },
                    &[0.0, 0.0, 0.0, 1.0],
                    None,
                )
            })?;
            renderer.render(&frame, &[mesh], RenderOptions::default())?;
            assert!(renderer.wait_idle(10000));
            let gpu = &renderer.textures[&42].gpu.as_ref().unwrap().0;
            assert_eq!(gpu.GetDesc().Format, DXGI_FORMAT_BC1_UNORM);
            assert_eq!(gpu.GetDesc().MipLevels, 10);
            assert_eq!(renderer.texture_bytes(42), old_compressed_bytes);
            assert!(old_compressed_bytes < rgba_bytes);
            assert!(renderer.textures[&42].prepared.bytes.is_empty());
            assert_eq!(renderer.cpu_texture_bytes, 0);
            let pixels = readback(&device, &queue, &target, DXGI_FORMAT_R8G8B8A8_UNORM)?;
            // The top mip is red; green proves a lower uploaded mip was sampled.
            assert_eq!(
                &pixels[32 * 256 + 32 * 4..32 * 256 + 32 * 4 + 4],
                &[0, 255, 0, 255]
            );
            let bytes = dds_fixture(4, 1, b"DXT3", |_| {
                let mut b = vec![0x88; 8];
                b.extend([0, 248, 0, 0, 0, 0, 0, 0]);
                b
            });
            assert!(renderer.upload_prepared(43, PreparedTexture::from_dds(bytes).unwrap()));
            mesh.texture = 43;
            run_list(&device, &queue, |list| {
                list.ClearRenderTargetView(
                    D3D12_CPU_DESCRIPTOR_HANDLE { ptr: frame.target },
                    &[0.0, 0.0, 0.0, 1.0],
                    None,
                )
            })?;
            renderer.render(&frame, &[mesh], RenderOptions::default())?;
            assert!(renderer.wait_idle(10000));
            let pixels = readback(&device, &queue, &target, DXGI_FORMAT_R8G8B8A8_UNORM)?;
            assert_eq!(
                &pixels[32 * 256 + 32 * 4..32 * 256 + 32 * 4 + 4],
                &[136, 0, 0, 255]
            );
            renderer.retire_texture(42);
            renderer.retire_texture(43);
            let hidden = renderer.render(&frame, &[], RenderOptions::default())?;
            assert_eq!(hidden.draw_calls, 0);
            assert_eq!(hidden.uploaded_bytes, 0);
            assert!(renderer.retired.is_empty());
            assert!(renderer.slots.iter().all(|slot| slot.uploads.is_empty()));
            let big_vertices = vec![vertices[0]; 8193];
            let big_indices = vec![0; 16386];
            let big_mesh = Mesh {
                texture: 0,
                vertices: big_vertices.as_ptr(),
                vertex_count: big_vertices.len() as u32,
                indices: big_indices.as_ptr(),
                index_count: big_indices.len() as u32,
                ..mesh
            };
            for _ in 0..SLOT_COUNT {
                assert!(
                    !renderer
                        .render(&frame, &[big_mesh], RenderOptions::default())?
                        .dropped
                );
                assert!(renderer.wait_idle(10000));
            }
            assert!(
                renderer
                    .slots
                    .iter()
                    .all(|s| s.vertices.capacity > INITIAL_VERTEX_BYTES
                        && s.indices.capacity > INITIAL_INDEX_BYTES)
            );
            let resources: Vec<_> = renderer
                .slots
                .iter()
                .map(|s| (s.vertices.resource.as_raw(), s.indices.resource.as_raw()))
                .collect();
            renderer.render(&frame, &[big_mesh], RenderOptions::default())?;
            assert!(renderer.wait_idle(10000));
            assert!(
                renderer
                    .slots
                    .iter()
                    .zip(resources)
                    .all(|(s, p)| s.vertices.resource.as_raw() == p.0
                        && s.indices.resource.as_raw() == p.1)
            );
            no_debug_errors(&device);
        }
        Ok(())
    }
}
