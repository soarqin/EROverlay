#include "steamapi.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef unsigned int AppId_t;

struct ISteamApps;
extern "C" {
ISteamApps *(*SteamAPI_SteamApps_v008)() = nullptr;
const char *(*SteamAPI_ISteamApps_GetCurrentGameLanguage)(ISteamApps *) = nullptr;
bool (*SteamAPI_ISteamApps_BIsDlcInstalled)(ISteamApps *self, AppId_t appID) = nullptr;
}

namespace er::steamapi {

static ISteamApps *sapps = nullptr;
static std::wstring gameLanguage;

bool init() {
#define LOAD_STEAM_API(name) name = (decltype(name))GetProcAddress(handle, #name)
    auto handle = GetModuleHandleW(L"steam_api64.dll");
    if (handle == nullptr) return false;

    LOAD_STEAM_API(SteamAPI_SteamApps_v008);
    LOAD_STEAM_API(SteamAPI_ISteamApps_GetCurrentGameLanguage);
    LOAD_STEAM_API(SteamAPI_ISteamApps_BIsDlcInstalled);

    return true;
}

const wchar_t *getGameLanguage() {
    if (!gameLanguage.empty())
        return gameLanguage.c_str();
    if (!SteamAPI_SteamApps_v008) {
        return L"engUS";
    }
    if (!sapps)
        sapps = SteamAPI_SteamApps_v008();
    const char *lang = SteamAPI_ISteamApps_GetCurrentGameLanguage(sapps);

    fwprintf(stderr, L"Steam Game Language: %hs\n", lang);
#define LANG_CHECK_AND_SET(str, str2) else if (lstrcmpA(lang, #str) == 0) { gameLanguage = L ## #str2; }
    if (false) {}
    LANG_CHECK_AND_SET(english, engUS)
    LANG_CHECK_AND_SET(german, deuDE)
    LANG_CHECK_AND_SET(french, fraFR)
    LANG_CHECK_AND_SET(italian, itaIT)
    LANG_CHECK_AND_SET(japanese, jpnJP)
    LANG_CHECK_AND_SET(koreana, korKR)
    LANG_CHECK_AND_SET(polish, polPL)
    LANG_CHECK_AND_SET(portuguese, porBR)
    LANG_CHECK_AND_SET(brazilian, porBR)
    LANG_CHECK_AND_SET(russian, rusRU)
    LANG_CHECK_AND_SET(latam, spaAR)
    LANG_CHECK_AND_SET(spanish, spaES)
    LANG_CHECK_AND_SET(thai, thaTH)
    LANG_CHECK_AND_SET(schinese, zhoCN)
    LANG_CHECK_AND_SET(tchinese, zhoTW)
#undef LANG_CHECK_AND_SET
    else {
        gameLanguage = L"engUS";
    }
    return gameLanguage.c_str();
}

bool isDLCInstalled(unsigned int dlc) {
    if (!SteamAPI_SteamApps_v008) return false;
    if (!sapps)
        sapps = SteamAPI_SteamApps_v008();
    return SteamAPI_ISteamApps_BIsDlcInstalled(sapps, dlc);
}

}
