#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

#include "gfx.hpp"

namespace er::minimap {
namespace {
class Bits {
public:
    explicit Bits(util::Bytes bytes, size_t offset = 0) : bytes_(bytes), bit_(offset * 8) {}
    bool get(uint32_t count, uint32_t &value) {
        if (count > 32 || bit_ > bytes_.size() * 8 || count > bytes_.size() * 8 - bit_)
            return false;
        value = 0;
        for (uint32_t i = 0; i < count; ++i, ++bit_)
            value = (value << 1) | ((bytes_[bit_ / 8] >> (7 - bit_ % 8)) & 1);
        return true;
    }
    bool signedValue(uint32_t count, float divisor, float &value) {
        uint32_t bits;
        if (!get(count, bits))
            return false;
        int64_t number = bits;
        if (count && (bits & (1u << (count - 1))))
            number -= int64_t{1} << count;
        value = static_cast<float>(number) / divisor;
        return true;
    }
    [[nodiscard]] size_t position() const { return (bit_ + 7) / 8; }

private:
    util::Bytes bytes_;
    size_t bit_;
};

bool readRectangle(util::AssetReader &reader, Bounds &bounds) {
    Bits bits(reader.bytes(), reader.position());
    uint32_t count;
    return bits.get(5, count) && bits.signedValue(count, 20, bounds.left) && bits.signedValue(count, 20, bounds.right) && bits.signedValue(count, 20, bounds.top) &&
           bits.signedValue(count, 20, bounds.bottom) && reader.seek(bits.position());
}
bool readMatrix(util::AssetReader &reader, Matrix &matrix) {
    Bits bits(reader.bytes(), reader.position());
    matrix = IDENTITY_MATRIX;
    uint32_t enabled, count;
    if (!bits.get(1, enabled))
        return false;
    if (enabled && (!bits.get(5, count) || !bits.signedValue(count, 65536, matrix[0]) || !bits.signedValue(count, 65536, matrix[3])))
        return false;
    if (!bits.get(1, enabled))
        return false;
    if (enabled && (!bits.get(5, count) || !bits.signedValue(count, 65536, matrix[1]) || !bits.signedValue(count, 65536, matrix[2])))
        return false;
    return bits.get(5, count) && bits.signedValue(count, 20, matrix[4]) && bits.signedValue(count, 20, matrix[5]) && reader.seek(bits.position());
}
bool readColorTransform(util::AssetReader &reader, bool &effects) {
    Bits bits(reader.bytes(), reader.position());
    uint32_t add, mult, count;
    if (!bits.get(1, add) || !bits.get(1, mult) || !bits.get(4, count))
        return false;
    float value;
    for (int i = 0; mult && i < 4; ++i) {
        if (!bits.signedValue(count, 256, value))
            return false;
        if (value != 1)
            effects = true;
    }
    for (int i = 0; add && i < 4; ++i) {
        if (!bits.signedValue(count, 1, value))
            return false;
        if (value != 0)
            effects = true;
    }
    return reader.seek(bits.position());
}
bool skipFilters(util::AssetReader &reader) {
    uint8_t count, kind;
    util::Bytes ignored;
    if (!reader.read(count))
        return false;
    for (uint8_t i = 0; i < count; ++i) {
        if (!reader.read(kind))
            return false;
        size_t size;
        switch (kind) {
            case 0:
                size = 23;
                break;
            case 1:
                size = 9;
                break;
            case 2:
                size = 15;
                break;
            case 3:
                size = 27;
                break;
            case 6:
                size = 80;
                break;
            case 4:
            case 7: {
                uint8_t colors;
                if (!reader.read(colors))
                    return false;
                size = colors * 5 + 19;
                break;
            }
            case 5: {
                uint8_t columns, rows;
                if (!reader.read(columns) || !reader.read(rows))
                    return false;
                size = 13 + columns * rows * 4;
                break;
            }
            default:
                return false;
        }
        if (!reader.take(size, ignored))
            return false;
    }
    return true;
}
bool readShape(util::Bytes bytes, uint16_t code, VectorShape &shape) {
    util::AssetReader reader(bytes, 2);
    if (!readRectangle(reader, shape.bounds))
        return false;
    if (code == 83) {
        Bounds edge;
        uint8_t flags;
        if (!readRectangle(reader, edge) || !reader.read(flags))
            return false;
    }
    auto countStyles = [&](uint16_t &count) {
        uint8_t small;
        if (!reader.read(small))
            return false;
        count = small;
        return small != 255 || reader.read(count);
    };
    uint16_t count;
    if (!countStyles(count) || count > 1024)
        return false;
    for (uint16_t i = 0; i < count; ++i) {
        uint8_t kind;
        uint32_t color;
        if (!reader.read(kind) || kind || !reader.read(color))
            return false;
        shape.fills.push_back({color, 0});
    }
    if (!countStyles(count) || count > 1024)
        return false;
    for (uint16_t i = 0; i < count; ++i) {
        uint16_t width;
        uint32_t color, flags = 0;
        if (!reader.read(width))
            return false;
        if (code == 83) {
            Bits bits(bytes, reader.position());
            if (!bits.get(16, flags) || flags & 0x800 || !reader.seek(bits.position()))
                return false;
            if (((flags >> 12) & 3) == 2) {
                uint16_t miter;
                if (!reader.read(miter))
                    return false;
            }
        }
        if (!reader.read(color))
            return false;
        shape.strokes.push_back({color, width / 20.f});
    }
    Bits bits(bytes, reader.position());
    uint32_t fillBits, lineBits, fill0 = 0, fill1 = 0, stroke = 0;
    if (!bits.get(4, fillBits) || !bits.get(4, lineBits))
        return false;
    Point position;
    for (size_t edge = 0; edge < 100000; ++edge) {
        uint32_t type;
        if (!bits.get(1, type))
            return false;
        if (type) {
            uint32_t straight, count;
            float dx = 0, dy = 0;
            if (!bits.get(1, straight) || !bits.get(4, count))
                return false;
            count += 2;
            Point control;
            if (straight) {
                uint32_t general, vertical;
                if (!bits.get(1, general))
                    return false;
                if (general) {
                    if (!bits.signedValue(count, 20, dx) || !bits.signedValue(count, 20, dy))
                        return false;
                } else {
                    if (!bits.get(1, vertical) || !bits.signedValue(count, 20, vertical ? dy : dx))
                        return false;
                }
            } else {
                if (!bits.signedValue(count, 20, dx) || !bits.signedValue(count, 20, dy))
                    return false;
                control = {position.x + dx, position.y + dy};
                position = control;
                if (!bits.signedValue(count, 20, dx) || !bits.signedValue(count, 20, dy))
                    return false;
            }
            position = {position.x + dx, position.y + dy};
            shape.commands.push_back(
                {static_cast<uint8_t>(straight ? 1 : 2), position, control, static_cast<uint16_t>(fill0), static_cast<uint16_t>(fill1), static_cast<uint16_t>(stroke)});
        } else {
            uint32_t flags, count;
            if (!bits.get(5, flags))
                return false;
            if (!flags)
                return bits.position() == bytes.size();
            if (flags & 16)
                return false;
            if (flags & 1) {
                if (!bits.get(5, count) || !bits.signedValue(count, 20, position.x) || !bits.signedValue(count, 20, position.y))
                    return false;
                shape.commands.push_back({0, position, {}});
            }
            if ((flags & 2) && !bits.get(fillBits, fill0))
                return false;
            if ((flags & 4) && !bits.get(fillBits, fill1))
                return false;
            if ((flags & 8) && !bits.get(lineBits, stroke))
                return false;
            if (fill0 > shape.fills.size() || fill1 > shape.fills.size() || stroke > shape.strokes.size())
                return false;
        }
    }
    return false;
}
void include(Bounds &bounds, bool &has, Point point) {
    if (!has) {
        bounds = {point.x, point.y, point.x, point.y};
        has = true;
    } else {
        bounds.left = std::min(bounds.left, point.x);
        bounds.top = std::min(bounds.top, point.y);
        bounds.right = std::max(bounds.right, point.x);
        bounds.bottom = std::max(bounds.bottom, point.y);
    }
}
void recipeBounds(IconRecipe &recipe) {
    bool has = false;
    for (const auto &layer: recipe.layers) {
        Bounds b = layer.bitmap() ? Bounds{0, 0, static_cast<float>(layer.width), static_cast<float>(layer.height)} : layer.shape.bounds;
        for (auto point: {Point{b.left, b.top}, Point{b.right, b.top}, Point{b.right, b.bottom}, Point{b.left, b.bottom}})
            include(recipe.bounds, has, transform(layer.matrix, point));
    }
}
} // namespace

Point transform(const Matrix &m, Point p) { return {m[0] * p.x + m[2] * p.y + m[4], m[1] * p.x + m[3] * p.y + m[5]}; }
Matrix multiply(const Matrix &a, const Matrix &b) {
    return {a[0] * b[0] + a[2] * b[1], a[1] * b[0] + a[3] * b[1],        a[0] * b[2] + a[2] * b[3],
            a[1] * b[2] + a[3] * b[3], a[0] * b[4] + a[2] * b[5] + a[4], a[1] * b[4] + a[3] * b[5] + a[5]};
}

bool GfxMovie::parseTimeline(util::Bytes bytes, uint32_t count, Character &character, unsigned nesting) {
    if (nesting > 32 || count > 4096)
        return false;
    util::AssetReader reader(bytes);
    std::map<uint16_t, Placement> display;
    while (!reader.finished()) {
        uint16_t header;
        uint32_t size;
        util::Bytes body;
        if (!reader.read(header))
            return false;
        uint16_t code = header >> 6;
        size = header & 63;
        if (size == 63 && !reader.read(size))
            return false;
        if (!reader.take(size, body))
            return false;
        util::AssetReader current(body);
        if (!code)
            return !size && reader.finished() && character.frames.size() == count;
        if (code == 1009) {
            uint32_t identity;
            uint16_t format, width, height;
            std::string image, file;
            if (!current.read(identity) || !current.read(format) || !current.read(width) || !current.read(height) || !width || !height || !current.text8(image) ||
                !current.text8(file) || !current.finished() || characters_.contains(identity))
                return false;
            auto &value = characters_[identity];
            value.image = util::assetKey(image);
            value.width = width;
            value.height = height;
        } else if (code == 39) {
            uint16_t identity, frames;
            if (!current.read(identity) || !current.read(frames) || characters_.contains(identity))
                return false;
            Character child;
            if (!parseTimeline(body.subspan(current.position()), frames, child, nesting + 1))
                return false;
            characters_.emplace(identity, std::move(child));
        } else if (code == 32 || code == 83) {
            uint16_t identity;
            Character child;
            child.vector = true;
            if (!current.read(identity) || characters_.contains(identity))
                return false;
            // The movie contains unrelated vector styles too. Only decode
            // shapes when resolving the recipes actually used by the overlay.
            shapeBytes_[identity] = {code, std::vector<uint8_t>(body.begin(), body.end())};
            characters_.emplace(identity, std::move(child));
        } else if (code == 37) {
            uint16_t identity;
            if (!current.read(identity) || characters_.contains(identity))
                return false;
            // Parse only requested dynamic text fields; unrelated fonts and
            // text are not rendered by the overlay's bitmap/vector recipes.
            textBytes_[identity] = std::vector<uint8_t>(body.begin(), body.end());
            characters_.emplace(identity, Character{});
        } else if (code == 26 || code == 70) {
            uint8_t flags, extra = 0;
            uint16_t depth;
            if (!current.read(flags) || (code == 70 && !current.read(extra)) || extra & 0xc0 || !current.read(depth))
                return false;
            bool move = flags & 1;
            if ((move && !display.contains(depth)) || (!move && !(flags & 2)))
                return false;
            Placement value = move ? display[depth] : Placement{};
            if (extra & 8) {
                std::string className;
                if (!current.text(className))
                    return false;
            }
            if (flags & 2) {
                uint16_t id;
                if (!current.read(id))
                    return false;
                value.character = id;
            }
            if ((flags & 4) && !readMatrix(current, value.matrix))
                return false;
            if ((flags & 8) && !readColorTransform(current, value.effects))
                return false;
            if (flags & 16) {
                uint16_t ratio;
                if (!current.read(ratio))
                    return false;
            }
            if ((flags & 32) && !current.text(value.name))
                return false;
            if (flags & 64) {
                uint16_t clip;
                if (!current.read(clip))
                    return false;
                value.effects = true;
            }
            if (extra & 1) {
                if (!skipFilters(current))
                    return false;
                value.effects = true;
            }
            if (extra & 2) {
                uint8_t blend;
                if (!current.read(blend))
                    return false;
                value.effects |= blend != 1;
            }
            if (extra & 4) {
                uint8_t cache;
                if (!current.read(cache))
                    return false;
            }
            if (extra & 32) {
                uint8_t visible;
                if (!current.read(visible))
                    return false;
                value.visible = visible != 0;
            }
            if (flags & 128) {
                value.effects = true;
                if (!current.seek(body.size()))
                    return false;
            }
            if (!current.finished())
                return false;
            display[depth] = std::move(value);
        } else if (code == 28) {
            uint16_t depth;
            if (!current.read(depth) || !current.finished())
                return false;
            display.erase(depth);
        } else if (code == 1) {
            if (size || character.frames.size() >= count)
                return false;
            character.frames.push_back(display);
        } else if (code == 76) {
            uint16_t symbols;
            if (!current.read(symbols))
                return false;
            for (uint16_t i = 0; i < symbols; ++i) {
                uint16_t identity;
                std::string name;
                if (!current.read(identity) || !current.text(name))
                    return false;
                if (name == "WorldMapItem") {
                    if (worldMapItem_)
                        return false;
                    worldMapItem_ = identity;
                }
            }
            if (!current.finished())
                return false;
        }
    }
    return false;
}

bool GfxMovie::parse(util::Bytes bytes) {
    characters_.clear();
    shapeBytes_.clear();
    textBytes_.clear();
    icons_ = worldMapItem_ = 0;
    if (bytes.size() < 8 || std::memcmp(bytes.data(), "GFX\x0b", 4))
        return false;
    uint32_t size;
    std::memcpy(&size, bytes.data() + 4, 4);
    if (size < 8 || size > bytes.size() || std::any_of(bytes.begin() + size, bytes.end(), [](auto b) { return b != 0; }))
        return false;
    util::AssetReader reader(bytes.first(size), 8);
    Bounds bounds;
    uint16_t rate, count;
    if (!readRectangle(reader, bounds) || !reader.read(rate) || !reader.read(count))
        return false;
    Character root;
    if (!parseTimeline(reader.bytes().subspan(reader.position()), count, root, 0))
        return false;
    characters_[0] = std::move(root);
    auto item = characters_.find(worldMapItem_);
    if (!worldMapItem_ || item == characters_.end() || item->second.frames.empty())
        return false;
    for (const auto &[depth, placement]: item->second.frames[0])
        if (placement.name == "Icon_0") {
            if (icons_)
                return false;
            icons_ = placement.character;
        }
    return icons_ && characters_.contains(icons_);
}

uint32_t GfxMovie::named(const std::string &path, Matrix *placementMatrix) const {
    uint32_t identity = 0;
    size_t start = 0;
    while (start < path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos)
            end = path.size();
        auto character = characters_.find(identity);
        if (character == characters_.end() || character->second.frames.empty())
            return 0;
        uint32_t match = 0;
        for (const auto &[depth, placement]: character->second.frames[0]) {
            if (placement.name == path.substr(start, end - start)) {
                if (match)
                    return 0;
                match = placement.character;
                if (placementMatrix)
                    *placementMatrix = placement.matrix;
            }
        }
        if (!match)
            return 0;
        identity = match;
        start = end + 1;
    }
    return identity;
}
bool GfxMovie::flatten(uint32_t identity, uint32_t frame, const Matrix &matrix, std::vector<uint16_t> &depth, std::vector<uint32_t> &parents, IconRecipe &recipe) const {
    auto found = characters_.find(identity);
    if (found == characters_.end() || parents.size() >= 32 || std::find(parents.begin(), parents.end(), identity) != parents.end())
        return false;
    const auto &character = found->second;
    if (!character.image.empty()) {
        IconLayer layer;
        layer.image = character.image;
        layer.width = character.width;
        layer.height = character.height;
        layer.matrix = matrix;
        layer.depth = depth;
        recipe.layers.push_back(std::move(layer));
        return true;
    }
    if (character.vector) {
        auto raw = shapeBytes_.find(identity);
        IconLayer layer;
        layer.matrix = matrix;
        layer.depth = depth;
        if (raw == shapeBytes_.end() || !readShape(raw->second.second, raw->second.first, layer.shape))
            return false;
        recipe.layers.push_back(std::move(layer));
        return true;
    }
    if (!frame || frame > character.frames.size())
        return false;
    parents.push_back(identity);
    for (const auto &[localDepth, placement]: character.frames[frame - 1]) {
        if (!placement.visible)
            continue;
        if (placement.effects)
            return false;
        depth.push_back(localDepth);
        if (!flatten(placement.character, 1, multiply(matrix, placement.matrix), depth, parents, recipe))
            return false;
        depth.pop_back();
    }
    parents.pop_back();
    return true;
}
bool GfxMovie::icon(uint32_t frame, IconRecipe &recipe) const {
    recipe = {};
    std::vector<uint16_t> depth;
    std::vector<uint32_t> parents;
    if (!flatten(icons_, frame, IDENTITY_MATRIX, depth, parents, recipe) || recipe.layers.empty())
        return false;
    recipeBounds(recipe);
    return true;
}
bool GfxMovie::special(const std::string &path, IconRecipe &recipe) const {
    recipe = {};
    std::vector<uint16_t> depth;
    std::vector<uint32_t> parents;
    uint32_t identity = named(path);
    if (!identity || !flatten(identity, 1, IDENTITY_MATRIX, depth, parents, recipe) || recipe.layers.empty())
        return false;
    recipeBounds(recipe);
    return true;
}
bool GfxMovie::image(const std::string &name, IconRecipe &recipe) const {
    recipe = {};
    for (const auto &[id, character]: characters_)
        if (character.image == name) {
            if (!recipe.layers.empty())
                return false;
            IconLayer layer;
            layer.image = name;
            layer.width = character.width;
            layer.height = character.height;
            recipe.layers.push_back(std::move(layer));
        }
    if (recipe.layers.empty())
        return false;
    recipeBounds(recipe);
    return true;
}
bool GfxMovie::text(const std::string &path, TextLayout &layout) const {
    layout = {};
    uint32_t identity = named(path, &layout.matrix);
    auto found = textBytes_.find(identity);
    if (!identity || found == textBytes_.end())
        return false;
    util::AssetReader reader(found->second);
    uint16_t id, font, height = 0, ignored;
    uint8_t high, low;
    if (!reader.read(id) || id != identity || !readRectangle(reader, layout.bounds) || !reader.read(high) || !reader.read(low))
        return false;
    uint16_t flags = (uint16_t(high) << 8) | low;
    if (((flags & 0x100) && !reader.read(font)) || ((flags & 0x80) && !reader.text(layout.fontClass)) || ((flags & 0x180) && !reader.read(height)))
        return false;
    layout.fontHeight = height / 20.f;
    if ((flags & 0x400) && !reader.read(layout.color))
        return false;
    if ((flags & 0x200) && !reader.read(ignored))
        return false;
    if (flags & 0x20) {
        uint16_t left, right, indent;
        int16_t leading;
        if (!reader.read(layout.align) || layout.align > 3 || !reader.read(left) || !reader.read(right) || !reader.read(indent) || !reader.read(leading))
            return false;
        layout.leftMargin = left / 20.f;
        layout.rightMargin = right / 20.f;
        layout.indent = indent / 20.f;
        layout.leading = leading / 20.f;
    }
    std::string variable, initial;
    if (!reader.text(variable) || ((flags & 0x8000) && !reader.text(initial)) || !reader.finished())
        return false;
    return std::isfinite(layout.fontHeight) && layout.fontHeight > 0 && layout.fontHeight <= 1024 && layout.bounds.right > layout.bounds.left &&
           layout.bounds.bottom > layout.bounds.top;
}
uint32_t GfxMovie::iconFrameCount() const {
    auto found = characters_.find(icons_);
    return found == characters_.end() ? 0 : static_cast<uint32_t>(found->second.frames.size());
}
uint32_t GfxMovie::iconLayerCount(uint32_t frame) const {
    auto found = characters_.find(icons_);
    return found == characters_.end() || !frame || frame > found->second.frames.size() ? 0 : static_cast<uint32_t>(found->second.frames[frame - 1].size());
}

} // namespace er::minimap
