#pragma once

#include "imgui.h"
#include "util/vector.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <vector>
#include <memory>
#include <cstdint>

struct ImGui_ImplDX12_InitInfo;

namespace er {

class D3DRenderer {
/*
    struct FrameContext {
        ID3D12CommandAllocator *commandAllocator;
        ID3D12Resource *resource;
        D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle;
    };
*/
    friend class EROverlayAPIWrapper;
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

    bool createDevice();
    bool hook();
    void unhook();

    void disableAll();

    void initOverlay();
    void overlay(IDXGISwapChain3 *pSwapChain);

    static LRESULT WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

/*
    static bool WorldToScreen(Vector3 pos, Vector2 &screen, const float matrix[16], int windowWidth, int windowHeight);
*/

    void loadFont();
    static void initStyle();

    bool LoadTextureFromMemory(const void *data, size_t dataSize, D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle, ID3D12Resource **outTexResource, int *outWidth, int *outHeight);
    bool LoadTextureFromFile(const wchar_t *filename, D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle, ID3D12Resource **outTexResource, int *outWidth, int *outHeight);
    void DestroyTexture(ID3D12Resource **texResource, D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle);

private:
    void CleanupRenderTarget();

    static HRESULT WINAPI hkPresent(IDXGISwapChain3 *pSwapChain,
                                    UINT SyncInterval,
                                    UINT Flags);
    static HRESULT WINAPI hkPresent1(IDXGISwapChain3 *pSwapChain,
                                     UINT SyncInterval,
                                     UINT PresentFlags,
                                     const DXGI_PRESENT_PARAMETERS *pPresentParameters);
    static HRESULT WINAPI hkResizeBuffers(IDXGISwapChain *pSwapChain,
                                          UINT BufferCount,
                                          UINT Width,
                                          UINT Height,
                                          DXGI_FORMAT NewFormat,
                                          UINT SwapChainFlags);
    static HRESULT WINAPI hkSetSourceSize(IDXGISwapChain1 *pSwapChain,
                                          UINT Width,
                                          UINT Height);
    static HRESULT WINAPI hkResizeBuffers1(IDXGISwapChain3 *pSwapChain,
                                           UINT BufferCount,
                                           UINT Width,
                                           UINT Height,
                                           DXGI_FORMAT NewFormat,
                                           UINT SwapChainFlags,
                                           const UINT *pCreationNodeMask,
                                           IUnknown *const *ppPresentQueue);
    static void WINAPI hkExecuteCommandLists(ID3D12CommandQueue *pCommandQueue,
                                             UINT NumCommandLists,
                                             ID3D12CommandList *ppCommandLists);
    static HRESULT WINAPI hkCreateSwapChain(IDXGIFactory *pFactory,
                                            IUnknown *pDevice,
                                            DXGI_SWAP_CHAIN_DESC *pDesc,
                                            IDXGISwapChain **ppSwapChain);
    static HRESULT WINAPI hkCreateSwapChainForHwnd(IDXGIFactory *pFactory,
                                                   IUnknown *pDevice,
                                                   HWND hWnd,
                                                   const DXGI_SWAP_CHAIN_DESC1 *pDesc,
                                                   const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc,
                                                   IDXGIOutput *pRestrictToOutput,
                                                   IDXGISwapChain1 **ppSwapChain);
    static HRESULT WINAPI hkCreateSwapChainForCoreWindow(IDXGIFactory *pFactory,
                                                         IUnknown *pDevice,
                                                         IUnknown *pWindow,
                                                         const DXGI_SWAP_CHAIN_DESC1 *pDesc,
                                                         IDXGIOutput *pRestrictToOutput,
                                                         IDXGISwapChain1 **ppSwapChain);
    static HRESULT WINAPI hkCreateSwapChainForComposition(IDXGIFactory *pFactory,
                                                          IUnknown *pDevice,
                                                          const DXGI_SWAP_CHAIN_DESC1 *pDesc,
                                                          IDXGIOutput *pRestrictToOutput,
                                                          IDXGISwapChain1 **ppSwapChain);

    static void SrvDescriptorAlloc(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* pOutCpuDescHandle, D3D12_GPU_DESCRIPTOR_HANDLE* pOutGpuDescHandle);
    static void SrvDescriptorFree(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE hCpuDescHandle, D3D12_GPU_DESCRIPTOR_HANDLE hGpuDescHandle);
    void HeapDescriptorAlloc(D3D12_CPU_DESCRIPTOR_HANDLE* pOutCpuDescHandle, D3D12_GPU_DESCRIPTOR_HANDLE* pOutGpuDescHandle);
    void HeapDescriptorFree(D3D12_CPU_DESCRIPTOR_HANDLE hCpuDescHandle, D3D12_GPU_DESCRIPTOR_HANDLE hGpuDescHandle);

    std::vector<uint32_t> freeDescriptors_;

    std::add_pointer_t<HRESULT WINAPI(IDXGISwapChain3 *, UINT, UINT)> oPresent_;
    std::add_pointer_t<HRESULT WINAPI(IDXGISwapChain3 *, UINT, UINT, const DXGI_PRESENT_PARAMETERS *)> oPresent1_;
    std::add_pointer_t<HRESULT WINAPI(IDXGISwapChain *, UINT, UINT, UINT, DXGI_FORMAT, UINT)> oResizeBuffers_;
    std::add_pointer_t<HRESULT WINAPI(IDXGISwapChain1 *, UINT, UINT)> oSetSourceSize_;
    std::add_pointer_t<HRESULT WINAPI(IDXGISwapChain3 *, UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT *, IUnknown *const *)> oResizeBuffers1_;
    std::add_pointer_t<void WINAPI(ID3D12CommandQueue *, UINT, ID3D12CommandList *)> oExecuteCommandLists_;
    std::add_pointer_t<HRESULT WINAPI(IDXGIFactory *, IUnknown *, DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **)> oCreateSwapChain_;
    std::add_pointer_t<HRESULT WINAPI(IDXGIFactory *,
                                      IUnknown *,
                                      HWND,
                                      const DXGI_SWAP_CHAIN_DESC1 *,
                                      const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *,
                                      IDXGIOutput *,
                                      IDXGISwapChain1 **)> oCreateSwapChainForHwnd_;
    std::add_pointer_t<HRESULT WINAPI(IDXGIFactory *, IUnknown *, IUnknown *, const DXGI_SWAP_CHAIN_DESC1 *, IDXGIOutput *, IDXGISwapChain1 **)>
        oCreateSwapChainForCoreWindow_;
    std::add_pointer_t<HRESULT WINAPI(IDXGIFactory *, IUnknown *, const DXGI_SWAP_CHAIN_DESC1 *, IDXGIOutput *, IDXGISwapChain1 **)>
        oCreateSwapChainForComposition_;

    uint64_t oldWndProc_ = 0;

    void *fnCreateSwapChain_ = nullptr;
    void *fnCreateSwapChainForHwndChain_ = nullptr;
    void *fnCreateSwapChainForCWindowChain_ = nullptr;
    void *fnCreateSwapChainForCompChain_ = nullptr;

    void *fnPresent_ = nullptr;
    void *fnPresent1_ = nullptr;

    void *fnResizeBuffers_ = nullptr;
    void *fnSetSourceSize_ = nullptr;
    void *fnResizeBuffers1_ = nullptr;

    void *fnExecuteCommandLists_ = nullptr;

    HWND gameWindow_ = nullptr;

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

/*
    FrameContext *frameContext_ = nullptr;
*/
    float fontSize_ = 0.0f;
    const ImWchar *charsetRange_;
};

inline std::unique_ptr<D3DRenderer> gD3DRenderer;
}
