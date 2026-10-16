#pragma once

#include "imgui.h"
#include "nativeapi.h"
#include "plugin.hpp"
#include "util/assets.hpp"

#define WIN32_LEAN_AND_MEAN
#include <d3d12.h>
#include <dxgi1_4.h>
#include <windows.h>

#include <atomic>
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

struct ImGui_ImplDX12_InitInfo;
struct ImDrawList;
struct ImDrawCmd;

namespace er {

class UiSceneBridge;
class DrawLayers;
struct UiSceneFrame;

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
    OffscreenTarget target;
    bool local = false;
    ImVec2 origin{}, displayPos{}, displaySize{};
    int firstVertex = 0, firstCommand = 0;
};

class D3DRenderer {
    friend class EROverlayAPIWrapper;
    friend struct NativeTextureVerifier;

public:
    explicit D3DRenderer();
    ~D3DRenderer() noexcept;
    D3DRenderer(D3DRenderer const &) = delete;
    D3DRenderer(D3DRenderer &&) = delete;
    D3DRenderer &operator=(D3DRenderer const &) = delete;
    D3DRenderer &operator=(D3DRenderer &&) = delete;

    [[nodiscard]] inline bool isForeground() const { return GetForegroundWindow() == gameWindow_; }

    bool hook();
    void unhook();

    bool initOverlay();
    void overlayScene(const UiSceneFrame &frame);

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
    void destroyOffscreenTarget(OffscreenTarget &target);
    void ensureOffscreenSize(OffscreenTarget &target, int w, int h);
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
    [[nodiscard]] bool queueUsesDevice(IUnknown *deviceIdentity) const;
    [[nodiscard]] bool createDeviceObjects(UINT frameCount, ID3D12Device *device, IUnknown *deviceIdentity);
    void renderCommands(D3D12_CPU_DESCRIPTOR_HANDLE target, const D3D12_VIEWPORT &viewport, RenderLayer layer);
    void releaseDeviceResources();
    void releaseCommandQueue();
    void handleDeviceLost(const wchar_t *where, HRESULT hr);

    static void SrvDescriptorAlloc(ImGui_ImplDX12_InitInfo *info, D3D12_CPU_DESCRIPTOR_HANDLE *pOutCpuDescHandle, D3D12_GPU_DESCRIPTOR_HANDLE *pOutGpuDescHandle);
    static void SrvDescriptorFree(ImGui_ImplDX12_InitInfo *info, D3D12_CPU_DESCRIPTOR_HANDLE hCpuDescHandle, D3D12_GPU_DESCRIPTOR_HANDLE hGpuDescHandle);
    void HeapDescriptorAlloc(D3D12_CPU_DESCRIPTOR_HANDLE *pOutCpuDescHandle, D3D12_GPU_DESCRIPTOR_HANDLE *pOutGpuDescHandle);
    void HeapDescriptorFree(D3D12_CPU_DESCRIPTOR_HANDLE hCpuDescHandle, D3D12_GPU_DESCRIPTOR_HANDLE hGpuDescHandle);

    std::vector<uint32_t> freeDescriptors_;

    uint64_t oldWndProc_ = 0;

    HWND gameWindow_ = nullptr;

    IUnknown *deviceIdentity_ = nullptr;
    // Last validated game swap chain; not owned. Compared only by address.
    IDXGISwapChain3 *boundSwapChain_ = nullptr;
    ID3D12Device *device_ = nullptr;
    ID3D12DescriptorHeap *descriptorHeap_ = nullptr;
    ID3D12DescriptorHeap *rtvDescriptorHeap_ = nullptr;
    ID3D12CommandAllocator **commandAllocator_ = nullptr;
    ID3D12GraphicsCommandList *commandList_ = nullptr;
    ID3D12CommandQueue *commandQueue_ = nullptr;

    uint32_t frameCount_ = 0;
    size_t rtvDescriptorSize_ = 0;
    size_t srvDescriptorSize_ = 0;

    float fontSize_ = 0.0f;
    const ImWchar *charsetRange_;
    bool deviceLost_ = false;
    std::unique_ptr<UiSceneBridge> uiScene_;
    UINT sceneWidth_ = 0, sceneHeight_ = 0;
    DXGI_FORMAT sceneFormat_ = DXGI_FORMAT_UNKNOWN;
    std::unique_ptr<DrawLayers> drawLayers_;
    OffscreenTarget sceneUnderlay_;
    uint64_t sceneSequence_ = 0;
    DXGI_FORMAT targetFormat_ = DXGI_FORMAT_UNKNOWN;
    std::array<bool, 2> sceneFirstDrawPending_{};
    uint64_t sceneGeneration_ = 0;
    std::atomic_bool windowDestroyed_{false};
    D3D12_CPU_DESCRIPTOR_HANDLE currentRTV_ = {};
    UINT frameIndex_ = 0;
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
    std::unordered_map<uint64_t, NativeTexture> nativeTextures_;
    std::vector<RetiredTexture> retiredTextures_;
    bool drawingFrame_ = false;
    uint64_t textureDrawFence_ = 0;
    std::recursive_mutex deviceMutex_;
};

inline std::unique_ptr<D3DRenderer> gD3DRenderer;
} // namespace er
