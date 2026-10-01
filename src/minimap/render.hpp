#pragma once

#include "data.hpp"
#include "resources.hpp"

#include "api.h"

#include <cstdint>
#include <vector>

namespace er::minimap {

enum class Shape { Rect, Rounded, Circle };

class Renderer {
public:
    explicit Renderer() = default;
    ~Renderer() noexcept;
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;
    Renderer(Renderer &&) = delete;
    Renderer &operator=(Renderer &&) = delete;
    void init(void *context, void *allocFunc, void *freeFunc, void *userData);
    bool render();

private:
    void drawRecipe(const IconRecipe *recipe, Point center, float scale, float angle = 0);
    void drawPlayerMarker(const PlayerMarkerInfo &marker, Point center, float scale);
    void drawTile(const TileView &tile, Point player, float cosMap, float sinMap);
    void renderContent(const MapSnapshot &snapshot);
    void composite(float alpha);
    [[nodiscard]] bool isPointInShape(float x, float y) const;

private:
    bool showDeath_ = true;
    bool showPlayerMarkers_ = true;
    bool fullMap_ = false;

    float minimapWidth_ = 0.f;
    float minimapHeight_ = 0.f;

    bool show_ = true;
    int toggleKey_ = 0;
    int scaleKey_ = 0;
    int gracesKey_ = 0;
    bool showGraces_ = true;
    int landmarksKey_ = 0;
    bool showLandmarks_ = true;
    std::vector<float> widthRatios_ = {0.3f, 0.4f};
    std::vector<float> heightRatios_ = {0.3f, 0.4f};
    std::vector<float> scales_ = {0.75f, 1.f};
    std::vector<float> alphas_ = {0.8f, 0.6f};
    std::vector<bool> isCentered_ = {false, true};
    std::vector<Shape> shapes_;
    size_t currentScaleIndex_ = 0;

    float currentWidthRatio_ = 0.3f;
    float currentHeightRatio_ = 0.3f;
    float currentScale_ = 0.75f;
    float currentAlpha_ = 0.8f;
    bool currentIsCentered_ = false;
    Shape currentShape_ = Shape::Rect;

    std::vector<float> roundings_;
    std::vector<bool> roundingIsPercent_;
    std::vector<bool> rotates_;
    float currentRounding_ = 0.f;
    bool currentRoundingIsPercent_ = true;
    bool currentRotate_ = false;

    float cachedRounding_ = 0.f;

    uint32_t borderColor_ = 0x64FFFFFF; // ABGR (ImGui IM_COL32 format): white, alpha=100
    float borderWidth_ = 1.5f;

    // Per-state extra scale multipliers (cycled via scale_key, like scales_/alphas_)
    std::vector<float> extraTileScales_;
    std::vector<float> extraDecorationScales_;
    std::vector<float> extraPlayerScales_;
    std::vector<float> extraBearingScales_;

    float currentExtraTileScale_ = 1.f;
    float currentExtraDecorationScale_ = 1.f;
    float currentExtraPlayerScale_ = 1.f;
    float currentExtraBearingScale_ = 1.f;

    // Per-frame effective scale values (computed in render())
    float effectiveScale_ = 1.f;
    float effectivePlayerScale_ = 1.f;
    float effectiveDecorationScale_ = 1.f;
    float effectiveBearingRatio_ = 1.f;

    void *offscreen_ = nullptr;
};

} // namespace er::minimap
