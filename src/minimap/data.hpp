#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

#include "nativeapi.h"

namespace er::minimap {
enum class DecorationSource : uint8_t { Grace, Landmark };
struct Camera {
    float yawCos = 1;
    int32_t dummy0 = 0;
    float yawSin = 0;
    int32_t dummy1 = 0;
};
struct DecorationInfo {
    uint64_t id = 0;
    uint32_t iconId = 0;
    uint32_t maps = 0;
    float x = 0, y = 0;
    float rotationRad = 0;
    DecorationSource source = DecorationSource::Landmark;
    bool areaIcon = false;
};
struct MapSnapshot {
    ERMapState state{};
    Camera camera;
    bool onGUI = true;
    bool valid = false;
    bool roundtable = false;
    uint32_t homeIcon = 48;
    std::vector<DecorationInfo> decorations;
};

class Data {
public:
    void update();
    [[nodiscard]] MapSnapshot snapshot() const;

private:
    mutable std::mutex mutex_;
    MapSnapshot snapshot_;
    uint64_t markerRefresh_ = 0;
};
extern Data gData;
} // namespace er::minimap
