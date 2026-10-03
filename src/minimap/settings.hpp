#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "api.h"

namespace er::minimap {

enum class Shape { Rect, Rounded, Circle };

enum class HorizontalAnchor { Left, Right };
enum class VerticalAnchor { Top, Bottom };

struct Margin {
    float value = 0.f;
    bool isPercent = false;
};

struct Position {
    bool centered = false;
    HorizontalAnchor horizontalAnchor = HorizontalAnchor::Right;
    VerticalAnchor verticalAnchor = VerticalAnchor::Top;
    Margin horizontalMargin;
    Margin verticalMargin;
};

struct Preset {
    std::string name;
    float widthRatio = 0.3f;
    float heightRatio = 0.3f;
    float zoom = 0.75f;
    float opacity = 0.8f;
    Position position;
    bool rotate = false;
    Shape shape = Shape::Rect;
    float rounding = 0.2f;
    bool roundingIsPercent = true;
    float mapScale = 1.f;
    float decorationScale = 1.f;
    float playerScale = 1.f;
    float compassScale = 1.f;
};

struct Settings {
    int toggleKey = 'M';
    int cycleKey = 'M';
    int gracesKey = 'N';
    int landmarksKey = 'N';
    bool showGraces = true;
    bool showLandmarks = true;
    bool showDeath = true;
    bool showBeacons = true;
    bool fullMap = false;
    uint32_t borderColor = 0x64FFFFFF; // ABGR: white with alpha 100.
    float borderWidth = 1.5f;
    std::vector<Preset> presets;
};

// Uses sectioned keys only. Values from the former flat configuration are ignored.
[[nodiscard]] Settings loadSettings(const EROverlayAPI &api);

} // namespace er::minimap
