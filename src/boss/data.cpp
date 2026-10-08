#include <algorithm>
#include <atomic>
#include <fstream>
#include <limits>

#include <nlohmann/json.hpp>

#include "api.h"
#include "data.hpp"
#include "util/memory.hpp"

extern EROverlayAPI *api;

namespace er::bosses {

BossDataSet gBossDataSet;

static bool readFlagId(const nlohmann::ordered_json &json, uint32_t &flagId) {
    if (!json.is_number_integer())
        return false;
    if (json.is_number_unsigned()) {
        auto value = json.get<uint64_t>();
        if (value == 0 || value > (std::numeric_limits<uint32_t>::max)())
            return false;
        flagId = static_cast<uint32_t>(value);
    } else {
        auto value = json.get<int64_t>();
        if (value <= 0 || value > (std::numeric_limits<uint32_t>::max)())
            return false;
        flagId = static_cast<uint32_t>(value);
    }
    return true;
}

static bool loadReviveFlags(const nlohmann::ordered_json &json, BossData &boss) {
    auto entry = json.find("revive_flags");
    if (entry == json.end())
        return true;
    if (!entry->is_array())
        return false;
    for (const auto &reset: *entry) {
        BossData::ReviveFlag flag;
        if (reset.is_object()) {
            auto id = reset.find("flag_id");
            auto value = reset.find("value");
            if (id == reset.end() || value == reset.end() || !value->is_boolean() || !readFlagId(*id, flag.flagId))
                return false;
            flag.value = value->get<bool>();
        } else if (!readFlagId(reset, flag.flagId)) {
            return false;
        }
        if (flag.flagId == boss.flagId) {
            if (flag.value)
                return false;
            continue;
        }
        auto duplicate = std::find_if(boss.reviveFlags.begin(), boss.reviveFlags.end(), [&](const auto &other) { return other.flagId == flag.flagId; });
        if (duplicate != boss.reviveFlags.end()) {
            if (duplicate->value != flag.value)
                return false;
            continue;
        }
        boss.reviveFlags.push_back(flag);
    }
    return true;
}

// --- ChallengeState ---

void ChallengeState::loadConfig(const wchar_t *modulePath) {
    if (!enabled)
        return;
    auto filename = std::wstring(modulePath) + L"\\Challenge.txt";
    std::ifstream ifs(filename.c_str());
    if (!ifs)
        return;

    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
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
    if (changeEvent == INVALID_HANDLE_VALUE)
        return;
    bool needReload = false;
    while (WaitForSingleObject(changeEvent, 0) == WAIT_OBJECT_0) {
        needReload = true;
        FindNextChangeNotification(changeEvent);
    }
    if (needReload)
        loadConfig(modulePath);
}

void ChallengeState::cleanup() noexcept {
    if (changeEvent != INVALID_HANDLE_VALUE) {
        FindCloseChangeNotification(changeEvent);
        changeEvent = INVALID_HANDLE_VALUE;
    }
}

// --- BossDataSet ---

void BossDataSet::load(bool hasDLC) {
    toggleFullModeKey_ = api->configGetVirtualKey("boss.toggle_full_mode", VK_OEM_PLUS);
    allowRevive_ = api->configEnabled("boss.allow_revive");
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
    auto j = nlohmann::ordered_json::parse(ifs, nullptr, false);
    ifs.close();
    if (!j.is_array()) {
        fwprintf(stderr, L"Invalid boss data in %ls\n", filename.c_str());
        return;
    }
    for (auto &p: j.items()) {
        if (!hasDLC && p.value()["dlc"] == 1) {
            continue;
        }
        auto regionIndex = regions_.size();
        auto &rd = regions_.emplace_back();
        rd.name = p.value()["region_name"];
        for (auto &n: p.value()["regions"]) {
            regionMap_[n.get<uint32_t>()] = static_cast<int>(regionIndex);
        }
        for (auto &n: p.value()["bosses"]) {
            auto index = bosses_.size();
            auto &bd = bosses_.emplace_back();
            bd.boss = n["boss"];
            bd.place = n["place"];
            auto primary = n.find("flag_id");
            bd.reviveFlagsValid = primary != n.end() && readFlagId(*primary, bd.flagId) && loadReviveFlags(n, bd);
            if (!bd.reviveFlagsValid) {
                fprintf(stderr, "Invalid flag_id or revive_flags for boss %u (%s); revival is disabled for this boss\n", bd.flagId, bd.boss.c_str());
            }
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
    if (!challenge_.enabled)
        return;
    challenge_.maxDeaths = api->configGetInt("boss.challenge_death_count", 0);
    challenge_.loadConfig(api->getModulePath());
}

BossDataSet::~BossDataSet() noexcept { challenge_.cleanup(); }

void BossDataSet::initMemoryAddresses() {
    auto addr = api->getGameAddresses();
    gameDataMan_ = addr.gameDataMan;
    fieldArea_ = addr.fieldArea;
}

void BossDataSet::update() {
    if (gameDataMan_ == 0 || fieldArea_ == 0) {
        auto gameAddresses = api->getGameAddresses();
        if (gameDataMan_ == 0)
            gameDataMan_ = gameAddresses.gameDataMan;
        if (fieldArea_ == 0)
            fieldArea_ = gameAddresses.fieldArea;
    }

    challenge_.checkForChange(api->getModulePath());
    if (api->screenState() != 0) {
        return;
    }
    auto igt = readInGameTime();
    if (igt <= 0)
        return;
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

bool BossDataSet::revive(int index) {
    std::lock_guard lock(mutex_);
    if (!allowRevive_ || index < 0 || static_cast<size_t>(index) >= bosses_.size() || api->screenState() != 0 || readInGameTime() <= 0)
        return false;
    const auto &boss = bosses_[index];
    if (!boss.reviveFlagsValid)
        return false;
    auto managerSlot = api->getGameAddresses().eventFlagMan;
    if (managerSlot == 0)
        return false;
    auto manager = util::MemoryHandle(managerSlot).as<uintptr_t &>();
    if (manager == 0)
        return false;
    struct FlagWrite {
        uintptr_t address;
        uint8_t bits;
        bool value;
    };
    std::vector<FlagWrite> writes;
    writes.reserve(boss.reviveFlags.size() + 1);
    auto resolve = [&](uint32_t id, bool value) {
        uint8_t bits = 0;
        auto address = api->resolveFlagAddress(id, &bits);
        if (address == 0 || bits == 0) {
            fprintf(stderr, "Cannot revive boss %u: flag %u is unavailable; no flags were changed\n", boss.flagId, id);
            return false;
        }
        writes.push_back({address, bits, value});
        return true;
    };
    // Resolve every address against the current save before changing any byte.
    for (const auto &flag: boss.reviveFlags) {
        if (!resolve(flag.flagId, flag.value))
            return false;
    }
    if (!resolve(boss.flagId, false))
        return false;
    if (api->screenState() != 0 || util::MemoryHandle(managerSlot).as<uintptr_t &>() != manager)
        return false;
    if ((std::atomic_ref(util::MemoryHandle(writes.back().address).as<uint8_t &>()).load(std::memory_order_relaxed) & writes.back().bits) == 0)
        return false;
    // Restore arena state first, then expose the boss as alive. Preserve other
    // flags sharing a byte with an atomic bit operation for each write.
    for (const auto &write: writes) {
        std::atomic_ref byte(util::MemoryHandle(write.address).as<uint8_t &>());
        if (write.value)
            byte.fetch_or(write.bits, std::memory_order_relaxed);
        else
            byte.fetch_and(static_cast<uint8_t>(~write.bits), std::memory_order_relaxed);
    }
    if (dead_[index]) {
        dead_[index] = 0;
        --count_;
        --regionCounts_[boss.regionIndex];
    }
    return true;
}

void BossDataSet::updateBosses() {
    bool needSave = false;
    {
        std::lock_guard lock(mutex_);
        int cnt = 0;
        std::fill(deadSwapBuf_.begin(), deadSwapBuf_.end(), 0);
        std::fill(regionCountSwapBuf_.begin(), regionCountSwapBuf_.end(), 0);
        size_t sz = bosses_.size();
        for (size_t i = 0; i < sz; i++) {
            auto &b = bosses_[i];
            // Addresses may change when a save is loaded; retry unavailable flags.
            b.offset = api->resolveFlagAddress(b.flagId, &b.bits);
            if (b.offset == 0 || b.bits == 0) {
                continue;
            }
            bool al = (std::atomic_ref(util::MemoryHandle(b.offset).as<uint8_t &>()).load(std::memory_order_relaxed) & b.bits) != 0;
            deadSwapBuf_[i] = al ? 1 : 0;
            if (al) {
                cnt++;
                regionCountSwapBuf_[b.regionIndex]++;
            }
        }

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
    if (challenge_.reachFlagOffset == 0)
        return;
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
    if (needSave)
        challenge_.saveConfig(api->getModulePath());
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
    if (gameDataMan_ == 0)
        return -1;
    auto addr = util::MemoryHandle(gameDataMan_).as<uintptr_t &>();
    if (addr == 0)
        return -1;
    return util::MemoryHandle(addr + offsets::kInGameTime).as<int &>();
}

int BossDataSet::readDeathCount() const {
    if (gameDataMan_ == 0)
        return 0;
    auto addr = util::MemoryHandle(gameDataMan_).as<uintptr_t &>();
    if (addr == 0)
        return 0;
    return util::MemoryHandle(addr + offsets::kDeathCount).as<int &>();
}

} // namespace er::bosses
