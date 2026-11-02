use eroverlay_renderer::NativeFrame;
use windows::{
    Win32::{
        Foundation::{CloseHandle, WAIT_OBJECT_0},
        Graphics::{
            Direct3D::D3D_FEATURE_LEVEL_11_0,
            Direct3D12::*,
            Dxgi::{Common::*, CreateDXGIFactory2, IDXGIAdapter, IDXGIFactory4},
        },
        System::Threading::{CreateEventW, WaitForSingleObject},
    },
    core::{Interface, Result},
};

pub struct WarpHost {
    device: ID3D12Device,
    queue: ID3D12CommandQueue,
    _target: ID3D12Resource,
    rtv: ID3D12DescriptorHeap,
}
impl WarpHost {
    pub fn new() -> Result<Self> {
        Self::new_size(192, 128)
    }
    pub fn new_size(width: u32, height: u32) -> Result<Self> {
        unsafe {
            let mut debug = None;
            if D3D12GetDebugInterface::<ID3D12Debug>(&mut debug).is_ok() {
                debug.unwrap().EnableDebugLayer();
            }
            let factory: IDXGIFactory4 = CreateDXGIFactory2(Default::default())?;
            let adapter: IDXGIAdapter = factory.EnumWarpAdapter()?;
            let mut device = None;
            D3D12CreateDevice(&adapter, D3D_FEATURE_LEVEL_11_0, &mut device)?;
            let device: ID3D12Device = device.unwrap();
            let queue = device.CreateCommandQueue(&D3D12_COMMAND_QUEUE_DESC {
                Type: D3D12_COMMAND_LIST_TYPE_DIRECT,
                ..Default::default()
            })?;
            let rtv: ID3D12DescriptorHeap =
                device.CreateDescriptorHeap(&D3D12_DESCRIPTOR_HEAP_DESC {
                    Type: D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                    NumDescriptors: 1,
                    ..Default::default()
                })?;
            let mut target = None;
            device.CreateCommittedResource(
                &D3D12_HEAP_PROPERTIES {
                    Type: D3D12_HEAP_TYPE_DEFAULT,
                    CreationNodeMask: 1,
                    VisibleNodeMask: 1,
                    ..Default::default()
                },
                D3D12_HEAP_FLAG_NONE,
                &D3D12_RESOURCE_DESC {
                    Dimension: D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                    Width: u64::from(width),
                    Height: height,
                    DepthOrArraySize: 1,
                    MipLevels: 1,
                    Format: DXGI_FORMAT_R10G10B10A2_UNORM,
                    SampleDesc: DXGI_SAMPLE_DESC {
                        Count: 1,
                        Quality: 0,
                    },
                    Flags: D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                    ..Default::default()
                },
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                None,
                &mut target,
            )?;
            let target = target.unwrap();
            device.CreateRenderTargetView(&target, None, rtv.GetCPUDescriptorHandleForHeapStart());
            let host = Self {
                device,
                queue,
                _target: target,
                rtv,
            };
            host.clear()?;
            Ok(host)
        }
    }
    pub fn clear(&self) -> Result<()> {
        unsafe {
            let allocator: ID3D12CommandAllocator = self
                .device
                .CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT)?;
            let list: ID3D12GraphicsCommandList = self.device.CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                &allocator,
                None,
            )?;
            list.ClearRenderTargetView(
                self.rtv.GetCPUDescriptorHandleForHeapStart(),
                &[0., 0., 0., 1.],
                None,
            );
            list.Close()?;
            self.queue.ExecuteCommandLists(&[Some(list.cast()?)]);
            self.drain()
        }
    }
    /// Controlled-host readback; never called by a production render callback.
    #[allow(dead_code)] // Optional Boss probe; the lifecycle binary shares this module.
    pub fn readback(&self) -> Result<(u32, u32, Vec<u8>)> {
        use std::mem::ManuallyDrop;
        unsafe {
            self.drain()?;
            let desc = self._target.GetDesc();
            let mut layout = D3D12_PLACED_SUBRESOURCE_FOOTPRINT::default();
            let mut bytes = 0;
            self.device.GetCopyableFootprints(
                &desc,
                0,
                1,
                0,
                Some(&mut layout),
                None,
                None,
                Some(&mut bytes),
            );
            let mut readback = None;
            self.device.CreateCommittedResource(
                &D3D12_HEAP_PROPERTIES {
                    Type: D3D12_HEAP_TYPE_READBACK,
                    CreationNodeMask: 1,
                    VisibleNodeMask: 1,
                    ..Default::default()
                },
                D3D12_HEAP_FLAG_NONE,
                &D3D12_RESOURCE_DESC {
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
                },
                D3D12_RESOURCE_STATE_COPY_DEST,
                None,
                &mut readback,
            )?;
            let readback: ID3D12Resource = readback.unwrap();
            let allocator: ID3D12CommandAllocator = self
                .device
                .CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT)?;
            let list: ID3D12GraphicsCommandList = self.device.CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                &allocator,
                None,
            )?;
            let transition = |before, after| D3D12_RESOURCE_BARRIER {
                Type: D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
                Anonymous: D3D12_RESOURCE_BARRIER_0 {
                    Transition: ManuallyDrop::new(D3D12_RESOURCE_TRANSITION_BARRIER {
                        pResource: ManuallyDrop::new(Some(self._target.clone())),
                        Subresource: D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                        StateBefore: before,
                        StateAfter: after,
                    }),
                },
                ..Default::default()
            };
            let mut to_copy = transition(
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_COPY_SOURCE,
            );
            list.ResourceBarrier(std::slice::from_ref(&to_copy));
            ManuallyDrop::drop(&mut (*to_copy.Anonymous.Transition).pResource);
            let mut source = D3D12_TEXTURE_COPY_LOCATION {
                pResource: ManuallyDrop::new(Some(self._target.clone())),
                Type: D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
                Anonymous: D3D12_TEXTURE_COPY_LOCATION_0 {
                    SubresourceIndex: 0,
                },
            };
            let mut destination = D3D12_TEXTURE_COPY_LOCATION {
                pResource: ManuallyDrop::new(Some(readback.clone())),
                Type: D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
                Anonymous: D3D12_TEXTURE_COPY_LOCATION_0 {
                    PlacedFootprint: layout,
                },
            };
            list.CopyTextureRegion(&destination, 0, 0, 0, &source, None);
            ManuallyDrop::drop(&mut source.pResource);
            ManuallyDrop::drop(&mut destination.pResource);
            let mut to_target = transition(
                D3D12_RESOURCE_STATE_COPY_SOURCE,
                D3D12_RESOURCE_STATE_RENDER_TARGET,
            );
            list.ResourceBarrier(std::slice::from_ref(&to_target));
            ManuallyDrop::drop(&mut (*to_target.Anonymous.Transition).pResource);
            list.Close()?;
            self.queue.ExecuteCommandLists(&[Some(list.cast()?)]);
            self.drain()?;
            let mut data = std::ptr::null_mut();
            readback.Map(
                0,
                Some(&D3D12_RANGE {
                    Begin: 0,
                    End: bytes as usize,
                }),
                Some(&mut data),
            )?;
            let input = std::slice::from_raw_parts(data.cast::<u8>(), bytes as usize);
            let (width, height) = (desc.Width as u32, desc.Height);
            let mut rgba = vec![0; width as usize * height as usize * 4];
            for y in 0..height as usize {
                for x in 0..width as usize {
                    let offset =
                        layout.Offset as usize + y * layout.Footprint.RowPitch as usize + x * 4;
                    let packed = u32::from_le_bytes(input[offset..offset + 4].try_into().unwrap());
                    let output = &mut rgba[(y * width as usize + x) * 4..][..4];
                    for (channel, shift) in output[..3].iter_mut().zip([0, 10, 20]) {
                        *channel = ((((packed >> shift) & 1023) * 255 + 511) / 1023) as u8;
                    }
                    output[3] = ((packed >> 30) * 85) as u8;
                }
            }
            readback.Unmap(0, Some(&D3D12_RANGE { Begin: 0, End: 0 }));
            Ok((width, height, rgba))
        }
    }
    pub fn frame(&self, width: f32, height: f32, sequence: u64, layer: u32) -> NativeFrame {
        NativeFrame {
            device: self.device.as_raw(),
            queue: self.queue.as_raw(),
            target: unsafe { self.rtv.GetCPUDescriptorHandleForHeapStart() }.ptr,
            format: 24,
            width,
            height,
            layer,
            sequence,
        }
    }
    /// This controlled host may wait; it is never a game render callback.
    pub fn drain(&self) -> Result<()> {
        unsafe {
            let fence: ID3D12Fence = self.device.CreateFence(0, D3D12_FENCE_FLAG_NONE)?;
            let event = CreateEventW(None, false, false, None)?;
            let result = (|| {
                self.queue.Signal(&fence, 1)?;
                fence.SetEventOnCompletion(1, event)?;
                if WaitForSingleObject(event, 10000) != WAIT_OBJECT_0 {
                    return Err(windows::core::Error::new(
                        windows::core::HRESULT(0x800705b4u32 as i32),
                        "WARP queue drain timed out",
                    ));
                }
                Ok(())
            })();
            let _ = CloseHandle(event);
            result
        }
    }
    pub fn hold_queue(&self) -> Result<ID3D12Fence> {
        unsafe {
            let gate = self.device.CreateFence(0, D3D12_FENCE_FLAG_NONE)?;
            self.queue.Wait(&gate, 1)?;
            Ok(gate)
        }
    }
    pub fn check_validation(&self) -> std::result::Result<(), String> {
        let Ok(info) = self.device.cast::<ID3D12InfoQueue>() else {
            return Ok(());
        };
        for index in 0..unsafe { info.GetNumStoredMessages() } {
            let mut bytes = 0;
            unsafe { info.GetMessage(index, None, &mut bytes) }.map_err(|e| e.to_string())?;
            let mut storage = vec![0u64; bytes.div_ceil(8)];
            let message = storage.as_mut_ptr().cast::<D3D12_MESSAGE>();
            unsafe { info.GetMessage(index, Some(message), &mut bytes) }
                .map_err(|e| e.to_string())?;
            let message = unsafe { &*message };
            if message.Severity == D3D12_MESSAGE_SEVERITY_ERROR
                || message.Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION
            {
                return Err(format!(
                    "D3D12 validation: {}",
                    unsafe { std::ffi::CStr::from_ptr(message.pDescription.cast()) }
                        .to_string_lossy()
                ));
            }
        }
        unsafe {
            info.ClearStoredMessages();
        }
        Ok(())
    }
}
