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

// Notification animation timing constants
namespace timing {
    constexpr auto kNotificationDuration = std::chrono::seconds(8);
    constexpr float kMinAlpha = 0.1f;
    constexpr float kFadeInEnd = 7.0f;    // seconds remaining when fade-in completes
    constexpr float kFadeOutStart = 1.5f;  // seconds remaining when fade-out begins
    constexpr float kFadeOutSlope = 0.6f;  // alpha gain per second during fade-out
} // namespace timing

using SetAchievementHookFunc = bool (*)(ISteamUserStats *, const char *);
SetAchievementHookFunc originalSetAchievementHook = nullptr;

static bool SetAchievementHook(ISteamUserStats *stats, const char *pchName) {
    if (originalSetAchievementHook(stats, pchName)) {
        gData.unlock(pchName);
        return true;
    }
    return false;
}

// Build a data file path relative to the module directory.
static std::wstring buildDataPath(const wchar_t *filename) {
    return std::wstring(api->getModulePath()) + L"\\data\\" + filename;
}

// Strip comments and whitespace from a line. Returns true if the line has content.
static bool parseLine(std::string &line) {
    auto pos = line.find('#');
    if (pos != std::string::npos) line.erase(pos);
    util::trimString(line);
    return !line.empty();
}

void Data::load() {
    auto *stats = SteamAPI_SteamUserStats();
    MH_Initialize();
    void **statsVTable = *(void ***)stats;
    MH_CreateHook(statsVTable[7], (void *)&SetAchievementHook, (void **)&originalSetAchievementHook);
    MH_EnableHook(statsVTable[7]);

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

    std::ifstream ifs(buildDataPath(L"ach_order.txt").c_str());
    uint32_t i = 0;
    std::string line;
    if (ifs.is_open()) {
        while (std::getline(ifs, line)) {
            if (!parseLine(line)) continue;

            util::toLower(line);
            auto it = indexMap.find(line);
            if (it == indexMap.end()) continue;
            bool b;
            if (SteamAPI_ISteamUserStats_GetAchievement(stats, it->first.c_str(), &b) && !b) {
                locked_[i++] = Locked{it->second, 0, 0};
            }
            indexMap.erase(it);
        }
        ifs.close();
    }
    for (auto &pair : indexMap) {
        bool b;
        if (SteamAPI_ISteamUserStats_GetAchievement(stats, pair.first.c_str(), &b) && !b) {
            locked_[i++] = Locked{pair.second, 0, 0};
        }
    }
    locked_.resize(i);
    lockedPrev_ = locked_;

    ifs.open(buildDataPath(L"ach_cols.txt").c_str());
    if (!ifs.is_open()) return;
    while (std::getline(ifs, line)) {
        if (!parseLine(line)) continue;
        auto sl = util::splitString(line, ',');
        if (sl.size() < 2) continue;
        util::toLower(sl[0]);
        auto it = indexMap_.find(sl[0]);
        if (it == indexMap_.end()) continue;
        auto &vec = achievements_[it->second].collectionFlags;
        vec.push_back(CollectionFlag{uint32_t(std::strtoul(sl[1].c_str(), nullptr, 10)), 0, 0});
    }
}

void Data::update() {
    if (!flagResolved_) {
        for (auto &l : lockedPrev_) {
            auto &ach = achievements_[l.index];
            auto flag = 1u;
            for (auto &cf : ach.collectionFlags) {
                auto offset = api->resolveFlagAddress(cf.flagId, &cf.bits);
                if (offset == 0) return;
                cf.offset = offset;
                if ((*(uint8_t *)(cf.offset) & cf.bits) != 0) {
                    l.flags |= flag;
                    l.flagCount++;
                }
                flag <<= 1;
            }
        }
        flagResolved_ = true;
    }
    bool changed = false;
    for (auto &l : lockedPrev_) {
        auto &ach = achievements_[l.index];
        if (ach.collectionFlags.empty()) continue;
        auto flag = 1u;
        for (auto &cf : ach.collectionFlags) {
            if (l.flags & flag) {
                flag <<= 1;
                continue;
            }
            if ((*(uint8_t *)(cf.offset) & cf.bits) != 0) {
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
    std::string n = name;
    util::toLower(n);
    auto it = indexMap_.find(n);
    if (it == indexMap_.end()) return;
    uint32_t index = it->second;
    auto findit = std::find_if(lockedPrev_.begin(), lockedPrev_.end(), [index](const auto &item) { return item.index == index; });
    if (findit == lockedPrev_.end()) return;
    lockedPrev_.erase(findit);
    fmt::print("Unlocking {} and adding notification\n", n);
    std::lock_guard lock(mutex_);
    locked_ = lockedPrev_;
    unlocking_.push_back(UnlockNotification{index, std::chrono::steady_clock::now() + timing::kNotificationDuration, timing::kMinAlpha});
}

void Data::updateUnlockingStatus() {
    auto now = std::chrono::steady_clock::now();
    std::erase_if(unlocking_, [&](UnlockNotification &n) {
        if (now >= n.deadline) {
            fmt::print("Removing notification {}\n", achievements_[n.index].name);
            return true;
        }
        auto seconds = std::chrono::duration<float>(n.deadline - now).count();
        if (seconds <= 0.0f)
            n.alpha = timing::kMinAlpha;
        else if (seconds <= timing::kFadeOutStart)
            n.alpha = timing::kMinAlpha + timing::kFadeOutSlope * seconds;
        else if (seconds <= timing::kFadeInEnd)
            n.alpha = 1.0f;
        else
            n.alpha = 1.0f - (seconds - timing::kFadeInEnd);
        return false;
    });
}

}
