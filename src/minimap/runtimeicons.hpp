#pragma once

#include "gfx.hpp"

namespace er::minimap {

struct RuntimeIcon {
    uint32_t id;
    util::Bytes lossless2;
    util::Bytes matrix;
};

// Decode SWF DefineBitsLossless2 (palette RGBA or premultiplied ARGB) into
// a straight-alpha RGBA8 DDS. The caller bounds the whole imported catalog.
[[nodiscard]] bool decodeRuntimeIcon(const RuntimeIcon &source, IconRecipe &recipe, std::vector<uint8_t> &dds, size_t byteLimit);

} // namespace er::minimap
