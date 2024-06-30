#include "d3drenderer.hpp"
#include "hooking.hpp"
#include "global.hpp"
#include "config.hpp"
#include "steamapi.hpp"

#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#include "MinHook.h"

#include "bosses/render.hpp"

#include <algorithm>
#include <fstream>

IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace er {

enum :int {
    NUM_BACK_BUFFERS = 3,
};

bool D3DRenderer::hook() {
    if (!createDevice()) {
        return false;
    }

    static MH_STATUS cscStatus = MH_CreateHook(reinterpret_cast<void*>(fnCreateSwapChain_), reinterpret_cast<void*>(&hkCreateSwapChain), reinterpret_cast<void**>(&oCreateSwapChain_));
    static MH_STATUS cschStatus = MH_CreateHook(reinterpret_cast<void*>(fnCreateSwapChainForHwndChain_), reinterpret_cast<void*>(&hkCreateSwapChainForHwnd), reinterpret_cast<void**>(&oCreateSwapChainForHwnd_));
    static MH_STATUS csccwStatus = MH_CreateHook(reinterpret_cast<void*>(fnCreateSwapChainForCWindowChain_), reinterpret_cast<void*>(&hkCreateSwapChainForCoreWindow), reinterpret_cast<void**>(&oCreateSwapChainForCoreWindow_));
    static MH_STATUS csccStatus = MH_CreateHook(reinterpret_cast<void*>(fnCreateSwapChainForCompChain_), reinterpret_cast<void*>(&hkCreateSwapChainForComposition), reinterpret_cast<void**>(&oCreateSwapChainForComposition_));

    static MH_STATUS presentStatus = MH_CreateHook(reinterpret_cast<void*>(fnPresent_), reinterpret_cast<void*>(&hkPresent), reinterpret_cast<void**>(&oPresent_));
    static MH_STATUS present1Status = MH_CreateHook(reinterpret_cast<void*>(fnPresent1_), reinterpret_cast<void*>(&hkPresent1), reinterpret_cast<void**>(&oPresent1_));

    static MH_STATUS resizeStatus = MH_CreateHook(reinterpret_cast<void*>(fnResizeBuffers_), reinterpret_cast<void*>(&hkResizeBuffers), reinterpret_cast<void**>(&oResizeBuffers_));
    static MH_STATUS setSourceSizeStatus = MH_CreateHook(reinterpret_cast<void*>(fnSetSourceSize_), reinterpret_cast<void*>(&hkSetSourceSize), reinterpret_cast<void**>(&oSetSourceSize_));
    static MH_STATUS resize1Status = MH_CreateHook(reinterpret_cast<void*>(fnResizeBuffers1_), reinterpret_cast<void*>(&hkResizeBuffers1), reinterpret_cast<void**>(&oResizeBuffers1_));

    static MH_STATUS eclStatus = MH_CreateHook(reinterpret_cast<void*>(fnExecuteCommandLists_), reinterpret_cast<void*>(&hkExecuteCommandLists), reinterpret_cast<void**>(&oExecuteCommandLists_));

    MH_EnableHook(fnCreateSwapChain_);
    MH_EnableHook(fnCreateSwapChainForHwndChain_);
    MH_EnableHook(fnCreateSwapChainForCWindowChain_);
    MH_EnableHook(fnCreateSwapChainForCompChain_);

    MH_EnableHook(fnPresent_);
    MH_EnableHook(fnPresent1_);

    MH_EnableHook(fnResizeBuffers_);
    MH_EnableHook(fnSetSourceSize_);
    MH_EnableHook(fnResizeBuffers1_);

    MH_EnableHook(fnExecuteCommandLists_);

    initOverlay();

    return true;
}

void D3DRenderer::unhook() {
    disableAll();
    if (oldWndProc_) {
        SetWindowLongPtr(gameWindow_, GWLP_WNDPROC, static_cast<LONG_PTR>(oldWndProc_));
        oldWndProc_ = 0;
    }
    if (ImGui::GetCurrentContext()) {
        if (ImGui::GetIO().BackendRendererUserData) {
            ImGui_ImplDX12_Shutdown();
            ImGui_ImplWin32_Shutdown();
        }
        ImGui::DestroyContext();
        descriptorHeap_->Release();
        descriptorHeap_ = nullptr;
        for (UINT i = 0; i < buffersCounts_; ++i) {
            commandAllocator_[i]->Release();
            backBuffer_[i]->Release();
        }
        rtvDescriptorHeap_->Release();
        rtvDescriptorHeap_ = nullptr;
        delete[] commandAllocator_;
        commandAllocator_ = nullptr;
        delete[] backBuffer_;
        backBuffer_ = nullptr;
    }
}

void D3DRenderer::disableAll() {
    MH_DisableHook(fnCreateSwapChain_);
    MH_DisableHook(fnCreateSwapChainForHwndChain_);
    MH_DisableHook(fnCreateSwapChainForCWindowChain_);
    MH_DisableHook(fnCreateSwapChainForCompChain_);

    MH_DisableHook(fnPresent_);
    MH_DisableHook(fnPresent1_);

    MH_DisableHook(fnResizeBuffers_);
    MH_DisableHook(fnResizeBuffers1_);

    MH_DisableHook(fnExecuteCommandLists_);
}

bool D3DRenderer::createDevice() {
    WNDCLASSEXW cls {};
    cls.cbSize = sizeof(WNDCLASSEX);
    cls.style = CS_HREDRAW | CS_VREDRAW;
    cls.lpfnWndProc = DefWindowProc;
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

    const HMODULE D3D12Module = GetModuleHandle("d3d12.dll");
    const HMODULE DXGIModule = GetModuleHandle("dxgi.dll");
    void *CreateDXGIFactory1;
    IDXGIAdapter *Adapter;
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

    if (dxgiFactory->EnumAdapters(0, &Adapter) == DXGI_ERROR_NOT_FOUND) {
        goto failed;
    }

    D3D12CreateDevice = reinterpret_cast<void *>(GetProcAddress(D3D12Module, "D3D12CreateDevice"));
    if (D3D12CreateDevice == nullptr) {
        goto failed;
    }

    if (reinterpret_cast<long (__stdcall *)(IUnknown *, D3D_FEATURE_LEVEL, const IID &, void **)>(D3D12CreateDevice)(
        Adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3dDevice)) != S_OK) {
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
            goto failed;
        }
        SwapChain->Release();
    }

    MH_Initialize();
    DestroyWindow(hwnd);
    UnregisterClassW(cls.lpszClassName, cls.hInstance);

    {
        void **swapChainVTable = *reinterpret_cast<void ***>(swapChain);
        void **commandQueueVTable = *reinterpret_cast<void ***>(d3dCommandQueue);
        void **dxgiFactoryVTable = *reinterpret_cast<void ***>(dxgiFactory);

        fnCreateSwapChain_ = dxgiFactoryVTable[10];
        fnCreateSwapChainForHwndChain_ = dxgiFactoryVTable[15];
        fnCreateSwapChainForCWindowChain_ = dxgiFactoryVTable[16];
        fnCreateSwapChainForCompChain_ = dxgiFactoryVTable[24];

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

void D3DRenderer::initOverlay() {
    if (ImGui::GetCurrentContext()) return;

    ImGui::CreateContext();
    initStyle();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;

    gameWindow_ = FindWindowW(L"ELDEN RING™", nullptr);
    ImGui_ImplWin32_Init(gameWindow_);
}

void D3DRenderer::overlay(IDXGISwapChain3 *pSwapChain) {
    if (commandQueue_ == nullptr)
        return;

    DXGI_SWAP_CHAIN_DESC sd;
    pSwapChain->GetDesc(&sd);

    if (!ImGui::GetIO().BackendRendererUserData) {
        ID3D12Device* device;
        if (pSwapChain->GetDevice(IID_PPV_ARGS(&device)) != S_OK)
            return;

        buffersCounts_ = sd.BufferCount;

        renderTargets_.clear();

        {
            D3D12_DESCRIPTOR_HEAP_DESC desc = {};
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            desc.NumDescriptors = 1;
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            if (device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap_)) != S_OK) {
                device->Release();
                return;
            }
        }
        {
            D3D12_DESCRIPTOR_HEAP_DESC desc = {};
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            desc.NumDescriptors = static_cast<UINT>(buffersCounts_);
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            desc.NodeMask = 1;
            if (device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&rtvDescriptorHeap_)) != S_OK) {
                descriptorHeap_->Release();
                device->Release();
                return;
            }

            const SIZE_T rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
            commandAllocator_ = new ID3D12CommandAllocator *[buffersCounts_];
            for (int i = 0; i < buffersCounts_; ++i) {
                renderTargets_.push_back(rtvHandle);
                rtvHandle.ptr += rtvDescriptorSize;
            }
            device->Release();
        }

        for (UINT i = 0; i < sd.BufferCount; ++i) {
            if (device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                IID_PPV_ARGS(&commandAllocator_[i])) != S_OK) {
                descriptorHeap_->Release();
                for (UINT j = 0; j < i; ++j) {
                    commandAllocator_[j]->Release();
                }
                rtvDescriptorHeap_->Release();
                delete[] commandAllocator_;
                device->Release();
                return;
            }
        }

        if (device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator_[0], nullptr,
                                       IID_PPV_ARGS(&commandList_)) != S_OK ||
            commandList_->Close() != S_OK) {
            descriptorHeap_->Release();
            for (UINT i = 0; i < buffersCounts_; ++i)
                commandAllocator_[i]->Release();
            rtvDescriptorHeap_->Release();
            delete[] commandAllocator_;
            device->Release();
            return;
        }

        loadFont();
        ImGui_ImplDX12_Init(device, static_cast<int>(buffersCounts_),
                            DXGI_FORMAT_R8G8B8A8_UNORM, descriptorHeap_,
                            descriptorHeap_->GetCPUDescriptorHandleForHeapStart(),
                            descriptorHeap_->GetGPUDescriptorHandleForHeapStart());
        ImGui::GetMainViewport()->PlatformHandleRaw = gameWindow_;
        oldWndProc_ = SetWindowLongPtr(gameWindow_, GWLP_WNDPROC, (LONG_PTR)WndProc);
    }
    if (!backBuffer_) {
        ID3D12Device* device;
        if (pSwapChain->GetDevice(IID_PPV_ARGS(&device)) != S_OK)
            return;
        backBuffer_ = new ID3D12Resource *[buffersCounts_];
        for (UINT i = 0; i < buffersCounts_; i++) {
            ID3D12Resource *buffer;
            if (pSwapChain->GetBuffer(i, IID_PPV_ARGS(&buffer)) != S_OK) {
                continue;
            }
            D3D12_RENDER_TARGET_VIEW_DESC desc = {};
            desc.Format = static_cast<DXGI_FORMAT>(GetCorrectDXGIFormat(sd.BufferDesc.Format));
            desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            device->CreateRenderTargetView(buffer, &desc, renderTargets_[i]);
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
        device->Release();
    }

    if (ImGui::GetCurrentContext() && commandQueue_) {
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        bool oldShowMenu = showMenu;
        for (auto &window: windows_)
            window->render(showMenu);
        if (showMenu != oldShowMenu) {
            gHooking->showMouseCursor(showMenu);
        }

        ImGui::EndFrame();

        UINT bufferIndex = pSwapChain->GetCurrentBackBufferIndex();
        ID3D12CommandAllocator *commandAllocator = commandAllocator_[bufferIndex];
        commandAllocator->Reset();

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = backBuffer_[bufferIndex];
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

        commandList_->Reset(commandAllocator, nullptr);
        commandList_->ResourceBarrier(1, &barrier);
        commandList_->OMSetRenderTargets(1, &renderTargets_[bufferIndex], FALSE, nullptr);
        commandList_->SetDescriptorHeaps(1, &descriptorHeap_);

        ImGui::Render();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList_);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        commandList_->ResourceBarrier(1, &barrier);
        commandList_->Close();

        commandQueue_->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList **>(&commandList_));
    }
}

LRESULT D3DRenderer::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui::GetCurrentContext())
        ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);

    return CallWindowProc(reinterpret_cast<WNDPROC>(gD3DRenderer->oldWndProc_), hWnd, msg, wParam, lParam);
}

D3DRenderer::~D3DRenderer() noexcept {
    unhook();
}

//	GUIDED HACKING TEMPLATE
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

//-----------------------------------------------------------------------------------
//									    STYLES
//-----------------------------------------------------------------------------------

void D3DRenderer::loadFont() {
    fontData_.clear();
    fontSize_ = strtof(gConfig.get("common.font_size", "18.0").c_str(), nullptr);
    if (fontSize_ == 0.0f) fontSize_ = 18.0f;
    const auto& fontFile = gConfig.getw("common.font", L"");
    if (fontFile.empty()) {
        return;
    }
    std::ifstream ifs((std::wstring(gModulePath) + L"\\data\\" + fontFile).c_str(), std::ios::in | std::ios::binary);
    if (!ifs) {
        fwprintf(stderr, L"Unable to load font file: %ls\n", fontFile.c_str());
        return;
    }
    ifs.seekg(0, std::ios::end);
    const std::streamsize size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    fontData_.resize(size);
    ifs.read((char*)fontData_.data(), size);
    ifs.close();

    const ImGuiIO& io = ImGui::GetIO();
    auto charset = gConfig["common.charset"];
    if (charset.empty()) {
        const auto &lang = er::getGameLanguage();
        if (lang == L"jpnJP") charset = "ja";
        else if (lang == L"korKR") charset = "ko";
        else if (lang == L"polPL") charset = "pl";
        else if (lang == L"rusRU") charset = "ru";
        else if (lang == L"thaTH") charset = "th";
        else if (lang == L"zhoCN") charset = "zh";
        else charset = "en";
    } else {
        charset = charset.substr(0, 2);
    }
    if (charset == "ja") {
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesJapanese());
        builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());
        static ImVector<ImWchar> jpRanges;
        builder.BuildRanges(&jpRanges);
        charsetRange_ = jpRanges.Data;
    }
    else if (charset == "ko") {
        charsetRange_ = io.Fonts->GetGlyphRangesKorean();
    }
    else if (charset == "pl") {
        ImFontGlyphRangesBuilder builder;
        static const ImWchar plRanges[] =
        {
            0x0020, 0x017F, // Basic Latin + Latin Supplement + Polish characters
            0,
        };
        charsetRange_ = plRanges;
    }
    else if (charset == "ru") {
        charsetRange_ = io.Fonts->GetGlyphRangesCyrillic();
    }
    else if (charset == "th") {
        static const ImWchar thaiRanges[] =
        {
            0x0020, 0x00FF, // Basic Latin
            0x2000, 0x205E, // Punctuations
            0x0E00, 0x0E7F, // Thai
            0,
        };
        charsetRange_ = thaiRanges;
    }
    else if (charset == "zh") {
        charsetRange_ = io.Fonts->GetGlyphRangesChineseFull();
    }
    else {
        charsetRange_ = io.Fonts->GetGlyphRangesDefault();
    }

    if (fontData_.empty()) {
        io.Fonts->AddFontDefault();
    } else {
        void *data = IM_ALLOC(fontData_.size());
        const auto fontSize = fontData_.size();
        memcpy(data, fontData_.data(), fontSize);
        io.Fonts->AddFontFromMemoryTTF(data, static_cast<int>(fontSize), fontSize_, nullptr, charsetRange_);
    }
}

void D3DRenderer::initStyle() {
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = ImGui::GetStyle().Colors;

    colors[ImGuiCol_Text] = ImVec4(0.75f, 0.75f, 0.29f, 0.90f);
    colors[ImGuiCol_TextDisabled] = colors[ImGuiCol_Text];//ImVec4(0.86f, 0.93f, 0.89f, 0.28f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.27f, 0.00f, 0.63f, 0.43f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.10f, 0.30f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.19f, 0.19f, 0.19f, 0.82f);
    colors[ImGuiCol_Border] = ImVec4(1.00f, 1.00f, 1.00f, 0.25f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.20f, 0.22f, 0.27f, 0.90f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.44f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.19f, 0.19f, 0.44f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.22f, 0.23f, 0.90f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.90f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.06f, 0.06f, 0.06f, 0.90f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.90f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 0.90f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.44f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.34f, 0.34f, 0.34f, 0.44f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.40f, 0.44f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.44f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.74f, 0.74f, 0.29f, 0.90f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.34f, 0.34f, 0.34f, 0.44f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.44f);
    colors[ImGuiCol_Button] = ImVec4(0.05f, 0.05f, 0.05f, 0.44f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.19f, 0.19f, 0.19f, 0.44f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.22f, 0.23f, 0.90f);
    colors[ImGuiCol_Header] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 0.00f, 0.00f, 0.36f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.22f, 0.23f, 0.33f);
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
    style.WindowBorderSize = 1;
    style.ChildBorderSize = 1;
    style.PopupBorderSize = 1;
    style.FrameBorderSize = 1;
    style.TabBorderSize = 1;
    style.WindowRounding = 7;
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
    return gD3DRenderer->oPresent_(pSwapChain, SyncInterval, Flags);
}

HRESULT WINAPI D3DRenderer::hkPresent1(IDXGISwapChain3 *pSwapChain,
                                       UINT SyncInterval,
                                       UINT PresentFlags,
                                       const DXGI_PRESENT_PARAMETERS *pPresentParameters) {
    gD3DRenderer->overlay(pSwapChain);
    return gD3DRenderer->oPresent1_(pSwapChain, SyncInterval, PresentFlags, pPresentParameters);
}

HRESULT WINAPI D3DRenderer::hkResizeBuffers(IDXGISwapChain *pSwapChain,
                                            UINT BufferCount,
                                            UINT Width,
                                            UINT Height,
                                            DXGI_FORMAT NewFormat,
                                            UINT SwapChainFlags) {
    gD3DRenderer->CleanupRenderTarget();
    return gD3DRenderer->oResizeBuffers_(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

HRESULT WINAPI D3DRenderer::hkSetSourceSize(IDXGISwapChain1 *pSwapChain, UINT Width, UINT Height) {
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
    gD3DRenderer->CleanupRenderTarget();
    return gD3DRenderer->oResizeBuffers1_(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags, pCreationNodeMask, ppPresentQueue);
}

void WINAPI D3DRenderer::hkExecuteCommandLists(ID3D12CommandQueue *pCommandQueue, UINT NumCommandLists, ID3D12CommandList *ppCommandLists) {
    if (!gD3DRenderer->commandQueue_)
        gD3DRenderer->commandQueue_ = pCommandQueue;
    return gD3DRenderer->oExecuteCommandLists_(pCommandQueue, NumCommandLists, ppCommandLists);
}

HRESULT WINAPI D3DRenderer::hkCreateSwapChain(IDXGIFactory *pFactory, IUnknown *pDevice, DXGI_SWAP_CHAIN_DESC *pDesc, IDXGISwapChain **ppSwapChain) {
    gD3DRenderer->CleanupRenderTarget();
    return gD3DRenderer->oCreateSwapChain_(pFactory, pDevice, pDesc, ppSwapChain);
}

HRESULT WINAPI D3DRenderer::hkCreateSwapChainForHwnd(IDXGIFactory *pFactory,
                                                     IUnknown *pDevice,
                                                     HWND hWnd,
                                                     const DXGI_SWAP_CHAIN_DESC1 *pDesc,
                                                     const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc,
                                                     IDXGIOutput *pRestrictToOutput,
                                                     IDXGISwapChain1 **ppSwapChain) {
    gD3DRenderer->CleanupRenderTarget();
    return gD3DRenderer->oCreateSwapChainForHwnd_(pFactory, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
}

HRESULT WINAPI D3DRenderer::hkCreateSwapChainForCoreWindow(IDXGIFactory *pFactory,
                                                           IUnknown *pDevice,
                                                           IUnknown *pWindow,
                                                           const DXGI_SWAP_CHAIN_DESC1 *pDesc,
                                                           IDXGIOutput *pRestrictToOutput,
                                                           IDXGISwapChain1 **ppSwapChain) {
    gD3DRenderer->CleanupRenderTarget();
    return gD3DRenderer->oCreateSwapChainForCoreWindow_(pFactory, pDevice, pWindow, pDesc, pRestrictToOutput, ppSwapChain);
}

HRESULT WINAPI D3DRenderer::hkCreateSwapChainForComposition(IDXGIFactory *pFactory,
                                                            IUnknown *pDevice,
                                                            const DXGI_SWAP_CHAIN_DESC1 *pDesc,
                                                            IDXGIOutput *pRestrictToOutput,
                                                            IDXGISwapChain1 **ppSwapChain) {
    gD3DRenderer->CleanupRenderTarget();
    return gD3DRenderer->oCreateSwapChainForComposition_(pFactory, pDevice, pDesc, pRestrictToOutput, ppSwapChain);
}

}
