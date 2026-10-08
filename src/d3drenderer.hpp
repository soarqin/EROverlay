#pragma once

#include "imgui.h"
#include "nativeapi.h"
#include "util/assets.hpp"

#define WIN32_LEAN_AND_MEAN
#include <d3d12.h>
#include <dxgi1_4.h>
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

struct ImGui_ImplDX12_InitInfo;
struct ImDrawList;
struct ImDrawCmd;

namespace er {

struct OffscreenTarget {
    ID3D12Resource *texture = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvCpuHandle = {};
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle = {};
    int width = 0;
    int height = 0;
};

struct OffscreenContext {
    class D3DRenderer *renderer = nullptr;
    std::vector<OffscreenTarget> targets;
    UINT currentIndex = 0;
    bool local = false;
    ImVec2 origin{}, displayPos{}, displaySize{};
    int firstVertex = 0, firstCommand = 0;
};

class D3DRenderer {
    friend class EROverlayAPIWrapper;
    friend struct NativeTextureVerifier;

public:
    explicit D3DRenderer() = default;
    ~D3DRenderer() noexcept;
    D3DRenderer(D3DRenderer const &) = delete;
    D3DRenderer(D3DRenderer &&) = delete;
    D3DRenderer &operator=(D3DRenderer const &) = delete;
    D3DRenderer &operator=(D3DRenderer &&) = delete;

    [[nodiscard]] inline bool isForeground() const { return GetForegroundWindow() == gameWindow_; }

    [[nodiscard]] bool isDeviceLost() const { return deviceLost_; }

    bool createDevice();
    bool hook();
    void unhook();

    void disableAll();

    bool initOverlay();
    void overlay(IDXGISwapChain3 *pSwapChain);

    static LRESULT WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void loadFont();
    static void initStyle();

    bool LoadTextureFromMemory(const void *data, size_t dataSize, D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle, ID3D12Resource **outTexResource, int *outWidth, int *outHeight);
    bool LoadTextureFromFile(const wchar_t *filename, D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle, ID3D12Resource **outTexResource, int *outWidth, int *outHeight);
    void DestroyTexture(ID3D12Resource **texResource, D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle);
    [[nodiscard]] uint64_t createDdsTexture(const void *bytes, uint64_t size);
    [[nodiscard]] ERTextureStatus pollTexture(uint64_t token, ERTextureView &view);
    [[nodiscard]] uint64_t textureMemoryBytes(uint64_t token) const;
    void retireTexture(uint64_t token);

    // Offscreen rendering
    OffscreenContext *CreateOffscreen();
    void DestroyOffscreen(OffscreenContext *ctx);
    [[nodiscard]] bool BeginOffscreen(OffscreenContext *ctx);
    [[nodiscard]] bool BeginOffscreenRegion(OffscreenContext *ctx, float x, float y, float width, float height);
    void *EndOffscreen(OffscreenContext *ctx);

    static void BeginOffscreenCallback(const ImDrawList *list, const ImDrawCmd *cmd);
    static void EndOffscreenCallback(const ImDrawList *list, const ImDrawCmd *cmd);

private:
    void ensureOffscreenSize(OffscreenContext *ctx, int w, int h);
    [[nodiscard]] bool beginOffscreen(OffscreenContext *ctx, int width, int height);
    struct NativeTexture {
        std::vector<uint8_t> bytes;
        util::DdsImage image;
        ID3D12Resource *texture = nullptr;
        ID3D12Resource *upload = nullptr;
        ID3D12CommandAllocator *allocator = nullptr;
        ID3D12GraphicsCommandList *list = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = {};
        D3D12_GPU_DESCRIPTOR_HANDLE gpu = {};
        uint64_t uploadValue = 0;
        uint64_t drawValue = 0;
        uint64_t allocationBytes = 0;
        size_t uploadEstimate = 0, uploadBytes = 0;
        ERTextureStatus status = ER_TEXTURE_PENDING;
        bool retired = false;
        bool working = false;
    };
    struct RetiredTexture {
        ID3D12Resource *texture;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu;
        D3D12_GPU_DESCRIPTOR_HANDLE gpu;
        uint64_t drawValue;
    };
    [[nodiscard]] bool initializeTextureUpload();
    [[nodiscard]] bool submitTextureUpload(NativeTexture &texture);
    void processTextureUploads();
    void finishTextureFrame(bool submitted = true);
    void releaseTextureUploads();
    void waitForFrames();
    void deferTexture(ID3D12Resource *texture, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu);

private:
    [[nodiscard]] bool bindSwapChain(IDXGISwapChain3 *pSwapChain);
    [[nodiscard]] bool queueUsesDevice(IUnknown *deviceIdentity) const;
    [[nodiscard]] bool createDeviceObjects(IDXGISwapChain3 *pSwapChain, const DXGI_SWAP_CHAIN_DESC &sd, ID3D12Device *device, IUnknown *deviceIdentity);
    [[nodiscard]] bool createRenderTargets(IDXGISwapChain3 *pSwapChain, const DXGI_SWAP_CHAIN_DESC &sd);
    void renderFrame(IDXGISwapChain3 *pSwapChain);
    void releaseDeviceResources(const wchar_t *reason);
    void releaseCommandQueue();
    bool captureCommandQueue(IUnknown *pDevice);
    void handleSwapChainCreated(HWND hwnd, IUnknown *pDevice, HRESULT hr);
    bool installExecuteCommandListsHook();
    void releaseExecuteCommandListsHook();
    void resetCommandQueueCapture();
    void handleDeviceLost(const wchar_t *where, HRESULT hr);
    void CleanupRenderTarget();

    static HRESULT WINAPI hkPresent(IDXGISwapChain3 *pSwapChain, UINT SyncInterval, UINT Flags);
    static HRESULT WINAPI hkPresent1(IDXGISwapChain3 *pSwapChain, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS *pPresentParameters);
    static HRESULT WINAPI hkResizeBuffers(IDXGISwapChain *pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
    static HRESULT WINAPI hkResizeBuffers1(IDXGISwapChain3 *pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags,
                                           const UINT *pCreationNodeMask, IUnknown *const *ppPresentQueue);
    static void WINAPI hkExecuteCommandLists(ID3D12CommandQueue *pCommandQueue, UINT NumCommandLists, ID3D12CommandList *const *ppCommandLists);
    static HRESULT WINAPI hkCreateSwapChain(IDXGIFactory *pFactory, IUnknown *pDevice, DXGI_SWAP_CHAIN_DESC *pDesc, IDXGISwapChain **ppSwapChain);
    static HRESULT WINAPI hkCreateSwapChainForHwnd(IDXGIFactory *pFactory, IUnknown *pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1 *pDesc,
                                                   const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc, IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain);
    static void SrvDescriptorAlloc(ImGui_ImplDX12_InitInfo *info, D3D12_CPU_DESCRIPTOR_HANDLE *pOutCpuDescHandle, D3D12_GPU_DESCRIPTOR_HANDLE *pOutGpuDescHandle);
    static void SrvDescriptorFree(ImGui_ImplDX12_InitInfo *info, D3D12_CPU_DESCRIPTOR_HANDLE hCpuDescHandle, D3D12_GPU_DESCRIPTOR_HANDLE hGpuDescHandle);
    void HeapDescriptorAlloc(D3D12_CPU_DESCRIPTOR_HANDLE *pOutCpuDescHandle, D3D12_GPU_DESCRIPTOR_HANDLE *pOutGpuDescHandle);
    void HeapDescriptorFree(D3D12_CPU_DESCRIPTOR_HANDLE hCpuDescHandle, D3D12_GPU_DESCRIPTOR_HANDLE hGpuDescHandle);

    std::vector<uint32_t> freeDescriptors_;

    std::add_pointer_t<HRESULT WINAPI(IDXGISwapChain3 *, UINT, UINT)> oPresent_;
    std::add_pointer_t<HRESULT WINAPI(IDXGISwapChain3 *, UINT, UINT, const DXGI_PRESENT_PARAMETERS *)> oPresent1_;
    std::add_pointer_t<HRESULT WINAPI(IDXGISwapChain *, UINT, UINT, UINT, DXGI_FORMAT, UINT)> oResizeBuffers_;
    std::add_pointer_t<HRESULT WINAPI(IDXGISwapChain3 *, UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT *, IUnknown *const *)> oResizeBuffers1_;
    std::add_pointer_t<void WINAPI(ID3D12CommandQueue *, UINT, ID3D12CommandList *const *)> oExecuteCommandLists_;
    std::add_pointer_t<HRESULT WINAPI(IDXGIFactory *, IUnknown *, DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **)> oCreateSwapChain_;
    std::add_pointer_t<HRESULT WINAPI(IDXGIFactory *, IUnknown *, HWND, const DXGI_SWAP_CHAIN_DESC1 *, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *, IDXGIOutput *, IDXGISwapChain1 **)>
        oCreateSwapChainForHwnd_;
    uint64_t oldWndProc_ = 0;

    void *fnCreateSwapChain_ = nullptr;
    void *fnCreateSwapChainForHwndChain_ = nullptr;

    void *fnPresent_ = nullptr;
    void *fnPresent1_ = nullptr;

    void *fnResizeBuffers_ = nullptr;
    void *fnResizeBuffers1_ = nullptr;

    void *fnExecuteCommandLists_ = nullptr;

    HWND gameWindow_ = nullptr;

    IUnknown *swapChainIdentity_ = nullptr;
    IUnknown *deviceIdentity_ = nullptr;
    // Last validated Present target; not owned. Compared only by address.
    IDXGISwapChain3 *boundSwapChain_ = nullptr;
    ID3D12CommandQueue *boundQueue_ = nullptr;
    ID3D12Device *device_ = nullptr;
    ID3D12DescriptorHeap *descriptorHeap_ = nullptr;
    ID3D12DescriptorHeap *rtvDescriptorHeap_ = nullptr;
    ID3D12CommandAllocator **commandAllocator_ = nullptr;
    ID3D12GraphicsCommandList *commandList_ = nullptr;
    ID3D12CommandQueue *commandQueue_ = nullptr;
    ID3D12Resource **backBuffer_ = nullptr;
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> renderTargets_;

    uint32_t buffersCounts_ = 0;
    size_t rtvDescriptorSize_ = 0;
    size_t srvDescriptorSize_ = 0;

    float fontSize_ = 0.0f;
    const ImWchar *charsetRange_;
    bool deviceLost_ = false;
    bool hooksInstalled_ = false;
    bool eclHookInstalled_ = false;
    D3D12_CPU_DESCRIPTOR_HANDLE currentRTV_ = {};
    UINT currentBackBufferIndex_ = 0;
    ID3D12CommandQueue *uploadQueue_ = nullptr;
    ID3D12Fence *uploadFence_ = nullptr;
    ID3D12Fence *frameFence_ = nullptr;
    uint64_t uploadValue_ = 0;
    uint64_t frameValue_ = 0;
    // ImGui_ImplDX12_RenderDrawData calls; selects ImGui's per-frame buffers.
    uint64_t imguiFrame_ = 0;
    uint64_t nextTextureToken_ = 1;
    std::atomic_size_t queuedTextureBytes_{0};
    std::atomic_size_t nativeTextureCount_{0};
    std::atomic_bool texturesQueued_{false};
    std::atomic_bool acceptingTextures_{false};
    std::mutex textureQueueMutex_;
    std::unordered_map<uint64_t, NativeTexture> queuedTextures_;
    std::vector<uint64_t> textureWork_;
    size_t uploadingTextureBytes_ = 0;
    unsigned uploadingTextureCount_ = 0;
    std::vector<uint64_t> allocatorFences_;
    std::vector<uint64_t> imguiFences_;
    std::unordered_map<uint64_t, NativeTexture> nativeTextures_;
    std::vector<RetiredTexture> retiredTextures_;
    bool drawingFrame_ = false;
    std::recursive_mutex deviceMutex_;
};

inline std::unique_ptr<D3DRenderer> gD3DRenderer;
} // namespace er
