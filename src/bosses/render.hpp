#pragma once

#include "../renderbase.hpp"

#include <cstdint>

namespace er::bosses {

class Render: public RenderBase {
public:
    void render(bool &showFull) override;

private:
    int counter_ = 0x1F;
    int lastRegionIndex_ = -1;
};

}