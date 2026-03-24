#include "data.hpp"

#include "api.h"
#include "util/memory.hpp"

#include <nlohmann/json.hpp>
#include <imgui.h>

#include <fstream>

extern EROverlayAPI *api;

namespace er::bosses {

BossDataSet gBossDataSet;

// --- ChallengeState ---

void ChallengeState::loadConfig(const wchar_t *modulePath) {
    if (!enabled) return;
    auto filename = std::wstring(modulePath) + L"\\Challenge.txt";
    std::ifstream ifs(filename.c_str());
    if (!ifs) return;

    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto key = line.substr(0, eq);
        auto val = line.substr(eq + 1);
        if (key == "tries") {
            tries = std::strtol(val.c_str(), nullptr, 10);
        } else if (key == "deaths_on_start") {
            deathsOnStart = std::strtol(val.c_str(), nullptr, 10);
        } else if (key == "best") {
            best = std::strtol(val.c_str(), nullptr, 10);
        }
    }
    // Only create change notification once (avoid handle leak on reload)
    if (changeEvent == INVALID_HANDLE_VALUE) {
        changeEvent = FindFirstChangeNotificationW(modulePath, FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE);
    }
}

void ChallengeState::saveConfig(const wchar_t *modulePath) {
    auto filename = std::wstring(modulePath) + L"\\Challenge.txt";
    std::ofstream ofs(filename.c_str());
    if (!ofs) {
        fwprintf(stderr, L"Unable to open %ls for writing\n", filename.c_str());
        return;
    }
    ofs << "# Total tries\ntries=" << tries << "\n";
    ofs << "# Personal Best\nbest=" << best << "\n";
    ofs << "\n# !!!DO NOT EDIT THIS!!!\ndeaths_on_start=" << deathsOnStart << "\n";
    ofs.flush();

    // Drain pending file change notifications to avoid self-triggered reload
    while (WaitForSingleObject(changeEvent, 0) == WAIT_OBJECT_0) {
        FindNextChangeNotification(changeEvent);
    }
}

void ChallengeState::checkForChange(const wchar_t *modulePath) {
    if (changeEvent == INVALID_HANDLE_VALUE) return;
    bool needReload = false;
    while (WaitForSingleObject(changeEvent, 0) == WAIT_OBJECT_0) {
        needReload = true;
        FindNextChangeNotification(changeEvent);
    }
    if (needReload) loadConfig(modulePath);
}

void ChallengeState::cleanup() noexcept {
    if (changeEvent != INVALID_HANDLE_VALUE) {
        FindCloseChangeNotification(changeEvent);
        changeEvent = INVALID_HANDLE_VALUE;
    }
}

// --- BossDataSet ---

void BossDataSet::load(bool hasDLC) {
    toggleFullModeKey_ = api->configGetImGuiKey("boss.toggle_full_mode", ImGuiKey_Equal);
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
    auto j = nlohmann::ordered_json::parse(ifs);
    ifs.close();
    for (auto &p : j.items()) {
        if (!hasDLC && p.value()["dlc"] == 1) {
            continue;
        }
        auto regionIndex = regions_.size();
        auto &rd = regions_.emplace_back();
        rd.name = p.value()["region_name"];
        for (auto &n : p.value()["regions"]) {
            regionMap_[n.get<uint32_t>()] = static_cast<int>(regionIndex);
        }
        for (auto &n : p.value()["bosses"]) {
            auto index = bosses_.size();
            auto &bd = bosses_.emplace_back();
            bd.boss = n["boss"];
            bd.place = n["place"];
            bd.flagId = n["flag_id"].get<uint32_t>();
            bd.index = index;
            rd.bosses.push_back(index);
            bd.regionIndex = regionIndex;
        }
    }
    regionCounts_.resize(regions_.size());
    dead_.resize(bosses_.size());
    deadSwapBuf_.resize(bosses_.size());
    regionCountSwapBuf_.resize(regions_.size());

    challenge_.enabled = api->configEnabled("boss.challenge_mode");
    if (!challenge_.enabled) return;
    challenge_.maxDeaths = api->configGetInt("boss.challenge_death_count", 0);
    challenge_.loadConfig(api->getModulePath());
}

BossDataSet::~BossDataSet() noexcept {
    challenge_.cleanup();
}

void BossDataSet::initMemoryAddresses() {
    auto addr = api->getGameAddresses();
    gameDataMan_ = addr.gameDataMan;
    fieldArea_ = addr.fieldArea;
}

void BossDataSet::update() {
    challenge_.checkForChange(api->getModulePath());
    if (api->screenState() != 0) {
        return;
    }
    auto igt = readInGameTime();
    if (igt <= 0) return;
    updateBosses();
    if (challenge_.enabled)
        updateChallengeMode();
    else
        updateNormalMode();
}

void BossDataSet::updateNormalMode() {
    std::lock_guard lock(mutex_);
    challenge_.playerDeaths = readDeathCount();
}

void BossDataSet::revive(int index) {
    if (!flagResolved_) {
        return;
    }
    auto &b = bosses_[index];
    if (b.offset == 0) {
        return;
    }
    *(uint8_t *)(b.offset) &= ~b.bits;
}

void BossDataSet::updateBosses() {
    if (!flagResolved_) {
        for (auto &b : bosses_) {
            b.offset = api->resolveFlagAddress(b.flagId, &b.bits);
        }
        flagResolved_ = true;
    }
    int cnt = 0;
    std::fill(regionCountSwapBuf_.begin(), regionCountSwapBuf_.end(), 0);
    size_t sz = bosses_.size();
    for (size_t i = 0; i < sz; i++) {
        auto &b = bosses_[i];
        if (b.offset == 0) {
            continue;
        }
        bool al = (*(uint8_t *)(b.offset) & b.bits) != 0;
        deadSwapBuf_[i] = al ? 1 : 0;
        if (al) {
            cnt++;
            regionCountSwapBuf_[b.regionIndex]++;
        }
    }

    bool needSave = false;
    {
        std::lock_guard lock(mutex_);
        regionCounts_.swap(regionCountSwapBuf_);
        dead_.swap(deadSwapBuf_);
        count_ = cnt;
        if (fieldArea_ != 0) {
            auto addr1 = *(uintptr_t *)fieldArea_;
            if (addr1 != 0) {
                auto mapIdOffset = api->getGameVersion() < offsets::kVersionThreshold1_12 ? offsets::kMapIdPre1_12 : offsets::kMapIdPost1_12;
                auto mapId = *(uint32_t *)(addr1 + mapIdOffset);
                if (mapId != 0 && mapId != mapId_) {
                    mapId_ = mapId;
                    auto ite = regionMap_.find(mapId / 1000);
                    if (ite != regionMap_.end()) {
                        regionIndex_ = ite->second;
                    }
                }
            }
        }
        if (challenge_.enabled && cnt > challenge_.best && challenge_.deaths() <= challenge_.maxDeaths) {
            challenge_.best = cnt;
            needSave = true;
        }
    }
    if (needSave) {
        challenge_.saveConfig(api->getModulePath());
    }
}

void BossDataSet::updateChallengeMode() {
    if (challenge_.reachFlagOffset == 0) {
        challenge_.reachFlagOffset = api->resolveFlagAddress(kStrandedGraveyardFlagId, &challenge_.reachFlagBits);
    }
    if (challenge_.reachFlagOffset == 0) return;
    auto deathCount = readDeathCount();
    auto reached = (*(uint8_t *)challenge_.reachFlagOffset & challenge_.reachFlagBits) != 0;
    bool needSave = false;
    {
        std::lock_guard lock(mutex_);
        if (reached != challenge_.reachedGraveyard) {
            challenge_.reachedGraveyard = reached;
            if (reached) {
                challenge_.deathsOnStart = deathCount;
            } else {
                challenge_.playerDeaths = 0;
                challenge_.deathsOnStart = 0;
            }
            needSave = true;
        }
        if (deathCount != challenge_.playerDeaths) {
            challenge_.playerDeaths = deathCount;
            if (challenge_.deaths() == challenge_.maxDeaths + 1) {
                challenge_.tries++;
            }
            needSave = true;
        }
    }
    if (needSave) challenge_.saveConfig(api->getModulePath());
}

void BossDataSet::fillRenderState(RenderState &out) {
    // IGT reads game memory directly (not our state), no mutex needed
    out.inGameTime = readInGameTime();
    std::lock_guard lock(mutex_);
    out.count = count_;
    out.deaths = challenge_.deaths();
    out.regionIndex = regionIndex_;
    out.challengeMode = challenge_.enabled;
    out.challengeBest = challenge_.best;
    out.challengeTries = challenge_.tries;
    out.dead = dead_;
    out.regionCounts = regionCounts_;
}

int BossDataSet::readInGameTime() const {
    if (gameDataMan_ == 0) return -1;
    auto addr = util::MemoryHandle(gameDataMan_).as<uintptr_t &>();
    if (addr == 0) return -1;
    return util::MemoryHandle(addr + offsets::kInGameTime).as<int &>();
}

int BossDataSet::readDeathCount() const {
    if (gameDataMan_ == 0) return 0;
    auto addr = util::MemoryHandle(gameDataMan_).as<uintptr_t &>();
    if (addr == 0) return 0;
    return util::MemoryHandle(addr + offsets::kDeathCount).as<int &>();
}

}
