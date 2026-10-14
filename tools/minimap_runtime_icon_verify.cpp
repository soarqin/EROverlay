// SWF format fixtures: palette rows are padded to four bytes; kind 5 is ARGB.
#include <array>
#include <cassert>
#include <cstdio>

#include "minimap/runtimeicons.hpp"

int main() {
    using namespace er::minimap;
    constexpr std::array<uint8_t, 21> argb{0, 0, 5, 2, 0, 1, 0, 120, 156, 107, 112, 80, 16, 96, 0, 2, 0, 6, 216, 0, 241};
    constexpr std::array<uint8_t, 27> palette{0, 0, 3, 3, 0, 2, 0, 1, 120, 156, 115, 80, 16, 104, 96, 0, 1, 70, 16, 98, 100, 0, 0, 13, 93, 0, 244};
    constexpr std::array<uint8_t, 29> badPalette{0, 0, 3, 3, 0, 2, 0, 1, 120, 156, 115, 80, 16, 104, 96, 0, 2, 38, 70, 6, 6, 70, 32, 4, 0, 13, 109, 0, 246};
    constexpr std::array<uint8_t, 1> identity{0};
    IconRecipe recipe;
    std::vector<uint8_t> dds;
    for (auto data: {er::util::Bytes(argb), er::util::Bytes(palette)}) {
        assert(decodeRuntimeIcon({0, data, identity}, recipe, dds, 1024));
        er::util::DdsImage image;
        assert(er::util::parseDds(dds, image) && image.format == 28 && image.levels.size() == 1 && image.levels[0].offset == 148);
        assert(image.width == (data[2] == 5 ? 2 : 3) && image.height == (data[2] == 5 ? 1 : 2));
        assert(dds[148] == 128 && dds[149] == 64 && dds[150] == 32 && dds[151] == 128);
        assert(dds[152] == 0 && dds[153] == 0 && dds[154] == 0 && dds[155] == 0);
        if (data[2] == 3) {
            assert(dds[160] == 0 && dds[163] == 0 && dds[164] == 128 && dds[167] == 128);
        }
        assert(recipe.layers.size() == 1 && recipe.layers[0].matrix == IDENTITY_MATRIX && recipe.bounds.right == image.width && recipe.bounds.bottom == image.height);
        assert(!decodeRuntimeIcon({0, data, identity}, recipe, dds, 148 + image.width * image.height * 4 - 1) && dds.empty());
        assert(!decodeRuntimeIcon({0, data.first(7), identity}, recipe, dds, 1024));
        assert(!decodeRuntimeIcon({0, data, {}}, recipe, dds, 1024));
    }
    assert(!decodeRuntimeIcon({0, badPalette, identity}, recipe, dds, 1024) && dds.empty());
    auto oversized = argb;
    oversized[3] = 1;
    oversized[4] = 4; // 1025 pixels: reject before inflate or allocation.
    assert(!decodeRuntimeIcon({0, oversized, identity}, recipe, dds, 64 * 1024 * 1024));
    std::puts("PASS: SWF palette padding, ARGB ordering, straight alpha, transparent pixels, DDS dimensions, matrix bounds, bad palette, truncated input and allocation limits.");
}
