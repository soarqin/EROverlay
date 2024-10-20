#include "data.hpp"

#include "api.h"
#include "util/string.hpp"

#include <steam/steam_api_flat.h>
#include <MinHook.h>
#include <fmt/format.h>

#include <map>
#include <fstream>

extern EROverlayAPI *api;

namespace er::achievements {

Data gData;

using SetAchievementHookFunc = bool(*)(ISteamUserStats*, const char*);
SetAchievementHookFunc originalSetAchievementHook = nullptr;

static bool SetAchievementHook(ISteamUserStats *stats, const char *pchName) {
    if (originalSetAchievementHook(stats, pchName)) {
        gData.unlock(pchName);
        return true;
    }
    return false;
}

void Data::load() {
    auto *stats = SteamAPI_SteamUserStats();
    MH_Initialize();
    void **statsVTable = *(void ***)stats;
    MH_CreateHook(statsVTable[7], (void*)&SetAchievementHook, (void**)&originalSetAchievementHook);
    MH_EnableHook(statsVTable[7]);

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
        indexMap_[a] = i;
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

            util::toLower(line);
            auto it = indexMap.find(line);
            if (it == indexMap.end()) {
                continue;
            }
            bool b;
            if (SteamAPI_ISteamUserStats_GetAchievement(stats, it->first.c_str(), &b) && !b) {
                locked_[i++] = it->second;
            }
            indexMap.erase(it);
        }
    }
    for (auto &pair : indexMap) {
        bool b;
        if (SteamAPI_ISteamUserStats_GetAchievement(SteamAPI_SteamUserStats(), pair.first.c_str(), &b) && !b) {
            locked_[i++] = pair.second;
        }
    }
    locked_.resize(i);
    lockedPrev_ = locked_;
    stats_ = stats;
}

using std::chrono_literals::operator""s;
void Data::unlock(const char *name) {
    std::string n = name;
    util::toLower(n);
    auto it = indexMap_.find(n);
    if (it == indexMap_.end()) return;
    uint32_t index = it->second;
    auto findit = std::find(lockedPrev_.begin(), lockedPrev_.end(), index);
    if (findit == lockedPrev_.end()) return;
    lockedPrev_.erase(findit);
    std::lock_guard lock(mutex_);
    locked_ = lockedPrev_;
    unlocking_.emplace_back(index, std::chrono::steady_clock::now() + 8s, 0.1f);
    fmt::print("Unlocking {} and adding notification\n", n);
}

void Data::updateUnlockingStatus() {
    auto now = std::chrono::steady_clock::now();
    for (int i = int(unlocking_.size() - 1); i >= 0; i--) {
        auto &p = unlocking_[i];
        auto &deadline = std::get<1>(p);
        if (now >= deadline) {
            unlocking_.erase(unlocking_.begin() + i);
            fmt::print("Removing notification {}\n", achievements_[std::get<0>(p)].name);
            continue;
        }
        // to double seconds
        auto seconds = std::chrono::duration<float>(deadline - now).count();
        if (seconds <= 0.0f) std::get<2>(unlocking_[i]) = 0.1f;
        else if (seconds <= 1.5f) std::get<2>(unlocking_[i]) = 0.1f + 0.6f * seconds;
        else if (seconds <= 7.0f) std::get<2>(unlocking_[i]) = 1.0f;
        else std::get<2>(unlocking_[i]) = 1.0f - (seconds - 7.0f);
    }
}

}
