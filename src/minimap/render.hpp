#pragma once

#include "api.h"

#include <vector>

namespace er::minimap {

class Renderer {
public:
    void init(void *context, void *allocFunc, void *freeFunc, void *userData);
    bool render();

private:
    void renderMinimap(int index, float posX, float posY, float scale = 1.0f);
    void renderPlayer();

private:
    TextureContext playerTexture_ = {};
    TextureContext arrowTexture_ = {};
    TextureContext roundTableTexture_ = {};
    TextureContext bonfireTexture_ = {};
    std::vector<TextureContext> textures_;

    float minimapWidth_ = 0.f;
    float minimapHeight_ = 0.f;

    bool show_ = true;
    int toggleKey_ = 0;
    int scaleKey_ = 0;
    std::vector<float> widthRatios_ = { 0.3f, 0.4f };
    std::vector<float> heightRatios_ = { 0.3f, 0.4f };
    std::vector<float> scales_ = { 0.75f, 1.f };
    size_t currentScaleIndex_ = 0;

    float currentWidthRatio_ = 0.3f;
    float currentHeightRatio_ = 0.3f;
    float currentScale_ = 0.75f;
    float alpha_ = 0.8f;
};

}
