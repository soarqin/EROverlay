#include "config.hpp"
#include "d3drenderer.hpp"
#include "drawlayers.hpp"
#include "global.hpp"
#include "hooking.hpp"
#include "input.hpp"
#include "plugin.hpp"
#include "uiscene.hpp"

#include "util/file.hpp"
#include "util/steam.hpp"
#include "util/string.hpp"
#include "util/sysfont.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include <algorithm>
#include <cmath>

IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

extern "C" {
extern int embedded_font_len;
extern unsigned char embedded_font[];
}

namespace er {

D3DRenderer::D3DRenderer() : drawLayers_(std::make_unique<DrawLayers>()) {}

bool D3DRenderer::hook() {
    if (uiScene_)
        return true;
    if (!initOverlay())
        return false;
    auto scene = std::make_unique<UiSceneBridge>(*this);
    if (!scene->install())
        return false;
    uiScene_ = std::move(scene);
    return true;
}

void D3DRenderer::unhook() {
    input::resetKeyChordStates();
    // Drain synchronous game callbacks before destroying renderers or GPU resources.
    uiScene_.reset();
    releaseDeviceResources();
    releaseCommandQueue();
    if (oldWndProc_) {
        if (IsWindow(gameWindow_) && !windowDestroyed_.load(std::memory_order_acquire))
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

void D3DRenderer::releaseCommandQueue() {
    if (commandQueue_) {
        commandQueue_->Release();
        commandQueue_ = nullptr;
    }
}

void D3DRenderer::releaseDeviceResources() {
    std::lock_guard lock(deviceMutex_);
    drawingFrame_ = false;
    drawLayers_->discard();
    waitForFrames();
    pluginsDestroyRenderers();
    destroyOffscreenTarget(sceneUnderlay_);

    // ImGui uploads its textures on uploadQueue_; shut it down first.
    if (ImGui::GetCurrentContext() && ImGui::GetIO().BackendRendererUserData) {
        ImGui_ImplDX12_Shutdown();
    }
    releaseTextureUploads();

    boundSwapChain_ = nullptr;

    if (commandList_) {
        commandList_->Release();
        commandList_ = nullptr;
    }
    if (commandAllocator_) {
        for (UINT i = 0; i < frameCount_; ++i) {
            if (commandAllocator_[i])
                commandAllocator_[i]->Release();
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

    freeDescriptors_.clear();
    frameCount_ = 0;
    rtvDescriptorSize_ = 0;
    srvDescriptorSize_ = 0;
    currentRTV_ = {};
    frameIndex_ = 0;
    targetFormat_ = DXGI_FORMAT_UNKNOWN;
    sceneFirstDrawPending_ = {};
    deviceLost_ = false;
}

void D3DRenderer::handleDeviceLost(const wchar_t *where, HRESULT hr) {
    fwprintf(stderr, L"[EROverlay] D3D12 device lost at %ls: 0x%08lX; waiting for a new device\n", where, hr);
    releaseDeviceResources();
    releaseCommandQueue();
}

bool D3DRenderer::initOverlay() {
    if (ImGui::GetCurrentContext()) return true;

    gameWindow_ = FindWindowW(nullptr, L"ELDEN RING™");
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

void D3DRenderer::overlayScene(const UiSceneFrame &frame) {
    std::lock_guard lock(deviceMutex_);
    if (!frame.device || !frame.queue || !frame.swapChain || !frame.target.ptr || !isUiSceneFormatSupported(frame.format, frame.layer) || !ImGui::GetCurrentContext())
        return;
    HRESULT removed = frame.device->GetDeviceRemovedReason();
    if (FAILED(removed)) {
        if (device_) handleDeviceLost(L"UI_SCENE device", removed);
        return;
    }
    DXGI_SWAP_CHAIN_DESC sd{};
    if (FAILED(frame.swapChain->GetDesc(&sd)) || !sd.OutputWindow || !IsWindow(sd.OutputWindow) || sd.BufferCount == 0 || sd.BufferCount > 8 ||
        !std::isfinite(frame.viewport.Width) || !std::isfinite(frame.viewport.Height) || frame.viewport.TopLeftX != 0 || frame.viewport.TopLeftY != 0 ||
        frame.viewport.Width <= 0 || frame.viewport.Height <= 0 || frame.viewport.Width > 16384 || frame.viewport.Height > 16384)
        return;
    if (sd.OutputWindow != gameWindow_ || windowDestroyed_.load(std::memory_order_acquire) || !ImGui::GetIO().BackendPlatformUserData) {
        // Ignore auxiliary game windows while the existing main window lives.
        if (sd.OutputWindow != gameWindow_ && IsWindow(gameWindow_) && !windowDestroyed_.load(std::memory_order_acquire)) return;
        releaseDeviceResources();
        if (oldWndProc_ && IsWindow(gameWindow_) && !windowDestroyed_.load(std::memory_order_acquire))
            SetWindowLongPtrW(gameWindow_, GWLP_WNDPROC, static_cast<LONG_PTR>(oldWndProc_));
        oldWndProc_ = 0;
        if (ImGui::GetIO().BackendPlatformUserData) ImGui_ImplWin32_Shutdown();
        gameWindow_ = sd.OutputWindow;
        if (!ImGui_ImplWin32_Init(gameWindow_)) return;
        windowDestroyed_.store(false, std::memory_order_release);
        input::resetKeyChordStates();
    }
    IUnknown *identity = nullptr;
    if (FAILED(frame.device->QueryInterface(IID_PPV_ARGS(&identity)))) return;
    UINT width = sd.BufferDesc.Width, height = sd.BufferDesc.Height;
    // There may be two RenderDrawData submissions per game frame. Allocators
    // and ImGui vertex/index buffers share the same submission ring.
    const UINT frameCount = 2 * sd.BufferCount;
    bool changed = identity != deviceIdentity_ || frame.queue != commandQueue_ || frame.swapChain != boundSwapChain_ || sceneWidth_ != width || sceneHeight_ != height ||
                   sceneFormat_ != sd.BufferDesc.Format || frameCount_ != frameCount || !ImGui::GetIO().BackendRendererUserData;
    if (changed) {
        releaseDeviceResources();
        releaseCommandQueue();
        frame.queue->AddRef();
        commandQueue_ = frame.queue;
        if (!queueUsesDevice(identity)) {
            identity->Release();
            releaseCommandQueue();
            return;
        }
        frame.device->AddRef();
        if (!createDeviceObjects(frameCount, frame.device, identity))
            return;
        boundSwapChain_ = frame.swapChain;
        sceneWidth_ = width;
        sceneHeight_ = height;
        sceneFormat_ = sd.BufferDesc.Format;
        sceneFirstDrawPending_ = {true, true};
        fwprintf(stderr, L"[EROverlay] UI_SCENE generation %llu: %ux%u format=%u swapchain-format=%u offscreen-format=%u HWND=%p layer=%ls; borrowing game RTV\n",
                 ++sceneGeneration_, width, height, unsigned(frame.format), unsigned(sd.BufferDesc.Format), unsigned(DXGI_FORMAT_R8G8B8A8_UNORM), gameWindow_,
                 frame.layer == RenderLayer::BelowUI ? L"below-ui" : L"above-ui");
    } else {
        identity->Release();
    }
    // Frame resources belong to Overlay; no game back-buffer is retained.
    if (sceneSequence_ != frame.sequence) {
        drawLayers_->discard();
        sceneSequence_ = frame.sequence;
    }
    frameIndex_ = static_cast<UINT>(imguiFrame_ % frameCount_);
    targetFormat_ = frame.format;
    renderCommands(frame.target, frame.viewport, frame.layer);
}

bool D3DRenderer::queueUsesDevice(IUnknown *deviceIdentity) const {
    ID3D12Device *queueDevice = nullptr;
    IUnknown *queueDeviceIdentity = nullptr;
    bool matches = false;
    if (SUCCEEDED(commandQueue_->GetDevice(IID_PPV_ARGS(&queueDevice)))) {
        if (SUCCEEDED(queueDevice->QueryInterface(IID_PPV_ARGS(&queueDeviceIdentity)))) {
            matches = queueDeviceIdentity == deviceIdentity;
            queueDeviceIdentity->Release();
        }
        queueDevice->Release();
    }
    return matches;
}

bool D3DRenderer::createDeviceObjects(UINT frameCount, ID3D12Device *device, IUnknown *deviceIdentity) {
    // Owned from here on; every failure path releases them with the rest.
    device_ = device;
    deviceIdentity_ = deviceIdentity;
    frameCount_ = frameCount;
    targetFormat_ = DXGI_FORMAT_R10G10B10A2_UNORM;

    enum {
        DESCRIPTOR_COUNT = 1024,
    };

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = DESCRIPTOR_COUNT;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap_)) != S_OK) {
            releaseDeviceResources();
            return false;
        }
        srvDescriptorSize_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    freeDescriptors_.clear();
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = DESCRIPTOR_COUNT;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        desc.NodeMask = 1;
        if (device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&rtvDescriptorHeap_)) != S_OK) {
            releaseDeviceResources();
            return false;
        }

        rtvDescriptorSize_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        commandAllocator_ = new ID3D12CommandAllocator *[frameCount_]();
        freeDescriptors_.resize(DESCRIPTOR_COUNT);
        for (int i = DESCRIPTOR_COUNT; i > 0; i--) {
            freeDescriptors_[DESCRIPTOR_COUNT - i] = i - 1;
        }
    }

    for (UINT i = 0; i < frameCount_; ++i) {
        if (device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator_[i])) != S_OK) {
            releaseDeviceResources();
            return false;
        }
    }

    if (device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator_[0], nullptr, IID_PPV_ARGS(&commandList_)) != S_OK ||
        commandList_->Close() != S_OK) {
        releaseDeviceResources();
        return false;
    }
    if (!initializeTextureUpload()) {
        releaseDeviceResources();
        return false;
    }

    // Fonts belong to the ImGui context and survive backend rebuilds: the atlas
    // keeps its pixels and is re-uploaded. Reloading on every resize would
    // re-read the font files and append duplicate fonts each time.
    if (ImGui::GetIO().Fonts->Fonts.empty())
        loadFont();
    ImGui_ImplDX12_InitInfo init_info = {};
    init_info.Device = device;
    init_info.NumFramesInFlight = static_cast<int>(frameCount_);
    init_info.RTVFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
    init_info.SrvDescriptorHeap = descriptorHeap_;
    // ImGui blocks until each font atlas upload completes. Waiting on the
    // game's queue would also drain the game's frame, so use the upload queue.
    init_info.CommandQueue = uploadQueue_;
    init_info.SrvDescriptorAllocFn = SrvDescriptorAlloc;
    init_info.SrvDescriptorFreeFn = SrvDescriptorFree;
    init_info.UserData = this;
    if (!ImGui_ImplDX12_Init(&init_info) || !ImGui_ImplDX12_CreateDeviceObjects()) {
        releaseDeviceResources();
        return false;
    }
    ImGui::GetMainViewport()->PlatformHandleRaw = gameWindow_;
    if (!oldWndProc_) {
        oldWndProc_ = SetWindowLongPtrW(gameWindow_, GWLP_WNDPROC, (LONG_PTR)WndProc);
    }

    ImGuiMemAllocFunc allocFunc;
    ImGuiMemFreeFunc freeFunc;
    void *userData;
    ImGui::GetAllocatorFunctions(&allocFunc, &freeFunc, &userData);
    pluginsLoadRenderers(ImGui::GetCurrentContext(), (void*)allocFunc, (void*)freeFunc, userData);
    return true;
}

void D3DRenderer::renderCommands(D3D12_CPU_DESCRIPTOR_HANDLE target, const D3D12_VIEWPORT &viewport, RenderLayer layer) {
    if (deviceLost_) {
        handleDeviceLost(L"frame submission", DXGI_ERROR_DEVICE_REMOVED);
        return;
    }
    UINT bufferIndex = frameIndex_;
    if (bufferIndex >= frameCount_ || !target.ptr || !frameFence_)
        return;
    // Reuse this frame slot's allocator and ImGui's next vertex buffers only
    // after the frame that last used them has completed on the GPU.
    uint64_t completed = frameFence_->GetCompletedValue();
    if (allocatorFences_[bufferIndex] > completed)
        return;
    // Reserve the following slot for the upper layer before building a shared
    // frame. If only one slot is free, the upper callback may still use it.
    if (layer == RenderLayer::BelowUI && allocatorFences_[(bufferIndex + 1) % frameCount_] > completed)
        return;
    processTextureUploads();
    if (deviceLost_) {
        handleDeviceLost(L"native texture upload", DXGI_ERROR_DEVICE_REMOVED);
        return;
    }

    if (layer == RenderLayer::BelowUI || !drawLayers_->pending()) {
        drawLayers_->begin();
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::GetIO().DisplaySize = {viewport.Width, viewport.Height};
        ImGui::NewFrame();

        er::input::beginFrame();
        drawingFrame_ = true;
        textureDrawFence_ = frameValue_ + (layer == RenderLayer::BelowUI ? 2 : 1);
        bool oldShowMenu = gShowMenu;
        gShowMenu = pluginsRender(&DrawLayers::observe, drawLayers_.get());
        if (gShowMenu != oldShowMenu && gHooking) {
            gHooking->showMouseCursor(gShowMenu);
        }
        ImGui::Render();
        drawLayers_->finish(ImGui::GetDrawData());
    }
    auto *drawData = drawLayers_->select(layer);
    struct RestoreLayer {
        DrawLayers *layers;
        bool last;
        ~RestoreLayer() noexcept {
            layers->restore();
            if (last)
                layers->discard();
        }
    } restore{drawLayers_.get(), layer == RenderLayer::AboveUI};
    bool texturesPending = false;
    if (drawData->Textures != nullptr)
        for (const auto *texture: *drawData->Textures)
            texturesPending |= texture->Status != ImTextureStatus_OK;
    // With nothing visible, leave the game target and queue alone:
    // no allocator reset, barriers or ExecuteCommandLists for an empty frame.
    if (drawData->DisplaySize.x <= 0 || drawData->DisplaySize.y <= 0 || (drawData->CmdListsCount == 0 && !texturesPending)) {
        finishTextureFrame(false);
        return;
    }

    OffscreenTarget *underlay = nullptr;
    if (layer == RenderLayer::BelowUI && drawData->CmdListsCount > 0) {
        ensureOffscreenSize(sceneUnderlay_, int(drawData->DisplaySize.x), int(drawData->DisplaySize.y));
        underlay = &sceneUnderlay_;
        if (!underlay->texture) {
            finishTextureFrame(false);
            return;
        }
    }
    currentRTV_ = underlay ? underlay->rtvCpuHandle : target;
    ID3D12CommandAllocator *commandAllocator = commandAllocator_[bufferIndex];
    if (FAILED(commandAllocator->Reset()) || FAILED(commandList_->Reset(commandAllocator, nullptr))) {
        handleDeviceLost(L"overlay command list reset", DXGI_ERROR_DEVICE_REMOVED);
        return;
    }

    if (underlay) {
        D3D12_RESOURCE_BARRIER transition{};
        transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        transition.Transition = {underlay->texture, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET};
        commandList_->ResourceBarrier(1, &transition);
        const float transparent[4]{};
        commandList_->ClearRenderTargetView(currentRTV_, transparent, 0, nullptr);
    }
    commandList_->OMSetRenderTargets(1, &currentRTV_, FALSE, nullptr);
    commandList_->SetDescriptorHeaps(1, &descriptorHeap_);

    // Atlas uploads transition textures that earlier overlay frames may still
    // sample on the game's queue. Order them after those frames on the GPU.
    if (texturesPending && frameFence_->GetCompletedValue() < frameValue_ && FAILED(uploadQueue_->Wait(frameFence_, frameValue_))) {
        commandList_->Close();
        handleDeviceLost(L"ID3D12CommandQueue::Wait", DXGI_ERROR_DEVICE_REMOVED);
        return;
    }
    const auto gameFormat = targetFormat_;
    if (underlay) targetFormat_ = DXGI_FORMAT_R8G8B8A8_UNORM;
    ImGui_ImplDX12_RenderDrawData(drawData, commandList_, targetFormat_);
    targetFormat_ = gameFormat;
    if (underlay) {
        D3D12_RESOURCE_BARRIER transition{};
        transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        transition.Transition = {underlay->texture, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
        commandList_->ResourceBarrier(1, &transition);
        currentRTV_ = target;
        commandList_->OMSetRenderTargets(1, &currentRTV_, FALSE, nullptr);
        ImGui_ImplDX12_CompositeUnderlay(commandList_, underlay->srvGpuHandle, gameFormat, drawData->DisplaySize.x, drawData->DisplaySize.y);
    }

    if (FAILED(commandList_->Close())) {
        handleDeviceLost(L"ID3D12GraphicsCommandList::Close", DXGI_ERROR_DEVICE_REMOVED);
        return;
    }

    ID3D12CommandList *commandLists[] = { commandList_ };
    commandQueue_->ExecuteCommandLists(1, commandLists);
    finishTextureFrame();
    auto layerIndex = layer == RenderLayer::BelowUI ? 1 : 0;
    if (sceneFirstDrawPending_[layerIndex] && drawData->TotalVtxCount > 0) {
        fwprintf(stderr, L"[EROverlay] UI_SCENE first draw submitted: layer=%ls vertices=%d indices=%d format=%u\n", layer == RenderLayer::BelowUI ? L"below-ui" : L"above-ui",
                 drawData->TotalVtxCount, drawData->TotalIdxCount, unsigned(targetFormat_));
        sceneFirstDrawPending_[layerIndex] = false;
    }
}

LRESULT D3DRenderer::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCDESTROY) gD3DRenderer->windowDestroyed_.store(true, std::memory_order_release);
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
}

void D3DRenderer::loadFont() {
    void *data = nullptr;
    size_t fontSize = 0;
    fontSize_ = gConfig.get("common.font_size", 20.0f);
    if (!(fontSize_ > 0.0f && fontSize_ <= 512.0f)) fontSize_ = 20.0f;
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
    if (parts.size() != 4) {
        return defVal;
    }
    float channels[4];
    for (size_t i = 0; i < 4; ++i) {
        long long value;
        // Malformed style text keeps the default instead of throwing.
        if (!parseConfigNumber(parts[i], value)) {
            return defVal;
        }
        channels[i] = std::clamp(float(value) / 255.0f, 0.0f, 1.0f);
    }
    return ImVec4(channels[0], channels[1], channels[2], channels[3]);
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
    auto borderWidth = gConfig.get("style.border_width", 1.0f);
    auto rounding = gConfig.get("style.rounding", 7.0f);

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

// ---- Offscreen rendering ----

OffscreenContext *D3DRenderer::CreateOffscreen() {
    auto *ctx = new OffscreenContext();
    ctx->renderer = this;
    return ctx;
}

void D3DRenderer::destroyOffscreenTarget(OffscreenTarget &target) {
    deferTexture(target.texture, target.srvCpuHandle, target.srvGpuHandle);
    target = {};
}

void D3DRenderer::DestroyOffscreen(OffscreenContext *ctx) {
    if (!ctx)
        return;
    destroyOffscreenTarget(ctx->target);
    delete ctx;
}

void D3DRenderer::ensureOffscreenSize(OffscreenTarget &target, int w, int h) {
    if (target.texture && target.width == w && target.height == h)
        return;
    destroyOffscreenTarget(target);

    target.width = w;
    target.height = h;

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

    if (FAILED(
            device_->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue, IID_PPV_ARGS(&target.texture)))) {
        target.texture = nullptr;
        return;
    }

    HeapDescriptorAlloc(&target.srvCpuHandle, &target.srvGpuHandle);
    if (!target.srvCpuHandle.ptr) {
        target.texture->Release();
        target.texture = nullptr;
        return;
    }
    // Pair each offscreen RTV with its unique SRV allocation. Fixed slots per
    // back buffer collide when more than one plugin creates an offscreen target.
    auto descriptorIndex = (target.srvCpuHandle.ptr - descriptorHeap_->GetCPUDescriptorHandleForHeapStart().ptr) / srvDescriptorSize_;
    target.rtvCpuHandle = rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
    target.rtvCpuHandle.ptr += rtvDescriptorSize_ * descriptorIndex;
    // Create RTV
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device_->CreateRenderTargetView(target.texture, &rtvDesc, target.rtvCpuHandle);

    // Create SRV
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    device_->CreateShaderResourceView(target.texture, &srvDesc, target.srvCpuHandle);
}

void D3DRenderer::BeginOffscreenCallback(const ImDrawList *list, const ImDrawCmd *cmd) {
    UNREFERENCED_PARAMETER(list);
    auto *ctx = (OffscreenContext *)cmd->UserCallbackData;
    if (!ctx)
        return;
    auto &target = ctx->target;
    if (!target.texture)
        return;
    auto *cl = ctx->renderer ? ctx->renderer->commandList_ : nullptr;
    if (!cl)
        return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = target.texture;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    cl->ResourceBarrier(1, &barrier);

    float clearColor[4] = {0.f, 0.f, 0.f, 0.f};
    cl->ClearRenderTargetView(target.rtvCpuHandle, clearColor, 0, nullptr);
    cl->OMSetRenderTargets(1, &target.rtvCpuHandle, FALSE, nullptr);
    ImGui_ImplDX12_SetRenderTargetFormat(cl, DXGI_FORMAT_R8G8B8A8_UNORM);
    if (ctx->local) {
        // The local vertices and clip rectangles use DisplayPos as origin,
        // allowing the stock ImGui backend to keep handling scissors.
        float width = ctx->displaySize.x, height = ctx->displaySize.y;
        float matrix[4][4] = {
            {2.f / width, 0, 0, 0}, {0, -2.f / height, 0, 0}, {0, 0, .5f, 0}, {-1.f - 2.f * ctx->displayPos.x / width, 1.f + 2.f * ctx->displayPos.y / height, .5f, 1}};
        D3D12_VIEWPORT viewport{};
        viewport.Width = width;
        viewport.Height = height;
        viewport.MaxDepth = 1;
        cl->RSSetViewports(1, &viewport);
        cl->SetGraphicsRoot32BitConstants(0, 16, matrix, 0);
    }
}

void D3DRenderer::EndOffscreenCallback(const ImDrawList *list, const ImDrawCmd *cmd) {
    UNREFERENCED_PARAMETER(list);
    auto *ctx = (OffscreenContext *)cmd->UserCallbackData;
    if (!ctx)
        return;
    auto &target = ctx->target;
    if (!target.texture)
        return;
    auto *cl = ctx->renderer ? ctx->renderer->commandList_ : nullptr;
    if (!cl)
        return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = target.texture;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cl->ResourceBarrier(1, &barrier);

    cl->OMSetRenderTargets(1, &ctx->renderer->currentRTV_, FALSE, nullptr);
    ImGui_ImplDX12_SetRenderTargetFormat(cl, ctx->renderer->targetFormat_);
}

bool D3DRenderer::BeginOffscreen(OffscreenContext *ctx) {
    if (!ctx)
        return false;
    ctx->local = false;
    auto *vp = ImGui::GetMainViewport();
    int w = (int)vp->Size.x;
    int h = (int)vp->Size.y;
    return beginOffscreen(ctx, w, h);
}

bool D3DRenderer::BeginOffscreenRegion(OffscreenContext *ctx, float x, float y, float width, float height) {
    if (!ctx || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) || width < 1 || height < 1 || width > 16384 || height > 16384)
        return false;
    auto *draw = ImGui::GetWindowDrawList();
    ctx->local = true;
    ctx->origin = ImVec2(x, y);
    ctx->displayPos = ImGui::GetMainViewport()->Pos;
    ctx->displaySize = ImVec2(std::ceil(width), std::ceil(height));
    ctx->firstVertex = draw->VtxBuffer.Size;
    if (!beginOffscreen(ctx, static_cast<int>(ctx->displaySize.x), static_cast<int>(ctx->displaySize.y)))
        return false;
    ctx->firstCommand = draw->CmdBuffer.Size - 1;
    return true;
}

bool D3DRenderer::beginOffscreen(OffscreenContext *ctx, int w, int h) {
    if (!ctx || !device_ || !rtvDescriptorHeap_ || w <= 0 || h <= 0)
        return false;

    // Every write and sample uses the same DIRECT queue. GPU queue order
    // protects one reusable target across frames and both render stages.
    auto &target = ctx->target;
    ensureOffscreenSize(target, w, h);
    if (!target.texture)
        return false;

    auto *drawList = ImGui::GetWindowDrawList();
    drawList->AddCallback(BeginOffscreenCallback, ctx);
    return true;
}

void *D3DRenderer::EndOffscreen(OffscreenContext *ctx) {
    if (!ctx)
        return nullptr;
    auto &target = ctx->target;
    if (!target.texture)
        return nullptr;

    auto *drawList = ImGui::GetWindowDrawList();
    if (ctx->local) {
        ImVec2 offset(ctx->displayPos.x - ctx->origin.x, ctx->displayPos.y - ctx->origin.y);
        for (int i = ctx->firstVertex; i < drawList->VtxBuffer.Size; ++i) {
            drawList->VtxBuffer[i].pos.x += offset.x;
            drawList->VtxBuffer[i].pos.y += offset.y;
        }
        for (int i = ctx->firstCommand; i < drawList->CmdBuffer.Size; ++i) {
            auto &command = drawList->CmdBuffer[i];
            command.ClipRect.x += offset.x;
            command.ClipRect.y += offset.y;
            command.ClipRect.z += offset.x;
            command.ClipRect.w += offset.y;
        }
        // EndOffscreen AddCallback starts a new command, restoring the draw
        // list's original clip header for border/composite drawing.
    }
    drawList->AddCallback(EndOffscreenCallback, ctx);
    drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);

    return (void *)target.srvGpuHandle.ptr;
}

// ---- Texture loading ----

// Synchronous image upload for the legacy loadTexture() API. It runs on the
// overlay upload queue, so it waits only for its own copy, never game work.
bool D3DRenderer::LoadTextureFromMemory(const void *data, size_t dataSize, D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle, ID3D12Resource **outTexResource,
                           int *outWidth, int *outHeight) {
    if (!device_ || !uploadQueue_ || !uploadFence_ || !srvCpuHandle.ptr || !data || dataSize > INT_MAX)
        return false;
    int width = 0;
    int height = 0;
    unsigned char *pixels = stbi_load_from_memory(static_cast<const unsigned char *>(data), static_cast<int>(dataSize), &width, &height, nullptr, 4);
    if (pixels == nullptr)
        return false;

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;

    UINT uploadPitch = (width * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC buffer = {};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = UINT64(uploadPitch) * height;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource *texture = nullptr;
    ID3D12Resource *upload = nullptr;
    ID3D12CommandAllocator *allocator = nullptr;
    ID3D12GraphicsCommandList *list = nullptr;
    void *mapped = nullptr;
    bool ok = SUCCEEDED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture))) &&
              SUCCEEDED(device_->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload))) &&
              SUCCEEDED(upload->Map(0, nullptr, &mapped));
    if (ok) {
        for (int y = 0; y < height; y++)
            memcpy(static_cast<uint8_t *>(mapped) + size_t(y) * uploadPitch, pixels + size_t(y) * width * 4, size_t(width) * 4);
        upload->Unmap(0, nullptr);
        ok = SUCCEEDED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) &&
             SUCCEEDED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list)));
    }
    stbi_image_free(pixels);

    if (ok) {
        D3D12_TEXTURE_COPY_LOCATION source = {};
        source.pResource = upload;
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        source.PlacedFootprint.Footprint.Width = width;
        source.PlacedFootprint.Footprint.Height = height;
        source.PlacedFootprint.Footprint.Depth = 1;
        source.PlacedFootprint.Footprint.RowPitch = uploadPitch;
        D3D12_TEXTURE_COPY_LOCATION destination = {};
        destination.pResource = texture;
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = texture;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        list->ResourceBarrier(1, &barrier);
        ok = SUCCEEDED(list->Close());
    }
    if (ok) {
        ID3D12CommandList *lists[] = {list};
        uploadQueue_->ExecuteCommandLists(1, lists);
        // A null event makes SetEventOnCompletion block until the copy completes.
        ok = SUCCEEDED(uploadQueue_->Signal(uploadFence_, uploadValue_ + 1)) && SUCCEEDED(uploadFence_->SetEventOnCompletion(++uploadValue_, nullptr));
    }
    if (list) list->Release();
    if (allocator) allocator->Release();
    if (upload) upload->Release();
    if (!ok) {
        if (texture) texture->Release();
        return false;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    device_->CreateShaderResourceView(texture, &srvDesc, srvCpuHandle);

    *outTexResource = texture;
    *outWidth = width;
    *outHeight = height;
    return true;
}

// Open and read a file, then forward to LoadTextureFromMemory()
bool D3DRenderer::LoadTextureFromFile(const wchar_t *filename, D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle, ID3D12Resource **outTexResource,
                                      int *outWidth, int *outHeight) {
    size_t size = 0;
    void *data = util::getFileContent(filename, size, ImGui::MemAlloc);
    if (data == nullptr)
        return false;
    bool ret = LoadTextureFromMemory(data, size, srvCpuHandle, outTexResource, outWidth, outHeight);
    ImGui::MemFree(data);
    return ret;
}

void D3DRenderer::DestroyTexture(ID3D12Resource **texResource, D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle) {
    if (!texResource) return;
    deferTexture(*texResource, srvCpuHandle, srvGpuHandle);
    *texResource = NULL;
}

}
