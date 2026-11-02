use eroverlay_renderer::NativeFrame;
use std::{ffi::c_void, mem::ManuallyDrop};
use windows::{
    Win32::{
        Foundation::{CloseHandle, RECT, WAIT_OBJECT_0},
        Graphics::{
            Direct3D::{
                D3D_FEATURE_LEVEL_11_0, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, Fxc::*, ID3DBlob,
            },
            Direct3D12::*,
            Dxgi::{Common::*, CreateDXGIFactory2, IDXGIAdapter, IDXGIFactory4},
        },
        System::Threading::{CreateEventW, WaitForSingleObject},
    },
    core::{Interface, PCSTR, Result, s},
};

pub struct WarpHost {
    device: ID3D12Device,
    queue: ID3D12CommandQueue,
    _target: ID3D12Resource,
    rtv: ID3D12DescriptorHeap,
    below: ID3D12Resource,
    srv: ID3D12DescriptorHeap,
    root: ID3D12RootSignature,
    pipeline: ID3D12PipelineState,
    pending: Vec<(ID3D12CommandAllocator, ID3D12GraphicsCommandList)>,
    pending_readbacks: Vec<ID3D12Resource>,
    pub command_error: Option<String>,
    pub composites: usize,
    capture_fence: ID3D12Fence,
    capture_value: u64,
}
/// One exact presented image, copied before the following frame clears its target.
/// The ticket and queued command owners must survive until this fence completes.
pub struct Capture {
    resource: ID3D12Resource,
    layout: D3D12_PLACED_SUBRESOURCE_FOOTPRINT,
    bytes: u64,
    width: u32,
    height: u32,
    fence: ID3D12Fence,
    value: u64,
}
impl Drop for WarpHost {
    fn drop(&mut self) {
        // Probe failures must not destroy caller-owned targets/command objects
        // while real core submissions still reference them. Queue gates have
        // their own RAII release and are declared after this host.
        let _ = self.drain();
    }
}
impl WarpHost {
    pub fn new_size(width: u32, height: u32) -> Result<Self> {
        unsafe {
            let mut debug = None;
            D3D12GetDebugInterface::<ID3D12Debug>(&mut debug)?;
            debug.unwrap().EnableDebugLayer();
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
                    NumDescriptors: 2,
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
            let target: ID3D12Resource = target.unwrap();
            device.CreateRenderTargetView(&target, None, rtv.GetCPUDescriptorHandleForHeapStart());
            let mut below = None;
            let mut below_desc = target.GetDesc();
            below_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            device.CreateCommittedResource(
                &D3D12_HEAP_PROPERTIES {
                    Type: D3D12_HEAP_TYPE_DEFAULT,
                    CreationNodeMask: 1,
                    VisibleNodeMask: 1,
                    ..Default::default()
                },
                D3D12_HEAP_FLAG_NONE,
                &below_desc,
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                None,
                &mut below,
            )?;
            let below: ID3D12Resource = below.unwrap();
            let mut below_rtv = rtv.GetCPUDescriptorHandleForHeapStart();
            below_rtv.ptr +=
                device.GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV) as usize;
            device.CreateRenderTargetView(&below, None, below_rtv);
            let srv: ID3D12DescriptorHeap =
                device.CreateDescriptorHeap(&D3D12_DESCRIPTOR_HEAP_DESC {
                    Type: D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                    NumDescriptors: 1,
                    Flags: D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
                    ..Default::default()
                })?;
            device.CreateShaderResourceView(
                &below,
                Some(&D3D12_SHADER_RESOURCE_VIEW_DESC {
                    Format: DXGI_FORMAT_R8G8B8A8_UNORM,
                    ViewDimension: D3D12_SRV_DIMENSION_TEXTURE2D,
                    Shader4ComponentMapping: D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                    Anonymous: D3D12_SHADER_RESOURCE_VIEW_DESC_0 {
                        Texture2D: D3D12_TEX2D_SRV {
                            MipLevels: 1,
                            ..Default::default()
                        },
                    },
                }),
                srv.GetCPUDescriptorHandleForHeapStart(),
            );
            let (root, pipeline) = compositor(&device)?;
            let capture_fence = device.CreateFence(0, D3D12_FENCE_FLAG_NONE)?;
            let mut host = Self {
                device,
                queue,
                _target: target,
                rtv,
                below,
                srv,
                root,
                pipeline,
                pending: Vec::new(),
                pending_readbacks: Vec::new(),
                command_error: None,
                composites: 0,
                capture_fence,
                capture_value: 0,
            };
            host.clear_async()?;
            host.drain()?;
            Ok(host)
        }
    }
    /// Only queues work. Allocator/list ownership lasts until an explicit test-host drain.
    pub fn clear_async(&mut self) -> Result<()> {
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
                &[0.025, 0.04, 0.06, 1.],
                None,
            );
            list.ClearRenderTargetView(self.below_rtv(), &[0., 0., 0., 1.], None);
            list.Close()?;
            self.queue.ExecuteCommandLists(&[Some(list.cast()?)]);
            self.pending.push((allocator, list));
            Ok(())
        }
    }
    /// Controlled-host readback; never called by a production render callback.
    pub fn readback(&mut self) -> Result<(u32, u32, Vec<u8>)> {
        let capture = self.capture_async()?;
        self.drain()?;
        Self::finish_capture(&capture)
    }
    /// Queue a copy at this exact position; never wait inside a render callback.
    pub fn capture_async(&mut self) -> Result<Capture> {
        use std::mem::ManuallyDrop;
        unsafe {
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
            self.pending.push((allocator, list));
            self.capture_value += 1;
            self.queue.Signal(&self.capture_fence, self.capture_value)?;
            // Keep the queued copy's destination alive even if an assertion
            // unwinds and drops its Capture ticket before the host drains.
            self.pending_readbacks.push(readback.clone());
            Ok(Capture {
                resource: readback,
                layout,
                bytes,
                width: desc.Width as u32,
                height: desc.Height,
                fence: self.capture_fence.clone(),
                value: self.capture_value,
            })
        }
    }
    pub fn finish_capture(capture: &Capture) -> Result<(u32, u32, Vec<u8>)> {
        unsafe {
            if capture.fence.GetCompletedValue() < capture.value {
                return Err(windows::core::Error::new(
                    windows::core::HRESULT(0x800700aau32 as i32),
                    "capture is still in flight",
                ));
            }
            let Capture {
                resource: readback,
                layout,
                bytes,
                width,
                height,
                ..
            } = capture;
            let mut data = std::ptr::null_mut();
            readback.Map(
                0,
                Some(&D3D12_RANGE {
                    Begin: 0,
                    End: *bytes as usize,
                }),
                Some(&mut data),
            )?;
            let input = std::slice::from_raw_parts(data.cast::<u8>(), *bytes as usize);
            let (width, height) = (*width, *height);
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
            target: if layer == 1 {
                self.below_rtv().ptr
            } else {
                unsafe { self.rtv.GetCPUDescriptorHandleForHeapStart() }.ptr
            },
            format: if layer == 1 { 28 } else { 24 },
            width,
            height,
            layer,
            sequence,
        }
    }
    /// This controlled host may wait; it is never a game render callback.
    pub fn drain(&mut self) -> Result<()> {
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
            if result.is_ok() {
                self.pending.clear();
                self.pending_readbacks.clear();
            }
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
    fn below_rtv(&self) -> D3D12_CPU_DESCRIPTOR_HANDLE {
        let mut handle = unsafe { self.rtv.GetCPUDescriptorHandleForHeapStart() };
        handle.ptr += unsafe {
            self.device
                .GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV)
        } as usize;
        handle
    }
    pub fn resource(&self, layer: u32) -> *mut c_void {
        if layer == 1 {
            self.below.as_raw()
        } else {
            self._target.as_raw()
        }
    }
    /// Simulated native UI command: same-queue GPU compositing, without CPU waits.
    pub fn composite_async(&mut self, width: f32, height: f32) -> Result<()> {
        unsafe {
            let allocator: ID3D12CommandAllocator = self
                .device
                .CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT)?;
            let list: ID3D12GraphicsCommandList = self.device.CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                &allocator,
                &self.pipeline,
            )?;
            transition(
                &list,
                &self.below,
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            );
            list.SetGraphicsRootSignature(&self.root);
            list.SetDescriptorHeaps(&[Some(self.srv.clone())]);
            list.SetGraphicsRootDescriptorTable(0, self.srv.GetGPUDescriptorHandleForHeapStart());
            list.OMSetRenderTargets(
                1,
                Some(&self.rtv.GetCPUDescriptorHandleForHeapStart()),
                false,
                None,
            );
            list.RSSetViewports(&[D3D12_VIEWPORT {
                Width: width,
                Height: height,
                MinDepth: 0.,
                MaxDepth: 1.,
                ..Default::default()
            }]);
            list.RSSetScissorRects(&[RECT {
                left: 0,
                top: 0,
                right: width as i32,
                bottom: height as i32,
            }]);
            list.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            list.DrawInstanced(3, 1, 0, 0);
            transition(
                &list,
                &self.below,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_RENDER_TARGET,
            );
            list.Close()?;
            self.queue.ExecuteCommandLists(&[Some(list.cast()?)]);
            self.pending.push((allocator, list));
            self.composites += 1;
            Ok(())
        }
    }
    pub fn check_validation(&self) -> std::result::Result<(), String> {
        let info = self
            .device
            .cast::<ID3D12InfoQueue>()
            .map_err(|e| e.to_string())?;
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

unsafe fn transition(
    list: &ID3D12GraphicsCommandList,
    resource: &ID3D12Resource,
    before: D3D12_RESOURCE_STATES,
    after: D3D12_RESOURCE_STATES,
) {
    let mut barrier = D3D12_RESOURCE_BARRIER {
        Type: D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        Anonymous: D3D12_RESOURCE_BARRIER_0 {
            Transition: ManuallyDrop::new(D3D12_RESOURCE_TRANSITION_BARRIER {
                pResource: ManuallyDrop::new(Some(resource.clone())),
                Subresource: D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                StateBefore: before,
                StateAfter: after,
            }),
        },
        ..Default::default()
    };
    unsafe {
        list.ResourceBarrier(std::slice::from_ref(&barrier));
        ManuallyDrop::drop(&mut (*barrier.Anonymous.Transition).pResource);
    }
}

unsafe fn compositor(device: &ID3D12Device) -> Result<(ID3D12RootSignature, ID3D12PipelineState)> {
    const SHADER: &str = "Texture2D<float4> ui : register(t0);\nfloat4 vs(uint i:SV_VertexID):SV_Position { float2 p=float2((i<<1)&2,i&2); return float4(p*float2(2,-2)+float2(-1,1),0,1); }\nfloat4 ps(float4 p:SV_Position):SV_Target { return ui.Load(int3(p.xy,0)); }";
    unsafe {
        let range = D3D12_DESCRIPTOR_RANGE {
            RangeType: D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
            NumDescriptors: 1,
            ..Default::default()
        };
        let parameter = D3D12_ROOT_PARAMETER {
            ParameterType: D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
            Anonymous: D3D12_ROOT_PARAMETER_0 {
                DescriptorTable: D3D12_ROOT_DESCRIPTOR_TABLE {
                    NumDescriptorRanges: 1,
                    pDescriptorRanges: &range,
                },
            },
            ShaderVisibility: D3D12_SHADER_VISIBILITY_PIXEL,
        };
        let desc = D3D12_ROOT_SIGNATURE_DESC {
            NumParameters: 1,
            pParameters: &parameter,
            ..Default::default()
        };
        let mut blob = None;
        D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &mut blob, None)?;
        let blob = blob.unwrap();
        let root: ID3D12RootSignature = device.CreateRootSignature(
            0,
            std::slice::from_raw_parts(blob.GetBufferPointer().cast(), blob.GetBufferSize()),
        )?;
        let compile = |entry, target| -> Result<ID3DBlob> {
            let mut code = None;
            D3DCompile(
                SHADER.as_ptr().cast(),
                SHADER.len(),
                None,
                None,
                None,
                entry,
                target,
                D3DCOMPILE_OPTIMIZATION_LEVEL3,
                0,
                &mut code,
                None,
            )?;
            Ok(code.unwrap())
        };
        let vs = compile(s!("vs"), PCSTR(c"vs_5_0".as_ptr().cast()))?;
        let ps = compile(s!("ps"), PCSTR(c"ps_5_0".as_ptr().cast()))?;
        let mut desc = D3D12_GRAPHICS_PIPELINE_STATE_DESC {
            pRootSignature: ManuallyDrop::new(Some(root.clone())),
            VS: D3D12_SHADER_BYTECODE {
                pShaderBytecode: vs.GetBufferPointer(),
                BytecodeLength: vs.GetBufferSize(),
            },
            PS: D3D12_SHADER_BYTECODE {
                pShaderBytecode: ps.GetBufferPointer(),
                BytecodeLength: ps.GetBufferSize(),
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
        desc.BlendState.RenderTarget[0] = D3D12_RENDER_TARGET_BLEND_DESC {
            BlendEnable: true.into(),
            SrcBlend: D3D12_BLEND_ONE,
            DestBlend: D3D12_BLEND_SRC_ALPHA,
            BlendOp: D3D12_BLEND_OP_ADD,
            SrcBlendAlpha: D3D12_BLEND_ZERO,
            DestBlendAlpha: D3D12_BLEND_ONE,
            BlendOpAlpha: D3D12_BLEND_OP_ADD,
            LogicOp: D3D12_LOGIC_OP_NOOP,
            RenderTargetWriteMask: D3D12_COLOR_WRITE_ENABLE_ALL.0 as u8,
            ..Default::default()
        };
        desc.RTVFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM;
        let pipeline = device.CreateGraphicsPipelineState(&desc);
        ManuallyDrop::drop(&mut desc.pRootSignature);
        Ok((root, pipeline?))
    }
}
