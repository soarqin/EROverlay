#include "plugin.hpp"

#include "global.hpp"
#include "api.h"

#include <fmt/format.h>
#include <fmt/xchar.h>
#include <filesystem>

namespace er {

static std::vector<std::pair<int, PluginExports&>> plugins;

void pluginsInit() {
    plugins.clear();
    const std::filesystem::path sandbox{std::wstring(gModulePath) + L"\\overlays"};
    for (const std::filesystem::directory_entry &dir_entry: std::filesystem::directory_iterator{sandbox}) {
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
                auto ver = exports->init();
                plugins.emplace_back(ver, *exports);
                fmt::print(" successful. (API Version {})\n", ver);
            } else {
                fmt::print(" unabled to load.\n");
            }
        }
    }
}

void pluginsUninit() {
    for (const auto &pl: plugins) {
        pl.second.uninit();
    }
    plugins.clear();
}

void pluginsUpdate() {
    for (const auto &pl: plugins) {
        pl.second.update();
    }
}

void pluginsLoadRenderers(void *context, void *allocFunc, void *freeFunc, void *userData) {
    for (const auto &pl: plugins) {
        pl.second.createRenderer(context, allocFunc, freeFunc, userData);
    }
}

void pluginsDestroyRenderers() {
    for (const auto &pl: plugins) {
        pl.second.destroyRenderer();
    }
}

bool pluginsRender() {
    bool showCursor = false;
    for (const auto &pl: plugins) {
        showCursor = showCursor || pl.second.render();
    }
    return showCursor;
}

void pluginsToggleFullMode() {
    for (const auto &pl: plugins) {
        if (pl.second.toggleFullMode) pl.second.toggleFullMode();
    }
}

}
