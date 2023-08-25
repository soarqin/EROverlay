#pragma once

#include "../renderbase.hpp"

#include <cstdint>

namespace er::bosses {

class Render: public RenderBase {
public:
    void render(bool &showFull) override;

private:
    int lastRegionIndex_ = -1;
};

}