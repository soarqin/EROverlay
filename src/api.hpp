#pragma once

#include <cstdint>

namespace er {

extern "C" {

struct EROverlayAPI {
    // Global variable
    uint64_t (*getGameVersion)();
    const wchar_t *(*getModulePath)();

    // Game status
    bool (*menuLoaded)();
    int (*screenState)();

    // Steam wrapper
    const wchar_t *(*getGameLanguage)();
    bool (*isDLC01Installed)();

    // Config, be note that these functions are not efficient, call them on init/load only.
    const char *(*configGet)(const char *name);
    const wchar_t *(*configGetString)(const char *name, const wchar_t *defValue);
    int (*configGetInt)(const char *name, int defValue);
    float (*configGetFloat)(const char *name, float defValue);
    bool (*configEnabled)(const char *name);
};

#if defined(_WIN32)
#if defined(EROVERLAY_EXPORTS)
__declspec(dllexport)
#else
__declspec(dllimport)
#endif
#endif
EROverlayAPI *getEROverlayAPI();

using PluginInitFunction = const wchar_t *(*)();
using PluginUninitFunction = void (*)();
using PluginLoadFunction = void (*)();
using PluginUpdateFunction = void (*)();
using PluginLoadRendererFunction = void (*)(void *, void *, void *, void *);
using PluginDestroyRendererFunction = void (*)();
using PluginRenderFunction = bool (*)();

struct PluginExports {
    PluginInitFunction init;
    PluginUninitFunction uninit;
    PluginLoadFunction load;
    PluginUpdateFunction update;
    PluginLoadRendererFunction loadRenderer;
    PluginDestroyRendererFunction destroyRenderer;
    PluginRenderFunction render;
};

}

}
