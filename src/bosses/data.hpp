#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <mutex>
#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace er::bosses {

struct BossData {
    std::string tip;
    std::string boss;
    std::string place;
    uint32_t flagId = 0;
    uintptr_t offset = 0;
    uint8_t bits = 0;
    size_t index = 0;
    size_t regionIndex = 0;
};

struct RegionData {
    std::string name;
    std::vector<size_t> bosses;
};

class BossDataSet {
public:
    void load(bool hasDLC);
    void loadConfig();
    void saveConfig() const;

    [[nodiscard]] inline const std::vector<BossData> &bosses() const { return bosses_; }
    [[nodiscard]] inline const std::vector<RegionData> &regions() const { return regions_; }
    [[nodiscard]] inline const std::vector<int> &regionCounts() const { return regionCounts_; }
    [[nodiscard]] inline int count() const { return count_; }
    [[nodiscard]] inline uint32_t mapId() const { return mapId_; }
    [[nodiscard]] inline int regionIndex() const { return regionIndex_; }
    [[nodiscard]] inline int total() const { return (int)bosses_.size(); }
    [[nodiscard]] inline std::mutex &mutex() { return mutex_; }
    [[nodiscard]] inline const std::vector<bool> &dead() const { return dead_; }
    [[nodiscard]] inline int deaths() const { return challengeMode_ ? (reachedStrandedGraveyard_ ? playerDeaths_ - challengeDeathsOnStart_ : 0) : playerDeaths_; }
    [[nodiscard]] inline bool challengeMode() const { return challengeMode_; }
    [[nodiscard]] inline int challengeTries() const { return challengeTries_; }
    [[nodiscard]] inline int challengeBest() const { return challengeBest_; }
    [[nodiscard]] int readInGameTime() const;

    void initMemoryAddresses();
    void update();
    void revive(int index);

private:
    void updateBosses();
    void updateChallengeMode();
    void checkForConfigChange();
    [[nodiscard]] int readDeathCount() const;

private:
    std::vector<BossData> bosses_;
    std::vector<RegionData> regions_;
    std::vector<int> regionCounts_;
    std::map<uint32_t, int> regionMap_;
    int count_ = 0;
    uint32_t mapId_ = 0;
    int regionIndex_ = -1;
    uintptr_t gameDataMan_ = 0;
    uintptr_t fieldArea_ = 0;
    bool flagResolved_ = false;
    std::vector<bool> dead_;

    bool challengeMode_ = false;
    int challengeDeathCount_ = 0;

    // Personal Best
    int challengeBest_ = 0;

    // Deaths on reaching Stranded Graveyard, real deaths is counted by deaths_ - challengeDeathsOnStart_
    int challengeDeathsOnStart_ = 0;
    // Total tries
    int challengeTries_ = 0;
    // Current death count
    int playerDeaths_ = 0;

    // This flag is used to check if you reached the Stranded Graveyard,
    // Deaths is counted after this flag is set.
    uintptr_t reachStrandedGraveyardFlagOffset_ = 0;
    uint8_t reachStrandedGraveyardFlagBits_ = 0;
    bool reachedStrandedGraveyard_ = true;

    HANDLE changeEvent_ = INVALID_HANDLE_VALUE;
    std::mutex mutex_;
};

extern BossDataSet gBossDataSet;

}
