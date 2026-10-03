#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_set>

#include "settings.hpp"

namespace er::minimap {
namespace {
constexpr std::wstring_view MISSING = L"\x01";

std::wstring_view trim(std::wstring_view text) {
    auto first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring_view::npos)
        return {};
    auto last = text.find_last_not_of(L" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::wstring lowercase(std::wstring_view text) {
    std::wstring result(trim(text));
    for (auto &character: result)
        if (character >= L'A' && character <= L'Z')
            character += L'a' - L'A';
    return result;
}

void invalid(const std::string &key) { std::fprintf(stderr, "[Minimap] 配置项 %s 的值无效，已使用默认值。\n", key.c_str()); }

bool number(std::wstring_view text, float &value, bool allowPercent = true) {
    text = trim(text);
    bool percent = allowPercent && text.ends_with(L'%');
    if (percent)
        text.remove_suffix(1);
    text = trim(text);
    if (text.starts_with(L'+'))
        text.remove_prefix(1);
    if (text.empty())
        return false;
    std::string ascii;
    for (wchar_t character: text) {
        if (character > 127)
            return false;
        ascii.push_back(static_cast<char>(character));
    }
    auto [end, error] = std::from_chars(ascii.data(), ascii.data() + ascii.size(), value);
    if (error != std::errc{} || end != ascii.data() + ascii.size() || !std::isfinite(value))
        return false;
    if (percent)
        value /= 100.f;
    return true;
}

class Reader {
public:
    explicit Reader(const EROverlayAPI &api) : api_(api) {}

    std::wstring get(const std::string &key, bool *present = nullptr) const {
        std::wstring text(api_.configGetString(key.c_str(), MISSING.data()));
        bool exists = text != MISSING;
        if (present && exists)
            *present = true;
        return exists ? std::wstring(trim(text)) : std::wstring{};
    }

    float scalar(const std::string &key, float fallback, bool *present = nullptr, bool percent = true, float maximum = std::numeric_limits<float>::max()) const {
        auto text = get(key, present);
        if (text.empty())
            return fallback;
        float value;
        if (!number(text, value, percent) || value < 0 || value > maximum) {
            invalid(key);
            return fallback;
        }
        return value;
    }

    bool boolean(const std::string &key, bool fallback, bool *present = nullptr) const {
        auto text = lowercase(get(key, present));
        if (text.empty())
            return fallback;
        if (text == L"1" || text == L"true" || text == L"yes" || text == L"on")
            return true;
        if (text == L"0" || text == L"false" || text == L"no" || text == L"off")
            return false;
        invalid(key);
        return fallback;
    }

    std::optional<Margin> margin(const std::string &key, bool *present) const {
        auto text = lowercase(get(key, present));
        if (text.empty())
            return {};
        std::wstring_view valueText = text;
        bool pixels = valueText.ends_with(L"px");
        if (pixels)
            valueText.remove_suffix(2);
        float value;
        if (!number(valueText, value, !pixels)) {
            std::fprintf(stderr, "[Minimap] 配置项 %s 的边距值无效，已忽略；请填写像素值或百分比。\n", key.c_str());
            return {};
        }
        return Margin{value, !pixels && valueText.ends_with(L'%')};
    }

    int key(const std::string &name, int fallback) const {
        std::wstring text(api_.configGetString(name.c_str(), MISSING.data()));
        if (text == MISSING)
            return fallback;
        auto normalized = lowercase(text);
        if (normalized.empty() || normalized == L"none")
            return 0;
        return api_.configGetVirtualKey(name.c_str(), 0);
    }

private:
    const EROverlayAPI &api_;
};

std::vector<Preset> defaults() {
    Preset compact;
    compact.name = "compact";
    Preset large = compact;
    large.name = "large";
    large.widthRatio = large.heightRatio = 0.9f;
    large.zoom = 1.5f;
    large.opacity = 0.6f;
    large.position.centered = true;
    return {compact, large};
}

bool preset(const Reader &reader, const std::string &name, Preset &result) {
    result = {};
    result.name = name;
    bool present = false;
    std::string prefix = "minimap.preset." + name + '.';
    result.widthRatio = reader.scalar(prefix + "width", result.widthRatio, &present);
    result.heightRatio = reader.scalar(prefix + "height", result.heightRatio, &present);
    result.zoom = reader.scalar(prefix + "zoom", result.zoom, &present);
    result.opacity = reader.scalar(prefix + "opacity", result.opacity, &present, true, 1.f);
    result.rotate = reader.boolean(prefix + "rotate", result.rotate, &present);
    auto position = lowercase(reader.get(prefix + "position", &present));
    if (position == L"center")
        result.position.centered = true;
    else if (!position.empty() && position != L"margins")
        invalid(prefix + "position");
    if (!result.position.centered) {
        auto left = reader.margin(prefix + "margin_left", &present);
        auto right = reader.margin(prefix + "margin_right", &present);
        auto top = reader.margin(prefix + "margin_top", &present);
        auto bottom = reader.margin(prefix + "margin_bottom", &present);
        if (left && right)
            std::fprintf(stderr, "[Minimap] %smargin_left 与 margin_right 同时填写，已使用左边距；请将 margin_right 留空。\n", prefix.c_str());
        if (top && bottom)
            std::fprintf(stderr, "[Minimap] %smargin_top 与 margin_bottom 同时填写，已使用上边距；请将 margin_bottom 留空。\n", prefix.c_str());
        result.position.horizontalAnchor = left ? HorizontalAnchor::Left : HorizontalAnchor::Right;
        result.position.verticalAnchor = top || !bottom ? VerticalAnchor::Top : VerticalAnchor::Bottom;
        result.position.horizontalMargin = left ? *left : right.value_or(Margin{});
        result.position.verticalMargin = top ? *top : bottom.value_or(Margin{});
    }
    auto shape = lowercase(reader.get(prefix + "shape", &present));
    if (shape == L"rounded")
        result.shape = Shape::Rounded;
    else if (shape == L"circle")
        result.shape = Shape::Circle;
    else if (!shape.empty() && shape != L"rect")
        invalid(prefix + "shape");
    auto rounding = lowercase(reader.get(prefix + "rounding", &present));
    if (!rounding.empty()) {
        std::wstring_view text = rounding;
        if (text.ends_with(L"px"))
            text.remove_suffix(2);
        float value;
        if (number(text, value) && value >= 0) {
            result.rounding = value;
            result.roundingIsPercent = text.ends_with(L'%');
        } else
            invalid(prefix + "rounding");
    }
    result.mapScale = reader.scalar(prefix + "map_scale", result.mapScale, &present);
    result.decorationScale = reader.scalar(prefix + "decoration_scale", result.decorationScale, &present);
    result.playerScale = reader.scalar(prefix + "player_scale", result.playerScale, &present);
    result.compassScale = reader.scalar(prefix + "compass_scale", result.compassScale, &present);
    return present;
}

uint32_t color(const Reader &reader, uint32_t fallback) {
    auto text = reader.get("minimap.border.color");
    if (text.empty())
        return fallback;
    uint32_t rgba[4]{};
    std::wstring_view remaining = text;
    for (unsigned i = 0; i < 4; ++i) {
        auto comma = remaining.find(L',');
        float value;
        if ((i < 3) != (comma != std::wstring_view::npos) || !number(remaining.substr(0, comma), value, false) || value < 0 || value > 255 || std::floor(value) != value) {
            invalid("minimap.border.color");
            return fallback;
        }
        rgba[i] = static_cast<uint32_t>(value);
        if (i < 3)
            remaining.remove_prefix(comma + 1);
    }
    return rgba[0] | (rgba[1] << 8) | (rgba[2] << 16) | (rgba[3] << 24);
}
} // namespace

Settings loadSettings(const EROverlayAPI &api) {
    Settings result;
    Reader reader(api);
    result.toggleKey = reader.key("minimap.controls.toggle", result.toggleKey);
    result.cycleKey = reader.key("minimap.controls.cycle", result.cycleKey);
    result.gracesKey = reader.key("minimap.controls.toggle_graces", result.gracesKey);
    result.landmarksKey = reader.key("minimap.controls.toggle_landmarks", result.landmarksKey);
    result.showGraces = reader.boolean("minimap.markers.graces", result.showGraces);
    result.showLandmarks = reader.boolean("minimap.markers.landmarks", result.showLandmarks);
    result.showDeath = reader.boolean("minimap.markers.death", result.showDeath);
    result.showBeacons = reader.boolean("minimap.markers.beacons", result.showBeacons);
    result.fullMap = reader.boolean("minimap.map.full_map", result.fullMap);
    result.borderColor = color(reader, result.borderColor);
    result.borderWidth = reader.scalar("minimap.border.width", result.borderWidth, nullptr, false);
    auto order = reader.get("minimap.presets.order");
    if (order.empty())
        order = L"compact,large";
    std::wstring_view remaining = order;
    std::unordered_set<std::string> names;
    while (!remaining.empty()) {
        auto comma = remaining.find(L',');
        auto label = trim(remaining.substr(0, comma));
        bool valid = !label.empty() && label.size() <= 64 && std::all_of(label.begin(), label.end(), [](wchar_t c) {
            return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'_' || c == L'-';
        });
        if (valid) {
            std::string name;
            for (wchar_t character: label)
                name.push_back(static_cast<char>(character));
            Preset loaded;
            if (names.insert(name).second) {
                if (preset(reader, name, loaded))
                    result.presets.push_back(std::move(loaded));
                else
                    std::fprintf(stderr, "[Minimap] 预设 %s 未定义，已跳过；请检查 [presets] 中的 order。\n", name.c_str());
            }
        } else
            invalid("minimap.presets.order");
        if (comma == std::wstring_view::npos)
            break;
        remaining.remove_prefix(comma + 1);
    }
    if (result.presets.empty())
        result.presets = defaults();
    if (result.toggleKey && result.toggleKey == result.cycleKey && std::none_of(result.presets.begin(), result.presets.end(), [](const auto &entry) { return entry.zoom == 0; })) {
        auto hidden = result.presets.back();
        hidden.name = "hidden";
        hidden.zoom = 0;
        result.presets.push_back(std::move(hidden));
    }
    return result;
}

} // namespace er::minimap
