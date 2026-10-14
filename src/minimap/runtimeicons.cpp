#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_ZLIB
#define STBI_SUPPORT_ZLIB
#include "../stb_image.h"

#include "runtimeicons.hpp"

namespace er::minimap {

bool decodeRuntimeIcon(const RuntimeIcon &source, IconRecipe &recipe, std::vector<uint8_t> &dds, size_t byteLimit) {
    recipe = {};
    dds.clear();
    if (source.lossless2.size() > 1024 * 1024 || source.matrix.size() > 64)
        return false;
    util::AssetReader reader(source.lossless2);
    uint16_t character, width, height;
    uint8_t kind, paletteSize = 0;
    if (!reader.read(character) || !reader.read(kind) || (kind != 3 && kind != 5) || !reader.read(width) || !reader.read(height) || !width || !height || width > 1024 ||
        height > 1024 || (kind == 3 && !reader.read(paletteSize)))
        return false;
    size_t pixels = size_t(width) * height;
    if (byteLimit < 148 || pixels > (byteLimit - 148) / 4)
        return false;
    IconLayer layer;
    if (!parseGfxMatrix(source.matrix, layer.matrix))
        return false;
    layer.width = width;
    layer.height = height;
    layer.image = "mfg:" + std::to_string(source.id);
    const size_t paletteBytes = kind == 3 ? (size_t(paletteSize) + 1) * 4 : 0;
    const size_t stride = kind == 3 ? (size_t(width) + 3) & ~size_t(3) : size_t(width) * 4;
    std::vector<uint8_t> raw(paletteBytes + stride * height);
    auto compressed = source.lossless2.subspan(reader.position());
    if (compressed.empty() || stbi_zlib_decode_buffer(reinterpret_cast<char *>(raw.data()), static_cast<int>(raw.size()), reinterpret_cast<const char *>(compressed.data()),
                                                      static_cast<int>(compressed.size())) != static_cast<int>(raw.size()))
        return false;
    dds.assign(148 + pixels * 4, 0);
    std::memcpy(dds.data(), "DDS ", 4);
    auto put = [&](size_t offset, uint32_t value) { std::memcpy(dds.data() + offset, &value, sizeof(value)); };
    put(4, 124);
    put(8, 0x100F);
    put(12, height);
    put(16, width);
    put(20, width * 4);
    put(28, 1);
    put(76, 32);
    put(80, 4);
    put(84, 0x30315844); // DX10
    put(108, 0x1000);
    put(128, 28); // DXGI_FORMAT_R8G8B8A8_UNORM
    put(132, 3);
    put(140, 1);
    for (size_t y = 0; y < height; ++y)
        for (size_t x = 0; x < width; ++x) {
            const uint8_t *pixel;
            if (kind == 3) {
                uint8_t index = raw[paletteBytes + y * stride + x];
                if (index > paletteSize) {
                    dds.clear();
                    return false;
                }
                pixel = raw.data() + size_t(index) * 4;
            } else
                pixel = raw.data() + y * stride + x * 4;
            auto target = dds.data() + 148 + (y * width + x) * 4;
            uint32_t alpha = pixel[kind == 3 ? 3 : 0];
            for (size_t channel = 0; channel < 3; ++channel) {
                uint32_t premultiplied = pixel[kind == 3 ? channel : channel + 1];
                target[channel] = alpha ? static_cast<uint8_t>(std::min(255u, (premultiplied * 255 + alpha / 2) / alpha)) : 0;
            }
            target[3] = static_cast<uint8_t>(alpha);
        }
    bool first = true;
    for (auto corner: {Point{}, Point{float(width), 0}, Point{0, float(height)}, Point{float(width), float(height)}}) {
        auto point = transform(layer.matrix, corner);
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || std::abs(point.x) > 4096 || std::abs(point.y) > 4096) {
            dds.clear();
            return false;
        }
        if (first) {
            recipe.bounds = {point.x, point.y, point.x, point.y};
            first = false;
        } else {
            recipe.bounds.left = std::min(recipe.bounds.left, point.x);
            recipe.bounds.top = std::min(recipe.bounds.top, point.y);
            recipe.bounds.right = std::max(recipe.bounds.right, point.x);
            recipe.bounds.bottom = std::max(recipe.bounds.bottom, point.y);
        }
    }
    if (recipe.bounds.right <= recipe.bounds.left || recipe.bounds.bottom <= recipe.bounds.top) {
        dds.clear();
        return false;
    }
    recipe.layers.push_back(std::move(layer));
    return true;
}

} // namespace er::minimap
