#include <MinHook.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <algorithm>
#include <thread>
#include <string>

#include "scene.hpp"

namespace er {
namespace {
std::atomic<UiSceneBridge *> gSceneBridge{nullptr};
SRWLOCK gSceneGate = SRWLOCK_INIT;
std::atomic_bool gSceneModuleRetained{false};
[[nodiscard]] bool retainSceneModule() {
    if (gSceneModuleRetained.load(std::memory_order_acquire)) return true;
    HMODULE module = nullptr;
    // A thread can have entered either detour without owning gSceneGate, or
    // have released it while still returning through module code. MinHook's
    // IP fixups do not drain those windows. PIN is process-lifetime retention;
    // neither a bridge counter nor an arbitrary delay can prove safe unmapping.
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&retainSceneModule), &module))
        return false;
    gSceneModuleRetained.store(true, std::memory_order_release);
    return true;
}
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


bool UiSceneBridge::install(uintptr_t module, const SceneAddresses &addresses) {
    if (target_)
        return true;
    module_ = module;
    addresses_ = addresses;
    // Retain before either hook can become reachable, including a partially
    // successful installation that later rolls back. A failed PIN enables none.
    if (!retainSceneModule()) return false;
    MH_STATUS status = MH_OK;
    {
        SceneWriteLock lock;
        UiSceneBridge *expected = nullptr;
        if (!gSceneBridge.compare_exchange_strong(expected, this))
            return false;
        enqueue_ = reinterpret_cast<Enqueue>(module_ + addresses_.enqueue);
        auto *target = reinterpret_cast<void *>(module_ + addresses_.submit);
        auto *compositor = reinterpret_cast<void *>(module_ + addresses_.composite);
        gSubmitEntry = reinterpret_cast<Submit>(target);
        gCompositeEntry = reinterpret_cast<Composite>(compositor);
        status = MH_CreateHook(target, reinterpret_cast<void *>(&submit), reinterpret_cast<void **>(&original_));
        if (status == MH_OK) {
            target_ = target; // Record ownership before any enable can fail.
            status = MH_CreateHook(compositor, reinterpret_cast<void *>(&composite), reinterpret_cast<void **>(&originalComposite_));
            if (status == MH_OK) {
                compositeTarget_ = compositor;
                accepting_.store(true, std::memory_order_release);
                status = MH_EnableHook(compositor);
                if (status == MH_OK)
                    status = MH_EnableHook(target);
            }
        }
        if (status != MH_OK) accepting_.store(false, std::memory_order_release);
    }
    if (status != MH_OK) {
        // The C owner uses stop() for rollback after releasing the write gate.
        // Created hooks/trampolines remain owned until disable succeeds; a
        // partially enabled hook must never fall back into its still-patched entry.
        fwprintf(stderr, L"[EROverlay] UI_SCENE hook installation failed: %d\n", status);
        return false;
    }
    fwprintf(stderr, L"[EROverlay] UI_SCENE enabled: submit=+%X callback=+%X consumer=+%X composite=+%X; no DXGI hooks\n", addresses_.submit, addresses_.enqueue,
             addresses_.consume, addresses_.composite);
    return true;
}

bool UiSceneBridge::stop() {
    if (!target_ && !compositeTarget_ && gSceneBridge.load(std::memory_order_acquire) != this)
        return true;
    accepting_.store(false, std::memory_order_release);
    const auto submitStatus = target_ ? MH_DisableHook(target_) : MH_OK;
    const auto compositeStatus = compositeTarget_ ? MH_DisableHook(compositeTarget_) : MH_OK;
    // Each callback is consumed synchronously before original_ returns. There
    // is no registered game task or cross-frame callback left to retire.
    SceneWriteLock lock;
    if ((submitStatus != MH_OK && submitStatus != MH_ERROR_DISABLED) || (compositeStatus != MH_OK && compositeStatus != MH_ERROR_DISABLED)) {
        // Inert hooks must still have a bridge/trampoline if patch restoration
        // fails. accepting=false and this exclusive gate drain user callbacks;
        // the C owner keeps this small native bridge resident instead of freeing
        // storage that a remaining hook can access. Its user is never read again.
        fwprintf(stderr, L"[EROverlay] UI_SCENE stopped accepting callbacks; hook removal deferred (%d/%d), native bridge retained\n", submitStatus, compositeStatus);
        return false;
    }
    auto *expected = this;
    gSceneBridge.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    if (target_) MH_RemoveHook(target_);
    if (compositeTarget_)
        MH_RemoveHook(compositeTarget_);
    target_ = nullptr;
    compositeTarget_ = nullptr;
    fwprintf(stderr, L"[EROverlay] UI_SCENE stopped: queues=%llu callbacks=%llu\n", invocations_.load(), callbacks_.load());
    return true;
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
        if (true) {
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
    NativeFrame frame{};
    D3D12_VIEWPORT viewport{};
    frame.layer = static_cast<uint32_t>(layer);
    frame.sequence = sequence;
    uint32_t pixelFormat = 0;
    if (!swap || !view || !texture || !resource || !queue || !swapInterface || pointer(view) != module_ + a.renderViewVtable || pointer(texture) != module_ + a.textureVtable ||
        !read(view + 0x10, frame.target) || !frame.target || !read(view + 0x18, pixelFormat) || !read(context + a.viewportOffset, viewport)) {
        if (!reportedFrameFailure.exchange(true))
            fwprintf(stderr, L"[EROverlay] UI_SCENE frame rejected: game target/queue validation failed\n");
        return;
    }
    auto *commandQueue = reinterpret_cast<ID3D12CommandQueue *>(queue);
    auto *target = reinterpret_cast<ID3D12Resource *>(resource);
    const auto desc = target->GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.SampleDesc.Count != 1 || desc.Width > 16384 || desc.Height > 16384 ||
        !std::isfinite(viewport.Width) || !std::isfinite(viewport.Height) || viewport.TopLeftX != 0 || viewport.TopLeftY != 0 ||
        viewport.Width <= 0 || viewport.Height <= 0 || viewport.Width > desc.Width || viewport.Height > desc.Height ||
        commandQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
        return;
    frame.format = reinterpret_cast<uint32_t (*)(uint32_t)>(module_ + a.pixelFormat)(pixelFormat);
    if (frame.format != DXGI_FORMAT_R10G10B10A2_UNORM && !(layer == RenderLayer::BelowUI && frame.format == DXGI_FORMAT_R8G8B8A8_UNORM))
        return;
    ID3D12Device *device = nullptr;
    if (FAILED(target->GetDevice(IID_PPV_ARGS(&device)))) return;
    frame.device = device;
    frame.queue = commandQueue;
    frame.width = viewport.Width;
    frame.height = viewport.Height;
    callback_(user_, &frame);
    device->Release();
}
} // namespace er

extern "C" void *er_rust_scene_install(Callback callback, void *user, uintptr_t module, const SceneAddresses *addresses) noexcept {
    if (!callback || !module || !addresses) return nullptr;
    const auto status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) return nullptr;
    std::unique_ptr<er::UiSceneBridge> bridge;
    try {
        bridge = std::make_unique<er::UiSceneBridge>(callback, user);
        if (bridge->install(module, *addresses)) return bridge.release();
    } catch (...) {
        // Bridge allocation cannot unwind into Rust's extern C boundary.
    }
    // Partial-install rollback has the same ownership rule as normal stop.
    // If patch restoration fails, keep the accepting=false bridge/trampolines;
    // the exclusive gate has drained all accesses to the caller's user state.
    if (bridge && !bridge->stop()) bridge.release();
    return nullptr;
}
extern "C" void er_rust_scene_stop(void *opaque) noexcept {
    if (!opaque) return;
    auto *bridge = static_cast<er::UiSceneBridge *>(opaque);
    if (bridge->stop()) delete bridge;
    // Other hook users may share MinHook. Do not globally uninitialize it.
}
extern "C" bool er_rust_scene_module_retained() noexcept {
    return er::gSceneModuleRetained.load(std::memory_order_acquire);
}

#include "checks.inc"
