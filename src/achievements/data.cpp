#include "data.hpp"

#include "api.h"
#include "util/string.hpp"

#include <steamapi.h>

#include <map>
#include <fstream>
#include <cctype>

extern EROverlayAPI *api;

namespace er::achievements {

Data gData;

void Data::load() {
    auto *stats = SteamAPI_SteamUserStats_v012();
    // SteamAPI_ISteamUserStats_RequestCurrentStats(stats);
    uint32_t sz = SteamAPI_ISteamUserStats_GetNumAchievements(stats);
    achievements_.resize(sz);
    std::map<std::string, uint32_t> indexMap;
    for (uint32_t i = 0; i < sz; i++) {
        const char *a = SteamAPI_ISteamUserStats_GetAchievementName(stats, i);
        Achievement &ach = achievements_[i];
        ach.name = a;
        ach.displayName = SteamAPI_ISteamUserStats_GetAchievementDisplayAttribute(stats, a, "name");
        ach.description = SteamAPI_ISteamUserStats_GetAchievementDisplayAttribute(stats, a, "desc");
        indexMap[a] = i;
    }
    locked_.resize(sz);

    std::ifstream ifs((std::wstring(api->getModulePath()) + L"\\ach_order.txt").c_str());
    uint32_t i = 0;
    if (ifs.is_open()) {
        std::string line;
        while (std::getline(ifs, line)) {
            auto pos = line.find('#');
            if (pos != std::string::npos) line.erase(pos);
            util::trimString(line);
            if (line.empty()) continue;

            auto it = indexMap.find(line);
            if (it == indexMap.end()) {
                continue;
            }
            locked_[i++] = it->second;
            indexMap.erase(it);
        }
    }
    for (auto &pair : indexMap) {
        locked_[i++] = pair.second;
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
