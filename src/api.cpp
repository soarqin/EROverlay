#include "api.hpp"

#include "config.hpp"
#include "global.hpp"
#include "hooking.hpp"
#include "util/steamapi.hpp"
#include <imgui.h>

namespace er {

EROverlayAPI *getEROverlayAPI() {
    static EROverlayAPI api = {[]{
        return gGameVersion;
    }, [] {
        return (const wchar_t *)gModulePath;
    }, [] {
        return gHooking->menuLoaded();
    }, [] {
        return gHooking->screenState();
    }, [] {
        return steamapi::getGameLanguage();
    }, [] {
        return gIsDLC01Installed;
    },
    [](const char *name) {
        static std::string value;
        value = gConfig[name];
        return value.c_str();
    },
    [](const char *name, const wchar_t *defValue) {
        static std::wstring value;
        value = gConfig.getw(name, defValue);
        return value.c_str();
    },
    [](const char *name, int defValue) {
        return gConfig.get(name, defValue);
    },
    [](const char *name, float defValue) {
        return gConfig.get(name, defValue);
    },
    [](const char *name) {
        return gConfig.enabled(name);
    },
    };
    return &api;
}

}
