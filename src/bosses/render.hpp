#pragma once

#include "../renderbase.hpp"

#include <cstdint>

namespace er::bosses {

class Render: public RenderBase {
public:
    void init() override;
    void render(bool &showFull) override;

private:
    bool allowRevive_ = false;
    int lastRegionIndex_ = -1;
    int popupBossIndex_ = -1;
};

}