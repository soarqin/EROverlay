#pragma once
#include "common.hpp"

namespace ER {
class D3DRenderer {
    struct _FrameContext {
        ID3D12CommandAllocator *CommandAllocator;
        ID3D12Resource *Resource;
        D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHandle;
    };

    typedef HRESULT (APIENTRY *Present12)(IDXGISwapChain *pSwapChain, UINT SyncInterval, UINT Flags);
    Present12 oPresent_ = NULL;

    typedef void (APIENTRY *ExecuteCommandLists)(ID3D12CommandQueue *queue, UINT NumCommandLists,
                                                 ID3D12CommandList *ppCommandLists);
    ExecuteCommandLists oExecuteCommandLists_ = nullptr;

    typedef HRESULT (APIENTRY *ResizeTarget)(IDXGISwapChain *_this, const DXGI_MODE_DESC *pNewTargetParameters);
    ResizeTarget oResizeTarget_ = nullptr;
    HWND gameWindow_ = nullptr;
    bool inited_ = false;

public:
    explicit D3DRenderer() = default;
    ~D3DRenderer() noexcept;
    D3DRenderer(D3DRenderer const &) = delete;
    D3DRenderer(D3DRenderer &&) = delete;
    D3DRenderer &operator=(D3DRenderer const &) = delete;
    D3DRenderer &operator=(D3DRenderer &&) = delete;

    static HRESULT APIENTRY HookPresent(IDXGISwapChain *pSwapChain, UINT SyncInterval, UINT Flags);
    static void HookExecuteCommandLists(ID3D12CommandQueue *queue, UINT NumCommandLists,
                                        ID3D12CommandList *ppCommandLists);
    static HRESULT APIENTRY HookResizeTarget(IDXGISwapChain *_this, const DXGI_MODE_DESC *pNewTargetParameters);
    void resetRenderState();

    bool initHook();
    bool hook();
    void unhook() const;

    bool initWindow();
    bool deleteWindow() const;

    static bool createHook(UINT16 Index, void **Original, void *Function);
    static void disableHook(UINT16 Index);
    static void disableAll();

    void overlay(IDXGISwapChain *pSwapChain);

    static LRESULT WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    static bool WorldToScreen(Vector3 pos, Vector2 &screen, float matrix[16], int windowWidth, int windowHeight);

    uint64_t oldWndProc_ = 0;

private:
    WNDCLASSEX windowClass_ {};
    HWND windowHwnd_ = nullptr;

    IDXGISwapChain3 *swapchain_ = nullptr;
    ID3D12Device *device_ = nullptr;

    ID3D12DescriptorHeap *descriptorHeap_ = nullptr;
    ID3D12DescriptorHeap *rtvDescriptorHeap_ = nullptr;
    ID3D12CommandAllocator **commandAllocator_ = nullptr;
    ID3D12GraphicsCommandList *commandList_ = nullptr;
    ID3D12CommandQueue *commandQueue_ = nullptr;
    ID3D12Resource **backBuffer_ = nullptr;
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> renderTargets_;

    uint64_t buffersCounts_ = -1;
    _FrameContext *frameContext_ = nullptr;
};

class Styles {
public:
    explicit Styles() = default;
    Styles(Styles const &) = delete;
    Styles(Styles &&) = delete;
    Styles &operator=(Styles const &) = delete;
    Styles &operator=(Styles &&) = delete;

    static void InitStyle();
};

inline std::unique_ptr<D3DRenderer> gD3DRenderer;
inline std::unique_ptr<Styles> gStyles;
}
