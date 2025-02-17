#pragma once

#if defined(__cplusplus)
#define EXTERN_C extern "C"
#else
#define EXTERN_C
#endif

#if defined(_WIN32)
#if defined(EROVERLAY_EXPORTS)
#define API_EXPORT __declspec(dllexport)
#else
#define API_EXPORT __declspec(dllimport)
#endif
#else
#define API_EXPORT
#endif

#if defined(__cplusplus)
extern "C" {
#include <cstdint>
#else
#include <stdint.h>
#include <stdbool.h>
#endif

#pragma pack(push, 8)

typedef struct {
    uintptr_t csMenuManImp;
    uintptr_t gameDataMan;
    uintptr_t eventFlagMan;
    uintptr_t fieldArea;
} GameAddresses;

typedef struct {
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

    // Game addresses
    GameAddresses (*getGameAddresses)();
    uintptr_t (*resolveFlagAddress)(uint32_t flagId, uint8_t *bits);
} EROverlayAPI;

typedef struct {
    /* ==== Version 0 ==== */
    /* init() is called on plugin loaded, return the plugin version. */
    int (*init)();
    /* uninit() is called on plugin uninit. */
    void (*uninit)();
    /* update() is called on each 1/60s, in an individual thread, which can be used to do heavy works. */
    void (*update)();
    /* createRenderer() is called to create the renderer, passing important imgui arguments which should be used for ImGui::SetCurrentContext() and ImGui::SetAllocatorFunctions(). */
    void (*createRenderer)(void *imguiContext, void *allocFunc, void *freeFunc, void *allocUserData);
    /* destroyRenderer() is called to destroy the renderer. */
    void (*destroyRenderer)();
    /* render() is called to render things, return true if the plugin wants to show the cursor. */
    bool (*render)();
    /* toggleFullMode() is called to toggle full mode on pressing keys defined in config file. */
    void (*toggleFullMode)();
} PluginExports;

#pragma pack(pop)

#if defined(_WIN32)
#define PLUGIN_EXPORT EXTERN_C __declspec(dllexport)
#else
#define PLUGIN_EXPORT EXTERN_C
#endif

API_EXPORT EROverlayAPI *getEROverlayAPI();

#define PLUGIN_DEFINE(pexp) \
    PLUGIN_EXPORT PluginExports *getExports() { return &pexp; }

#if defined(__cplusplus)
}
#endif
