#include "plugin.hpp"

#include "global.hpp"
#include "api.h"

#include <fmt/format.h>
#include <fmt/xchar.h>
#include <filesystem>
#include <tuple>

namespace er {

static std::vector<std::tuple<int, int, PluginExports*>> plugins;

void pluginsInit() {
    plugins.clear();
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
                    fmt::print(" library enty not found.\n");
                    FreeLibrary(lib);
                    continue;
                }
                auto *exports = getExports();
                auto ver = exports->init(api);
                plugins.emplace_back(ver, 0, exports);
                fmt::print(" successful. (API Version {})\n", ver);
            } else {
                fmt::print(" unabled to load.\n");
            }
        }
    }
}

void pluginsUninit() {
    for (const auto &pl: plugins) {
        std::get<2>(pl)->uninit();
    }
    plugins.clear();
}

void pluginsUpdate() {
    for (const auto &pl: plugins) {
        std::get<2>(pl)->update();
    }
}

void pluginsLoadRenderers(void *context, void *allocFunc, void *freeFunc, void *userData) {
    for (auto &pl: plugins) {
        auto priority = std::get<2>(pl)->createRenderer(context, allocFunc, freeFunc, userData);
        std::get<1>(pl) = priority;
    }
    std::sort(plugins.begin(), plugins.end(), [](const auto &a, const auto &b) {
        return std::get<1>(a) < std::get<1>(b);
    });
}

void pluginsDestroyRenderers() {
    for (const auto &pl: plugins) {
        std::get<2>(pl)->destroyRenderer();
    }
}

bool pluginsRender() {
    bool showCursor = false;
    for (const auto &pl: plugins) {
        auto r = std::get<2>(pl)->render();
        showCursor = showCursor || r;
    }
    return showCursor;
}

}
