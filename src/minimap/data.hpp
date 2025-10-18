#pragma once

#include <chrono>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <tuple>
#include <string>
#include <cstdint>

namespace er::minimap {

struct Location {
    int32_t mapId; 
    float x;
    float y;
    int32_t underground;
    float rad;
};

struct BonfireInfo {
    uint64_t id;
    int32_t textId;
    uint32_t eventFlagId;
    int32_t layer;
    float x;
    float y;
    float localX;
    float localY;
    int32_t sortKey;
    mutable uintptr_t eventFlagAddress = 0;
    mutable uint8_t eventFlagBits = 0;

    [[nodiscard]] bool isUnlocked() const;
};
    
class Data {
public:
    void load();
    void update();
    [[nodiscard]] inline bool paramsLoaded() const { return paramsLoaded_; }
    [[nodiscard]] inline const Location &location() const { return location_; }
    [[nodiscard]] inline bool onGUI() const { return onGUI_; }
    [[nodiscard]] inline const std::vector<BonfireInfo> &bonfires(int32_t layer) const { return bonfires_[layer]; }
    [[nodiscard]] std::tuple<const BonfireInfo *, const BonfireInfo *> bonfiresAround(int32_t layer, int u, int v) const;
    
private:
    uintptr_t csMenuManImp_ = 0;
    size_t locationOffset_ = 0;
    Location location_ = {};
    bool onGUI_ = false;
    bool paramsLoaded_ = false;

    std::vector<BonfireInfo> bonfires_[3];
    std::unordered_map<int32_t, std::tuple<const BonfireInfo *, const BonfireInfo *>> bonfiresAround_[3];
};

extern Data gData;

}
