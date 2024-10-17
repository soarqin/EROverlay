#include "data.hpp"

#include "api/api.h"

#include <steamapi.h>
#include <fmt/xchar.h>

#include <fstream>

extern EROverlayAPI *api;

namespace er::achievements {

Data gData;

void Data::load() {
    auto *stats = SteamAPI_SteamUserStats_v012();
    // SteamAPI_ISteamUserStats_RequestCurrentStats(stats);
    uint32 sz = SteamAPI_ISteamUserStats_GetNumAchievements(stats);
    for (uint32 i = 0; i < sz; i++) {
        const char *a = SteamAPI_ISteamUserStats_GetAchievementName(stats, i);
        Achievement ach;
        ach.name = a;
        ach.displayName = SteamAPI_ISteamUserStats_GetAchievementDisplayAttribute(stats, a, "name");
        ach.description = SteamAPI_ISteamUserStats_GetAchievementDisplayAttribute(stats, a, "desc");
    }
    locked_.resize(sz);
    for (uint32 i = 0; i < sz; i++) {
        locked_[i] = i;
    }
}

void Data::update() {
}

}
