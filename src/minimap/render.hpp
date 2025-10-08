#pragma once

#include "api.h"

#include <string>
#include <vector>
#include <cstdint>

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
    float minimapWidthRatio_ = .3f;
    float minimapHeightRatio_ = .3f;
    float minimapWidth_ = 400.0f;
    float minimapHeight_ = 400.0f;
    float mapScale_ = 0.75f;
    float mapAlpha_ = 0.8f;
};

}
