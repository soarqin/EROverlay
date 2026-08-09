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

// Generated random run seeds are 8 to 10 decimal digits, short enough to read out loud.
constexpr uint64_t kSeedMin = 10000000ULL;
constexpr uint64_t kSeedMax = 9999999999ULL;
constexpr int kSeedMaxDigits = 10;

struct BossData {
    std::string boss;
    std::string place;
    uint32_t flagId = 0;
    uintptr_t offset = 0;
    uint8_t bits = 0;
    size_t index = 0;
    size_t regionIndex = 0;
    bool dlc = false;
};

struct RegionData {
    std::string name;
    std::vector<size_t> bosses;
};

// Random run lifecycle.
// Armed holds a computed but hidden selection: both players randomize the same seed, then
// reveal together, so nobody gains planning time by randomizing first.
enum class RunState : int {
    Idle = 0,
    Armed = 1,
    Running = 2,
    Finished = 3,
};

// Seeded random boss run state (protected by BossDataSet::mutex_).
struct RandomRun {
    RunState state = RunState::Idle;
    uint64_t seed = 0;
    int count = 0;
    int killed = 0;
    int finalIgt = 0;
    std::vector<size_t> selected; // indices into BossDataSet::bosses()
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

    RunState runState = RunState::Idle;
    uint64_t runSeed = 0;
    int runCount = 0;
    int runKilled = 0;
    int runFinalIgt = 0;
    std::vector<size_t> runSelected;
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

    // Random run control, called from the render thread.
    void startRandomRun(uint64_t seed, int count);
    void revealRandomRun();
    void endRandomRun();

    // Non-deterministic seed for the "leave the seed box empty" case.
    [[nodiscard]] static uint64_t generateSeed();

    // Immutable after load() — safe to read from any thread without synchronization.
    [[nodiscard]] int toggleFullModeKey() const { return toggleFullModeKey_; }
    [[nodiscard]] const std::vector<BossData> &bosses() const { return bosses_; }
    [[nodiscard]] const std::vector<RegionData> &regions() const { return regions_; }
    [[nodiscard]] int total() const { return static_cast<int>(bosses_.size()); }
    [[nodiscard]] int randomPoolSize() const { return static_cast<int>(basePool_.size()); }

    // Thread-safe snapshot for the render thread: copies all mutable state under mutex.
    // The caller should keep a RenderState member to reuse vector capacity across frames.
    void fillRenderState(RenderState &out);

private:
    void updateBosses(int igt);
    void updateNormalMode();
    void updateChallengeMode();
    [[nodiscard]] int readDeathCount() const;
    [[nodiscard]] int readInGameTime() const;

    // Selection depends only on (seed, count) and the canonical base-game boss order,
    // so the same inputs give the same list on every machine.
    void selectRandomBosses(uint64_t seed, int count, std::vector<size_t> &out) const;
    void loadRandomRun();
    void saveRandomRun();

private:
    int toggleFullModeKey_ = 0;
    std::vector<BossData> bosses_;
    std::vector<RegionData> regions_;
    std::map<uint32_t, int> regionMap_;

    // Indices of base-game bosses in file order. DLC bosses are never randomized, and this
    // sequence is identical with or without the DLC installed, which is what makes seeds portable.
    std::vector<size_t> basePool_;

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
    RandomRun random_;
};

extern BossDataSet gBossDataSet;

}
