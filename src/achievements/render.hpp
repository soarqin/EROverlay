#pragma once

#include <string>
#include <cstdint>

namespace er::achievements {

class Renderer {
public:
    void init(void *context, void *allocFunc, void *freeFunc, void *userData);
    bool render();

private:
    float posX_ = -0.0001f;
    float posY_ = 0.05f;
    float width_ = 0.1f;
    float height_ = 0.9f;
};

}
