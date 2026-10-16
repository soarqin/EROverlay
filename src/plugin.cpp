#include "plugin.hpp"

#include "api.h"
#include "global.hpp"

#include <algorithm>
#include <filesystem>
#include <fmt/format.h>
#include <fmt/xchar.h>
#include <mutex>
#include <shared_mutex>
#include <vector>

namespace er {

struct Plugin {
    int priority;
    PluginExports *exports;
    bool concurrent;
    RenderLayer layer;
};
static std::vector<Plugin> plugins;
static bool renderersLoaded = false;
// Lifecycle changes are exclusive. Legacy plugins keep serialized callbacks;
// only plugins explicitly exporting the opt-in may update during rendering.
static std::shared_mutex pluginsMutex;
static std::mutex legacyCallbacksMutex;

[[nodiscard]] static RenderLayer pluginRenderLayer(HMODULE module) {
    auto getLayer = reinterpret_cast<ERRenderLayer (*)()>(GetProcAddress(module, "getRenderLayer"));
    return getLayer && getLayer() == ER_RENDER_LAYER_BELOW_UI ? RenderLayer::BelowUI : RenderLayer::AboveUI;
}

void pluginsInit() {
    std::lock_guard lock(pluginsMutex);
    plugins.clear();
    renderersLoaded = false;
    const std::filesystem::path sandbox{std::wstring(gModulePath)};
    auto *api = getEROverlayAPI();
    for (const auto &dir_entry: std::filesystem::directory_iterator{sandbox / L"overlays"}) {
        if (dir_entry.is_regular_file()) {
            const auto &path = dir_entry.path();
            auto filename = path.wstring();
            fmt::print(L"Loading plugin: {}...", filename);
            if (auto lib = LoadLibraryW(filename.c_str())) {
                auto getExports = reinterpret_cast<PluginExports *(*)()>(GetProcAddress(lib, "getExports"));
                if (!getExports) {
                    fmt::print(" library entry not found.\n");
                    FreeLibrary(lib);
                    continue;
                }
                auto *exports = getExports();
                if (!exports || !exports->init) {
                    fmt::print(" library entry not valid.\n");
                    FreeLibrary(lib);
                    continue;
                }
                auto ver = exports->init(api);
                auto concurrent = reinterpret_cast<bool (*)()>(GetProcAddress(lib, "supportsConcurrentUpdateRender"));
                plugins.push_back({0, exports, concurrent && concurrent(), pluginRenderLayer(lib)});
                fmt::print(" successful. (API Version {})\n", ver);
            } else {
                fmt::print(" unable to load.\n");
            }
        }
    }
}

void pluginsUninit() {
    std::lock_guard lock(pluginsMutex);
    for (const auto &pl: plugins) {
        auto *p = pl.exports;
        if (p->uninit) {
            p->uninit();
        }
    }
    plugins.clear();
}

void pluginsUpdate() {
    std::shared_lock lock(pluginsMutex);
    for (const auto &pl: plugins) {
        auto *p = pl.exports;
        if (p->update) {
            std::unique_lock<std::mutex> callLock;
            if (!pl.concurrent)
                callLock = std::unique_lock(legacyCallbacksMutex);
            p->update();
        }
    }
}

void pluginsLoadRenderers(void *context, void *allocFunc, void *freeFunc, void *userData) {
    std::lock_guard lock(pluginsMutex);
    renderersLoaded = false;
    for (auto &pl: plugins) {
        auto *p = pl.exports;
        if (p->createRenderer) {
            auto priority = p->createRenderer(context, allocFunc, freeFunc, userData);
            pl.priority = priority;
        }
    }
    std::sort(plugins.begin(), plugins.end(), [](const auto &a, const auto &b) { return a.priority < b.priority; });
    renderersLoaded = true;
}

void pluginsDestroyRenderers() {
    std::lock_guard lock(pluginsMutex);
    if (!renderersLoaded)
        return;
    renderersLoaded = false;
    for (const auto &pl: plugins) {
        auto *p = pl.exports;
        if (p->destroyRenderer) {
            p->destroyRenderer();
        }
    }
}

bool pluginsUseLayer(RenderLayer layer) {
    std::shared_lock lock(pluginsMutex);
    return std::any_of(plugins.begin(), plugins.end(), [layer](const auto &pl) { return pl.layer == layer && pl.exports->render; });
}

bool pluginsRender(PluginRenderObserver observer, void *context) {
    std::shared_lock lock(pluginsMutex);
    if (!renderersLoaded)
        return false;
    bool showCursor = false;
    for (const auto &pl: plugins) {
        auto *p = pl.exports;
        if (p->render) {
            std::unique_lock<std::mutex> callLock;
            if (!pl.concurrent)
                callLock = std::unique_lock(legacyCallbacksMutex);
            if (observer)
                observer(pl.layer, true, context);
            auto r = p->render();
            if (observer)
                observer(pl.layer, false, context);
            showCursor = showCursor || r;
        }
    }
    return showCursor;
}

} // namespace er
