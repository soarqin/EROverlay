#include "d3drenderer.hpp"
#include "hooking.hpp"

IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace ER {
bool showMenu = FALSE;

//-----------------------------------------------------------------------------------
//									    D3DWindow
//-----------------------------------------------------------------------------------
static uint64_t *MethodsTable = nullptr;

bool D3DRenderer::hook() {
    if (!initHook()) {
        return false;
    }
    createHook(54, reinterpret_cast<void **>(&oExecuteCommandLists_), reinterpret_cast<void *>(HookExecuteCommandLists));
    createHook(140, reinterpret_cast<void **>(&oPresent_), reinterpret_cast<void *>(HookPresent));
    createHook(146, reinterpret_cast<void **>(&oResizeTarget_), reinterpret_cast<void *>(HookResizeTarget));
    return true;
}

void D3DRenderer::unhook() const {
    SetWindowLongPtr(gameWindow_, GWLP_WNDPROC, static_cast<LONG_PTR>(oldWndProc_));
    disableAll();
}

bool D3DRenderer::initHook() {
    if (initWindow() == false) {
        return false;
    }

    const HMODULE D3D12Module = GetModuleHandle("d3d12.dll");
    const HMODULE DXGIModule = GetModuleHandle("dxgi.dll");
    if (D3D12Module == nullptr || DXGIModule == nullptr) {
        deleteWindow();
        return false;
    }

    void *CreateDXGIFactory = reinterpret_cast<void *>(GetProcAddress(DXGIModule, "CreateDXGIFactory"));
    if (CreateDXGIFactory == nullptr) {
        deleteWindow();
        return false;
    }

    IDXGIFactory *Factory;
    if (reinterpret_cast<long (__stdcall *)(const IID &, void **)>(CreateDXGIFactory)(
        __uuidof(IDXGIFactory), (void **)&Factory) < 0) {
        deleteWindow();
        return false;
    }

    IDXGIAdapter *Adapter;
    if (Factory->EnumAdapters(0, &Adapter) == DXGI_ERROR_NOT_FOUND) {
        deleteWindow();
        return false;
    }

    void *D3D12CreateDevice = reinterpret_cast<void *>(GetProcAddress(D3D12Module, "D3D12CreateDevice"));
    if (D3D12CreateDevice == nullptr) {
        deleteWindow();
        return false;
    }

    ID3D12Device *Device;
    if (reinterpret_cast<long (__stdcall *)(IUnknown *, D3D_FEATURE_LEVEL, const IID &, void **)>(D3D12CreateDevice)(
        Adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void **)&Device) < 0) {
        deleteWindow();
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC QueueDesc;
    QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    QueueDesc.Priority = 0;
    QueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    QueueDesc.NodeMask = 0;

    ID3D12CommandQueue *CommandQueue;
    if (Device->CreateCommandQueue(&QueueDesc, __uuidof(ID3D12CommandQueue), (void **)&CommandQueue) < 0) {
        deleteWindow();
        return false;
    }

    ID3D12CommandAllocator *CommandAllocator;
    if (Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                                       (void **)&CommandAllocator) < 0) {
        deleteWindow();
        return false;
    }

    ID3D12GraphicsCommandList *CommandList;
    if (Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, CommandAllocator, nullptr,
                                  __uuidof(ID3D12GraphicsCommandList), (void **)&CommandList) < 0) {
        deleteWindow();
        return false;
    }

    DXGI_RATIONAL RefreshRate;
    RefreshRate.Numerator = 60;
    RefreshRate.Denominator = 1;

    DXGI_MODE_DESC BufferDesc;
    BufferDesc.Width = 100;
    BufferDesc.Height = 100;
    BufferDesc.RefreshRate = RefreshRate;
    BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

    DXGI_SAMPLE_DESC SampleDesc;
    SampleDesc.Count = 1;
    SampleDesc.Quality = 0;

    DXGI_SWAP_CHAIN_DESC SwapChainDesc = {};
    SwapChainDesc.BufferDesc = BufferDesc;
    SwapChainDesc.SampleDesc = SampleDesc;
    SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    SwapChainDesc.BufferCount = 2;
    SwapChainDesc.OutputWindow = windowHwnd_;
    SwapChainDesc.Windowed = 1;
    SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    SwapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    IDXGISwapChain *SwapChain;
    if (Factory->CreateSwapChain(CommandQueue, &SwapChainDesc, &SwapChain) < 0) {
        deleteWindow();
        return false;
    }

    MethodsTable = static_cast<uint64_t *>(::calloc(150, sizeof(uint64_t)));
    memcpy(MethodsTable, *(uint64_t **)Device, 44 * sizeof(uint64_t));
    memcpy(MethodsTable + 44, *(uint64_t **)CommandQueue, 19 * sizeof(uint64_t));
    memcpy(MethodsTable + 44 + 19, *(uint64_t **)CommandAllocator, 9 * sizeof(uint64_t));
    memcpy(MethodsTable + 44 + 19 + 9, *(uint64_t **)CommandList, 60 * sizeof(uint64_t));
    memcpy(MethodsTable + 44 + 19 + 9 + 60, *(uint64_t **)SwapChain, 18 * sizeof(uint64_t));
    Sleep(1000);

    //	INIT NOTICE
    Beep(300, 300);

    MH_Initialize();
    Device->Release();
    Device = nullptr;
    CommandQueue->Release();
    CommandQueue = nullptr;
    CommandAllocator->Release();
    CommandAllocator = nullptr;
    CommandList->Release();
    CommandList = nullptr;
    SwapChain->Release();
    SwapChain = nullptr;
    deleteWindow();
    return true;
}

void D3DRenderer::overlay(IDXGISwapChain *pSwapChain) {
    if (commandQueue_ == nullptr)
        return;

    ID3D12CommandQueue *pCmdQueue = this->commandQueue_;

    IDXGISwapChain3 *pSwapChain3 = nullptr;
    DXGI_SWAP_CHAIN_DESC sc_desc;
    pSwapChain->QueryInterface(IID_PPV_ARGS(&pSwapChain3));
    if (pSwapChain3 == nullptr)
        return;

    pSwapChain3->GetDesc(&sc_desc);

    if (!inited_) {
        UINT bufferIndex = pSwapChain3->GetCurrentBackBufferIndex();
        ID3D12Device *pDevice;
        if (pSwapChain3->GetDevice(IID_PPV_ARGS(&pDevice)) != S_OK)
            return;

        buffersCounts_ = sc_desc.BufferCount;

        renderTargets_.clear();

        {
            D3D12_DESCRIPTOR_HEAP_DESC desc = {};
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            desc.NumDescriptors = 1;
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            if (pDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap_)) != S_OK) {
                pDevice->Release();
                pSwapChain3->Release();
                return;
            }
        }
        {
            D3D12_DESCRIPTOR_HEAP_DESC desc = {};
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            desc.NumDescriptors = static_cast<UINT>(buffersCounts_);
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            desc.NodeMask = 1;
            if (pDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&rtvDescriptorHeap_)) != S_OK) {
                pDevice->Release();
                pSwapChain3->Release();
                descriptorHeap_->Release();
                return;
            }

            const SIZE_T rtvDescriptorSize = pDevice->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
            commandAllocator_ = new ID3D12CommandAllocator *[buffersCounts_];
            for (int i = 0; i < buffersCounts_; ++i) {
                renderTargets_.push_back(rtvHandle);
                rtvHandle.ptr += rtvDescriptorSize;
            }
        }

        for (UINT i = 0; i < sc_desc.BufferCount; ++i) {
            if (pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                IID_PPV_ARGS(&commandAllocator_[i])) != S_OK) {
                pDevice->Release();
                pSwapChain3->Release();
                descriptorHeap_->Release();
                for (UINT j = 0; j < i; ++j) {
                    commandAllocator_[j]->Release();
                }
                rtvDescriptorHeap_->Release();
                delete[]commandAllocator_;
                return;
            }
        }

        if (pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator_[0], nullptr,
                                       IID_PPV_ARGS(&commandList_)) != S_OK ||
            commandList_->Close() != S_OK) {
            pDevice->Release();
            pSwapChain3->Release();
            descriptorHeap_->Release();
            for (UINT i = 0; i < buffersCounts_; ++i)
                commandAllocator_[i]->Release();
            rtvDescriptorHeap_->Release();
            delete[]commandAllocator_;
            return;
        }

        backBuffer_ = new ID3D12Resource *[buffersCounts_];
        for (UINT i = 0; i < buffersCounts_; i++) {
            pSwapChain3->GetBuffer(i, IID_PPV_ARGS(&backBuffer_[i]));
            pDevice->CreateRenderTargetView(backBuffer_[i], nullptr, renderTargets_[i]);
        }

        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;

        gameWindow_ = FindWindowW(L"ELDEN RING™", nullptr);
        ImGui_ImplWin32_Init(gameWindow_);
        ImGui_ImplDX12_Init(pDevice, static_cast<int>(buffersCounts_), DXGI_FORMAT_R8G8B8A8_UNORM, nullptr,
                            descriptorHeap_->GetCPUDescriptorHandleForHeapStart(),
                            descriptorHeap_->GetGPUDescriptorHandleForHeapStart());
        ImGui_ImplDX12_CreateDeviceObjects();
        ImGui::GetIO().ImeWindowHandle = gameWindow_;
        oldWndProc_ = SetWindowLongPtr(gameWindow_, GWLP_WNDPROC, (LONG_PTR)WndProc);

        inited_ = true;

        pDevice->Release();
    }

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (showMenu) {
        ImGui::Begin("ELDEN RING INTERNAL DEBUG", &showMenu, 96);
        ImGui::Button("OK");
        ImGui::End();
    }

    ImGui::EndFrame();

    UINT bufferIndex = pSwapChain3->GetCurrentBackBufferIndex();

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = backBuffer_[bufferIndex];
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

    commandAllocator_[bufferIndex]->Reset();
    commandList_->Reset(commandAllocator_[bufferIndex], nullptr);
    commandList_->ResourceBarrier(1, &barrier);
    commandList_->OMSetRenderTargets(1, &renderTargets_[bufferIndex], FALSE, nullptr);
    commandList_->SetDescriptorHeaps(1, &descriptorHeap_);

    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList_);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    commandList_->ResourceBarrier(1, &barrier);
    commandList_->Close();

    pCmdQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList **>(&commandList_));

    pSwapChain3->Release();
}

HRESULT APIENTRY D3DRenderer::HookResizeTarget(IDXGISwapChain *_this, const DXGI_MODE_DESC *pNewTargetParameters) {
    gD3DRenderer->resetRenderState();
    return gD3DRenderer->oResizeTarget_(_this, pNewTargetParameters);
}

LRESULT D3DRenderer::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui::GetCurrentContext())
        ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);

    return CallWindowProc(reinterpret_cast<WNDPROC>(gD3DRenderer->oldWndProc_), hWnd, msg, wParam, lParam);
}

HRESULT APIENTRY D3DRenderer::HookPresent(IDXGISwapChain *pSwapChain, UINT SyncInterval, UINT Flags) {
    if (g_KillSwitch) {
        gHooking->unhook();
        gD3DRenderer->oPresent_(pSwapChain, SyncInterval, Flags);
        g_Running = FALSE;
        return 0;
    }
    gD3DRenderer->overlay(pSwapChain);
    return gD3DRenderer->oPresent_(pSwapChain, SyncInterval, Flags);
}

void D3DRenderer::HookExecuteCommandLists(ID3D12CommandQueue *queue, UINT NumCommandLists,
                                          ID3D12CommandList *ppCommandLists) {
    if (!gD3DRenderer->commandQueue_)
        gD3DRenderer->commandQueue_ = queue;

    gD3DRenderer->oExecuteCommandLists_(queue, NumCommandLists, ppCommandLists);
}

bool D3DRenderer::initWindow() {
    windowClass_.cbSize = sizeof(WNDCLASSEX);
    windowClass_.style = CS_HREDRAW | CS_VREDRAW;
    windowClass_.lpfnWndProc = DefWindowProc;
    windowClass_.cbClsExtra = 0;
    windowClass_.cbWndExtra = 0;
    windowClass_.hInstance = GetModuleHandle(nullptr);
    windowClass_.hIcon = nullptr;
    windowClass_.hCursor = nullptr;
    windowClass_.hbrBackground = nullptr;
    windowClass_.lpszMenuName = nullptr;
    windowClass_.lpszClassName = "MJ";
    windowClass_.hIconSm = nullptr;
    RegisterClassEx(&windowClass_);
    windowHwnd_ = CreateWindow(windowClass_.lpszClassName, "DirectX Window", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100,
                               NULL, NULL, windowClass_.hInstance, NULL);
    if (windowHwnd_ == nullptr) {
        return false;
    }
    return true;
}

void D3DRenderer::resetRenderState() {
    if (inited_) {
        descriptorHeap_->Release();
        for (UINT i = 0; i < buffersCounts_; ++i) {
            commandAllocator_[i]->Release();
            backBuffer_[i]->Release();
        }
        rtvDescriptorHeap_->Release();
        delete[]commandAllocator_;
        delete[]backBuffer_;

        ImGui_ImplDX12_Shutdown();
        //Windows_Hook::Inst()->resetRenderState();
        SetWindowLongPtr(gameWindow_, GWLP_WNDPROC, static_cast<LONG_PTR>(oldWndProc_));
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        inited_ = false;
    }
}

bool D3DRenderer::deleteWindow() const {
    DestroyWindow(windowHwnd_);
    UnregisterClass(windowClass_.lpszClassName, windowClass_.hInstance);
    if (windowHwnd_ != nullptr) {
        return false;
    }
    return true;
}

bool D3DRenderer::createHook(UINT16 Index, void **Original, void *Function) {
    void *target = (void *)MethodsTable[Index];
    if (MH_CreateHook(target, Function, Original) != MH_OK || MH_EnableHook(target) != MH_OK) {
        return false;
    }
    return true;
}

void D3DRenderer::disableHook(UINT16 Index) {
    MH_DisableHook((void *)MethodsTable[Index]);
}

void D3DRenderer::disableAll() {
    disableHook(54);
    disableHook(140);
    disableHook(146);
}

D3DRenderer::~D3DRenderer() noexcept {
    unhook();
}

//	GUIDED HACKING TEMPLATE
bool D3DRenderer::WorldToScreen(Vector3 pos, Vector2 &screen, float matrix[16], int windowWidth, int windowHeight) {
    //Matrix-vector Product, multiplying world(eye) coordinates by projection matrix = clipCoords
    Vector4 clipCoords;
    clipCoords.x = pos.x * matrix[0] + pos.y * matrix[1] + pos.z * matrix[2] + matrix[3];
    clipCoords.y = pos.x * matrix[4] + pos.y * matrix[5] + pos.z * matrix[6] + matrix[7];
    clipCoords.z = pos.x * matrix[8] + pos.y * matrix[9] + pos.z * matrix[10] + matrix[11];
    clipCoords.w = pos.x * matrix[12] + pos.y * matrix[13] + pos.z * matrix[14] + matrix[15];

    if (clipCoords.w < 0.1f)
        return false;

    //perspective division, dividing by clip.W = Normalized Device Coordinates
    Vector3 NDC;
    NDC.x = clipCoords.x / clipCoords.w;
    NDC.y = clipCoords.y / clipCoords.w;
    NDC.z = clipCoords.z / clipCoords.w;

    screen.x = (windowWidth / 2 * NDC.x) + (NDC.x + windowWidth / 2);
    screen.y = -(windowHeight / 2 * NDC.y) + (NDC.y + windowHeight / 2);
    return true;
}

//-----------------------------------------------------------------------------------
//									    STYLES
//-----------------------------------------------------------------------------------

void Styles::InitStyle() {
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = ImGui::GetStyle().Colors;

    colors[ImGuiCol_Separator] = ImVec4(0.28f, 0.28f, 0.28f, 0.90f);

    colors[ImGuiCol_Text] = ImVec4(0.75f, 0.75f, 0.29f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.86f, 0.93f, 0.89f, 0.28f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.27f, 0.00f, 0.63f, 0.43f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.10f, 0.70f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.19f, 0.19f, 0.19f, 0.92f);
    colors[ImGuiCol_Border] = ImVec4(1.00f, 1.00f, 1.00f, 0.25f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.40f, 0.54f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.74f, 0.74f, 0.29f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
    colors[ImGuiCol_Button] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 0.00f, 0.00f, 0.36f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.22f, 0.23f, 0.33f);
    //colors[ImGuiCol_Separator] = ImVec4(0.28f, 0.28f, 0.28f, 0.90f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.20f, 0.20f, 0.36f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_PlotLines] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
    colors[ImGuiCol_NavHighlight] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 0.00f, 0.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(1.00f, 0.00f, 0.00f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(1.00f, 0.00f, 0.00f, 0.35f);
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    style.WindowPadding = ImVec2(8.00f, 8.00f);
    style.FramePadding = ImVec2(5.00f, 2.00f);
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
}
