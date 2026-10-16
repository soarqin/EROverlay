// Drive the production dispatcher with controlled callbacks. Including its
// implementation makes the internal registration list available to fixtures;
// no DLL is loaded into the game and no dispatch logic is reimplemented here.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

#include "../src/plugin.cpp"

namespace er {
wchar_t gModulePath[MAX_PATH]{};
}
EROverlayAPI *getEROverlayAPI() { return nullptr; }

namespace {
ERRenderLayer selectedLayer = ER_RENDER_LAYER_ABOVE_UI;
std::atomic_bool updating{false}, releaseUpdate{false}, rendered{false}, destroyed{false};
unsigned destroyCalls = 0;
bool waitFor(const std::atomic_bool &value, unsigned milliseconds = 1000) {
    for (unsigned i = 0; i < milliseconds && !value.load(std::memory_order_acquire); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return value.load(std::memory_order_acquire);
}
void update() {
    updating.store(true, std::memory_order_release);
    while (!releaseUpdate.load(std::memory_order_acquire))
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
bool render() {
    rendered.store(true, std::memory_order_release);
    return false;
}
void destroy() {
    ++destroyCalls;
    destroyed.store(true, std::memory_order_release);
}

bool lifecycleCheck() {
    PluginExports exports{nullptr, nullptr, nullptr, nullptr, destroy, nullptr};
    er::plugins.clear();
    er::plugins.push_back({0, &exports, false, er::RenderLayer::AboveUI});
    er::renderersLoaded = false;
    destroyCalls = 0;
    er::pluginsDestroyRenderers();
    bool ok = destroyCalls == 0;
    er::pluginsLoadRenderers(nullptr, nullptr, nullptr, nullptr);
    er::pluginsDestroyRenderers();
    er::pluginsDestroyRenderers();
    ok &= destroyCalls == 1;
    er::pluginsUninit();
    std::printf("%s: destroyRenderer only follows a loaded generation, once; calls=%u\n", ok ? "PASS" : "FAIL", destroyCalls);
    return ok;
}

bool check(bool concurrent) {
    PluginExports exports{nullptr, nullptr, update, nullptr, destroy, render};
    updating = releaseUpdate = rendered = destroyed = false;
    er::plugins.clear();
    er::plugins.push_back({0, &exports, concurrent, er::RenderLayer::AboveUI});
    er::renderersLoaded = true;
    std::thread updater(er::pluginsUpdate);
    bool entered = waitFor(updating);
    std::thread renderer([] { (void)er::pluginsRender(); });
    bool parallel = waitFor(rendered, concurrent ? 1000 : 30);
    std::thread teardown(er::pluginsDestroyRenderers);
    bool premature = waitFor(destroyed, 30);
    releaseUpdate.store(true, std::memory_order_release);
    updater.join();
    renderer.join();
    teardown.join();
    // The legacy render may finish after teardown acquires the lifecycle lock;
    // either way it must never overlap update. Opt-in render completes first.
    return entered && parallel == concurrent && !premature && destroyed;
}
} // namespace

extern "C" __declspec(dllexport) ERRenderLayer getRenderLayer() { return selectedLayer; }

int main() {
    static_assert(sizeof(PluginExports) == 6 * sizeof(void *));
    if (er::pluginRenderLayer(GetModuleHandleW(L"kernel32.dll")) != er::RenderLayer::AboveUI)
        return 3;
    auto module = GetModuleHandleW(nullptr);
    for (auto layer: {ER_RENDER_LAYER_ABOVE_UI, ER_RENDER_LAYER_BELOW_UI, static_cast<ERRenderLayer>(99)}) {
        selectedLayer = layer;
        auto expected = layer == ER_RENDER_LAYER_BELOW_UI ? er::RenderLayer::BelowUI : er::RenderLayer::AboveUI;
        if (er::pluginRenderLayer(module) != expected)
            return 4;
    }
    std::puts("PASS: PluginExports ABI unchanged; missing/invalid optional layer export defaults above UI; below-UI opt-in resolves through GetProcAddress.");
    if (!lifecycleCheck())
        return 5;
    if (!check(false)) {
        std::puts("FAIL: legacy callback serialization / teardown exclusion");
        return 1;
    }
    if (!check(true)) {
        std::puts("FAIL: opt-in callback concurrency / teardown exclusion");
        return 2;
    }
    er::pluginsUninit();
    std::puts("PASS: production dispatcher serializes legacy callbacks, runs opted-in update/render concurrently and waits for active callbacks before teardown.");
}
