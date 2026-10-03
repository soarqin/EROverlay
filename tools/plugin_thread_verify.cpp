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
std::atomic_bool updating{false}, releaseUpdate{false}, rendered{false}, destroyed{false};
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
void destroy() { destroyed.store(true, std::memory_order_release); }

bool check(bool concurrent) {
    PluginExports exports{nullptr, nullptr, update, nullptr, destroy, render};
    updating = releaseUpdate = rendered = destroyed = false;
    er::plugins.clear();
    er::plugins.emplace_back(0, 0, &exports, concurrent);
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

int main() {
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
