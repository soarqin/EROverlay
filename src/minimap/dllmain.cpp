#include "data.hpp"
#include "render.hpp"
#include "resources.hpp"

#include "api.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

EROverlayAPI *api;

int init(EROverlayAPI *erOverlayAPI) {
    api = erOverlayAPI;

    auto core = GetModuleHandleW(L"EROverlay.dll");
    if (!core)
        core = GetModuleHandleW(L"winhttp.dll");
    auto getNative = core ? reinterpret_cast<const EROverlayNativeAPI *(*)(uint32_t)>(GetProcAddress(core, "getEROverlayNativeAPI")) : nullptr;
    er::minimap::nativeApi = getNative ? getNative(1) : nullptr;
    if (er::minimap::nativeApi && (er::minimap::nativeApi->size < sizeof(EROverlayNativeAPI) || er::minimap::nativeApi->version != 1))
        er::minimap::nativeApi = nullptr;

    return 0;
}

void update() {
    er::minimap::gResources.update();
    er::minimap::gData.update();
}

static er::minimap::Renderer *renderer = nullptr;

int createRenderer(void *context, void *allocFunc, void *freeFunc, void *userData) {
    renderer = new er::minimap::Renderer();
    renderer->init(context, allocFunc, freeFunc, userData);
    return 0;
}

void destroyRenderer() {
    delete renderer;
    renderer = nullptr;
}

bool render() {
    if (!renderer)
        return false;
    return renderer->render();
}

void uninit() {
    destroyRenderer();
    er::minimap::gResources.stop();
    er::minimap::nativeApi = nullptr;
}

static PluginExports exports = {init, uninit, update, createRenderer, destroyRenderer, render};

PLUGIN_DEFINE(exports)
