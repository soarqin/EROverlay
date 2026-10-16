#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d3d12.h>
#include <dxgi1_4.h>
#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

#include "plugin.hpp"
#include "util/uiscenescan.hpp"

namespace er {

class D3DRenderer;

[[nodiscard]] inline bool isUiSceneFormatSupported(DXGI_FORMAT format, RenderLayer layer = RenderLayer::AboveUI) {
    return format == DXGI_FORMAT_R10G10B10A2_UNORM || (layer == RenderLayer::BelowUI && format == DXGI_FORMAT_R8G8B8A8_UNORM);
}

// Every pointer/descriptor is borrowed only for this synchronous game callback.
// In particular the renderer must not retain a game back-buffer reference.
struct UiSceneFrame {
    ID3D12Device *device = nullptr;
    ID3D12CommandQueue *queue = nullptr;
    IDXGISwapChain3 *swapChain = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE target{};
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    D3D12_VIEWPORT viewport{};
    RenderLayer layer = RenderLayer::AboveUI;
    uint64_t sequence = 0;
};

class UiSceneBridge {
    friend struct NativeTextureVerifier;

public:
    explicit UiSceneBridge(D3DRenderer &renderer) : renderer_(renderer) {}
    ~UiSceneBridge() noexcept;
    UiSceneBridge(const UiSceneBridge &) = delete;
    UiSceneBridge(UiSceneBridge &&) = delete;
    UiSceneBridge &operator=(const UiSceneBridge &) = delete;
    UiSceneBridge &operator=(UiSceneBridge &&) = delete;

    [[nodiscard]] bool install();
    void stop();

private:
    struct Invocation;
    static void submit(uintptr_t drawSystem, void *queue);
    static void composite(uintptr_t drawSystem, uintptr_t context);
    static bool draw(uintptr_t context, Invocation *invocation, void *unused);
    void render(uintptr_t context, uintptr_t drawSystem, RenderLayer layer, uint64_t sequence);

    using Submit = void (*)(uintptr_t, void *);
    using Composite = void (*)(uintptr_t, uintptr_t);
    using Draw = bool (*)(uintptr_t, Invocation *, void *);
    using Enqueue = bool (*)(void *, Draw, Invocation *, void *, uint32_t);

    D3DRenderer &renderer_;
    util::uiscene::Addresses addresses_{};
    uintptr_t module_ = 0;
    void *target_ = nullptr;
    void *compositeTarget_ = nullptr;
    Submit original_ = nullptr;
    Composite originalComposite_ = nullptr;
    Enqueue enqueue_ = nullptr;
    std::atomic_bool accepting_{false};
    std::atomic_uint64_t invocations_{0};
    std::atomic_uint64_t callbacks_{0};
    std::array<std::atomic_bool, 2> reportedFrameFailures_{};
    std::atomic_bool reportedBoundaryFailure_{false};
    struct Boundary {
        uintptr_t drawSystem = 0, queue = 0, command = 0;
        uint32_t priority = 0, index = 0;
    };
    std::array<Boundary, 2> boundaries_{};
    std::mutex boundaryMutex_;
};

} // namespace er
