//! Immutable retained images and independently owned presentation command slots.
use super::*;
use arc_swap::{ArcSwap, ArcSwapOption};
use std::sync::{
    MutexGuard,
    atomic::{AtomicBool, AtomicUsize},
};

const PRESENT_SLOTS: usize = 16;
const MAX_GROUPS: usize = 64;
const SURFACE_DESCRIPTORS: u32 = 128;
const MAX_SURFACE_BYTES: u64 = 128 * 1024 * 1024;

pub type PresentationHandle = Arc<Presentation>;

struct PresentSlot {
    allocator: ID3D12CommandAllocator,
    list: ID3D12GraphicsCommandList,
    fence: ID3D12Fence,
    submitted: u64,
    images: Vec<Arc<Surface>>,
}
struct Surface {
    key: usize,
    resource: ID3D12Resource,
    descriptor: u32,
    bytes: u64,
    size: [u32; 2],
    viewport: [u32; 2],
    area: [f32; 4],
    options: RenderOptions,
    main_fence: u64,
    epoch: u64,
}
struct Group {
    epoch: AtomicU64,
    image: ArcSwapOption<Surface>,
}
struct Maintenance {
    free: Vec<u32>,
    groups: HashMap<usize, Arc<Group>>,
    retired: Vec<Arc<Surface>>,
}

/// Owns overlay images, descriptors and command storage, never a destination
/// RTV. Publishing owns no replay slot; replay owns no maintenance lock.
pub struct Presentation {
    device: ID3D12Device,
    queue: ID3D12CommandQueue,
    main_fence: ID3D12Fence,
    last_main: AtomicU64,
    root: ID3D12RootSignature,
    rgba: ID3D12PipelineState,
    hdr: ID3D12PipelineState,
    srv: ID3D12DescriptorHeap,
    stride: usize,
    slots: Vec<Mutex<PresentSlot>>,
    next: AtomicUsize,
    published: ArcSwap<HashMap<usize, Arc<Group>>>,
    maintenance: Mutex<Maintenance>,
    bytes: Arc<AtomicU64>,
    failed: AtomicBool,
    accepting: AtomicBool,
    #[cfg(test)]
    allocations: AtomicU64,
    #[cfg(test)]
    before_submit: Mutex<Option<Arc<dyn Fn() + Send + Sync>>>,
}
// SAFETY: D3D12 device/queue calls are thread safe. Each mutable allocator/list
// has exclusive slot ownership; immutable images retain their COM references.
unsafe impl Send for Presentation {}
unsafe impl Sync for Presentation {}

pub(super) struct CopyPublication<'a> {
    owner: &'a Presentation,
    maintenance: MutexGuard<'a, Maintenance>,
    group: Arc<Group>,
    image: Option<Arc<Surface>>,
}
impl CopyPublication<'_> {
    pub(super) fn confirm(mut self) {
        let image = self.image.take().unwrap();
        if self.owner.accepting.load(Ordering::Acquire)
            && self.group.epoch.load(Ordering::Acquire) == image.epoch
        {
            if let Some(old) = self.group.image.swap(Some(image)) {
                self.maintenance.retired.push(old);
            }
        } else {
            self.maintenance.retired.push(image);
        }
        // Replay checks the image epoch too: a concurrent invalidate between
        // the check and swap cannot make this old image visible again.
        let _ = self.owner.collect(&mut self.maintenance);
    }
}
impl Drop for CopyPublication<'_> {
    fn drop(&mut self) {
        if let Some(image) = self.image.take() {
            // A failed submission must not release a copy destination early.
            self.maintenance.retired.push(image);
        }
    }
}

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
                slots.push(Mutex::new(PresentSlot {
                    allocator,
                    list,
                    fence: device.CreateFence(0, D3D12_FENCE_FLAG_NONE)?,
                    submitted: 0,
                    images: Vec::with_capacity(MAX_GROUPS),
                }));
            }
            Ok(Self {
                device: device.clone(),
                queue: queue.clone(),
                main_fence: main_fence.clone(),
                last_main: AtomicU64::new(0),
                root: root.clone(),
                rgba: rgba.clone(),
                hdr: hdr.clone(),
                stride: device
                    .GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
                    as usize,
                srv,
                slots,
                next: AtomicUsize::new(0),
                published: ArcSwap::from_pointee(HashMap::with_capacity(MAX_GROUPS)),
                maintenance: Mutex::new(Maintenance {
                    free: (0..SURFACE_DESCRIPTORS).rev().collect(),
                    groups: HashMap::with_capacity(MAX_GROUPS),
                    retired: Vec::with_capacity(SURFACE_DESCRIPTORS as usize),
                }),
                bytes,
                failed: AtomicBool::new(false),
                accepting: AtomicBool::new(true),
                #[cfg(test)]
                allocations: AtomicU64::new(0),
                #[cfg(test)]
                before_submit: Mutex::new(None),
            })
        }
    }

    fn collect(&self, state: &mut Maintenance) -> Result<()> {
        let main = unsafe { self.main_fence.GetCompletedValue() };
        if main == u64::MAX {
            return Err(invalid());
        }
        self.collect_slots()?;
        // Hide only bumps an epoch, so it never waits for a publisher. Old
        // publications stay leased until CPU readers and slot fences finish.
        for group in state.groups.values() {
            let image = group.image.load_full();
            if image
                .as_ref()
                .is_some_and(|s| s.epoch != group.epoch.load(Ordering::Acquire))
                && let Some(image) = group.image.swap(None)
            {
                state.retired.push(image);
            }
        }
        let mut cached = [0usize; MAX_GROUPS];
        let mut cached_count = 0;
        state.retired.retain(|image| {
            if image.main_fence <= main && Arc::strong_count(image) == 1 {
                // Keep at most one idle destination per live group and size.
                // This preserves normal two-image reuse, including idle checks.
                let reusable = state.groups.get(&image.key).is_some_and(|g| {
                    g.image.load().as_ref().is_some_and(|current| {
                        current.epoch == g.epoch.load(Ordering::Acquire)
                            && current.size == image.size
                    })
                });
                if reusable
                    && !cached[..cached_count].contains(&image.key)
                    && cached_count < MAX_GROUPS
                {
                    cached[cached_count] = image.key;
                    cached_count += 1;
                    return true;
                }
                state.free.push(image.descriptor);
                self.bytes.fetch_sub(image.bytes, Ordering::Relaxed);
                false
            } else {
                true
            }
        });
        Ok(())
    }

    fn collect_slots(&self) -> Result<()> {
        for slot in &self.slots {
            if let Ok(mut slot) = slot.try_lock() {
                let completed = unsafe { slot.fence.GetCompletedValue() };
                if completed == u64::MAX {
                    return Err(invalid());
                }
                if completed >= slot.submitted {
                    slot.images.clear();
                }
            }
        }
        Ok(())
    }

    pub fn invalidate(&self, key: usize) {
        if let Some(group) = self.published.load().get(&key) {
            group.epoch.fetch_add(1, Ordering::AcqRel);
        }
        if let Ok(mut state) = self.maintenance.try_lock() {
            let _ = self.collect(&mut state);
        }
    }
    pub fn invalidate_all(&self) {
        for group in self.published.load().values() {
            group.epoch.fetch_add(1, Ordering::AcqRel);
        }
        if let Ok(mut state) = self.maintenance.try_lock() {
            let _ = self.collect(&mut state);
        }
    }
    /// Permanently reject new recording. Stop calls this before draining slots;
    /// ordinary zero-timeout idle checks do not close the presenter.
    pub fn close(&self) {
        self.accepting.store(false, Ordering::Release);
    }
    /// Resource bytes, including both-fence retirements. D3D12 does not expose
    /// driver command storage, fence or descriptor heap allocation sizes.
    pub fn committed_bytes(&self) -> u64 {
        self.bytes.load(Ordering::Relaxed)
    }
    pub fn matches_device(&self, frame: &NativeFrame) -> bool {
        frame.device == self.device.as_raw() && frame.queue == self.queue.as_raw()
    }
    #[cfg(any(test, feature = "native-scene-fixture"))]
    pub fn with_maintenance_lock_for_fixture<R>(&self, action: impl FnOnce() -> R) -> R {
        let _guard = self.maintenance.lock().unwrap();
        action()
    }
    #[cfg(test)]
    pub(super) fn allocation_count(&self) -> u64 {
        self.allocations.load(Ordering::Relaxed)
    }
    #[cfg(test)]
    pub(super) fn before_submit_for_test(&self, action: Option<Arc<dyn Fn() + Send + Sync>>) {
        *self.before_submit.lock().unwrap() = action;
    }

    #[allow(clippy::too_many_arguments)]
    pub(super) unsafe fn record_copy(
        &self,
        key: usize,
        frame: &NativeFrame,
        options: RenderOptions,
        area: [f32; 4],
        source: &ID3D12Resource,
        list: &ID3D12GraphicsCommandList,
        main_value: u64,
    ) -> Result<Option<CopyPublication<'_>>> {
        if self.failed.load(Ordering::Acquire) || !self.accepting.load(Ordering::Acquire) {
            return Ok(None);
        }
        let Ok(mut state) = self.maintenance.try_lock() else {
            return Ok(None);
        };
        if !self.accepting.load(Ordering::Acquire) {
            return Ok(None);
        }
        let size = [(area[2] - area[0]) as u32, (area[3] - area[1]) as u32];
        let main = unsafe { self.main_fence.GetCompletedValue() };
        if main == u64::MAX {
            return Err(invalid());
        }
        self.collect(&mut state)?;
        if !state.groups.contains_key(&key) {
            state.groups.retain(|_, g| g.image.load().is_some());
            if state.groups.len() >= MAX_GROUPS {
                return Ok(None);
            }
            state.groups.insert(
                key,
                Arc::new(Group {
                    epoch: AtomicU64::new(0),
                    image: ArcSwapOption::empty(),
                }),
            );
            self.published.store(Arc::new(state.groups.clone()));
        }
        let group = state.groups[&key].clone();
        let epoch = group.epoch.load(Ordering::Acquire);
        // Reuse only a wholly unleased destination. Copying a published resource
        // could combine new pixels with old position/options in a parallel replay.
        let reusable = state
            .retired
            .iter()
            .position(|s| {
                s.key == key && s.size == size && s.main_fence <= main && Arc::strong_count(s) == 1
            })
            .map(|i| state.retired.swap_remove(i));
        let mut image = if let Some(surface) = reusable {
            surface
        } else {
            let desc = rgba_desc(size[0], size[1], D3D12_RESOURCE_FLAG_NONE);
            let allocation =
                unsafe { self.device.GetResourceAllocationInfo(0, &[desc]) }.SizeInBytes;
            if state.free.is_empty()
                || allocation > MAX_SURFACE_BYTES.saturating_sub(self.committed_bytes())
            {
                // Idle reuse is an optimization, never a permanent obstacle to
                // a new size/group at the descriptor or resource byte ceiling.
                let Maintenance { free, retired, .. } = &mut *state;
                retired.retain(|image| {
                    if image.main_fence <= main && Arc::strong_count(image) == 1 {
                        free.push(image.descriptor);
                        self.bytes.fetch_sub(image.bytes, Ordering::Relaxed);
                        false
                    } else {
                        true
                    }
                });
            }
            if state.free.is_empty() {
                return Ok(None);
            }
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
            let descriptor = state.free.pop().unwrap();
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
            #[cfg(test)]
            self.allocations.fetch_add(1, Ordering::Relaxed);
            Arc::new(Surface {
                key,
                resource,
                descriptor,
                bytes: allocation,
                size,
                viewport: [0; 2],
                area,
                options,
                main_fence: main_value,
                epoch,
            })
        };
        // No Weak references are created; retirement removed the sole strong
        // owner only after every CPU/GPU lease completed.
        let surface = Arc::get_mut(&mut image).unwrap();
        surface.viewport = [frame.width.to_bits(), frame.height.to_bits()];
        surface.area = area;
        surface.options = options;
        surface.main_fence = main_value;
        surface.epoch = epoch;
        unsafe {
            barrier(
                list,
                source,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_COPY_SOURCE,
            );
            barrier(
                list,
                &image.resource,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_COPY_DEST,
            );
            list.CopyResource(&image.resource, source);
            barrier(
                list,
                &image.resource,
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
        self.last_main.fetch_max(main_value, Ordering::Release);
        Ok(Some(CopyPublication {
            owner: self,
            maintenance: state,
            group,
            image: Some(image),
        }))
    }

    /// Draw complete images in caller order, without a publication lock, geometry
    /// uploads, command allocator growth or a GPU wait.
    /// # Safety
    /// `frame` borrows a live RTV and DIRECT queue/device for this call only.
    pub unsafe fn replay(&self, frame: &NativeFrame, groups: &[usize]) -> Result<RenderStats> {
        let result = unsafe { self.replay_inner(frame, groups) };
        if result.is_err() {
            self.failed.store(true, Ordering::Release);
        }
        result
    }
    unsafe fn replay_inner(&self, frame: &NativeFrame, groups: &[usize]) -> Result<RenderStats> {
        if self.failed.load(Ordering::Acquire)
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
        if !self.accepting.load(Ordering::Acquire) {
            return Ok(RenderStats {
                dropped: true,
                ..Default::default()
            });
        }
        let viewport = [frame.width.to_bits(), frame.height.to_bits()];
        let published = self.published.load();
        let mut images: [Option<Arc<Surface>>; MAX_GROUPS] = std::array::from_fn(|_| None);
        let mut owners: [Option<Arc<Group>>; MAX_GROUPS] = std::array::from_fn(|_| None);
        let mut count = 0;
        let mut missing = false;
        for &key in groups {
            let image = published.get(&key).and_then(|g| {
                let image = g.image.load_full()?;
                if image.viewport != viewport {
                    g.epoch.fetch_add(1, Ordering::AcqRel);
                    return None;
                }
                (image.epoch == g.epoch.load(Ordering::Acquire)).then_some(image)
            });
            if count < MAX_GROUPS && image.is_some() {
                owners[count] = published.get(&key).cloned();
                images[count] = image;
                count += 1;
            } else {
                missing = true;
            }
        }
        if count == 0 {
            drop(published);
            if let Ok(mut state) = self.maintenance.try_lock() {
                self.collect(&mut state)?;
            }
            return Ok(RenderStats {
                dropped: !groups.is_empty(),
                resident_bytes: self.committed_bytes(),
                ..Default::default()
            });
        }
        let start = self.next.fetch_add(1, Ordering::Relaxed) % PRESENT_SLOTS;
        let mut owned = None;
        for offset in 0..PRESENT_SLOTS {
            if let Ok(mut slot) = self.slots[(start + offset) % PRESENT_SLOTS].try_lock() {
                let completed = unsafe { slot.fence.GetCompletedValue() };
                if completed == u64::MAX {
                    return Err(invalid());
                }
                if completed >= slot.submitted {
                    slot.images.clear();
                    owned = Some(slot);
                    break;
                }
            }
        }
        let Some(mut slot) = owned else {
            return Ok(RenderStats {
                dropped: true,
                resident_bytes: self.committed_bytes(),
                ..Default::default()
            });
        };
        if !self.accepting.load(Ordering::Acquire) {
            return Ok(RenderStats {
                dropped: true,
                ..Default::default()
            });
        }
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
            for image in images[..count].iter().flatten() {
                let [left, top, right, bottom] = image.area;
                let width = right - left;
                let height = bottom - top;
                let options = image.options;
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
                            + (image.descriptor as usize * self.stride) as u64,
                    },
                );
                list.DrawInstanced(3, 1, 0, 0);
            }
            list.Close()?;
            let command = list.cast()?;
            #[cfg(test)]
            {
                let action = self.before_submit.lock().unwrap().clone();
                if let Some(action) = action {
                    action();
                }
            }
            if !self.accepting.load(Ordering::Acquire)
                || images[..count]
                    .iter()
                    .zip(&owners[..count])
                    .any(|(image, owner)| {
                        image.as_ref().unwrap().epoch
                            != owner.as_ref().unwrap().epoch.load(Ordering::Acquire)
                    })
            {
                return Ok(RenderStats {
                    dropped: true,
                    resident_bytes: self.committed_bytes(),
                    ..Default::default()
                });
            }
            // Mark leases before Execute/Signal, including the failure path.
            slot.images
                .extend(images[..count].iter_mut().filter_map(Option::take));
            slot.submitted = slot.submitted.checked_add(1).ok_or_else(invalid)?;
            self.queue.ExecuteCommandLists(&[Some(command)]);
            self.queue.Signal(&slot.fence, slot.submitted)?;
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
        let Ok(mut state) = self.maintenance.try_lock() else {
            return false;
        };
        // Hold all slot leases until the complete idle decision. In particular,
        // stop cannot miss an admitted recording that has not signaled yet.
        let mut slots: [Option<MutexGuard<'_, PresentSlot>>; PRESENT_SLOTS] =
            std::array::from_fn(|_| None);
        for (slot, owned) in self.slots.iter().zip(&mut slots) {
            let Ok(slot) = slot.try_lock() else {
                return false;
            };
            *owned = Some(slot);
        }
        if !wait_fence(
            &self.main_fence,
            self.last_main.load(Ordering::Acquire),
            timeout_ms,
            started,
        ) {
            return false;
        }
        for slot in slots.iter_mut().flatten() {
            if !wait_fence(&slot.fence, slot.submitted, timeout_ms, started) {
                return false;
            }
            slot.images.clear();
        }
        drop(slots);
        self.collect(&mut state).is_ok()
    }
}

fn wait_fence(
    fence: &ID3D12Fence,
    value: u64,
    timeout_ms: u32,
    started: std::time::Instant,
) -> bool {
    let completed = unsafe { fence.GetCompletedValue() };
    if completed == u64::MAX {
        return false;
    }
    if completed >= value {
        return true;
    }
    let remaining =
        timeout_ms.saturating_sub(started.elapsed().as_millis().min(u128::from(u32::MAX)) as u32);
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
        finished
    }
}

impl Drop for Presentation {
    fn drop(&mut self) {
        self.close();
        if !self.wait_idle(5000) {
            // Keep bounded command/resource references alive on a wedged GPU.
            std::mem::forget(std::mem::take(&mut self.slots));
            let state = self
                .maintenance
                .get_mut()
                .unwrap_or_else(|poisoned| poisoned.into_inner());
            std::mem::forget(std::mem::take(&mut state.groups));
            std::mem::forget(std::mem::take(&mut state.retired));
            std::mem::forget(self.published.swap(Arc::new(HashMap::new())));
            std::mem::forget(self.srv.clone());
            std::mem::forget(self.root.clone());
            std::mem::forget(self.rgba.clone());
            std::mem::forget(self.hdr.clone());
        }
    }
}
