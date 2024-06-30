#pragma once

#include "../renderbase.hpp"

#include <cstdint>

namespace er::bosses {

class Render : public RenderBase {
public:
    void init() override;
    void render(bool &showFull) override;

private:
    bool allowRevive_ = false;
    int lastRegionIndex_ = -1;
    int popupBossIndex_ = -1;
    float posX_ = -10.f;
    float posY_ = 10.f;
    float width_ = 0.12f;
    float height_ = 0.9f;
};

}