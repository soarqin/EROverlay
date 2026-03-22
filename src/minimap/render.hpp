#pragma once

#include "data.hpp"

#include "api.h"

#include <vector>
#include <cstdint>

namespace er::minimap {

enum class Shape { Rect, Rounded, Circle };

class Renderer {
public:
    void init(void *context, void *allocFunc, void *freeFunc, void *userData);
    bool render();

private:
    struct TileInfo {
        TextureContext *texture;
        float posX, posY, width, height;
        float texWidth, texHeight;
        float clipU, clipV;  // texture offset from clipping
    };
    [[nodiscard]] bool prepareTile(int index, float &posX, float &posY, float scale, TileInfo &out);
    void renderMinimap(int index, float posX, float posY, float scale = 1.0f);
    void renderShapedMinimap(int index, float posX, float posY, float scale = 1.0f);
    void renderPlayer();
    [[nodiscard]] bool isPointInShape(float x, float y) const;

private:
    const SpriteInfo *playerSprite_ = nullptr;
    const SpriteInfo *arrowSprite_ = nullptr;
    const SpriteInfo *roundTableSprite_ = nullptr;
    std::vector<TextureContext> textures_;

    float minimapWidth_ = 0.f;
    float minimapHeight_ = 0.f;

    bool show_ = true;
    int toggleKey_ = 0;
    int scaleKey_ = 0;
    int gracesKey_ = 0;
    bool showGraces_ = true;
    int landmarksKey_ = 0;
    bool showLandmarks_ = true;
    std::vector<float> widthRatios_ = { 0.3f, 0.4f };
    std::vector<float> heightRatios_ = { 0.3f, 0.4f };
    std::vector<float> scales_ = { 0.75f, 1.f };
    std::vector<float> alphas_ = { 0.8f, 0.6f };
    std::vector<bool> isCentered_ = { false, true };
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
    float currentRounding_ = 0.f;
    bool currentRoundingIsPercent_ = true;

    float cachedRounding_ = 0.f;

    uint32_t borderColor_ = 0x64FFFFFF;  // ABGR (ImGui IM_COL32 format): white, alpha=100
    float borderWidth_ = 1.5f;
};

}
