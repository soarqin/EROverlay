#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "util/assets.hpp"

namespace er::minimap {

struct Point {
    float x = 0, y = 0;
};
using Matrix = std::array<float, 6>; // x=a*x+c*y+tx, y=b*x+d*y+ty
constexpr Matrix IDENTITY_MATRIX{1, 0, 0, 1, 0, 0};
struct Bounds {
    float left = 0, top = 0, right = 0, bottom = 0;
};
struct VectorStyle {
    uint32_t color = 0;
    float width = 0;
};
struct VectorCommand {
    uint8_t op;
    Point to, control;
    uint16_t fill0 = 0, fill1 = 0, stroke = 0;
};
struct VectorShape {
    Bounds bounds;
    std::vector<VectorStyle> fills, strokes;
    std::vector<VectorCommand> commands;
};
struct IconLayer {
    std::string image;
    uint32_t width = 0, height = 0;
    Matrix matrix = IDENTITY_MATRIX;
    VectorShape shape;
    std::vector<uint16_t> depth;
    [[nodiscard]] bool bitmap() const { return !image.empty(); }
};
struct IconRecipe {
    std::vector<IconLayer> layers;
    Bounds bounds;
};

class GfxMovie {
public:
    [[nodiscard]] bool parse(util::Bytes bytes);
    [[nodiscard]] bool icon(uint32_t frame, IconRecipe &recipe) const;
    [[nodiscard]] bool special(const std::string &path, IconRecipe &recipe) const;
    [[nodiscard]] bool image(const std::string &name, IconRecipe &recipe) const;
    [[nodiscard]] uint32_t iconFrameCount() const;
    [[nodiscard]] uint32_t iconLayerCount(uint32_t frame) const;

private:
    struct Placement {
        uint32_t character = 0;
        Matrix matrix = IDENTITY_MATRIX;
        std::string name;
        bool visible = true;
        bool effects = false;
    };
    struct Character {
        std::string image;
        uint32_t width = 0, height = 0;
        VectorShape shape;
        bool vector = false;
        std::vector<std::map<uint16_t, Placement>> frames;
    };
    [[nodiscard]] bool parseTimeline(util::Bytes bytes, uint32_t count, Character &character, unsigned nesting);
    [[nodiscard]] bool flatten(uint32_t character, uint32_t frame, const Matrix &matrix, std::vector<uint16_t> &depth, std::vector<uint32_t> &parents, IconRecipe &recipe) const;
    [[nodiscard]] uint32_t named(const std::string &path) const;
    std::unordered_map<uint32_t, Character> characters_;
    std::unordered_map<uint32_t, std::pair<uint16_t, std::vector<uint8_t>>> shapeBytes_;
    uint32_t icons_ = 0;
    uint32_t worldMapItem_ = 0;
};

[[nodiscard]] Point transform(const Matrix &matrix, Point point);
[[nodiscard]] Matrix multiply(const Matrix &outer, const Matrix &inner);

} // namespace er::minimap
