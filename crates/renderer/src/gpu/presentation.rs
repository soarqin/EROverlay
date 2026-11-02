//! Retained plugin images and a bounded, geometry-free presentation ring.
use super::*;

const PRESENT_SLOTS: usize = 16;
const MAX_GROUPS: usize = 64;
const SURFACE_DESCRIPTORS: u32 = 128;
const MAX_SURFACE_BYTES: u64 = 128 * 1024 * 1024;

pub type PresentationHandle = Arc<Mutex<Presentation>>;

struct PresentSlot {
    allocator: ID3D12CommandAllocator,
    list: ID3D12GraphicsCommandList,
    fence: u64,
}
struct Surface {
    resource: ID3D12Resource,
    descriptor: u32,
    bytes: u64,
    size: [u32; 2],
    viewport: [u32; 2],
    area: [f32; 4],
    options: RenderOptions,
    main_fence: u64,
    present_fence: u64,
    ready: bool,
}

/// Owns only overlay images, descriptors and command storage. Every destination
/// RTV is borrowed afresh from the synchronous native callback.
pub struct Presentation {
    device: ID3D12Device,
    queue: ID3D12CommandQueue,
    main_fence: ID3D12Fence,
    fence: ID3D12Fence,
    submitted: u64,
    last_main: u64,
    root: ID3D12RootSignature,
    rgba: ID3D12PipelineState,
    hdr: ID3D12PipelineState,
    srv: ID3D12DescriptorHeap,
    stride: usize,
    slots: Vec<PresentSlot>,
    next: usize,
    free: Vec<u32>,
    groups: HashMap<usize, Surface>,
    retired: Vec<Surface>,
    bytes: Arc<AtomicU64>,
    failed: bool,
}
// SAFETY: the handle serializes command recording and metadata; the DIRECT queue
// orders normal-image copies and replay reads. No foreign mapped pointer is kept.
unsafe impl Send for Presentation {}

impl Presentation {
    #[allow(clippy::too_many_arguments)]
    pub(super) unsafe fn new(
        device: &ID3D12Device,
        queue: &ID3D12CommandQueue,
        main_fence: &ID3D12Fence,
        root: &ID3D12RootSignature,
        rgba: &ID3D12PipelineState,
        hdr: &ID3D12PipelineState,
        bytes: Arc<AtomicU64>,
    ) -> Result<Self> {
        unsafe {
            let fence = device.CreateFence(0, D3D12_FENCE_FLAG_NONE)?;
            let srv = device.CreateDescriptorHeap(&D3D12_DESCRIPTOR_HEAP_DESC {
                Type: D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                NumDescriptors: SURFACE_DESCRIPTORS,
                Flags: D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
                NodeMask: 0,
            })?;
            let mut slots = Vec::with_capacity(PRESENT_SLOTS);
            for _ in 0..PRESENT_SLOTS {
                let allocator = device.CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT)?;
                let list: ID3D12GraphicsCommandList = device.CreateCommandList(
                    0,
                    D3D12_COMMAND_LIST_TYPE_DIRECT,
                    &allocator,
                    None,
                )?;
                list.Close()?;
                slots.push(PresentSlot {
                    allocator,
                    list,
                    fence: 0,
                });
            }
            Ok(Self {
                device: device.clone(),
                queue: queue.clone(),
                main_fence: main_fence.clone(),
                fence,
                submitted: 0,
                last_main: 0,
                root: root.clone(),
                rgba: rgba.clone(),
                hdr: hdr.clone(),
                stride: device
                    .GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
                    as usize,
                srv,
                slots,
                next: 0,
                free: (0..SURFACE_DESCRIPTORS).rev().collect(),
                groups: HashMap::with_capacity(MAX_GROUPS),
                retired: Vec::with_capacity(SURFACE_DESCRIPTORS as usize),
                bytes,
                failed: false,
            })
        }
    }

    fn collect(&mut self) -> Result<()> {
        let main = unsafe { self.main_fence.GetCompletedValue() };
        let present = unsafe { self.fence.GetCompletedValue() };
        if main == u64::MAX || present == u64::MAX {
            return Err(invalid());
        }
        let free = &mut self.free;
        let bytes = &self.bytes;
        self.retired.retain(|surface| {
            if surface.main_fence <= main && surface.present_fence <= present {
                free.push(surface.descriptor);
                bytes.fetch_sub(surface.bytes, Ordering::Relaxed);
                false
            } else {
                true
            }
        });
        Ok(())
    }

    pub fn invalidate(&mut self, key: usize) {
        if let Some(surface) = self.groups.remove(&key) {
            self.retired.push(surface);
        }
        let _ = self.collect();
    }
    pub fn invalidate_all(&mut self) {
        self.retired
            .extend(self.groups.drain().map(|(_, surface)| surface));
        let _ = self.collect();
    }
    /// Image resource bytes, including both-fence retirements. D3D12 does not
    /// expose the driver's command allocator/list or descriptor heap allocation.
    pub fn committed_bytes(&self) -> u64 {
        self.bytes.load(Ordering::Relaxed)
    }

    pub fn matches_device(&self, frame: &NativeFrame) -> bool {
        frame.device == self.device.as_raw() && frame.queue == self.queue.as_raw()
    }

    #[allow(clippy::too_many_arguments)]
    pub(super) unsafe fn record_copy(
        &mut self,
        key: usize,
        frame: &NativeFrame,
        options: RenderOptions,
        area: [f32; 4],
        source: &ID3D12Resource,
        list: &ID3D12GraphicsCommandList,
        main_value: u64,
    ) -> Result<Option<usize>> {
        if self.failed {
            return Ok(None);
        }
        self.collect()?;
        let size = [(area[2] - area[0]) as u32, (area[3] - area[1]) as u32];
        let replace = self.groups.get(&key).is_none_or(|s| s.size != size);
        if replace {
            if self.free.is_empty()
                || (!self.groups.contains_key(&key) && self.groups.len() >= MAX_GROUPS)
            {
                return Ok(None);
            }
            let desc = rgba_desc(size[0], size[1], D3D12_RESOURCE_FLAG_NONE);
            let allocation =
                unsafe { self.device.GetResourceAllocationInfo(0, &[desc]) }.SizeInBytes;
            if allocation > MAX_SURFACE_BYTES.saturating_sub(self.committed_bytes()) {
                return Ok(None);
            }
            let resource = unsafe {
                texture(
                    &self.device,
                    size[0],
                    size[1],
                    D3D12_RESOURCE_FLAG_NONE,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                )?
            };
            let descriptor = self.free.pop().unwrap();
            unsafe {
                create_srv(
                    &self.device,
                    &resource,
                    D3D12_CPU_DESCRIPTOR_HANDLE {
                        ptr: self.srv.GetCPUDescriptorHandleForHeapStart().ptr
                            + descriptor as usize * self.stride,
                    },
                );
            }
            self.bytes.fetch_add(allocation, Ordering::Relaxed);
            if let Some(old) = self.groups.insert(
                key,
                Surface {
                    resource,
                    descriptor,
                    bytes: allocation,
                    size,
                    viewport: [0; 2],
                    area,
                    options,
                    main_fence: main_value,
                    present_fence: 0,
                    ready: false,
                },
            ) {
                self.retired.push(old);
            }
        }
        let surface = self.groups.get_mut(&key).unwrap();
        // The same DIRECT queue orders prior replay reads, this write, and future
        // reads. Immutable descriptors are not rewritten while in flight.
        unsafe {
            barrier(
                list,
                source,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_COPY_SOURCE,
            );
            barrier(
                list,
                &surface.resource,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_COPY_DEST,
            );
            list.CopyResource(&surface.resource, source);
            barrier(
                list,
                &surface.resource,
                D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            );
            barrier(
                list,
                source,
                D3D12_RESOURCE_STATE_COPY_SOURCE,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            );
        }
        surface.viewport = [frame.width.to_bits(), frame.height.to_bits()];
        surface.area = area;
        surface.options = options;
        surface.main_fence = main_value;
        surface.ready = false;
        self.last_main = self.last_main.max(main_value);
        Ok(Some(key))
    }
    pub(super) fn confirm_copy(&mut self, key: usize) {
        if let Some(surface) = self.groups.get_mut(&key) {
            surface.ready = true;
        }
    }

    /// Draw the most recent complete images in caller-supplied group order.
    /// No UI, texture upload, allocator growth policy or GPU wait runs here.
    /// # Safety
    /// `frame` borrows a live RTV and DIRECT queue/device for this call only.
    pub unsafe fn replay(&mut self, frame: &NativeFrame, groups: &[usize]) -> Result<RenderStats> {
        let result = unsafe { self.replay_inner(frame, groups) };
        if result.is_err() {
            self.failed = true;
        }
        result
    }
    unsafe fn replay_inner(
        &mut self,
        frame: &NativeFrame,
        groups: &[usize],
    ) -> Result<RenderStats> {
        if self.failed
            || frame.layer != 0
            || !self.matches_device(frame)
            || frame.target == 0
            || !frame.width.is_finite()
            || !frame.height.is_finite()
            || frame.width <= 0.0
            || frame.height <= 0.0
            || frame.width > 16384.0
            || frame.height > 16384.0
            || (frame.format != 24 && frame.format != 28)
        {
            return Err(invalid());
        }
        self.collect()?;
        let viewport = [frame.width.to_bits(), frame.height.to_bits()];
        let mut keys = [0usize; MAX_GROUPS];
        let mut count = 0;
        let mut missing = false;
        for &key in groups {
            if self
                .groups
                .get(&key)
                .is_some_and(|s| s.viewport != viewport)
            {
                self.invalidate(key);
            }
            if count < MAX_GROUPS && self.groups.get(&key).is_some_and(|s| s.ready) {
                keys[count] = key;
                count += 1;
            } else {
                missing = true;
            }
        }
        if count == 0 {
            return Ok(RenderStats {
                dropped: !groups.is_empty(),
                resident_bytes: self.committed_bytes(),
                ..Default::default()
            });
        }
        let completed = unsafe { self.fence.GetCompletedValue() };
        let Some(index) = (0..PRESENT_SLOTS)
            .map(|n| (self.next + n) % PRESENT_SLOTS)
            .find(|&i| self.slots[i].fence <= completed)
        else {
            return Ok(RenderStats {
                dropped: true,
                resident_bytes: self.committed_bytes(),
                ..Default::default()
            });
        };
        self.next = (index + 1) % PRESENT_SLOTS;
        let slot = &mut self.slots[index];
        unsafe {
            slot.allocator.Reset()?;
            slot.list.Reset(&slot.allocator, None)?;
            let list = &slot.list;
            list.OMSetRenderTargets(
                1,
                Some(&D3D12_CPU_DESCRIPTOR_HANDLE { ptr: frame.target }),
                false,
                None,
            );
            list.SetDescriptorHeaps(&[Some(self.srv.clone())]);
            list.SetGraphicsRootSignature(&self.root);
            list.SetPipelineState(if frame.format == 24 {
                &self.hdr
            } else {
                &self.rgba
            });
            list.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            for &key in &keys[..count] {
                let surface = &self.groups[&key];
                let [left, top, right, bottom] = surface.area;
                let width = right - left;
                let height = bottom - top;
                let options = surface.options;
                let params = [
                    width,
                    height,
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
                list.SetGraphicsRoot32BitConstants(0, 20, params.as_ptr().cast(), 0);
                list.RSSetViewports(&[D3D12_VIEWPORT {
                    TopLeftX: left,
                    TopLeftY: top,
                    Width: width,
                    Height: height,
                    MaxDepth: 1.0,
                    ..Default::default()
                }]);
                list.RSSetScissorRects(&[RECT {
                    left: left as i32,
                    top: top as i32,
                    right: right as i32,
                    bottom: bottom as i32,
                }]);
                list.SetGraphicsRootDescriptorTable(
                    1,
                    D3D12_GPU_DESCRIPTOR_HANDLE {
                        ptr: self.srv.GetGPUDescriptorHandleForHeapStart().ptr
                            + (surface.descriptor as usize * self.stride) as u64,
                    },
                );
                list.DrawInstanced(3, 1, 0, 0);
            }
            list.Close()?;
            self.queue.ExecuteCommandLists(&[Some(list.cast()?)]);
            self.submitted += 1;
            slot.fence = self.submitted;
            for &key in &keys[..count] {
                self.groups.get_mut(&key).unwrap().present_fence = self.submitted;
            }
            self.queue.Signal(&self.fence, self.submitted)?;
        }
        Ok(RenderStats {
            draw_calls: count as u32,
            retained: true,
            dropped: missing,
            resident_bytes: self.committed_bytes(),
            ..Default::default()
        })
    }

    pub fn wait_idle(&self, timeout_ms: u32) -> bool {
        let started = std::time::Instant::now();
        for (fence, value) in [
            (&self.main_fence, self.last_main),
            (&self.fence, self.submitted),
        ] {
            let completed = unsafe { fence.GetCompletedValue() };
            if completed == u64::MAX {
                return false;
            }
            if completed >= value {
                continue;
            }
            let remaining = timeout_ms
                .saturating_sub(started.elapsed().as_millis().min(u128::from(u32::MAX)) as u32);
            if remaining == 0 {
                return false;
            }
            unsafe {
                let Ok(event) = CreateEventW(None, false, false, None) else {
                    return false;
                };
                #[cfg(test)]
                WAIT_EVENT_CREATES.with(|count| count.set(count.get() + 1));
                let finished = fence.SetEventOnCompletion(value, event).is_ok()
                    && WaitForSingleObject(event, remaining) == WAIT_OBJECT_0;
                let _ = CloseHandle(event);
                if !finished {
                    return false;
                }
            }
        }
        true
    }
}
impl Drop for Presentation {
    fn drop(&mut self) {
        if !self.wait_idle(5000) {
            std::mem::forget(std::mem::take(&mut self.slots));
            std::mem::forget(std::mem::take(&mut self.groups));
            std::mem::forget(std::mem::take(&mut self.retired));
            std::mem::forget(self.srv.clone());
            std::mem::forget(self.root.clone());
            std::mem::forget(self.rgba.clone());
            std::mem::forget(self.hdr.clone());
        }
    }
}
