#pragma once

#include <cstdint>

namespace er {

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
#define API_EXPORT extern "C" __declspec(dllexport)
#else
#define API_EXPORT extern "C" __declspec(dllimport)
#endif
#else
#define API_EXPORT extern "C"
#endif
API_EXPORT EROverlayAPI *getEROverlayAPI();

using PluginInitFunction = const wchar_t *(*)();
using PluginUninitFunction = void (*)();
using PluginUpdateFunction = void (*)();
using PluginCreateRendererFunction = void (*)(void *, void *, void *, void *);
using PluginDestroyRendererFunction = void (*)();
using PluginRenderFunction = bool (*)();

struct PluginExports {
    /* init() is called on plugin loaded, return the plugin name. */
    PluginInitFunction init;
    /* uninit() is called on plugin uninit. */
    PluginUninitFunction uninit;
    /* update() is called on each 1/60s, in an individual thread, which can be used to do heavy works. */
    PluginUpdateFunction update;
    /* createRenderer() is called to create the renderer, passing important imgui arguments which should be used for ImGui::SetCurrentContext() and ImGui::SetAllocatorFunctions(). */
    PluginCreateRendererFunction createRenderer;
    /* destroyRenderer() is called to destroy the renderer. */
    PluginDestroyRendererFunction destroyRenderer;
    /* render() is called to render things, return true if the plugin wants to show the cursor. */
    PluginRenderFunction render;
};

#if defined(_WIN32)
#define PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define PLUGIN_EXPORT extern "C"
#endif

#define PLUGIN_DEFINE() PLUGIN_EXPORT ::er::PluginExports *getExports()

}
