#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "data.hpp"

#include "api.h"
#include "util/memory.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>

extern EROverlayAPI *api;

namespace er::bosses {

BossDataSet gBossDataSet;

namespace {

// splitmix64. Fixed, fully specified bit mixing so a given seed produces the same boss list on
// every machine, compiler and build. Do not swap this for a <random> engine: the standard
// engines' and distributions' output is implementation-defined, which would break seed sharing.
uint64_t splitmix64(uint64_t &state) {
    uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// Uniform value in [0, bound), rejecting the tail that would otherwise skew short via modulo bias.
uint64_t nextBelow(uint64_t &state, uint64_t bound) {
    if (bound == 0) return 0;
    const uint64_t threshold = (0ULL - bound) % bound; // 2^64 mod bound
    while (true) {
        auto r = splitmix64(state);
        if (r >= threshold) return r % bound;
    }
}

const char *runStateName(RunState state) {
    switch (state) {
        case RunState::Armed: return "armed";
        case RunState::Running: return "running";
        case RunState::Finished: return "finished";
        default: return "idle";
    }
}

RunState runStateFromName(const std::string &name) {
    if (name == "armed") return RunState::Armed;
    if (name == "running") return RunState::Running;
    if (name == "finished") return RunState::Finished;
    return RunState::Idle;
}

}

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
    toggleFullModeKey_ = api->configGetVirtualKey("boss.toggle_full_mode", VK_OEM_PLUS);
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
        bool isDLC = p.value().value("dlc", 0) == 1;
        if (!hasDLC && isDLC) {
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
            bd.dlc = isDLC;
            rd.bosses.push_back(index);
            bd.regionIndex = regionIndex;
            if (!isDLC) {
                basePool_.push_back(index);
            }
        }
    }
    regionCounts_.resize(regions_.size());
    dead_.resize(bosses_.size());
    deadSwapBuf_.resize(bosses_.size());
    regionCountSwapBuf_.resize(regions_.size());

    challenge_.enabled = api->configEnabled("boss.challenge_mode");
    if (!challenge_.enabled) {
        // Challenge mode and random runs are mutually exclusive: only restore a saved run
        // when challenge mode is off, so a stale run file cannot filter the challenge list.
        loadRandomRun();
        return;
    }
    challenge_.maxDeaths = api->configGetInt("boss.challenge_death_count", 0);
    challenge_.loadConfig(api->getModulePath());
}

BossDataSet::~BossDataSet() noexcept {
    challenge_.cleanup();
}

// --- Random run ---

void BossDataSet::selectRandomBosses(uint64_t seed, int count, std::vector<size_t> &out) const {
    out.clear();
    auto poolSize = basePool_.size();
    if (poolSize == 0 || count <= 0) return;
    auto n = std::min(static_cast<size_t>(count), poolSize);

    // Partial Fisher-Yates over pool positions rather than boss indices: basePool_ holds the
    // same bosses in the same order whether or not the DLC is installed, so both players draw
    // the same set even though their absolute boss indices differ.
    std::vector<size_t> positions(poolSize);
    for (size_t i = 0; i < poolSize; i++) {
        positions[i] = i;
    }
    uint64_t state = seed;
    for (size_t i = 0; i < n; i++) {
        auto j = i + static_cast<size_t>(nextBelow(state, poolSize - i));
        std::swap(positions[i], positions[j]);
    }
    positions.resize(n);
    // Display in canonical region order, not draw order, so the list reads as a route.
    std::sort(positions.begin(), positions.end());
    out.reserve(n);
    for (auto p : positions) {
        out.push_back(basePool_[p]);
    }
}

uint64_t BossDataSet::generateSeed() {
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    uint64_t state = static_cast<uint64_t>(counter.QuadPart) ^ (GetTickCount64() * 0x9E3779B97F4A7C15ULL) ^
                     (static_cast<uint64_t>(GetCurrentThreadId()) << 32);
    splitmix64(state); // discard the first draw, which is a thin mix of the raw inputs
    return kSeedMin + nextBelow(state, kSeedMax - kSeedMin + 1);
}

void BossDataSet::startRandomRun(uint64_t seed, int count) {
    std::vector<size_t> selected;
    selectRandomBosses(seed, count, selected);
    {
        std::lock_guard lock(mutex_);
        random_.state = selected.empty() ? RunState::Idle : RunState::Armed;
        random_.seed = seed;
        random_.count = static_cast<int>(selected.size());
        random_.killed = 0;
        random_.finalIgt = 0;
        random_.selected.swap(selected);
    }
    saveRandomRun();
}

void BossDataSet::revealRandomRun() {
    {
        std::lock_guard lock(mutex_);
        if (random_.state != RunState::Armed) return;
        random_.state = RunState::Running;
    }
    saveRandomRun();
}

void BossDataSet::endRandomRun() {
    {
        std::lock_guard lock(mutex_);
        random_ = RandomRun{};
    }
    auto filename = std::wstring(api->getModulePath()) + L"\\RandomRun.txt";
    DeleteFileW(filename.c_str());
}

void BossDataSet::loadRandomRun() {
    auto filename = std::wstring(api->getModulePath()) + L"\\RandomRun.txt";
    std::ifstream ifs(filename.c_str());
    if (!ifs) return;

    RunState state = RunState::Idle;
    uint64_t seed = 0;
    int count = 0;
    int finalIgt = 0;
    std::vector<uint32_t> flagIds;
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto key = line.substr(0, eq);
        auto val = line.substr(eq + 1);
        if (key == "state") {
            state = runStateFromName(val);
        } else if (key == "seed") {
            seed = std::strtoull(val.c_str(), nullptr, 10);
        } else if (key == "count") {
            count = std::strtol(val.c_str(), nullptr, 10);
        } else if (key == "final_igt") {
            finalIgt = std::strtol(val.c_str(), nullptr, 10);
        } else if (key == "bosses") {
            size_t pos = 0;
            while (pos < val.size()) {
                auto comma = val.find(',', pos);
                auto token = val.substr(pos, comma - pos);
                if (!token.empty()) {
                    flagIds.push_back(static_cast<uint32_t>(std::strtoul(token.c_str(), nullptr, 10)));
                }
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
        }
    }
    if (state == RunState::Idle || count <= 0) return;

    // Prefer the stored flag ids so an in-flight run survives a boss data update. Fall back to
    // recomputing from the seed if any of them no longer resolve.
    std::vector<size_t> selected;
    selected.reserve(flagIds.size());
    for (auto flagId : flagIds) {
        auto ite = std::find_if(bosses_.begin(), bosses_.end(), [flagId](const BossData &bd) { return bd.flagId == flagId; });
        if (ite == bosses_.end()) {
            selected.clear();
            break;
        }
        selected.push_back(ite->index);
    }
    if (selected.size() != static_cast<size_t>(count)) {
        selectRandomBosses(seed, count, selected);
    }
    if (selected.empty()) return;

    random_.state = state;
    random_.seed = seed;
    random_.count = static_cast<int>(selected.size());
    random_.finalIgt = finalIgt;
    random_.selected.swap(selected);
}

void BossDataSet::saveRandomRun() {
    RandomRun snapshot;
    {
        std::lock_guard lock(mutex_);
        snapshot = random_;
    }
    auto filename = std::wstring(api->getModulePath()) + L"\\RandomRun.txt";
    if (snapshot.state == RunState::Idle) {
        DeleteFileW(filename.c_str());
        return;
    }
    std::ofstream ofs(filename.c_str());
    if (!ofs) {
        fwprintf(stderr, L"Unable to open %ls for writing\n", filename.c_str());
        return;
    }
    ofs << "# Seeded random boss run. Delete this file to reset the run.\n";
    ofs << "state=" << runStateName(snapshot.state) << "\n";
    ofs << "seed=" << snapshot.seed << "\n";
    ofs << "count=" << snapshot.count << "\n";
    ofs << "final_igt=" << snapshot.finalIgt << "\n";
    ofs << "bosses=";
    for (size_t i = 0; i < snapshot.selected.size(); i++) {
        if (i != 0) ofs << ',';
        ofs << bosses_[snapshot.selected[i]].flagId;
    }
    ofs << "\n";
    ofs.flush();
}

void BossDataSet::initMemoryAddresses() {
    auto addr = api->getGameAddresses();
    gameDataMan_ = addr.gameDataMan;
    fieldArea_ = addr.fieldArea;
}

void BossDataSet::update() {
    if (gameDataMan_ == 0 || fieldArea_ == 0) {
        auto gameAddresses = api->getGameAddresses();
        if (gameDataMan_ == 0) gameDataMan_ = gameAddresses.gameDataMan;
        if (fieldArea_ == 0) fieldArea_ = gameAddresses.fieldArea;
    }

    challenge_.checkForChange(api->getModulePath());
    if (api->screenState() != 0) {
        return;
    }
    auto igt = readInGameTime();
    if (igt <= 0) return;
    updateBosses(igt);
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

void BossDataSet::updateBosses(int igt) {
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
    bool needRunSave = false;
    {
        std::lock_guard lock(mutex_);
        regionCounts_.swap(regionCountSwapBuf_);
        dead_.swap(deadSwapBuf_);
        count_ = cnt;
        if (random_.state == RunState::Armed || random_.state == RunState::Running) {
            int killed = 0;
            for (auto index : random_.selected) {
                if (dead_[index]) killed++;
            }
            random_.killed = killed;
            // Only a revealed run can complete, and it completes exactly once: a later revive
            // must not reopen a finished run or restart the frozen timer.
            if (random_.state == RunState::Running && killed >= random_.count) {
                random_.state = RunState::Finished;
                random_.finalIgt = igt;
                needRunSave = true;
            }
        }
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
    if (needRunSave) {
        saveRandomRun();
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
    out.runState = random_.state;
    out.runSeed = random_.seed;
    out.runCount = random_.count;
    out.runKilled = random_.killed;
    out.runFinalIgt = random_.finalIgt;
    out.runSelected = random_.selected;
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
