// Controlled offline fixture. Calls the production detours and stop() against local function entries.
namespace er {
struct NativeSceneVerifier {
    struct LayerQueue {
        uint8_t padding[0x28]{};
        QueueEntry *entries = records.data();
        uint32_t count = 0, unused = 0, lastPriority = 0, tail = 0;
        struct Command {
            uint8_t opcode = 0;
            char tag = 0;
            UiSceneBridge::Draw callback = nullptr;
            UiSceneBridge::Invocation *data = nullptr;
        };
        std::array<QueueEntry, 16> records{};
        std::array<Command, 16> commands{};
        std::string order;
        uintptr_t context = 0;
        void (*nativeCommand)(void *) = nullptr;
        void *nativeUser = nullptr;
        void append(uint8_t opcode, char tag, uint32_t priority, UiSceneBridge::Draw callback = nullptr, UiSceneBridge::Invocation *data = nullptr) {
            commands[count] = {opcode, tag, callback, data};
            records[count] = {reinterpret_cast<uintptr_t>(&commands[count]), priority, 0};
            ++count;
            lastPriority = std::max(lastPriority, priority);
        }
    };
    static void compositeFixture(uintptr_t drawSystem, uintptr_t) {
        auto &queue = *reinterpret_cast<LayerQueue *>(pointer(drawSystem + 0x20));
        queue.append(0, 'U', queue.lastPriority + 0x800000);
    }
    static bool enqueueLayerFixture(void *queue, UiSceneBridge::Draw callback, UiSceneBridge::Invocation *data, void *, uint32_t priority) {
        static_cast<LayerQueue *>(queue)->append(2, data->layer == RenderLayer::BelowUI ? 'L' : 'H', priority, callback, data);
        return true;
    }
    static void consumeLayerFixture(uintptr_t, void *opaque) {
        auto &queue = *static_cast<LayerQueue *>(opaque);
        // Teardown case: consume callbacks normally while drawing is stopped.
        gSceneBridge.load()->accepting_ = false;
        std::sort(queue.records.begin(), queue.records.begin() + queue.count, [](const auto &a, const auto &b) { return a.priority < b.priority; });
        for (uint32_t i = 0; i < queue.count; ++i) {
            auto &command = *reinterpret_cast<LayerQueue::Command *>(queue.records[i].command);
            queue.order += command.tag;
            if (command.callback)
                command.callback(0, command.data, nullptr);
        }
    }
    static bool boundaryCheck() {
        static_assert(offsetof(LayerQueue, entries) == 0x28 && offsetof(LayerQueue, count) == 0x30 && offsetof(LayerQueue, lastPriority) == 0x38);
        UiSceneBridge bridge(nullptr, nullptr);
        bridge.addresses_.queueIndexOffset = 0x10;
        bridge.addresses_.queuesOffset = 0x20;
        bridge.originalComposite_ = &compositeFixture;
        bridge.original_ = &consumeLayerFixture;
        bridge.enqueue_ = &enqueueLayerFixture;
        gSceneBridge = &bridge;
        bool ok = true;
        for (int stale = 0; stale < 3; ++stale) {
            LayerQueue queue;
            std::array<uintptr_t, 16> drawSystem{};
            drawSystem[0x20 / sizeof(uintptr_t)] = reinterpret_cast<uintptr_t>(&queue);
            bridge.accepting_ = true;
            queue.append(0, 'S', 0x800000);
            if (stale != 2)
                UiSceneBridge::composite(reinterpret_cast<uintptr_t>(drawSystem.data()), 0);
            if (stale == 1)
                queue.records[1].priority += 10; // Replaced entry must not reuse the boundary.
            queue.append(0, 'T', queue.lastPriority + 0x800000);
            UiSceneBridge::submit(reinterpret_cast<uintptr_t>(drawSystem.data()), &queue);
            ok &= queue.order == (stale == 0 ? "SLUTH" : stale == 1 ? "SUTH" : "STH");
        }
        gSceneBridge = nullptr;
        std::puts(ok ? "PASS: exact native UI command boundary survives later native work; stale/missing boundaries suppress only the lower stage."
                     : "FAIL: native UI command boundary/order fixture");
        return ok;
    }
    static uint32_t fixturePixelFormat(uint32_t format) { return format; }
    static void consumeFrameFixture(uintptr_t, void *opaque) {
        auto &queue = *static_cast<LayerQueue *>(opaque);
        std::sort(queue.records.begin(), queue.records.begin() + queue.count, [](const auto &a, const auto &b) { return a.priority < b.priority; });
        for (uint32_t i = 0; i < queue.count; ++i) {
            auto &command = *reinterpret_cast<LayerQueue::Command *>(queue.records[i].command);
            queue.order += command.tag;
            if (command.tag == 'U' && queue.nativeCommand)
                queue.nativeCommand(queue.nativeUser);
            if (command.callback)
                command.callback(queue.context, command.data, nullptr);
        }
    }
    static bool driveFrame(Callback callback, void *user, const NativeFrame &above, void *aboveResource,
                           const NativeFrame &below, void *belowResource, void (*nativeCommand)(void *), void *nativeUser, uint32_t scenario) {
        if (!callback || !aboveResource || !belowResource || !nativeCommand || scenario > 4 || gSceneBridge.load()) return false;
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&fixturePixelFormat), &module)) return false;
        UiSceneBridge bridge(callback, user);
        bridge.module_ = reinterpret_cast<uintptr_t>(module);
        auto localFunction = reinterpret_cast<uintptr_t>(&fixturePixelFormat);
        if (localFunction < bridge.module_ || localFunction - bridge.module_ > UINT32_MAX) return false;
        bridge.addresses_.pixelFormat = static_cast<uint32_t>(localFunction - bridge.module_);
        bridge.addresses_.renderViewVtable = bridge.addresses_.textureVtable = bridge.addresses_.pixelFormat;
        bridge.addresses_.queueIndexOffset = 0x10;
        bridge.addresses_.queuesOffset = 0x20;
        bridge.addresses_.swapChainsOffset = 0x40;
        bridge.addresses_.renderPostOffset = 0x80;
        bridge.addresses_.viewportOffset = 0x20;
        bridge.addresses_.contextEnvironmentOffset = 0x10;
        bridge.addresses_.backendOffset = bridge.addresses_.queueOffset = 0x10;
        bridge.originalComposite_ = &compositeFixture;
        bridge.original_ = &consumeFrameFixture;
        bridge.enqueue_ = &enqueueLayerFixture;
        bridge.invocations_ = above.sequence ? above.sequence - 1 : 0;
        bridge.accepting_ = true;
        std::array<uintptr_t, 32> drawSystem{}, swap{}, post{}, belowHolder{}, context{}, environment{}, backend{};
        std::array<uintptr_t, 8> aboveView{}, belowView{}, aboveTexture{}, belowTexture{};
        LayerQueue queue;
        drawSystem[0x20 / 8] = reinterpret_cast<uintptr_t>(&queue);
        drawSystem[0x40 / 8] = reinterpret_cast<uintptr_t>(swap.data());
        swap[0] = reinterpret_cast<uintptr_t>(swap.data()); // Only non-null interface identity is read.
        swap[0x70 / 8] = reinterpret_cast<uintptr_t>(aboveView.data());
        swap[0x80 / 8] = reinterpret_cast<uintptr_t>(post.data());
        post[0x78 / 8] = reinterpret_cast<uintptr_t>(belowHolder.data());
        belowHolder[0] = reinterpret_cast<uintptr_t>(belowView.data());
        for (auto pair: {std::pair{aboveView.data(), aboveTexture.data()}, std::pair{belowView.data(), belowTexture.data()}}) {
            pair.first[0] = pair.second[0] = localFunction;
            pair.first[1] = reinterpret_cast<uintptr_t>(pair.second);
        }
        aboveView[0x10 / 8] = above.target;
        belowView[0x10 / 8] = below.target;
        aboveView[0x18 / 8] = above.format;
        belowView[0x18 / 8] = below.format;
        aboveTexture[0x20 / 8] = reinterpret_cast<uintptr_t>(aboveResource);
        belowTexture[0x20 / 8] = reinterpret_cast<uintptr_t>(belowResource);
        context[0x10 / 8] = reinterpret_cast<uintptr_t>(environment.data());
        environment[0x10 / 8] = reinterpret_cast<uintptr_t>(backend.data());
        backend[0x10 / 8] = reinterpret_cast<uintptr_t>(above.queue);
        D3D12_VIEWPORT viewport{0, 0, above.width, above.height, 0, 1};
        if (above.queue != below.queue || above.width != below.width || above.height != below.height) return false;
        std::memcpy(reinterpret_cast<uint8_t *>(context.data()) + 0x20, &viewport, sizeof(viewport));
        queue.context = reinterpret_cast<uintptr_t>(context.data());
        queue.nativeCommand = nativeCommand;
        queue.nativeUser = nativeUser;
        gSceneBridge = &bridge;
        struct ClearFixtureBridge {
            ~ClearFixtureBridge() { gSceneBridge = nullptr; }
        } clearBridge;
        const auto draw = reinterpret_cast<uintptr_t>(drawSystem.data());
        queue.append(0, 'S', 0x800000);
        if (scenario != 1 && scenario != 4)
            UiSceneBridge::composite(draw, queue.context);
        else
            compositeFixture(draw, queue.context);
        if (scenario == 2) queue.records[1].priority += 10;
        if (scenario == 3) UiSceneBridge::composite(draw, queue.context);
        queue.append(0, 'T', queue.lastPriority + 0x800000);
        UiSceneBridge::submit(draw, &queue);
        return bridge.accepting_.load() && bridge.callbacks_.load() == ((scenario == 1 || scenario == 2 || scenario == 4) ? 1u : 2u);
    }
    struct QueueFixture {
        UiSceneBridge::Draw callback = nullptr;
        UiSceneBridge::Invocation *data = nullptr;
        HANDLE entered = nullptr, resume = nullptr;
        unsigned callbacks = 0;
        unsigned compositeCalls = 0;
    };
    static bool enqueueFixture(void *queue, UiSceneBridge::Draw callback, UiSceneBridge::Invocation *data, void *, uint32_t priority) {
        auto &fixture = *static_cast<QueueFixture *>(queue);
        if (priority != UINT32_MAX - 1)
            return false;
        fixture.callback = callback;
        fixture.data = data;
        return true;
    }
    __declspec(noinline) static void submitFixture(uintptr_t, void *queue) {
        auto &fixture = *static_cast<QueueFixture *>(queue);
        SetEvent(fixture.entered);
        WaitForSingleObject(fixture.resume, 5000);
        if (fixture.callback) {
            ++fixture.callbacks;
            fixture.callback(0, fixture.data, nullptr);
        }
    }
    __declspec(noinline) static void compositeLifecycleFixture(uintptr_t, uintptr_t opaque) {
        auto &fixture = *reinterpret_cast<QueueFixture *>(opaque);
        SetEvent(fixture.entered);
        WaitForSingleObject(fixture.resume, 5000);
        ++fixture.compositeCalls;
    }
    struct OutsideGatePause {
        HANDLE entered = nullptr, resume = nullptr;
        std::atomic<DWORD> producer{0};
        bool afterUnlock = false;
    };
    inline static std::atomic<OutsideGatePause *> outsidePause{nullptr};
    inline static void (WINAPI *originalAcquireShared)(PSRWLOCK) = nullptr;
    inline static void (WINAPI *originalReleaseShared)(PSRWLOCK) = nullptr;
    static void pauseOutsideGate(PSRWLOCK gate, bool afterUnlock) {
        auto *pause = outsidePause.load(std::memory_order_acquire);
        if (pause && gate == &gSceneGate && pause->afterUnlock == afterUnlock && pause->producer.load(std::memory_order_acquire) == GetCurrentThreadId()) {
            SetEvent(pause->entered);
            WaitForSingleObject(pause->resume, 5000);
        }
    }
    static void WINAPI acquireSharedFixture(PSRWLOCK gate) {
        pauseOutsideGate(gate, false);
        originalAcquireShared(gate);
    }
    static void WINAPI releaseSharedFixture(PSRWLOCK gate) {
        originalReleaseShared(gate);
        pauseOutsideGate(gate, true);
    }
    static bool outsideGateCheck() {
        const auto status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) return false;
        if (!retainSceneModule()) return false;
        bool ok = true;
        // Hook the lock entry in this controlled process, not a production
        // hot-path probe. The thread has already entered the real scene detour
        // when paused before acquire or after release, but owns no read lock.
        for (int repeat = 0; repeat < 3; ++repeat) {
            for (int window = 0; window < 4; ++window) {
                UiSceneBridge bridge(nullptr, nullptr);
                QueueFixture fixture;
                OutsideGatePause pause;
                fixture.entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                fixture.resume = CreateEventW(nullptr, TRUE, TRUE, nullptr);
                pause.entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                pause.resume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                if (!fixture.entered || !fixture.resume || !pause.entered || !pause.resume) return false;
                pause.afterUnlock = (window & 1) != 0;
                bridge.target_ = reinterpret_cast<void *>(&submitFixture);
                bridge.compositeTarget_ = reinterpret_cast<void *>(&compositeLifecycleFixture);
                bridge.enqueue_ = &enqueueFixture;
                bridge.accepting_.store(true);
                gSubmitEntry = &submitFixture;
                gCompositeEntry = &compositeLifecycleFixture;
                gSceneBridge.store(&bridge);
                auto *lockEntry = pause.afterUnlock ? reinterpret_cast<void *>(&ReleaseSRWLockShared) : reinterpret_cast<void *>(&AcquireSRWLockShared);
                auto *lockDetour = pause.afterUnlock ? reinterpret_cast<void *>(&releaseSharedFixture) : reinterpret_cast<void *>(&acquireSharedFixture);
                auto **lockOriginal = pause.afterUnlock ? reinterpret_cast<void **>(&originalReleaseShared) : reinterpret_cast<void **>(&originalAcquireShared);
                bool hooked = MH_CreateHook(bridge.target_, reinterpret_cast<void *>(&UiSceneBridge::submit), reinterpret_cast<void **>(&bridge.original_)) == MH_OK &&
                              MH_CreateHook(bridge.compositeTarget_, reinterpret_cast<void *>(&UiSceneBridge::composite), reinterpret_cast<void **>(&bridge.originalComposite_)) == MH_OK &&
                              MH_CreateHook(lockEntry, lockDetour, lockOriginal) == MH_OK && MH_EnableHook(lockEntry) == MH_OK &&
                              MH_EnableHook(bridge.target_) == MH_OK && MH_EnableHook(bridge.compositeTarget_) == MH_OK;
                if (!hooked) {
                    MH_DisableHook(lockEntry);
                    MH_RemoveHook(lockEntry);
                    bridge.stop();
                    for (auto event: {fixture.entered, fixture.resume, pause.entered, pause.resume}) CloseHandle(event);
                    return false;
                }
                outsidePause.store(&pause, std::memory_order_release);
                std::atomic_bool exited{false};
                std::thread producer([&] {
                    pause.producer = GetCurrentThreadId();
                    if (window < 2) submitFixture(0, &fixture);
                    else compositeLifecycleFixture(0, reinterpret_cast<uintptr_t>(&fixture));
                    exited.store(true, std::memory_order_release);
                });
                bool paused = WaitForSingleObject(pause.entered, 5000) == WAIT_OBJECT_0;
                bool stopped = bridge.stop();
                // stop has returned and destroyed neither the process module
                // nor this fixture. The suspended detour still has a return
                // address in scene code: bridge draining cannot prove unmapping.
                ok &= stopped && paused && !exited.load(std::memory_order_acquire) && gSceneBridge.load() == nullptr && gSceneModuleRetained.load();
                SetEvent(pause.resume);
                producer.join();
                outsidePause.store(nullptr, std::memory_order_release);
                ok &= exited.load() && fixture.compositeCalls == (window >= 2 ? 1u : 0u);
                ok &= MH_DisableHook(lockEntry) == MH_OK && MH_RemoveHook(lockEntry) == MH_OK;
                for (auto event: {fixture.entered, fixture.resume, pause.entered, pause.resume}) CloseHandle(event);
            }
        }
        std::puts(ok ? "CONFIRMED: 12 real-detour pre-acquire/post-release pauses outlive stop(); a drained bridge does not prove its module can be unmapped."
                     : "FAIL: detour prologue/epilogue stop-window fixture");
        return ok;
    }
    static bool bridgeCheck() {
        if (gSceneBridge.load()) return false;
        const auto status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) return false;
        if (!retainSceneModule()) return false;
        bool ok = true;
        for (int pass = 0; pass < 2; ++pass) {
            UiSceneBridge bridge(nullptr, nullptr);
            QueueFixture fixture;
            fixture.entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            fixture.resume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!fixture.entered || !fixture.resume)
                return false;
            bridge.target_ = reinterpret_cast<void *>(&submitFixture);
            bridge.compositeTarget_ = reinterpret_cast<void *>(&compositeLifecycleFixture);
            bridge.enqueue_ = &enqueueFixture;
            bridge.accepting_.store(true);
            gSubmitEntry = &submitFixture;
            gCompositeEntry = &compositeLifecycleFixture;
            gSceneBridge.store(&bridge);
            if (MH_CreateHook(bridge.target_, reinterpret_cast<void *>(&UiSceneBridge::submit), reinterpret_cast<void **>(&bridge.original_)) != MH_OK ||
                MH_CreateHook(bridge.compositeTarget_, reinterpret_cast<void *>(&UiSceneBridge::composite), reinterpret_cast<void **>(&bridge.originalComposite_)) != MH_OK ||
                MH_EnableHook(bridge.target_) != MH_OK || MH_EnableHook(bridge.compositeTarget_) != MH_OK)
                return false;
            std::thread producer([&] {
                if (pass == 0)
                    submitFixture(0, &fixture);
                else
                    compositeLifecycleFixture(0, reinterpret_cast<uintptr_t>(&fixture));
            });
            ok &= WaitForSingleObject(fixture.entered, 5000) == WAIT_OBJECT_0;
            std::atomic_bool stopped{false};
            std::thread stopper([&] {
                bridge.stop();
                stopped.store(true);
            });
            auto deadline = GetTickCount64() + 5000;
            while (bridge.accepting_.load() && GetTickCount64() < deadline)
                Sleep(1);
            ok &= !bridge.accepting_.load() && !stopped.load();
            SetEvent(fixture.resume);
            producer.join();
            stopper.join();
            ok &= stopped.load() && fixture.callbacks == (pass == 0 ? 1u : 0u) && fixture.compositeCalls == (pass == 1 ? 1u : 0u) && gSceneBridge.load() == nullptr;
            // A subsequent call goes to the restored game entry, with no enqueue.
            fixture.callback = nullptr;
            submitFixture(0, &fixture);
            compositeLifecycleFixture(0, reinterpret_cast<uintptr_t>(&fixture));
            ok &= fixture.callbacks == (pass == 0 ? 1u : 0u) && fixture.compositeCalls == (pass == 1 ? 2u : 1u);
            CloseHandle(fixture.entered);
            CloseHandle(fixture.resume);
        }
        std::puts(ok ? "PASS: two real MinHook detours drain lock-protected callbacks; restored local entries and fixture reinstall work without unmapping the module."
                     : "FAIL: bridge queue/teardown fixture");
        return ok;
    }
    static bool disableFailureCheck() {
        bool ok = true;
        for (int repeat = 0; repeat < 3; ++repeat) {
            UiSceneBridge bridge(nullptr, nullptr);
            QueueFixture fixture;
            fixture.entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            fixture.resume = CreateEventW(nullptr, TRUE, TRUE, nullptr);
            if (!fixture.entered || !fixture.resume) return false;
            auto *target = reinterpret_cast<void *>(&submitFixture);
            bridge.target_ = target;
            bridge.compositeTarget_ = reinterpret_cast<void *>(&compositeLifecycleFixture);
            bridge.enqueue_ = &enqueueFixture;
            bridge.accepting_.store(true);
            gSubmitEntry = &submitFixture;
            gCompositeEntry = &compositeLifecycleFixture;
            gSceneBridge.store(&bridge);
            if (!retainSceneModule() ||
                MH_CreateHook(target, reinterpret_cast<void *>(&UiSceneBridge::submit), reinterpret_cast<void **>(&bridge.original_)) != MH_OK ||
                MH_CreateHook(bridge.compositeTarget_, reinterpret_cast<void *>(&UiSceneBridge::composite), reinterpret_cast<void **>(&bridge.originalComposite_)) != MH_OK ||
                MH_EnableHook(target) != MH_OK || MH_EnableHook(bridge.compositeTarget_) != MH_OK) return false;
            // Deterministically obtain a real MinHook disable error without
            // mocking Win32: this unregistered entry returns NOT_CREATED, while
            // the actual submit detour/trampoline remain reachable and valid.
            bridge.target_ = reinterpret_cast<void *>(&compositeFixture);
            ok &= !bridge.stop() && !bridge.accepting_.load() && gSceneBridge.load() == &bridge;
            submitFixture(0, &fixture); // Must use retained original, not recurse.
            ok &= fixture.callbacks == 0 && WaitForSingleObject(fixture.entered, 0) == WAIT_OBJECT_0;
            bridge.target_ = target;
            ok &= bridge.stop() && gSceneBridge.load() == nullptr;
            CloseHandle(fixture.entered);
            CloseHandle(fixture.resume);
        }
        std::puts(ok ? "PASS: three real MinHook disable-error branches retain an inert bridge/trampoline, skip user callbacks, and permit later successful cleanup."
                     : "FAIL: disable-error ownership fixture");
        return ok;
    }
};

} // namespace er
extern "C" bool er_rust_scene_lifecycle_check() noexcept {
    try { return er::NativeSceneVerifier::bridgeCheck() && er::NativeSceneVerifier::outsideGateCheck() && er::NativeSceneVerifier::disableFailureCheck() && er::NativeSceneVerifier::boundaryCheck(); }
    catch (...) { return false; }
}
extern "C" bool er_rust_scene_drive_fixture(Callback callback, void *user, const NativeFrame *above, void *aboveResource,
                                           const NativeFrame *below, void *belowResource, void (*nativeCommand)(void *), void *nativeUser, uint32_t scenario) noexcept {
    if (!above || !below) return false;
    try { return er::NativeSceneVerifier::driveFrame(callback, user, *above, aboveResource, *below, belowResource, nativeCommand, nativeUser, scenario); }
    catch (...) { return false; }
}
