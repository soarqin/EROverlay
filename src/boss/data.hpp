#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace er::bosses {

// Game memory offsets
namespace offsets {
    constexpr uintptr_t kInGameTime = 0xA0;
    constexpr uintptr_t kDeathCount = 0x94;
    constexpr uintptr_t kMapIdPre1_12 = 0xE4;
    constexpr uintptr_t kMapIdPost1_12 = 0xE8;
    constexpr uint64_t kVersionThreshold1_12 = 0x0002000200000000ULL;
}

constexpr uint32_t kStrandedGraveyardFlagId = 101;
constexpr int kUpdateTickMask = 0x1F; // update every 32 ticks (~60Hz / 32 ≈ ~2Hz)

struct BossData {
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

// Thread-safe snapshot of mutable state for the render thread.
// Populated by BossDataSet::fillRenderState() under mutex, then read freely by the render thread.
struct RenderState {
    int count = 0;
    int deaths = 0;
    int regionIndex = -1;
    int inGameTime = 0;
    bool challengeMode = false;
    int challengeBest = 0;
    int challengeTries = 0;
    std::vector<uint8_t> dead;
    std::vector<int> regionCounts;
};

// Challenge mode state and persistence (config read/write for Challenge.txt).
struct ChallengeState {
    bool enabled = false;
    int maxDeaths = 0;
    int best = 0;
    int deathsOnStart = 0;
    int tries = 0;
    int playerDeaths = 0;

    uintptr_t reachFlagOffset = 0;
    uint8_t reachFlagBits = 0;
    bool reachedGraveyard = true;

    HANDLE changeEvent = INVALID_HANDLE_VALUE;

    [[nodiscard]] int deaths() const { return enabled ? (reachedGraveyard ? playerDeaths - deathsOnStart : 0) : playerDeaths; }

    void loadConfig(const wchar_t *modulePath);
    void saveConfig(const wchar_t *modulePath);
    void checkForChange(const wchar_t *modulePath);
    void cleanup() noexcept;
};

class BossDataSet {
public:
    ~BossDataSet() noexcept;

    void load(bool hasDLC);
    void initMemoryAddresses();
    void update();
    void revive(int index);

    // Immutable after load() — safe to read from any thread without synchronization.
    [[nodiscard]] int toggleFullModeKey() const { return toggleFullModeKey_; }
    [[nodiscard]] const std::vector<BossData> &bosses() const { return bosses_; }
    [[nodiscard]] const std::vector<RegionData> &regions() const { return regions_; }
    [[nodiscard]] int total() const { return static_cast<int>(bosses_.size()); }

    // Thread-safe snapshot for the render thread: copies all mutable state under mutex.
    // The caller should keep a RenderState member to reuse vector capacity across frames.
    void fillRenderState(RenderState &out);

private:
    void updateBosses();
    void updateNormalMode();
    void updateChallengeMode();
    [[nodiscard]] int readDeathCount() const;
    [[nodiscard]] int readInGameTime() const;

private:
    int toggleFullModeKey_ = 0;
    std::vector<BossData> bosses_;
    std::vector<RegionData> regions_;
    std::map<uint32_t, int> regionMap_;

    // Mutable state (protected by mutex_)
    std::mutex mutex_;
    int count_ = 0;
    uint32_t mapId_ = 0;
    int regionIndex_ = -1;
    std::vector<uint8_t> dead_;
    std::vector<int> regionCounts_;

    // Boss flag resolution
    uintptr_t gameDataMan_ = 0;
    uintptr_t fieldArea_ = 0;
    bool flagResolved_ = false;

    // Swap buffers (avoid per-frame heap allocation in updateBosses)
    std::vector<uint8_t> deadSwapBuf_;
    std::vector<int> regionCountSwapBuf_;

    ChallengeState challenge_;
};

extern BossDataSet gBossDataSet;

}
