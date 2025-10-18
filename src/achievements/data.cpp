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
        std::string lname = a;
        util::toLower(lname);
        indexMap[lname] = i;
        indexMap_[lname] = i;
    }
    locked_.resize(sz);

    std::ifstream ifs((std::wstring(api->getModulePath()) + L"\\data\\ach_order.txt").c_str());
    uint32_t i = 0;
    std::string line;
    if (ifs.is_open()) {
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
                locked_[i++] = Locked { it->second, 0, 0 };
            }
            indexMap.erase(it);
        }
        ifs.close();
    }
    for (auto &pair : indexMap) {
        bool b;
        if (SteamAPI_ISteamUserStats_GetAchievement(stats, pair.first.c_str(), &b) && !b) {
            locked_[i++] = Locked { pair.second, 0, 0 };
        }
    }
    locked_.resize(i);
    lockedPrev_ = locked_;

    ifs.open((std::wstring(api->getModulePath()) + L"\\data\\ach_cols.txt").c_str());
    if (!ifs.is_open()) return;
    while (std::getline(ifs, line)) {
        auto pos = line.find('#');
        if (pos != std::string::npos) line.erase(pos);
        util::trimString(line);
        if (line.empty()) continue;
        auto sl = util::splitString(line, ',');
        if (sl.size() < 2) continue;
        util::toLower(sl[0]);
        auto it = indexMap_.find(sl[0]);
        if (it == indexMap_.end()) continue;
        auto &vec = achievements_[it->second].collectionFlags;
        vec.emplace_back(uint32_t(std::strtoul(sl[1].c_str(), nullptr, 10)), 0, 0);
    }
}

void Data::update() {
    if (!flagResolved_) {
        for (auto &l: lockedPrev_) {
            auto &ach = achievements_[l.index];
            auto flag = 1u;
            for (auto &p: ach.collectionFlags) {
                uint8_t bits;
                auto offset = api->resolveFlagAddress(std::get<0>(p), &bits);
                if (offset == 0) {
                    return;
                }
                std::get<1>(p) = offset;
                std::get<2>(p) = bits;
                if ((*(uint8_t *)(offset) & bits) != 0) {
                    l.flags |= flag;
                    l.flagCount++;
                }
                flag <<= 1;
            }
        }
        flagResolved_ = true;
    }
    bool changed = false;
    for (auto &l: lockedPrev_) {
        auto &ach = achievements_[l.index];
        if (ach.collectionFlags.empty()) continue;
        auto flag = 1u;
        for (auto &p: ach.collectionFlags) {
            if (l.flags & flag) {
                flag <<= 1;
                continue;
            }
            auto offset = std::get<1>(p);
            auto bits = std::get<2>(p);
            if ((*(uint8_t *)(offset) & bits) != 0) {
                l.flags |= flag;
                l.flagCount++;
                changed = true;
            }
            flag <<= 1;
        }
    }
    if (changed) {
        std::lock_guard lock(mutex_);
        locked_ = lockedPrev_;
    }
}

void Data::unlock(const char *name) {
    using namespace std::chrono_literals;
    std::string n = name;
    util::toLower(n);
    auto it = indexMap_.find(n);
    if (it == indexMap_.end()) return;
    uint32_t index = it->second;
    auto findit = std::find_if(lockedPrev_.begin(), lockedPrev_.end(), [index](const auto &n) { return n.index == index; });
    if (findit == lockedPrev_.end()) return;
    lockedPrev_.erase(findit);
    fmt::print("Unlocking {} and adding notification\n", n);
    std::lock_guard lock(mutex_);
    locked_ = lockedPrev_;
    unlocking_.emplace_back(index, std::chrono::steady_clock::now() + 8s, 0.1f);
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
