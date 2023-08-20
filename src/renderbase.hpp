#pragma once

namespace er {

class RenderBase {
public:
    virtual ~RenderBase() = default;
    virtual void render(bool &showFull) = 0;
};

}
