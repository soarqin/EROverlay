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
    float mapAlpha_ = 0.f;
};

}
