#pragma once

#include <string>
#include <cstdint>

namespace er::achievements {

class Renderer {
public:
    void init(void *context, void *allocFunc, void *freeFunc, void *userData);
    bool render();
};

}
