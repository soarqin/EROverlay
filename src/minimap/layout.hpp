#pragma once

#include "resources.hpp"

namespace er::minimap {

[[nodiscard]] inline ERGameLayout gameLayout() {
    ERGameLayout layout{sizeof(ERGameLayout), 0x80, 0x250, 0x720, 0x730, 0x350, 0x348, 1, 7, 194};
    if (nativeApi && nativeApi->size >= offsetof(EROverlayNativeAPI, readGameLayout) + sizeof(nativeApi->readGameLayout) && nativeApi->readGameLayout) {
        ERGameLayout selected{};
        if (nativeApi->readGameLayout(&selected) && selected.size == sizeof(ERGameLayout))
            layout = selected;
    }
    return layout;
}

} // namespace er::minimap
