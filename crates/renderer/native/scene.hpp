#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

enum class RenderLayer : uint32_t { AboveUI = 0, BelowUI = 1 };
// Borrowed only until the synchronous callback returns. No game resource retained.
struct NativeFrame {
    void *device;
    void *queue;
    size_t target;
    uint32_t format;
    float width, height;
    uint32_t layer;
    uint64_t sequence;
};
using Callback = void (*)(void *, const NativeFrame *);

// Internal FFI only: copied from the Rust resolver before enabling any hook.
struct SceneAddresses {
    uint32_t submit, enqueue, consume, pixelFormat;
    uint32_t renderViewVtable, textureVtable;
    uint32_t swapChainsOffset, viewportOffset, contextEnvironmentOffset;
    uint32_t backendOffset, queueOffset;
    uint32_t composite, renderPostOffset;
    uint32_t queueIndexOffset, queuesOffset;
};
static_assert(sizeof(SceneAddresses) == 60);
static_assert(offsetof(SceneAddresses, queuesOffset) == 56);

namespace er {
class UiSceneBridge {
    friend struct NativeSceneVerifier;
public:
    explicit UiSceneBridge(Callback callback, void *user) : callback_(callback), user_(user) {}
    ~UiSceneBridge() noexcept { stop(); }
    bool install(uintptr_t module, const SceneAddresses &addresses);
    bool stop();
private:
    struct Invocation;
    static void submit(uintptr_t, void *);
    static void composite(uintptr_t, uintptr_t);
    static bool draw(uintptr_t, Invocation *, void *);
    void render(uintptr_t, uintptr_t, RenderLayer, uint64_t);
    using Submit = void (*)(uintptr_t, void *);
    using Composite = void (*)(uintptr_t, uintptr_t);
    using Draw = bool (*)(uintptr_t, Invocation *, void *);
    using Enqueue = bool (*)(void *, Draw, Invocation *, void *, uint32_t);
    Callback callback_;
    void *user_;
    SceneAddresses addresses_{};
    uintptr_t module_ = 0;
    void *target_ = nullptr, *compositeTarget_ = nullptr;
    Submit original_ = nullptr;
    Composite originalComposite_ = nullptr;
    Enqueue enqueue_ = nullptr;
    std::atomic_bool accepting_{false};
    std::atomic_uint64_t invocations_{0}, callbacks_{0};
    std::array<std::atomic_bool, 2> reportedFrameFailures_{};
    std::atomic_bool reportedBoundaryFailure_{false};
    struct Boundary { uintptr_t drawSystem = 0, queue = 0, command = 0; uint32_t priority = 0, index = 0; };
    std::array<Boundary, 2> boundaries_{};
    std::mutex boundaryMutex_;
};
}

static_assert(sizeof(NativeFrame) == 48);
static_assert(offsetof(NativeFrame, sequence) == 40);
