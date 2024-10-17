#include "api.h"

#include "config.hpp"
#include "global.hpp"
#include "hooking.hpp"
#include "util/steam.hpp"

EROverlayAPI *getEROverlayAPI() {
    static EROverlayAPI api = {[]{
        return er::gGameVersion;
    }, [] {
        return (const wchar_t *)er::gModulePath;
    }, [] {
        return er::gHooking->menuLoaded();
    }, [] {
        return er::gHooking->screenState();
    }, [] {
        return er::util::getGameLanguage();
    }, [] {
        return er::gIsDLC01Installed;
    },
    [](const char *name) {
        static std::string value;
        value = er::gConfig[name];
        return value.c_str();
    },
    [](const char *name, const wchar_t *defValue) {
        static std::wstring value;
        value = er::gConfig.getw(name, defValue);
        return value.c_str();
    },
    [](const char *name, int defValue) {
        return er::gConfig.get(name, defValue);
    },
    [](const char *name, float defValue) {
        return er::gConfig.get(name, defValue);
    },
    [](const char *name) {
        return er::gConfig.enabled(name);
    },
    };
    return &api;
}
