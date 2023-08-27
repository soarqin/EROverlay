#pragma once

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <vector>
#include <memory>

#include "renderbase.hpp"
#include "util/vector.hpp"

namespace er {

class D3DRenderer {
/*
    struct FrameContext {
        ID3D12CommandAllocator *commandAllocator;
        ID3D12Resource *resource;
        D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle;
    };
*/

    typedef HRESULT (APIENTRY *Present12)(IDXGISwapChain *pSwapChain, UINT SyncInterval, UINT Flags);
    Present12 oPresent_ = nullptr;

    typedef void (APIENTRY *ExecuteCommandLists)(ID3D12CommandQueue *queue, UINT NumCommandLists,
                                                 ID3D12CommandList *ppCommandLists);
    ExecuteCommandLists oExecuteCommandLists_ = nullptr;

    typedef HRESULT (APIENTRY *ResizeTarget)(IDXGISwapChain *thisObj, const DXGI_MODE_DESC *newTargetParameters);
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

    [[nodiscard]] inline bool isForeground() const {
        return GetForegroundWindow() == gameWindow_;
    }

    static HRESULT APIENTRY HookPresent(IDXGISwapChain *swapChain, UINT syncInterval, UINT flags);
    static void HookExecuteCommandLists(ID3D12CommandQueue *queue, UINT numCommandLists,
                                        ID3D12CommandList *commandLists);
    static HRESULT APIENTRY HookResizeTarget(IDXGISwapChain *thisObj, const DXGI_MODE_DESC *newTargetParameters);
    void resetRenderState();

    bool initHook();
    bool hook();
    void unhook() const;

    bool initWindow();
    bool deleteWindow() const;

    static bool createHook(UINT16 idx, void **original, void *function);
    static void disableHook(UINT16 idx);
    static void disableAll();

    void overlay(IDXGISwapChain *pSwapChain);

    static LRESULT WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

/*
    static bool WorldToScreen(Vector3 pos, Vector2 &screen, const float matrix[16], int windowWidth, int windowHeight);
*/

    template<typename T>
    std::enable_if_t<std::is_base_of<RenderBase, T>::value, void> registerWindow() {
        windows_.emplace_back(std::unique_ptr<RenderBase>(new T()));
    }
    void initStyle();

private:
    WNDCLASSEX windowClass_ {};
    HWND windowHwnd_ = nullptr;

    uint64_t oldWndProc_ = 0;

/*
    IDXGISwapChain3 *swapchain_ = nullptr;
    ID3D12Device *device_ = nullptr;
*/

    ID3D12DescriptorHeap *descriptorHeap_ = nullptr;
    ID3D12DescriptorHeap *rtvDescriptorHeap_ = nullptr;
    ID3D12CommandAllocator **commandAllocator_ = nullptr;
    ID3D12GraphicsCommandList *commandList_ = nullptr;
    ID3D12CommandQueue *commandQueue_ = nullptr;
    ID3D12Resource **backBuffer_ = nullptr;
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> renderTargets_;

    uint64_t buffersCounts_ = -1;
/*
    FrameContext *frameContext_ = nullptr;
*/
    std::vector<std::unique_ptr<RenderBase>> windows_;
    std::vector<uint8_t> fontData_;
};

inline std::unique_ptr<D3DRenderer> gD3DRenderer;
}
