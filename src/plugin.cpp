#include "plugin.hpp"
#include "api.hpp"

#include "global.hpp"

#include <fmt/format.h>
#include <fmt/xchar.h>
#include <dlfcn.h>
#include <filesystem>

namespace er {

static std::vector<std::pair<const wchar_t *, PluginExports>> plugins;

void pluginsInit() {
    plugins.clear();
    const std::filesystem::path sandbox{std::wstring(gModulePath) + L"\\overlays"};
    for (const std::filesystem::directory_entry &dir_entry: std::filesystem::directory_iterator{sandbox}) {
        if (dir_entry.is_regular_file()) {
            const auto &path = dir_entry.path();
            auto filename = path.string();
            fmt::print("Loading plugin: {}...", filename);
            if (auto *lib = dlopen(filename.c_str(), RTLD_LAZY)) {
                PluginExports pl{};
                pl.init = reinterpret_cast<PluginInitFunction>(dlsym(lib, "init"));
                pl.uninit = reinterpret_cast<PluginUninitFunction>(dlsym(lib, "uninit"));
                pl.load = reinterpret_cast<PluginLoadFunction>(dlsym(lib, "load"));
                if (!pl.init && !pl.load) {
                    fmt::print(" failed.\n", filename);
                    dlclose(lib);
                    continue;
                }
                pl.update = reinterpret_cast<PluginUpdateFunction>(dlsym(lib, "update"));
                pl.loadRenderer = reinterpret_cast<PluginLoadRendererFunction>(dlsym(lib, "loadRenderer"));
                pl.destroyRenderer = reinterpret_cast<PluginDestroyRendererFunction>(dlsym(lib, "destroyRenderer"));
                pl.render = reinterpret_cast<PluginRenderFunction>(dlsym(lib, "render"));
                const auto *name = pl.init();
                plugins.emplace_back(name, pl);
                fmt::print(L" successful with name {}\n", name);
            } else {
                fmt::print(" failed.\n", filename);
            }
        }
    }
}

void pluginsOnLoad() {
    for (const auto &pl: plugins) {
        pl.second.load();
    }
}

void pluginsUpdate() {
    for (const auto &pl: plugins) {
        pl.second.update();
    }
}

void pluginsLoadRenderers(void *context, void *allocFunc, void *freeFunc, void *userData) {
    for (const auto &pl: plugins) {
        pl.second.loadRenderer(context, allocFunc, freeFunc, userData);
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

}
