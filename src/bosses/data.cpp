#include "data.hpp"

#include "api.h"
#include "util/memory.hpp"

#include <nlohmann/json.hpp>
#include <ini.h>

#include <fstream>

extern EROverlayAPI *api;

namespace er::bosses {

BossDataSet gBossDataSet;
static std::vector<bool> deadSwapTmp;
static std::vector<int> deadByRegionSwapTmp;

void BossDataSet::load(bool hasDLC) {
    nlohmann::ordered_json j;
    std::wstring name = api->configGetString("boss.data_file", L"bosses.json");
    std::wstring lang = api->configGetString("common.language", L"");
    if (lang.empty()) {
        lang = api->getGameLanguage();
    }
    std::wstring filename = std::wstring(api->getModulePath()) + L"\\data\\" + lang + L"\\" + name;
    std::ifstream ifs(filename.c_str());
    if (!ifs) {
        /* fallback to use english */
        lang = L"engUS";
        filename = std::wstring(api->getModulePath()) + L"\\data\\" + lang + L"\\" + name;
        ifs.open(filename.c_str());
        if (!ifs) {
            fwprintf(stderr, L"Unable to open %ls\n", filename.c_str());
            return;
        }
    }
    j = nlohmann::ordered_json::parse(ifs);
    ifs.close();
    for (auto &p: j.items()) {
        if (!hasDLC && p.value()["dlc"] == 1) {
            continue;
        }
        auto regionIndex = regions_.size();
        auto &rd = regions_.emplace_back();
        rd.name = p.value()["region_name"];
        for (auto &n: p.value()["regions"]) {
            regionMap_[n.get<uint32_t>()] = (int)regionIndex;
        }
        for (auto &n: p.value()["bosses"]) {
            auto index = bosses_.size();
            auto &bd = bosses_.emplace_back();
            bd.boss = n["boss"];
            bd.place = n["place"];
            bd.flagId = n["flag_id"].get<uint32_t>();
            bd.index = index;
            bd.tip = bd.boss + ": " + bd.place;
            rd.bosses.push_back(index);
            bd.regionIndex = regionIndex;
        }
    }
    regionCounts_.resize(regions_.size());
    dead_.resize(bosses_.size());
    deadSwapTmp.resize(bosses_.size());
    deadByRegionSwapTmp.resize(regions_.size());

    challengeMode_ = api->configEnabled("boss.challenge_mode");
    if (!challengeMode_) return;
    challengeDeathCount_ = api->configGetInt("boss.challenge_death_count", 0);
}

void BossDataSet::loadConfig() {
    if (!challengeMode_) return;
    auto filename = std::wstring(api->getModulePath()) + L"\\Challenge.txt";
    auto *f = _wfopen(filename.c_str(), L"rt");
    if (f == nullptr) {
        return;
    }
    ini_parse_file(f, [](void *user, const char *section, const char *name, const char *value) {
        if (section != nullptr && section[0] != 0) {
            return 0;
        }
        auto *bds = static_cast<BossDataSet *>(user);
        if (lstrcmpA(name, "tries") == 0) {
            bds->challengeTries_ = std::strtol(value, nullptr, 10);
        } else if (lstrcmpA(name, "deaths_on_start") == 0) {
            bds->challengeDeathsOnStart_ = std::strtol(value, nullptr, 10);
        } else if (lstrcmpA(name, "best") == 0) {
            bds->challengeBest_ = std::strtol(value, nullptr, 10);
        }
        return 1;
    }, this);
    fclose(f);
    changeEvent_ = FindFirstChangeNotificationW(api->getModulePath(), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE);
}

void BossDataSet::saveConfig() const {
    auto filename = std::wstring(api->getModulePath()) + L"\\Challenge.txt";
    auto *f = _wfopen(filename.c_str(), L"wt");
    if (f == nullptr) {
        fprintf(stderr, "Unable to open %ls for writing\n", filename.c_str());
        fflush(stderr);
        return;
    }
    fprintf(f, "# Total tries\ntries=%d\n", challengeTries_);
    fprintf(f, "# Personal Best\nbest=%d\n", challengeBest_);
    fprintf(f, "\n# !!!DO NOT EDIT THIS!!!\ndeaths_on_start=%d\n", challengeDeathsOnStart_);
    fflush(f);
    fclose(f);
    while (WaitForSingleObject(changeEvent_, 0) == WAIT_OBJECT_0) {
        FindNextChangeNotification(changeEvent_);
    }
}

void BossDataSet::initMemoryAddresses() {
    auto addr = api->getGameAddresses();
    gameDataMan_ = addr.gameDataMan;
    eventFlagMan_ = addr.eventFlagMan;
    fieldArea_ = addr.fieldArea;
}

void BossDataSet::update() {
    checkForConfigChange();
    if (api->screenState() != 0) {
        return;
    }
    auto igt = readInGameTime();
    if (igt == 0) return;
    updateBosses();
    if (!challengeMode_ || igt < 0) return;
    updateChallengeMode();
}

void BossDataSet::revive(int index) {
    if (!flagResolved_) {
        return;
    }
    auto &b = bosses_[index];
    *(uint8_t *)(b.offset) &= ~b.bits;
}

void BossDataSet::updateBosses() {
    if (!flagResolved_) {
        for (auto &b: bosses_) {
            b.offset = api->resolveFlagAddress(b.flagId, &b.bits);
            if (b.offset == 0) {
                return;
            }
        }
        flagResolved_ = true;
    }
    int cnt = 0;
    std::fill(deadByRegionSwapTmp.begin(), deadByRegionSwapTmp.end(), 0);
    size_t sz = bosses_.size();
    for (size_t i = 0; i < sz; i++) {
        auto &b = bosses_[i];
        bool al = (*(uint8_t *)(b.offset) & b.bits) != 0;
        deadSwapTmp[i] = al;
        if (al) {
            cnt++;
            deadByRegionSwapTmp[b.regionIndex]++;
        }
    }
    {
        std::unique_lock lock(mutex_);
        regionCounts_.swap(deadByRegionSwapTmp);
        dead_.swap(deadSwapTmp);
        count_ = cnt;
        auto addr1 = *(uintptr_t *)fieldArea_;
        if (addr1 == 0) {
            return;
        }
        auto mapId = *(uint32_t *)(addr1 + (api->getGameVersion() < 0x0002000200000000ULL ? 0xE4 : 0xE8));
        if (mapId == 0 || mapId == mapId_) return;
        mapId_ = mapId;
        auto ite = regionMap_.find(mapId / 1000);
        if (ite == regionMap_.end()) return;
        regionIndex_ = ite->second;
    }

    if (challengeMode_ && cnt > challengeBest_ && deaths() <= challengeDeathCount_) {
        challengeBest_ = cnt;
        saveConfig();
    }
}

void BossDataSet::updateChallengeMode() {
    if (reachStrandedGraveyardFlagOffset_ == 0) {
        // Flag 101: Reached Stranded Graveyard
        reachStrandedGraveyardFlagOffset_ = api->resolveFlagAddress(101, &reachStrandedGraveyardFlagBits_);
    }
    auto deathCount = readDeathCount();
    auto reached = (*(uint8_t *)reachStrandedGraveyardFlagOffset_ & reachStrandedGraveyardFlagBits_) != 0;
    bool needSave = false;
    std::unique_lock lock(mutex_);
    if (reached != reachedStrandedGraveyard_) {
        reachedStrandedGraveyard_ = reached;
        if (reached) {
            challengeDeathsOnStart_ = deathCount;
            needSave = true;
        } else {
            playerDeaths_ = 0;
            challengeDeathsOnStart_ = 0;
            needSave = true;
        }
    }
    if (deathCount != playerDeaths_) {
        playerDeaths_ = deathCount;
        if (deaths() == challengeDeathCount_ + 1) {
            challengeTries_++;
        }
        needSave = true;
    }
    if (needSave) saveConfig();
}

void BossDataSet::checkForConfigChange() {
    if (changeEvent_ == INVALID_HANDLE_VALUE) return;
    bool needReload = false;
    while (WaitForSingleObject(changeEvent_, 0) == WAIT_OBJECT_0) {
        needReload = true;
        FindNextChangeNotification(changeEvent_);
    }
    if (needReload) loadConfig();
}

int BossDataSet::readInGameTime() const {
    auto addr = util::MemoryHandle(gameDataMan_).as<uintptr_t &>();
    if (addr == 0) {
        return -1;
    }
    return util::MemoryHandle(addr + 0xA0).as<int&>();
}

int BossDataSet::readDeathCount() const {
    auto addr = util::MemoryHandle(gameDataMan_).as<uintptr_t &>();
    if (addr == 0) {
        return 0;
    }
    return util::MemoryHandle(addr + 0x94).as<int&>();
}

}
