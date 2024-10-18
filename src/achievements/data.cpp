#include "data.hpp"

#include "api.h"

#include <steamapi.h>

extern EROverlayAPI *api;

namespace er::achievements {

Data gData;

void Data::load() {
    auto *stats = SteamAPI_SteamUserStats_v012();
    // SteamAPI_ISteamUserStats_RequestCurrentStats(stats);
    uint32 sz = SteamAPI_ISteamUserStats_GetNumAchievements(stats);
    achievements_.resize(sz);
    for (uint32 i = 0; i < sz; i++) {
        const char *a = SteamAPI_ISteamUserStats_GetAchievementName(stats, i);
        Achievement &ach = achievements_[i];
        ach.name = a;
        ach.displayName = SteamAPI_ISteamUserStats_GetAchievementDisplayAttribute(stats, a, "name");
        ach.description = SteamAPI_ISteamUserStats_GetAchievementDisplayAttribute(stats, a, "desc");
    }
    locked_.resize(sz);
    for (uint32 i = 0; i < sz; i++) {
        locked_[i] = i;
    }
    lockedPrev_ = locked_;
    stats_ = stats;
}

void Data::update() {
    auto *stats = (ISteamUserStats*)stats_;
    bool changed = false;
    for (int i = int(lockedPrev_.size()) - 1; i >= 0; i--) {
        auto index = lockedPrev_[i];
        bool achieved;
        if (SteamAPI_ISteamUserStats_GetAchievement(stats, achievements_[index].name, &achieved) && achieved) {
            lockedPrev_.erase(lockedPrev_.begin() + i);
            changed = true;
        }
    }
    if (changed) {
        std::lock_guard lock(mutex_);
        locked_ = lockedPrev_;
    }
}

}
