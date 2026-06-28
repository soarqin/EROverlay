#include "config.hpp"
#include "d3drenderer.hpp"
#include "global.hpp"
#include "hooking.hpp"
#include "plugin.hpp"

#include "util/file.hpp"
#include "util/steam.hpp"
#include "util/string.hpp"
#include "util/sysfont.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#include <MinHook.h>

#include <algorithm>

IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

extern "C" {
extern int embedded_font_len;
extern unsigned char embedded_font[];
}

namespace er {

enum :int {
    NUM_BACK_BUFFERS = 3,
};

bool D3DRenderer::hook() {
    if (hooksInstalled_) return true;

    if (!createDevice()) {
        return false;
    }

    if (!initOverlay()) {
        return false;
    }

    auto createAndEnableHook = [](const char *name, void *target, void *detour, void **original) {
        if (target == nullptr) {
            fwprintf(stderr, L"[EROverlay] %hs hook target is null\n", name);
            return false;
        }
        MH_STATUS status = MH_CreateHook(target, detour, original);
        if (status != MH_OK) {
            fwprintf(stderr, L"[EROverlay] MH_CreateHook failed for %hs: %d\n", name, status);
            return false;
        }
        status = MH_EnableHook(target);
        if (status != MH_OK) {
            fwprintf(stderr, L"[EROverlay] MH_EnableHook failed for %hs: %d\n", name, status);
            return false;
        }
        return true;
    };

    if (!createAndEnableHook("Present", fnPresent_, reinterpret_cast<void*>(&hkPresent), reinterpret_cast<void**>(&oPresent_)) ||
        !createAndEnableHook("Present1", fnPresent1_, reinterpret_cast<void*>(&hkPresent1), reinterpret_cast<void**>(&oPresent1_)) ||
        !createAndEnableHook("ResizeBuffers", fnResizeBuffers_, reinterpret_cast<void*>(&hkResizeBuffers), reinterpret_cast<void**>(&oResizeBuffers_)) ||
        !createAndEnableHook("SetSourceSize", fnSetSourceSize_, reinterpret_cast<void*>(&hkSetSourceSize), reinterpret_cast<void**>(&oSetSourceSize_)) ||
        !createAndEnableHook("ResizeBuffers1", fnResizeBuffers1_, reinterpret_cast<void*>(&hkResizeBuffers1), reinterpret_cast<void**>(&oResizeBuffers1_)) ||
        !installExecuteCommandListsHook()) {
        disableAll();
        return false;
    }

    hooksInstalled_ = true;
    return true;
}

void D3DRenderer::unhook() {
    disableAll();
    releaseDeviceResources(L"unhook");
    releaseCommandQueue();
    if (oldWndProc_) {
        SetWindowLongPtrW(gameWindow_, GWLP_WNDPROC, static_cast<LONG_PTR>(oldWndProc_));
        oldWndProc_ = 0;
    }
    if (ImGui::GetCurrentContext()) {
        if (ImGui::GetIO().BackendPlatformUserData) {
            ImGui_ImplWin32_Shutdown();
        }
        ImGui::DestroyContext();
    }
}

void D3DRenderer::disableAll() {
    auto disableAndRemoveHook = [](void *target) {
        if (target == nullptr) return;
        MH_DisableHook(target);
        MH_RemoveHook(target);
    };

    disableAndRemoveHook(fnPresent_);
    disableAndRemoveHook(fnPresent1_);

    disableAndRemoveHook(fnResizeBuffers_);
    disableAndRemoveHook(fnSetSourceSize_);
    disableAndRemoveHook(fnResizeBuffers1_);

    releaseExecuteCommandListsHook();
    hooksInstalled_ = false;
}

void D3DRenderer::releaseCommandQueue() {
    if (commandQueue_) {
        commandQueue_->Release();
        commandQueue_ = nullptr;
    }
}

bool D3DRenderer::installExecuteCommandListsHook() {
    if (eclHookInstalled_) return true;
    if (fnExecuteCommandLists_ == nullptr) {
        fwprintf(stderr, L"[EROverlay] ExecuteCommandLists hook target is null\n");
        return false;
    }

    MH_STATUS status = MH_CreateHook(fnExecuteCommandLists_, reinterpret_cast<void*>(&hkExecuteCommandLists), reinterpret_cast<void**>(&oExecuteCommandLists_));
    if (status != MH_OK) {
        fwprintf(stderr, L"[EROverlay] MH_CreateHook failed for ExecuteCommandLists: %d\n", status);
        return false;
    }
    status = MH_EnableHook(fnExecuteCommandLists_);
    if (status != MH_OK) {
        fwprintf(stderr, L"[EROverlay] MH_EnableHook failed for ExecuteCommandLists: %d\n", status);
        MH_RemoveHook(fnExecuteCommandLists_);
        return false;
    }

    eclHookInstalled_ = true;
    return true;
}

void D3DRenderer::releaseExecuteCommandListsHook() {
    if (!eclHookInstalled_) return;

    MH_DisableHook(fnExecuteCommandLists_);
    MH_RemoveHook(fnExecuteCommandLists_);
    eclHookInstalled_ = false;
}

void D3DRenderer::resetCommandQueueCapture() {
    releaseCommandQueue();
    if (hooksInstalled_) installExecuteCommandListsHook();
}

void D3DRenderer::releaseDeviceResources(const wchar_t *reason) {
    (void)reason;
    pluginsDestroyRenderers();

    if (ImGui::GetCurrentContext() && ImGui::GetIO().BackendRendererUserData) {
        ImGui_ImplDX12_Shutdown();
    }

    CleanupRenderTarget();

    if (commandList_) {
        commandList_->Release();
        commandList_ = nullptr;
    }
    if (commandAllocator_) {
        for (UINT i = 0; i < buffersCounts_; ++i) {
            if (commandAllocator_[i]) commandAllocator_[i]->Release();
        }
        delete[] commandAllocator_;
        commandAllocator_ = nullptr;
    }
    if (rtvDescriptorHeap_) {
        rtvDescriptorHeap_->Release();
        rtvDescriptorHeap_ = nullptr;
    }
    if (descriptorHeap_) {
        descriptorHeap_->Release();
        descriptorHeap_ = nullptr;
    }
    if (device_) {
        device_->Release();
        device_ = nullptr;
    }
    if (deviceIdentity_) {
        deviceIdentity_->Release();
        deviceIdentity_ = nullptr;
    }

    if (swapChainIdentity_) {
        swapChainIdentity_->Release();
        swapChainIdentity_ = nullptr;
    }
    renderTargets_.clear();
    freeDescriptors_.clear();
    buffersCounts_ = 0;
    rtvDescriptorSize_ = 0;
    srvDescriptorSize_ = 0;
    currentRTV_ = {};
    deviceLost_ = false;
}

void D3DRenderer::handleDeviceLost(const wchar_t *where, HRESULT hr) {
    fwprintf(stderr, L"[EROverlay] D3D12 device lost at %ls: 0x%08lX; waiting for a new device\n", where, hr);
    releaseDeviceResources(L"device loss");
    resetCommandQueueCapture();
}

bool D3DRenderer::createDevice() {
    WNDCLASSEXW cls {};
    cls.cbSize = sizeof(WNDCLASSEX);
    cls.style = CS_HREDRAW | CS_VREDRAW;
    cls.lpfnWndProc = DefWindowProcW;
    cls.cbClsExtra = 0;
    cls.cbWndExtra = 0;
    cls.hInstance = GetModuleHandle(nullptr);
    cls.hIcon = nullptr;
    cls.hCursor = nullptr;
    cls.hbrBackground = nullptr;
    cls.lpszMenuName = nullptr;
    cls.lpszClassName = L"MJ";
    cls.hIconSm = nullptr;
    RegisterClassExW(&cls);
    HWND hwnd = CreateWindowW(cls.lpszClassName, L"DirectX Window", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100,
                             nullptr, nullptr, cls.hInstance, nullptr);
    if (hwnd == nullptr) {
        UnregisterClassW(cls.lpszClassName, cls.hInstance);
        return false;
    }

    ID3D12Device* d3dDevice = nullptr;
    ID3D12CommandQueue* d3dCommandQueue = nullptr;
    IDXGIFactory4* dxgiFactory = nullptr;
    IDXGISwapChain3* swapChain = nullptr;

    const HMODULE D3D12Module = GetModuleHandleW(L"d3d12.dll");
    const HMODULE DXGIModule = GetModuleHandleW(L"dxgi.dll");
    void *CreateDXGIFactory1;
    void *D3D12CreateDevice;
    if (D3D12Module == nullptr || DXGIModule == nullptr) {
        goto failed;
    }

    CreateDXGIFactory1 = reinterpret_cast<void *>(GetProcAddress(DXGIModule, "CreateDXGIFactory1"));
    if (CreateDXGIFactory1 == nullptr) {
        goto failed;
    }

    if (reinterpret_cast<long (__stdcall *)(const IID &, void **)>(CreateDXGIFactory1)(IID_PPV_ARGS(&dxgiFactory)) != S_OK) {
        goto failed;
    }

    D3D12CreateDevice = reinterpret_cast<void *>(GetProcAddress(D3D12Module, "D3D12CreateDevice"));
    if (D3D12CreateDevice == nullptr) {
        goto failed;
    }

    if (reinterpret_cast<long (__stdcall *)(IUnknown *, D3D_FEATURE_LEVEL, const IID &, void **)>(D3D12CreateDevice)(
        nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3dDevice)) != S_OK) {
        goto failed;
    }

    {
        D3D12_COMMAND_QUEUE_DESC QueueDesc;
        QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        QueueDesc.Priority = 0;
        QueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        QueueDesc.NodeMask = 0;

        if (d3dDevice->CreateCommandQueue(&QueueDesc, __uuidof(ID3D12CommandQueue), (void **)&d3dCommandQueue) != S_OK) {
            goto failed;
        }

        DXGI_SWAP_CHAIN_DESC1 sd = {};
        sd.BufferCount = NUM_BACK_BUFFERS;
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.SampleDesc.Count = 1;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        IDXGISwapChain1 *SwapChain;
        if (dxgiFactory->CreateSwapChainForHwnd(d3dCommandQueue, hwnd, &sd, NULL, NULL, &SwapChain) != S_OK) {
            goto failed;
        }
        if (SwapChain->QueryInterface(IID_PPV_ARGS(&swapChain)) != S_OK) {
            SwapChain->Release();
            goto failed;
        }
        SwapChain->Release();
    }

    DestroyWindow(hwnd);
    UnregisterClassW(cls.lpszClassName, cls.hInstance);

    {
        void **swapChainVTable = *reinterpret_cast<void ***>(swapChain);
        void **commandQueueVTable = *reinterpret_cast<void ***>(d3dCommandQueue);
        fnPresent_ = swapChainVTable[8];
        fnPresent1_ = swapChainVTable[22];

        fnResizeBuffers_ = swapChainVTable[13];
        fnSetSourceSize_ = swapChainVTable[29];
        fnResizeBuffers1_ = swapChainVTable[39];

        fnExecuteCommandLists_ = commandQueueVTable[10];
    }

    swapChain->Release();
    d3dCommandQueue->Release();
    dxgiFactory->Release();
    d3dDevice->Release();

    return true;

failed:
    DestroyWindow(hwnd);
    UnregisterClassW(cls.lpszClassName, cls.hInstance);
    if (swapChain) {
        swapChain->Release();
    }
    if (d3dCommandQueue) {
        d3dCommandQueue->Release();
    }
    if (dxgiFactory) {
        dxgiFactory->Release();
    }
    if (d3dDevice) {
        d3dDevice->Release();
        d3dDevice = nullptr;
    }
    return false;
}

static inline int GetCorrectDXGIFormat(int eCurrentFormat) {
    switch (eCurrentFormat) {
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        default:
            return eCurrentFormat;
    }
}

bool D3DRenderer::initOverlay() {
    if (ImGui::GetCurrentContext()) return true;

    gameWindow_ = FindWindowW(L"ELDEN RING™", nullptr);
    if (gameWindow_ == nullptr) {
        fwprintf(stderr, L"[EROverlay] Elden Ring window not found\n");
        return false;
    }

    ImGui::CreateContext();
    initStyle();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;

    if (!ImGui_ImplWin32_Init(gameWindow_)) {
        ImGui::DestroyContext();
        return false;
    }
    return true;
}

void D3DRenderer::SrvDescriptorAlloc(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* pOutCpuDescHandle, D3D12_GPU_DESCRIPTOR_HANDLE* pOutGpuDescHandle) {
    static_cast<D3DRenderer*>(info->UserData)->HeapDescriptorAlloc(pOutCpuDescHandle, pOutGpuDescHandle);
}

void D3DRenderer::SrvDescriptorFree(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE hCpuDescHandle, D3D12_GPU_DESCRIPTOR_HANDLE hGpuDescHandle) {
    static_cast<D3DRenderer*>(info->UserData)->HeapDescriptorFree(hCpuDescHandle, hGpuDescHandle);
}

void D3DRenderer::HeapDescriptorAlloc(D3D12_CPU_DESCRIPTOR_HANDLE* pOutCpuDescHandle, D3D12_GPU_DESCRIPTOR_HANDLE* pOutGpuDescHandle) {
    if (freeDescriptors_.empty() || descriptorHeap_ == nullptr || srvDescriptorSize_ == 0) {
        pOutCpuDescHandle->ptr = 0;
        pOutGpuDescHandle->ptr = 0;
        return;
    }
    auto cpu_handle = descriptorHeap_->GetCPUDescriptorHandleForHeapStart();
    auto gpu_handle = descriptorHeap_->GetGPUDescriptorHandleForHeapStart();
    auto idx = freeDescriptors_.back();
    freeDescriptors_.pop_back();
    cpu_handle.ptr += srvDescriptorSize_ * idx;
    gpu_handle.ptr += srvDescriptorSize_ * idx;
    *pOutCpuDescHandle = cpu_handle;
    *pOutGpuDescHandle = gpu_handle;
}

void D3DRenderer::HeapDescriptorFree(D3D12_CPU_DESCRIPTOR_HANDLE hCpuDescHandle, D3D12_GPU_DESCRIPTOR_HANDLE hGpuDescHandle) {
    if (descriptorHeap_ == nullptr || srvDescriptorSize_ == 0 || hCpuDescHandle.ptr == 0 || hGpuDescHandle.ptr == 0) return;
    auto cpuIdx = (hCpuDescHandle.ptr - descriptorHeap_->GetCPUDescriptorHandleForHeapStart().ptr) / srvDescriptorSize_;
    auto gpuIdx = (hGpuDescHandle.ptr - descriptorHeap_->GetGPUDescriptorHandleForHeapStart().ptr) / srvDescriptorSize_;
    assert(cpuIdx == gpuIdx);
    freeDescriptors_.push_back(static_cast<uint32_t>(cpuIdx));
}

void D3DRenderer::overlay(IDXGISwapChain3 *pSwapChain) {
    DXGI_SWAP_CHAIN_DESC sd;
    if (FAILED(pSwapChain->GetDesc(&sd)))
        return;

    if (gameWindow_ != nullptr && sd.OutputWindow != nullptr && sd.OutputWindow != gameWindow_)
        return;

    if (commandQueue_ == nullptr)
        return;

    if (!ImGui::GetCurrentContext())
        return;

    if (swapChainIdentity_) {
        IUnknown *currentIdentity = nullptr;
        if (FAILED(pSwapChain->QueryInterface(IID_PPV_ARGS(&currentIdentity))))
            return;

        bool replaced = currentIdentity != swapChainIdentity_;
        currentIdentity->Release();
        if (replaced) {
            releaseDeviceResources(L"swap-chain replacement");
            resetCommandQueueCapture();
            return;
        }
    }

    ID3D12Device *currentDevice = nullptr;
    HRESULT hr = pSwapChain->GetDevice(IID_PPV_ARGS(&currentDevice));
    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            handleDeviceLost(L"IDXGISwapChain::GetDevice", hr);
        }
        return;
    }

    IUnknown *currentDeviceIdentity = nullptr;
    if (FAILED(currentDevice->QueryInterface(IID_PPV_ARGS(&currentDeviceIdentity)))) {
        currentDevice->Release();
        return;
    }

    if (deviceIdentity_ && currentDeviceIdentity != deviceIdentity_) {
        currentDeviceIdentity->Release();
        currentDevice->Release();
        releaseDeviceResources(L"swap-chain device replacement");
        resetCommandQueueCapture();
        return;
    }

    ID3D12Device *queueDevice = nullptr;
    IUnknown *queueDeviceIdentity = nullptr;
    bool queueMatchesDevice = false;
    if (SUCCEEDED(commandQueue_->GetDevice(IID_PPV_ARGS(&queueDevice)))) {
        if (SUCCEEDED(queueDevice->QueryInterface(IID_PPV_ARGS(&queueDeviceIdentity)))) {
            queueMatchesDevice = queueDeviceIdentity == currentDeviceIdentity;
            queueDeviceIdentity->Release();
        }
        queueDevice->Release();
    }
    if (!queueMatchesDevice) {
        currentDeviceIdentity->Release();
        currentDevice->Release();
        resetCommandQueueCapture();
        return;
    }

    if (device_) {
        hr = device_->GetDeviceRemovedReason();
        if (hr != S_OK) {
            currentDeviceIdentity->Release();
            currentDevice->Release();
            handleDeviceLost(L"GetDeviceRemovedReason", hr);
            return;
        }
    }

    if (!ImGui::GetIO().BackendRendererUserData) {
        ID3D12Device *device = currentDevice;
        IUnknown *deviceIdentity = currentDeviceIdentity;
        currentDevice = nullptr;
        currentDeviceIdentity = nullptr;

        IUnknown *swapChainIdentity = nullptr;
        if (FAILED(pSwapChain->QueryInterface(IID_PPV_ARGS(&swapChainIdentity)))) {
            deviceIdentity->Release();
            device->Release();
            return;
        }

        device_ = device;
        deviceIdentity_ = deviceIdentity;
        swapChainIdentity_ = swapChainIdentity;
        buffersCounts_ = sd.BufferCount;

        renderTargets_.clear();

        enum {
            DESCRIPTOR_COUNT = 1024,
        };

        {
            D3D12_DESCRIPTOR_HEAP_DESC desc = {};
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            desc.NumDescriptors = DESCRIPTOR_COUNT;
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            if (device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap_)) != S_OK) {
                releaseDeviceResources(L"SRV descriptor heap creation failure");
                return;
            }
            srvDescriptorSize_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }

        freeDescriptors_.clear();
        {
            D3D12_DESCRIPTOR_HEAP_DESC desc = {};
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            desc.NumDescriptors = buffersCounts_ + DESCRIPTOR_COUNT;
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            desc.NodeMask = 1;
            if (device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&rtvDescriptorHeap_)) != S_OK) {
                releaseDeviceResources(L"RTV descriptor heap creation failure");
                return;
            }

            rtvDescriptorSize_ = device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
            commandAllocator_ = new ID3D12CommandAllocator *[buffersCounts_];
            freeDescriptors_.resize(DESCRIPTOR_COUNT);
            for (int i = DESCRIPTOR_COUNT; i > 0; i--) {
                freeDescriptors_[DESCRIPTOR_COUNT - i] = i - 1;
            }
            for (uint32_t i = 0; i < buffersCounts_; ++i) {
                renderTargets_.push_back(rtvHandle);
                rtvHandle.ptr += rtvDescriptorSize_;
            }
        }

        for (UINT i = 0; i < sd.BufferCount; ++i) {
            if (device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                IID_PPV_ARGS(&commandAllocator_[i])) != S_OK) {
                releaseDeviceResources(L"command allocator creation failure");
                return;
            }
        }

        if (device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator_[0], nullptr,
                                        IID_PPV_ARGS(&commandList_)) != S_OK ||
            commandList_->Close() != S_OK) {
            releaseDeviceResources(L"command list creation failure");
            return;
        }

        loadFont();
        ImGui_ImplDX12_InitInfo init_info = {};
        init_info.Device = device;
        init_info.NumFramesInFlight = static_cast<int>(buffersCounts_);
        init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        init_info.SrvDescriptorHeap = descriptorHeap_;
        init_info.CommandQueue = commandQueue_;
        init_info.SrvDescriptorAllocFn = SrvDescriptorAlloc;
        init_info.SrvDescriptorFreeFn = SrvDescriptorFree;
        init_info.UserData = this;
        if (!ImGui_ImplDX12_Init(&init_info)) {
            releaseDeviceResources(L"ImGui DX12 initialization failure");
            return;
        }
        ImGui::GetMainViewport()->PlatformHandleRaw = gameWindow_;
        oldWndProc_ = SetWindowLongPtrW(gameWindow_, GWLP_WNDPROC, (LONG_PTR)WndProc);

        ImGuiMemAllocFunc allocFunc;
        ImGuiMemFreeFunc freeFunc;
        void *userData;
        ImGui::GetAllocatorFunctions(&allocFunc, &freeFunc, &userData);
        pluginsLoadRenderers(ImGui::GetCurrentContext(), (void*)allocFunc, (void*)freeFunc, userData);
    }

    if (currentDeviceIdentity) {
        currentDeviceIdentity->Release();
    }
    if (currentDevice) {
        currentDevice->Release();
    }

    if (!backBuffer_) {
        if (device_ == nullptr)
            return;
        backBuffer_ = new ID3D12Resource *[buffersCounts_]();
        for (UINT i = 0; i < buffersCounts_; i++) {
            ID3D12Resource *buffer;
            if (pSwapChain->GetBuffer(i, IID_PPV_ARGS(&buffer)) != S_OK) {
                continue;
            }
            D3D12_RENDER_TARGET_VIEW_DESC desc = {};
            desc.Format = static_cast<DXGI_FORMAT>(GetCorrectDXGIFormat(sd.BufferDesc.Format));
            desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            device_->CreateRenderTargetView(buffer, &desc, renderTargets_[i]);
            backBuffer_[i] = buffer;
        }
        if (backBuffer_[0] == nullptr) {
            for (UINT i = 0; i < buffersCounts_; ++i) {
                if (backBuffer_[i])
                    backBuffer_[i]->Release();
            }
            delete[] backBuffer_;
            backBuffer_ = nullptr;
        }
    }

    if (ImGui::GetCurrentContext()) {
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        bool oldShowMenu = gShowMenu;
        gShowMenu = pluginsRender();
        if (gShowMenu != oldShowMenu) {
            gHooking->showMouseCursor(gShowMenu);
        }

        ImGui::EndFrame();

        UINT bufferIndex = pSwapChain->GetCurrentBackBufferIndex();
        currentRTV_ = renderTargets_[bufferIndex];
        ID3D12CommandAllocator *commandAllocator = commandAllocator_[bufferIndex];
        if (FAILED(commandAllocator->Reset())) {
            handleDeviceLost(L"ID3D12CommandAllocator::Reset", DXGI_ERROR_DEVICE_REMOVED);
            return;
        }

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = backBuffer_[bufferIndex];
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

        if (FAILED(commandList_->Reset(commandAllocator, nullptr))) {
            handleDeviceLost(L"ID3D12GraphicsCommandList::Reset", DXGI_ERROR_DEVICE_REMOVED);
            return;
        }
        commandList_->ResourceBarrier(1, &barrier);
        commandList_->OMSetRenderTargets(1, &renderTargets_[bufferIndex], FALSE, nullptr);
        commandList_->SetDescriptorHeaps(1, &descriptorHeap_);

        ImGui::Render();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList_);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        commandList_->ResourceBarrier(1, &barrier);
        if (FAILED(commandList_->Close())) {
            handleDeviceLost(L"ID3D12GraphicsCommandList::Close", DXGI_ERROR_DEVICE_REMOVED);
            return;
        }

        ID3D12CommandList *commandLists[] = { commandList_ };
        commandQueue_->ExecuteCommandLists(1, commandLists);
    }
}

LRESULT D3DRenderer::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
    auto &io = ImGui::GetIO();
    if (io.WantCaptureMouse) {
        if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) {
            return 1L;
        }
    }
    if (io.WantCaptureKeyboard) {
        if (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) {
            return 1L;
        }
    }
    return CallWindowProcW(reinterpret_cast<WNDPROC>(gD3DRenderer->oldWndProc_), hWnd, msg, wParam, lParam);
}

D3DRenderer::~D3DRenderer() noexcept {
    unhook();
    pluginsDestroyRenderers();
}

/*
bool D3DRenderer::WorldToScreen(Vector3 pos, Vector2 &screen, const float matrix[16], int windowWidth, int windowHeight) {
    //Matrix-vector Product, multiplying world(eye) coordinates by projection matrix = clipCoords
    Vector4 clipCoords = {
        .x = pos.x * matrix[0] + pos.y * matrix[1] + pos.z * matrix[2] + matrix[3],
        .y = pos.x * matrix[4] + pos.y * matrix[5] + pos.z * matrix[6] + matrix[7],
        .z = pos.x * matrix[8] + pos.y * matrix[9] + pos.z * matrix[10] + matrix[11],
        .w = pos.x * matrix[12] + pos.y * matrix[13] + pos.z * matrix[14] + matrix[15],
    };

    if (clipCoords.w < 0.1f)
        return false;

    //perspective division, dividing by clip.W = Normalized Device Coordinates
    Vector3 NDC;
    NDC.x = clipCoords.x / clipCoords.w;
    NDC.y = clipCoords.y / clipCoords.w;
    NDC.z = clipCoords.z / clipCoords.w;

    screen.x = ((float)windowWidth * .5f * NDC.x) + (NDC.x + (float)windowWidth * .5f);
    screen.y = -((float)windowHeight * .5f * NDC.y) + (NDC.y + (float)windowHeight * .5f);
    return true;
}
*/

void D3DRenderer::loadFont() {
    void *data = nullptr;
    size_t fontSize = 0;
    fontSize_ = strtof(gConfig.get("common.font_size", "20.0").c_str(), nullptr);
    if (fontSize_ == 0.0f) fontSize_ = 20.0f;
    const auto& fontFile = gConfig.getw("common.font", L"");
    if (!fontFile.empty()) {
        std::wstring prefix;
        if (fontFile.find(L':') != std::wstring::npos) {
            prefix = L"";
        } else {
            prefix = std::wstring(gModulePath) + L"\\data\\";
        }
        data = util::getFileContent(prefix + fontFile, fontSize, ImGui::MemAlloc);
        if (!data) {
            data = util::getFileContent(prefix + fontFile + L".ttf", fontSize, ImGui::MemAlloc);
            if (!data) {
                data = util::getFileContent(prefix + fontFile + L".ttc", fontSize, ImGui::MemAlloc);
                if (!data) {
                    data = util::getFileContent(prefix + fontFile + L".otf", fontSize, ImGui::MemAlloc);
                }
            }
        }
        if (!data) {
            fwprintf(stderr, L"Unable to load font file: %ls\n", fontFile.c_str());
        }
    }

    const ImGuiIO& io = ImGui::GetIO();
    auto charset = gConfig["common.charset"];
    charset = charset.substr(0, 2);
    ImFontGlyphRangesBuilder builder;
    if (charset.empty() || (charset != "en" && charset != "ja" && charset != "ko" && charset != "pl" && charset != "ru" && charset != "th" && charset != "zh")) {
        const std::wstring &lang = util::getGameLanguage();
        if (lang == L"jpnJP") charset = "ja";
        else if (lang == L"korKR") charset = "ko";
        else if (lang == L"polPL") charset = "pl";
        else if (lang == L"rusRU") charset = "ru";
        else if (lang == L"thaTH") charset = "th";
        else if (lang == L"zhoCN") charset = "zh";
        else charset = "en";
    }
    auto useSysFont = false;
    if (charset == "ja") {
        builder.AddRanges(io.Fonts->GetGlyphRangesJapanese());
        builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());
        useSysFont = true;
    }
    else if (charset == "ko") {
        builder.AddRanges(io.Fonts->GetGlyphRangesKorean());
        useSysFont = true;
    }
    else if (charset == "pl") {
        static const ImWchar plRanges[] =
        {
            0x0020, 0x017F, // Basic Latin + Latin Supplement + Polish characters
            0,
        };
        builder.AddRanges(plRanges);
    }
    else if (charset == "ru") {
        builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
    }
    else if (charset == "th") {
        static const ImWchar thaiRanges[] =
        {
            0x0020, 0x00FF, // Basic Latin
            0x2000, 0x205E, // Punctuations
            0x0E00, 0x0E7F, // Thai
            0,
        };
        builder.AddRanges(thaiRanges);
        useSysFont = true;
    }
    else if (charset == "zh") {
        builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());
        useSysFont = true;
    }
    else {
        builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    }
    static const ImWchar symbolRanges[] =
        {
            0x2000, 0x2fFF, // Various symbols
            0,
        };
    builder.AddRanges(symbolRanges);
    static ImVector<ImWchar> finalRanges;
    builder.BuildRanges(&finalRanges);
    charsetRange_ = finalRanges.Data;

    if (data != nullptr) {
        io.Fonts->AddFontFromMemoryTTF(data, static_cast<int>(fontSize), fontSize_, nullptr, charsetRange_);
        return;
    }
    if (useSysFont) {
        auto *pathList = util::systemFontFileList(L"Segoe UI");
        if (pathList) {
            bool first = true;
            for (auto *p = pathList; *p; p++) {
                fwprintf(stdout, L"Trying to add font file: %ls... ", *p);
                data = util::getFileContent(*p, fontSize, ImGui::MemAlloc);
                if (data == nullptr) {
                    fwprintf(stdout, L"not found\n");
                    continue;
                }
                ImFontConfig cfg;
                cfg.MergeMode = !first;
                io.Fonts->AddFontFromMemoryTTF(data, static_cast<int>(fontSize), fontSize_, &cfg, charsetRange_);
                fwprintf(stdout, L"done\n");
                if (first) first = false;
            }
            util::freeSystemFontFileList(pathList);
            if (!first) return;
        }
    }
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(embedded_font, embedded_font_len, fontSize_, &cfg, charsetRange_);
}

static inline ImVec4 extractColor(const std::string &s, const ImVec4 &defVal) {
    auto parts = util::splitString(s, ',');
    if (parts.size() == 4) {
        return ImVec4 {
            std::clamp(float(std::stoi(parts[0])) / 255.0f, 0.0f, 1.0f),
            std::clamp(float(std::stoi(parts[1])) / 255.0f, 0.0f, 1.0f),
            std::clamp(float(std::stoi(parts[2])) / 255.0f, 0.0f, 1.0f),
            std::clamp(float(std::stoi(parts[3])) / 255.0f, 0.0f, 1.0f)
        };
    }
    return defVal;
}

void D3DRenderer::initStyle() {
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = ImGui::GetStyle().Colors;

    auto txtColor = extractColor(gConfig["style.text_color"], ImVec4(0.75f, 0.75f, 0.29f, 0.90f));
    auto markColor = extractColor(gConfig["style.check_mark_color"], ImVec4(0.74f, 0.74f, 0.29f, 0.90f));
    auto bgColor = extractColor(gConfig["style.bg_color"], ImVec4(0.10f, 0.10f, 0.10f, 0.30f));
    auto borderColor = extractColor(gConfig["style.border_color"], ImVec4(1.00f, 1.00f, 1.00f, 0.25f));
    auto btnColor = extractColor(gConfig["style.button_color"], ImVec4(0.05f, 0.05f, 0.05f, 0.44f));
    auto btnHoverColor = extractColor(gConfig["style.button_hover_color"], ImVec4(0.19f, 0.19f, 0.19f, 0.44f));
    auto btnPressColor = extractColor(gConfig["style.button_press_color"], ImVec4(0.20f, 0.22f, 0.23f, 0.90f));
    auto nodeColor = extractColor(gConfig["style.node_color"], ImVec4(0.00f, 0.00f, 0.00f, 0.52f));
    auto nodeHoverColor = extractColor(gConfig["style.node_hover_color"], ImVec4(0.00f, 0.00f, 0.00f, 0.36f));
    auto nodePressColor = extractColor(gConfig["style.node_press_color"], ImVec4(0.20f, 0.22f, 0.23f, 0.33f));
    auto scrollBgColor = extractColor(gConfig["style.scroll_bg_color"], ImVec4(0.05f, 0.05f, 0.05f, 0.44f));
    auto scrollColor = extractColor(gConfig["style.scroll_color"], ImVec4(0.34f, 0.34f, 0.34f, 0.44f));
    auto scrollHoverColor = extractColor(gConfig["style.scroll_hover_color"], ImVec4(0.40f, 0.40f, 0.40f, 0.44f));
    auto scrollPressColor = extractColor(gConfig["style.scroll_press_color"], ImVec4(0.56f, 0.56f, 0.56f, 0.44f));
    auto borderWidth = std::stof(gConfig.get("style.border_width", "1.0"));
    auto rounding = std::stof(gConfig.get("style.rounding", "7.0"));

    colors[ImGuiCol_Text] = txtColor;
    colors[ImGuiCol_TextDisabled] = colors[ImGuiCol_Text];//ImVec4(0.86f, 0.93f, 0.89f, 0.28f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.27f, 0.00f, 0.63f, 0.43f);
    colors[ImGuiCol_WindowBg] = bgColor;
/*
    colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.19f, 0.19f, 0.19f, 0.82f);
*/
    colors[ImGuiCol_Border] = borderColor;
/*
    colors[ImGuiCol_BorderShadow] = ImVec4(0.20f, 0.22f, 0.27f, 0.90f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.44f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.19f, 0.19f, 0.44f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.22f, 0.23f, 0.90f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.90f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.06f, 0.06f, 0.06f, 0.90f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.90f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 0.90f);
*/
    colors[ImGuiCol_ScrollbarBg] = scrollBgColor;
    colors[ImGuiCol_ScrollbarGrab] = scrollColor;
    colors[ImGuiCol_ScrollbarGrabHovered] = scrollHoverColor;
    colors[ImGuiCol_ScrollbarGrabActive] = scrollPressColor;
    colors[ImGuiCol_CheckMark] = markColor;
/*
    colors[ImGuiCol_SliderGrab] = ImVec4(0.34f, 0.34f, 0.34f, 0.44f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.44f);
*/
    colors[ImGuiCol_Button] = btnColor;
    colors[ImGuiCol_ButtonHovered] = btnHoverColor;
    colors[ImGuiCol_ButtonActive] = btnPressColor;
    colors[ImGuiCol_Header] = nodeColor;
    colors[ImGuiCol_HeaderHovered] = nodeHoverColor;
    colors[ImGuiCol_HeaderActive] = nodePressColor;
/*
    colors[ImGuiCol_Separator] = ImVec4(0.28f, 0.28f, 0.28f, 0.80f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.40f, 0.44f, 0.47f, 0.90f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.40f, 0.44f, 0.47f, 0.90f);
    colors[ImGuiCol_Tab] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.14f, 0.14f, 0.14f, 0.90f);
    colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.20f, 0.20f, 0.36f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.14f, 0.14f, 0.90f);
    colors[ImGuiCol_PlotLines] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.00f, 0.00f, 0.90f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(1.00f, 0.00f, 0.00f, 0.90f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.00f, 0.00f, 0.90f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.20f, 0.22f, 0.23f, 0.90f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(0.33f, 0.67f, 0.86f, 0.90f);
    colors[ImGuiCol_NavHighlight] = ImVec4(1.00f, 0.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 0.00f, 0.00f, 0.60f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(1.00f, 0.00f, 0.00f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(1.00f, 0.00f, 0.00f, 0.35f);
*/
    style.DisabledAlpha = 1.0f;
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    style.WindowPadding = ImVec2(8.00f, 8.00f);
    style.FramePadding = ImVec2(2.00f, 2.00f);
    style.CellPadding = ImVec2(6.00f, 6.00f);
    style.ItemSpacing = ImVec2(6.00f, 6.00f);
    style.ItemInnerSpacing = ImVec2(6.00f, 6.00f);
    style.TouchExtraPadding = ImVec2(0.00f, 0.00f);
    style.IndentSpacing = 25;
    style.ScrollbarSize = 15;
    style.GrabMinSize = 10;
    style.WindowBorderSize = borderWidth;
    style.ChildBorderSize = 1;
    style.PopupBorderSize = 1;
    style.FrameBorderSize = 1;
    style.TabBorderSize = 1;
    style.WindowRounding = rounding;
    style.ChildRounding = 4;
    style.FrameRounding = 3;
    style.PopupRounding = 4;
    style.ScrollbarRounding = 9;
    style.GrabRounding = 3;
    style.LogSliderDeadzone = 4;
    style.TabRounding = 4;
}

void D3DRenderer::CleanupRenderTarget() {
    if (!backBuffer_) return;
    for (UINT i = 0; i < buffersCounts_; ++i) {
        if (backBuffer_[i]) {
            backBuffer_[i]->Release();
            backBuffer_[i] = nullptr;
        }
    }
    delete[] backBuffer_;
    backBuffer_ = nullptr;
}

HRESULT WINAPI D3DRenderer::hkPresent(IDXGISwapChain3 *pSwapChain, UINT SyncInterval, UINT Flags) {
    gD3DRenderer->overlay(pSwapChain);
    HRESULT hr = gD3DRenderer->oPresent_(pSwapChain, SyncInterval, Flags);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        gD3DRenderer->handleDeviceLost(L"IDXGISwapChain::Present", hr);
    }
    return hr;
}

HRESULT WINAPI D3DRenderer::hkPresent1(IDXGISwapChain3 *pSwapChain,
                                       UINT SyncInterval,
                                       UINT PresentFlags,
                                       const DXGI_PRESENT_PARAMETERS *pPresentParameters) {
    gD3DRenderer->overlay(pSwapChain);
    HRESULT hr = gD3DRenderer->oPresent1_(pSwapChain, SyncInterval, PresentFlags, pPresentParameters);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        gD3DRenderer->handleDeviceLost(L"IDXGISwapChain1::Present1", hr);
    }
    return hr;
}

HRESULT WINAPI D3DRenderer::hkResizeBuffers(IDXGISwapChain *pSwapChain,
                                            UINT BufferCount,
                                            UINT Width,
                                            UINT Height,
                                            DXGI_FORMAT NewFormat,
                                            UINT SwapChainFlags) {
    gD3DRenderer->releaseDeviceResources(L"swap-chain ResizeBuffers");
    gD3DRenderer->releaseCommandQueue();
    HRESULT hr = gD3DRenderer->oResizeBuffers_(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    gD3DRenderer->installExecuteCommandListsHook();
    return hr;
}

HRESULT WINAPI D3DRenderer::hkSetSourceSize(IDXGISwapChain2 *pSwapChain, UINT Width, UINT Height) {
    gD3DRenderer->CleanupRenderTarget();
    return gD3DRenderer->oSetSourceSize_(pSwapChain, Width, Height);
}

HRESULT WINAPI D3DRenderer::hkResizeBuffers1(IDXGISwapChain3 *pSwapChain,
                                             UINT BufferCount,
                                             UINT Width,
                                             UINT Height,
                                              DXGI_FORMAT NewFormat,
                                              UINT SwapChainFlags,
                                              const UINT *pCreationNodeMask,
                                              IUnknown *const *ppPresentQueue) {
    gD3DRenderer->releaseDeviceResources(L"swap-chain ResizeBuffers1");
    gD3DRenderer->releaseCommandQueue();
    HRESULT hr = gD3DRenderer->oResizeBuffers1_(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags, pCreationNodeMask, ppPresentQueue);
    gD3DRenderer->installExecuteCommandListsHook();
    return hr;
}

void WINAPI D3DRenderer::hkExecuteCommandLists(ID3D12CommandQueue *pCommandQueue, UINT NumCommandLists, ID3D12CommandList *const *ppCommandLists) {
    bool captured = false;
    if (!gD3DRenderer->commandQueue_) {
        D3D12_COMMAND_QUEUE_DESC desc = pCommandQueue->GetDesc();
        if (desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
            pCommandQueue->AddRef();
            gD3DRenderer->commandQueue_ = pCommandQueue;
            captured = true;
        }
    }
    gD3DRenderer->oExecuteCommandLists_(pCommandQueue, NumCommandLists, ppCommandLists);
    if (captured) {
        gD3DRenderer->releaseExecuteCommandListsHook();
    }
}

// ---- Offscreen rendering ----

OffscreenContext *D3DRenderer::CreateOffscreen() {
    auto *ctx = new OffscreenContext();
    return ctx;
}

void D3DRenderer::DestroyOffscreen(OffscreenContext *ctx) {
    if (!ctx) return;
    if (ctx->texture) {
        ctx->texture->Release();
        ctx->texture = nullptr;
    }
    if (ctx->srvCpuHandle.ptr) {
        HeapDescriptorFree(ctx->srvCpuHandle, ctx->srvGpuHandle);
        ctx->srvCpuHandle.ptr = 0;
        ctx->srvGpuHandle.ptr = 0;
    }
    delete ctx;
}

void D3DRenderer::ensureOffscreenSize(OffscreenContext *ctx, int w, int h) {
    if (ctx->texture && ctx->width == w && ctx->height == h) return;

    // Release old resources
    if (ctx->texture) {
        ctx->texture->Release();
        ctx->texture = nullptr;
    }
    if (ctx->srvCpuHandle.ptr) {
        HeapDescriptorFree(ctx->srvCpuHandle, ctx->srvGpuHandle);
        ctx->srvCpuHandle.ptr = 0;
        ctx->srvGpuHandle.ptr = 0;
    }

    ctx->width = w;
    ctx->height = h;

    // Create texture
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = w;
    texDesc.Height = h;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

    if (FAILED(device_->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
                                                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
                                                IID_PPV_ARGS(&ctx->texture)))) {
        ctx->texture = nullptr;
        return;
    }

    // Create RTV
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device_->CreateRenderTargetView(ctx->texture, &rtvDesc, ctx->rtvCpuHandle);

    // Create SRV
    HeapDescriptorAlloc(&ctx->srvCpuHandle, &ctx->srvGpuHandle);
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    device_->CreateShaderResourceView(ctx->texture, &srvDesc, ctx->srvCpuHandle);
}

void D3DRenderer::BeginOffscreenCallback(const ImDrawList *list, const ImDrawCmd *cmd) {
    UNREFERENCED_PARAMETER(list);
    auto *ctx = (OffscreenContext *)cmd->UserCallbackData;
    if (!ctx || !ctx->texture) return;
    auto *cl = gD3DRenderer->commandList_;
    if (!cl) return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = ctx->texture;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    cl->ResourceBarrier(1, &barrier);

    float clearColor[4] = {0.f, 0.f, 0.f, 0.f};
    cl->ClearRenderTargetView(ctx->rtvCpuHandle, clearColor, 0, nullptr);
    cl->OMSetRenderTargets(1, &ctx->rtvCpuHandle, FALSE, nullptr);
}

void D3DRenderer::EndOffscreenCallback(const ImDrawList *list, const ImDrawCmd *cmd) {
    UNREFERENCED_PARAMETER(list);
    auto *ctx = (OffscreenContext *)cmd->UserCallbackData;
    if (!ctx || !ctx->texture) return;
    auto *cl = gD3DRenderer->commandList_;
    if (!cl) return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = ctx->texture;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cl->ResourceBarrier(1, &barrier);

    cl->OMSetRenderTargets(1, &gD3DRenderer->currentRTV_, FALSE, nullptr);
}

void D3DRenderer::BeginOffscreen(OffscreenContext *ctx) {
    if (!ctx || !device_ || !rtvDescriptorHeap_) return;

    auto *vp = ImGui::GetMainViewport();
    int w = (int)vp->Size.x;
    int h = (int)vp->Size.y;
    if (w <= 0 || h <= 0) return;

    // Ensure an RTV handle exists (allocated once, reused).
    // Use the first slot after backbuffer RTVs in the RTV descriptor heap.
    // These slots are referenced by freeDescriptors_ but used with the SRV heap,
    // so they are effectively unused in the RTV heap.
    if (ctx->rtvCpuHandle.ptr == 0) {
        auto rtvStart = rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
        rtvStart.ptr += rtvDescriptorSize_ * buffersCounts_;
        ctx->rtvCpuHandle = rtvStart;
    }

    ensureOffscreenSize(ctx, w, h);
    if (!ctx->texture) return;

    auto *drawList = ImGui::GetWindowDrawList();
    drawList->AddCallback(BeginOffscreenCallback, ctx);
}

void *D3DRenderer::EndOffscreen(OffscreenContext *ctx) {
    if (!ctx || !ctx->texture) return nullptr;

    auto *drawList = ImGui::GetWindowDrawList();
    drawList->AddCallback(EndOffscreenCallback, ctx);
    drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);

    return (void *)ctx->srvGpuHandle.ptr;
}

// ---- Texture loading ----

bool D3DRenderer::LoadTextureFromMemory(const void *data, size_t dataSize, D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle, ID3D12Resource **outTexResource,
                           int *outWidth, int *outHeight) {
    // Load from disk into a raw RGBA buffer
    int image_width = 0;
    int image_height = 0;
    unsigned char *image_data = stbi_load_from_memory((const unsigned char *)data, (int)dataSize, &image_width, &image_height, NULL, 4);
    if (image_data == NULL)
        return false;

    // Create texture resource
    D3D12_HEAP_PROPERTIES props;
    memset(&props, 0, sizeof(D3D12_HEAP_PROPERTIES));
    props.Type = D3D12_HEAP_TYPE_DEFAULT;
    props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    D3D12_RESOURCE_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment = 0;
    desc.Width = image_width;
    desc.Height = image_height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ID3D12Resource *pTexture = NULL;
    device_->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, NULL, IID_PPV_ARGS(&pTexture));

    // Create a temporary upload resource to move the data in
    UINT uploadPitch = (image_width * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
    UINT uploadSize = image_height * uploadPitch;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment = 0;
    desc.Width = uploadSize;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    props.Type = D3D12_HEAP_TYPE_UPLOAD;
    props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    ID3D12Resource *uploadBuffer = NULL;
    HRESULT hr = device_->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL, IID_PPV_ARGS(&uploadBuffer));
    IM_ASSERT(SUCCEEDED(hr));

    // Write pixels into the upload resource
    void *mapped = NULL;
    D3D12_RANGE range = {0, uploadSize};
    hr = uploadBuffer->Map(0, &range, &mapped);
    IM_ASSERT(SUCCEEDED(hr));
    for (int y = 0; y < image_height; y++)
        memcpy((void *)((uintptr_t)mapped + y * uploadPitch), image_data + y * image_width * 4, image_width * 4);
    uploadBuffer->Unmap(0, &range);
    stbi_image_free(image_data);

    // Copy the upload resource content into the real resource
    D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
    srcLocation.pResource = uploadBuffer;
    srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLocation.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srcLocation.PlacedFootprint.Footprint.Width = image_width;
    srcLocation.PlacedFootprint.Footprint.Height = image_height;
    srcLocation.PlacedFootprint.Footprint.Depth = 1;
    srcLocation.PlacedFootprint.Footprint.RowPitch = uploadPitch;

    D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
    dstLocation.pResource = pTexture;
    dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLocation.SubresourceIndex = 0;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = pTexture;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    // Create a temporary command queue to do the copy with
    ID3D12Fence *fence = NULL;
    hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    IM_ASSERT(SUCCEEDED(hr));

    HANDLE event = CreateEvent(0, 0, 0, 0);
    IM_ASSERT(event != NULL);

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 1;

    ID3D12CommandQueue *cmdQueue = NULL;
    hr = device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&cmdQueue));
    IM_ASSERT(SUCCEEDED(hr));

    ID3D12CommandAllocator *cmdAlloc = NULL;
    hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc));
    IM_ASSERT(SUCCEEDED(hr));

    ID3D12GraphicsCommandList *cmdList = NULL;
    hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc, NULL, IID_PPV_ARGS(&cmdList));
    IM_ASSERT(SUCCEEDED(hr));

    cmdList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, NULL);
    cmdList->ResourceBarrier(1, &barrier);

    hr = cmdList->Close();
    IM_ASSERT(SUCCEEDED(hr));

    // Execute the copy
    cmdQueue->ExecuteCommandLists(1, (ID3D12CommandList *const *)&cmdList);
    hr = cmdQueue->Signal(fence, 1);
    IM_ASSERT(SUCCEEDED(hr));

    // Wait for everything to complete
    fence->SetEventOnCompletion(1, event);
    WaitForSingleObject(event, INFINITE);

    // Tear down our temporary command queue and release the upload resource
    cmdList->Release();
    cmdAlloc->Release();
    cmdQueue->Release();
    CloseHandle(event);
    fence->Release();
    uploadBuffer->Release();

    // Create a shader resource view for the texture
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
    ZeroMemory(&srvDesc, sizeof(srvDesc));
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    device_->CreateShaderResourceView(pTexture, &srvDesc, srvCpuHandle);

    // Return results
    *outTexResource = pTexture;
    *outWidth = image_width;
    *outHeight = image_height;

    return true;
}

// Open and read a file, then forward to LoadTextureFromMemory()
bool D3DRenderer::LoadTextureFromFile(const wchar_t *filename, D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle, ID3D12Resource **outTexResource,
                                      int *outWidth, int *outHeight) {
    HANDLE file = CreateFileW(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD file_size = GetFileSize(file, NULL);
    if (file_size == INVALID_FILE_SIZE) {
        CloseHandle(file);
        return false;
    }
    void *file_data = IM_ALLOC(file_size);
    DWORD bytes_read = 0;
    ReadFile(file, file_data, file_size, &bytes_read, NULL);
    CloseHandle(file);
    bool ret = LoadTextureFromMemory(file_data, file_size, srvCpuHandle, outTexResource, outWidth, outHeight);
    IM_FREE(file_data);
    return ret;
}

void D3DRenderer::DestroyTexture(ID3D12Resource **texResource, D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle) {
    (*texResource)->Release();
    *texResource = NULL;
    HeapDescriptorFree(srvCpuHandle, srvGpuHandle);
}

}
