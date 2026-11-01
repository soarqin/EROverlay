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
                    Width: 192,
                    Height: 128,
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
            Ok(Self {
                device,
                queue,
                _target: target,
                rtv,
            })
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
