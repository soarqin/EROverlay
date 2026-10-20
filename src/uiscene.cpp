#include <MinHook.h>

#include <cmath>
#include <cstdio>

#include "d3drenderer.hpp"
#include "uiscene.hpp"
#include "util/memory.hpp"

namespace er {
namespace {
std::atomic<UiSceneBridge *> gSceneBridge{nullptr};
SRWLOCK gSceneGate = SRWLOCK_INIT;
// Kept after hook removal so a thread already at the detour's entry can call
// the restored game entry without touching a retired bridge or trampoline.
void (*gSubmitEntry)(uintptr_t, void *) = nullptr;
void (*gCompositeEntry)(uintptr_t, uintptr_t) = nullptr;

struct SceneReadLock {
    SceneReadLock() { AcquireSRWLockShared(&gSceneGate); }
    ~SceneReadLock() noexcept { ReleaseSRWLockShared(&gSceneGate); }
};

struct SceneWriteLock {
    SceneWriteLock() { AcquireSRWLockExclusive(&gSceneGate); }
    ~SceneWriteLock() noexcept { ReleaseSRWLockExclusive(&gSceneGate); }
};

template<typename T>
[[nodiscard]] bool read(uintptr_t address, T &value) {
    SIZE_T count = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void *>(address), &value, sizeof(T), &count) && count == sizeof(T);
}

[[nodiscard]] uintptr_t pointer(uintptr_t address) {
    uintptr_t value = 0;
    return read(address, value) ? value : 0;
}

struct QueueEntry {
    uintptr_t command;
    uint32_t priority, padding;
};
static_assert(sizeof(QueueEntry) == 16);

[[nodiscard]] bool queueEntry(uintptr_t queue, uint32_t index, QueueEntry &entry) {
    uintptr_t entries = 0;
    uint32_t count = 0;
    return read(queue + 0x28, entries) && entries && read(queue + 0x30, count) && index < count && count <= 65536 && read(entries + size_t(index) * sizeof(QueueEntry), entry);
}
} // namespace

struct UiSceneBridge::Invocation {
    UiSceneBridge *bridge;
    uintptr_t drawSystem;
    unsigned callbacks = 0;
    uint64_t sequence = 0;
    RenderLayer layer = RenderLayer::AboveUI;
};

UiSceneBridge::~UiSceneBridge() noexcept { stop(); }

bool UiSceneBridge::install() {
    if (target_)
        return true;
    auto module = util::Module(nullptr);
    module_ = module.base().as<uintptr_t>();
    util::PeImage image({module.base().as<const uint8_t *>(), module.size()}, module_);
    const char *error = nullptr;
    if (!util::uiscene::resolve(image, addresses_, error)) {
        fwprintf(stderr, L"[EROverlay] UI_SCENE unavailable: %hs\n", error);
        return false;
    }
    SceneWriteLock lock;
    UiSceneBridge *expected = nullptr;
    if (!gSceneBridge.compare_exchange_strong(expected, this))
        return false;
    enqueue_ = reinterpret_cast<Enqueue>(module_ + addresses_.enqueue);
    auto *target = reinterpret_cast<void *>(module_ + addresses_.submit);
    auto *compositor = reinterpret_cast<void *>(module_ + addresses_.composite);
    gSubmitEntry = reinterpret_cast<Submit>(target);
    gCompositeEntry = reinterpret_cast<Composite>(compositor);
    MH_STATUS status = MH_CreateHook(target, reinterpret_cast<void *>(&submit), reinterpret_cast<void **>(&original_));
    if (status == MH_OK) {
        status = MH_CreateHook(compositor, reinterpret_cast<void *>(&composite), reinterpret_cast<void **>(&originalComposite_));
        if (status == MH_OK) {
            accepting_.store(true, std::memory_order_release);
            status = MH_EnableHook(compositor);
            if (status == MH_OK)
                status = MH_EnableHook(target);
            if (status != MH_OK) {
                MH_DisableHook(compositor);
                MH_RemoveHook(compositor);
            }
        }
        if (status != MH_OK)
            MH_RemoveHook(target);
    }
    if (status != MH_OK) {
        accepting_.store(false, std::memory_order_release);
        gSceneBridge.store(nullptr, std::memory_order_release);
        fwprintf(stderr, L"[EROverlay] UI_SCENE hook installation failed: %d\n", status);
        return false;
    }
    target_ = target;
    compositeTarget_ = compositor;
    fwprintf(stderr, L"[EROverlay] UI_SCENE enabled: submit=+%X callback=+%X consumer=+%X composite=+%X; no DXGI hooks\n", addresses_.submit, addresses_.enqueue,
             addresses_.consume, addresses_.composite);
    return true;
}

void UiSceneBridge::stop() {
    if (!target_)
        return;
    accepting_.store(false, std::memory_order_release);
    MH_DisableHook(target_);
    if (compositeTarget_)
        MH_DisableHook(compositeTarget_);
    // Each callback is consumed synchronously before original_ returns. There
    // is no registered game task or cross-frame callback left to retire.
    SceneWriteLock lock;
    gSceneBridge.store(nullptr, std::memory_order_release);
    MH_RemoveHook(target_);
    if (compositeTarget_)
        MH_RemoveHook(compositeTarget_);
    target_ = nullptr;
    compositeTarget_ = nullptr;
    fwprintf(stderr, L"[EROverlay] UI_SCENE stopped: queues=%llu callbacks=%llu\n", invocations_.load(), callbacks_.load());
}

void UiSceneBridge::composite(uintptr_t drawSystem, uintptr_t context) {
    SceneReadLock lock;
    auto *bridge = gSceneBridge.load(std::memory_order_acquire);
    if (!bridge) {
        gCompositeEntry(drawSystem, context);
        return;
    }
    const auto &a = bridge->addresses_;
    uint32_t slot = 2, count = 0, priorPriority = 0;
    uintptr_t queue = 0;
    bool track = bridge->accepting_.load(std::memory_order_acquire) && read(drawSystem + a.queueIndexOffset, slot) && slot < bridge->boundaries_.size();
    if (track) {
        queue = pointer(drawSystem + a.queuesOffset + slot * sizeof(uintptr_t));
        track = queue && read(queue + 0x30, count) && count < 65536 && read(queue + 0x38, priorPriority);
        std::lock_guard guard(bridge->boundaryMutex_);
        bridge->boundaries_[slot] = {};
    }
    bridge->originalComposite_(drawSystem, context);
    uint32_t after = 0;
    QueueEntry entry{};
    uint8_t opcode = 0;
    // Record the exact command emitted by BlendUI. Subsequent native work is
    // allowed: at submit we validate this entry again, never infer "last".
    if (track && read(queue + 0x30, after) && after == count + 1 && queueEntry(queue, count, entry) && read(entry.command, opcode) && opcode == 0 &&
        entry.priority > priorPriority && entry.priority - priorPriority > 1 && entry.priority < UINT32_MAX - 1) {
        std::lock_guard guard(bridge->boundaryMutex_);
        bridge->boundaries_[slot] = {drawSystem, queue, entry.command, entry.priority, count};
    }
}

void UiSceneBridge::submit(uintptr_t drawSystem, void *queue) {
    SceneReadLock lock;
    auto *bridge = gSceneBridge.load(std::memory_order_acquire);
    if (!bridge) {
        gSubmitEntry(drawSystem, queue);
        return;
    }
    Invocation invocation{bridge, drawSystem};
    Invocation below{bridge, drawSystem};
    below.layer = RenderLayer::BelowUI;
    bool belowQueued = false;
    if (queue && bridge->accepting_.load(std::memory_order_acquire)) {
        invocation.sequence = bridge->invocations_.fetch_add(1, std::memory_order_relaxed) + 1;
        below.sequence = invocation.sequence;
        if (invocation.sequence <= 4)
            fwprintf(stderr, L"[EROverlay] UI_SCENE queue %llu: enqueue before sort/consume; thread=%lu\n", invocation.sequence, GetCurrentThreadId());
        uint32_t priority = 0;
        {
            std::lock_guard guard(bridge->boundaryMutex_);
            for (auto &boundary: bridge->boundaries_) {
                if (boundary.drawSystem != drawSystem || boundary.queue != reinterpret_cast<uintptr_t>(queue))
                    continue;
                QueueEntry entry{};
                if (queueEntry(boundary.queue, boundary.index, entry) && entry.command == boundary.command && entry.priority == boundary.priority)
                    priority = boundary.priority - 1;
                boundary = {};
            }
        }
        if (pluginsUseLayer(RenderLayer::BelowUI)) {
            if (priority) {
                belowQueued = bridge->enqueue_(queue, &draw, &below, nullptr, priority);
                if (belowQueued)
                    bridge->reportedBoundaryFailure_ = false;
                if (invocation.sequence <= 4)
                    fwprintf(stderr, L"[EROverlay] UI_SCENE queue %llu: below-ui before native UI composition; priority=%08X\n", invocation.sequence, priority);
            }
            if (!belowQueued && !bridge->reportedBoundaryFailure_.exchange(true))
                fwprintf(stderr, L"[EROverlay] UI_SCENE below-ui skipped: no validated native UI composition boundary\n");
        }
        bridge->enqueue_(queue, &draw, &invocation, nullptr, UINT32_MAX - 1);
    }
    bridge->original_(drawSystem, queue);
    if (invocation.sequence && (invocation.sequence <= 4 || invocation.callbacks != 1 || (belowQueued && below.callbacks != 1)))
        fwprintf(stderr, L"[EROverlay] UI_SCENE queue %llu: consumed %u callback(s) (below-ui=%u above-ui=%u), returning to game\n", invocation.sequence,
                 invocation.callbacks + below.callbacks, below.callbacks, invocation.callbacks);
    if (invocation.sequence && (invocation.callbacks != 1 || (belowQueued && below.callbacks != 1)))
        bridge->accepting_.store(false, std::memory_order_release);
}

bool UiSceneBridge::draw(uintptr_t context, Invocation *invocation, void *unused) {
    UNREFERENCED_PARAMETER(unused);
    auto *bridge = invocation->bridge;
    if (++invocation->callbacks == 1 && bridge->accepting_.load(std::memory_order_acquire)) {
        bridge->callbacks_.fetch_add(1, std::memory_order_relaxed);
        if (invocation->sequence <= 4)
            fwprintf(stderr, L"[EROverlay] UI_SCENE queue %llu: callback after native batch flush; layer=%ls thread=%lu\n", invocation->sequence,
                     invocation->layer == RenderLayer::BelowUI ? L"below-ui" : L"above-ui", GetCurrentThreadId());
        bridge->render(context, invocation->drawSystem, invocation->layer, invocation->sequence);
    }
    // This command list belongs to Overlay. It does not alter the cached state
    // of the game's own command list, so no engine state reset is requested.
    return false;
}

void UiSceneBridge::render(uintptr_t context, uintptr_t drawSystem, RenderLayer layer, uint64_t sequence) {
    const auto &a = addresses_;
    auto &reportedFrameFailure = reportedFrameFailures_[layer == RenderLayer::BelowUI ? 1 : 0];
    uintptr_t swap = pointer(drawSystem + a.swapChainsOffset);
    // GXSwapChain and CGView are small ABI records. Their resource/view types
    // are checked with RTTI, and the resource offset is checked in RTV_CREATE.
    uintptr_t view = layer == RenderLayer::BelowUI ? pointer(pointer(pointer(swap + a.renderPostOffset) + 0x78)) : pointer(swap + 0x70);
    uintptr_t texture = pointer(view + 8);
    uintptr_t resource = pointer(texture + 0x20);
    uintptr_t environment = pointer(context + a.contextEnvironmentOffset);
    uintptr_t backend = pointer(environment + a.backendOffset);
    uintptr_t queue = pointer(backend + a.queueOffset);
    uintptr_t swapInterface = pointer(pointer(swap));
    UiSceneFrame frame{};
    frame.layer = layer;
    frame.sequence = sequence;
    uint32_t pixelFormat = 0;
    if (!swap || !view || !texture || !resource || !queue || !swapInterface || pointer(view) != module_ + a.renderViewVtable || pointer(texture) != module_ + a.textureVtable ||
        !read(view + 0x10, frame.target) || !frame.target.ptr || !read(view + 0x18, pixelFormat) || !read(context + a.viewportOffset, frame.viewport)) {
        if (!reportedFrameFailure.exchange(true))
            fwprintf(stderr, L"[EROverlay] UI_SCENE frame rejected: game target/queue validation failed\n");
        return;
    }
    frame.queue = reinterpret_cast<ID3D12CommandQueue *>(queue);
    auto *target = reinterpret_cast<ID3D12Resource *>(resource);
    auto desc = target->GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.SampleDesc.Count != 1 || desc.Width > 16384 || desc.Height > 16384 || !std::isfinite(frame.viewport.Width) ||
        !std::isfinite(frame.viewport.Height) || frame.viewport.TopLeftX != 0 || frame.viewport.TopLeftY != 0 || frame.viewport.Width <= 0 || frame.viewport.Height <= 0 ||
        frame.viewport.Width > desc.Width || frame.viewport.Height > desc.Height || frame.queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
        return;
    frame.format = static_cast<DXGI_FORMAT>(reinterpret_cast<uint32_t (*)(uint32_t)>(module_ + a.pixelFormat)(pixelFormat));
    if (!isUiSceneFormatSupported(frame.format, layer)) {
        if (!reportedFrameFailure.exchange(true))
            fwprintf(stderr, L"[EROverlay] UI_SCENE unsupported target format=%u layer=%ls\n", unsigned(frame.format), layer == RenderLayer::BelowUI ? L"below-ui" : L"above-ui");
        return;
    }
    if (FAILED(target->GetDevice(IID_PPV_ARGS(&frame.device))))
        return;
    if (SUCCEEDED(reinterpret_cast<IUnknown *>(swapInterface)->QueryInterface(IID_PPV_ARGS(&frame.swapChain)))) {
        renderer_.overlayScene(frame);
        frame.swapChain->Release();
        reportedFrameFailure = false;
    }
    frame.device->Release();
}

} // namespace er
