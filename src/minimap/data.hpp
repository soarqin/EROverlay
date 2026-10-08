#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <utility>
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
    bool cleared = false;
};
struct PlayerMarkerInfo {
    int32_t id = -1;
    float x = 0, y = 0;
    uint8_t number = 0;
    uint8_t map = 0;
};
class PlayerMarkers {
public:
    [[nodiscard]] size_t size() const { return count_; }
    [[nodiscard]] bool empty() const { return !count_; }
    [[nodiscard]] const PlayerMarkerInfo *begin() const { return values_.data(); }
    [[nodiscard]] const PlayerMarkerInfo *end() const { return values_.data() + count_; }
    [[nodiscard]] const PlayerMarkerInfo &operator[](size_t index) const { return values_[index]; }
    void clear() { count_ = 0; }
    void push_back(const PlayerMarkerInfo &marker) {
        if (count_ < values_.size())
            values_[count_++] = marker;
    }

private:
    std::array<PlayerMarkerInfo, 5> values_{};
    uint8_t count_ = 0;
};
struct MapSnapshot {
    ERMapState state{};
    Camera camera;
    bool onGUI = true;
    bool valid = false;
    bool roundtable = false;
    uint32_t homeIcon = 48;
    std::span<const DecorationInfo> decorations;
    PlayerMarkers playerMarkers;

private:
    friend class Data;
    // Snapshot copies share immutable decoration storage; no per-frame vector
    // allocation/copy is needed on either the update or render thread.
    std::shared_ptr<const std::vector<DecorationInfo>> decorationStorage_;
};

// A legacy-map conversion found in a native tree, keyed by tree head and raw map.
struct MapConversion {
    bool found = false;
    uint32_t target = 0;
    float offset[3]{};
};
using MapConversionCache = std::map<std::pair<uintptr_t, uint32_t>, MapConversion>;

class Data {
public:
    void update();
    [[nodiscard]] MapSnapshot snapshot() const;

private:
    mutable std::mutex mutex_;
    MapSnapshot snapshot_;
    uint64_t markerRefresh_ = 0;
    // Update-thread only: conversion trees are static within a game context.
    MapConversionCache conversions_;
    uint64_t conversionGeneration_ = 0;
};
extern Data gData;
} // namespace er::minimap
