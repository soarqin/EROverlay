#pragma once

#include <mutex>
#include <string>
#include <vector>
#include <cstdint>
#include <map>

namespace er::bosses {

struct BossData {
    std::string displayName;
    std::string region;
    std::string boss;
    std::string place;
    uintptr_t offset = 0;
    uint8_t bits = 0;
    size_t index;
    size_t regionIndex = 0;
};

struct RegionData {
    std::string name;
    std::vector<size_t> bosses;
    int count = 0;
};

class BossDataSet {
public:
    void load(const std::wstring &filename);

    [[nodiscard]] inline const std::vector<BossData>& bosses() const { return bosses_; }
    [[nodiscard]] inline const std::vector<RegionData>& regions() const { return regions_; }
    [[nodiscard]] inline int count() const { return count_; }
    [[nodiscard]] inline uint32_t mapId() const { return mapId_; }
    [[nodiscard]] inline int regionIndex() const { return regionIndex_; }
    [[nodiscard]] inline int total() const { return (int)bosses_.size(); }
    [[nodiscard]] inline std::mutex& mutex() { return mutex_; }
    [[nodiscard]] inline const std::vector<bool>& alive() const { return alive_; }

    void initMemoryAddresses();
    void update();

private:
    std::vector<BossData> bosses_;
    std::vector<RegionData> regions_;
    std::map<uint32_t, int> regionMap_;
    int count_ = 0;
    uint32_t mapId_ = 0;
    int regionIndex_ = -1;
    uintptr_t eventFlagMan_ = 0;
    uintptr_t flagAddress_ = 0;
    uintptr_t fieldArea_ = 0;
    std::vector<bool> alive_;
    std::mutex mutex_;
};

extern BossDataSet gBossDataSet;

}
