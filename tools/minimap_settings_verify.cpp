// Read real INI sections through Config and the production Minimap settings loader.
// No game, GPU, injection or native game calls are involved.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <string>

#include "config.hpp"
#include "input.hpp"
#include "minimap/settings.hpp"

namespace er {
wchar_t gModulePath[MAX_PATH]{};
}

namespace {
const er::Config *currentConfig = nullptr;

EROverlayAPI makeApi() {
    EROverlayAPI api{};
    api.configGetString = [](const char *key, const wchar_t *fallback) {
        thread_local std::wstring value;
        value = currentConfig->getw(key, fallback);
        return value.c_str();
    };
    api.configGetVirtualKey = [](const char *key, int fallback) { return currentConfig->getVirtualKey(key, fallback); };
    return api;
}

bool close(float value, float expected) { return std::fabs(value - expected) < 0.0001f; }

bool check(bool result, const char *label) {
    if (!result)
        std::fprintf(stderr, "FAIL: %s\n", label);
    return result;
}

er::minimap::Settings fixture(const char *contents, const EROverlayAPI &api) {
    CreateDirectoryW(L"build/native-checks/settings-fixture", nullptr);
    const wchar_t *path = L"build/native-checks/settings-fixture/minimap.ini";
    auto *stream = _wfopen(path, L"wb");
    if (!stream) {
        std::fprintf(stderr, "Cannot create settings fixture\n");
        return {};
    }
    std::fputs(contents, stream);
    std::fclose(stream);
    er::Config config;
    config.loadDir(L"build/native-checks/settings-fixture");
    currentConfig = &config;
    return er::minimap::loadSettings(api);
}
} // namespace

int main() {
    if (!GetCurrentDirectoryW(MAX_PATH, er::gModulePath))
        return 1;
    auto api = makeApi();
    er::Config config;
    config.loadDir(L"configs");
    currentConfig = &config;
    auto defaults = er::minimap::loadSettings(api);
    if (!check(defaults.presets.size() == 3, "shipped compact, large and automatic hidden cycle") ||
        !check(defaults.toggleKey == 'M' && defaults.cycleKey == 'M' && defaults.gracesKey == 'N' && defaults.landmarksKey == 'N', "shipped keys") ||
        !check(defaults.showGraces && defaults.showLandmarks && defaults.showDeath && defaults.showBeacons && !defaults.fullMap, "shipped marker and terrain switches") ||
        !check(close(defaults.borderWidth, 1.5f) && defaults.borderColor == 0x64FFFFFF, "pixel border and spaced RGBA color"))
        return 2;
    const auto &small = defaults.presets[0], &large = defaults.presets[1];
    if (!check(small.name == "compact" && close(small.widthRatio, 0.32f) && close(small.heightRatio, 0.32f) && close(small.zoom, 0.75f) && close(small.opacity, 0.9f) &&
                   !small.position.centered && small.position.horizontalAnchor == er::minimap::HorizontalAnchor::Right &&
                   small.position.verticalAnchor == er::minimap::VerticalAnchor::Top && close(small.position.horizontalMargin.value, 10) &&
                   close(small.position.verticalMargin.value, 10) && !small.position.horizontalMargin.isPercent && !small.position.verticalMargin.isPercent && !small.rotate &&
                   small.shape == er::minimap::Shape::Circle && close(small.rounding, 0.2f) && small.roundingIsPercent && close(small.mapScale, 1) &&
                   close(small.decorationScale, 1) && close(small.playerScale, 1) && close(small.compassScale, 0.5f),
               "shipped compact keeps the current user-selected defaults") ||
        !check(large.name == "large" && close(large.widthRatio, 0.9f) && close(large.heightRatio, 0.9f) && close(large.zoom, 1.5f) && close(large.opacity, 0.8f) &&
                   large.position.centered && large.shape == er::minimap::Shape::Rounded && close(large.rounding, 0.1f) && defaults.presets[2].zoom == 0,
               "shipped large and hidden keep the current defaults"))
        return 3;

    auto custom = fixture(R"ini(
[controls]
toggle = CTRL+M
cycle = ALT+N
toggle_graces =
toggle_landmarks = none
[map]
full_map = TRUE
[markers]
graces = false
landmarks = 0
death = no
beacons = off
[border]
width = 2.75
color = 10, 20, 30, 40
[presets]
order = overview, close-up, overview
[preset.close-up]
zoom = 2
[preset.overview]
width = 0.6
height = 40%
position = CENTER
zoom = 125%
opacity = 50%
rotate = yes
shape = Rounded
rounding = 30px
map_scale = 1.5
decoration_scale = 200%
player_scale = 0
compass_scale = 75%
[preset.unused]
zoom = 9
)ini",
                          api);
    if (!check(custom.presets.size() == 2 && custom.presets[0].name == "overview" && custom.presets[1].name == "close-up", "order, duplicate and unused preset handling") ||
        !check(custom.toggleKey == ('M' | er::input::KEY_MOD_CTRL) && custom.cycleKey == ('N' | er::input::KEY_MOD_ALT) && !custom.gracesKey && !custom.landmarksKey,
               "separate shortcut chords and explicit disabled shortcuts") ||
        !check(!custom.showGraces && !custom.showLandmarks && !custom.showDeath && !custom.showBeacons && custom.fullMap, "all switches accept boolean spellings") ||
        !check(custom.borderColor == 0x281E140A && close(custom.borderWidth, 2.75f), "all border channels and direct pixel units"))
        return 4;
    const auto &overview = custom.presets[0];
    if (!check(close(overview.widthRatio, 0.6f) && close(overview.heightRatio, 0.4f) && overview.position.centered && close(overview.zoom, 1.25f) &&
                   close(overview.opacity, 0.5f) && overview.rotate && overview.shape == er::minimap::Shape::Rounded && close(overview.rounding, 30) &&
                   !overview.roundingIsPercent && close(overview.mapScale, 1.5f) && close(overview.decorationScale, 2) && overview.playerScale == 0 &&
                   close(overview.compassScale, 0.75f),
               "every custom preset field reaches production settings") ||
        !check(custom.presets[1].shape == er::minimap::Shape::Rect && !custom.presets[1].position.centered && close(custom.presets[1].opacity, 0.8f), "presets do not inherit"))
        return 5;

    auto hidden = fixture(R"ini(
[presets]
order = off, map
[preset.off]
zoom = 0
[preset.map]
zoom = 1
)ini",
                          api);
    if (!check(hidden.presets.size() == 2 && hidden.presets[0].zoom == 0 && hidden.presets[1].zoom == 1, "explicit hidden preset is not duplicated"))
        return 6;
    auto disabled = fixture(R"ini(
[controls]
toggle =
cycle =
[presets]
order = solo
[preset.solo]
zoom = 1
)ini",
                            api);
    if (!check(!disabled.toggleKey && !disabled.cycleKey && disabled.presets.size() == 1, "disabled keys do not add an unreachable hidden step"))
        return 7;

    auto invalid = fixture(R"ini(
[controls]
toggle = M
cycle = N
[border]
color = 255, wrong, 0, 100
width = -2
[presets]
order = broken, missing
[preset.broken]
width = not-a-number
height = inf
zoom = 1,2
opacity = 120%
rotate = maybe
position = nowhere
shape = triangle
rounding = -10px
map_scale = nan
decoration_scale = -1
player_scale = 0
compass_scale = 1e999
)ini",
                           api);
    if (!check(invalid.presets.size() == 1, "undefined preset skipped") ||
        !check(close(invalid.presets[0].widthRatio, 0.3f) && close(invalid.presets[0].heightRatio, 0.3f) && close(invalid.presets[0].zoom, 0.75f) &&
                   close(invalid.presets[0].opacity, 0.8f) && !invalid.presets[0].rotate && !invalid.presets[0].position.centered &&
                   invalid.presets[0].shape == er::minimap::Shape::Rect && close(invalid.presets[0].rounding, 0.2f) && close(invalid.presets[0].mapScale, 1) &&
                   close(invalid.presets[0].decorationScale, 1) && invalid.presets[0].playerScale == 0 && close(invalid.presets[0].compassScale, 1) &&
                   close(invalid.borderWidth, 1.5f) && invalid.borderColor == 0x64FFFFFF,
               "invalid, nonfinite and old list values fall back without exceptions"))
        return 8;
    auto old = fixture(R"ini(
toggle_key = X
scale_key = Y
graces = 0
landmarks = 0
death_marker = 0
player_markers = 0
full_map = 1
width_ratio = 10%,20%
scale = 0,+8
alpha = 0.1
border_width_x10 = 100
)ini",
                       api);
    if (!check(old.toggleKey == 'M' && old.cycleKey == 'M' && old.presets.size() == 3 && old.showGraces && old.showLandmarks && old.showDeath && old.showBeacons && !old.fullMap &&
                   close(old.presets[0].widthRatio, 0.3f) && close(old.presets[0].zoom, 0.75f) && close(old.borderWidth, 1.5f),
               "old flat keys are ignored"))
        return 9;

    auto margins = fixture(R"ini(
[controls]
toggle =
cycle =
[presets]
order = upper_right, lower_left, upper_left, lower_right, zero, horizontal, vertical, conflict, invalid, centered
[preset.upper_right]
position = margins
margin_right = 24px
margin_top = +36
[preset.lower_left]
margin_left = 5%
margin_bottom = 10%
[preset.upper_left]
margin_left = -8px
margin_top = -2%
[preset.lower_right]
margin_right = 1.5%
margin_bottom = 12.5px
[preset.zero]
margin_left = 0
margin_bottom = 0
[preset.horizontal]
margin_left = 20
[preset.vertical]
margin_bottom = 25
[preset.conflict]
margin_left = 0
margin_right = 50
margin_top = 0%
margin_bottom = 60
[preset.invalid]
margin_left = NaN
margin_right = 4%px
margin_top = infinity
margin_bottom = 1e999
[preset.centered]
position = center
margin_left = 100
margin_right = 200
margin_top = -10%
margin_bottom = 500px
)ini",
                           api);
    using er::minimap::HorizontalAnchor;
    using er::minimap::VerticalAnchor;
    if (!check(margins.presets.size() == 10, "margin-only presets and disabled keys"))
        return 10;
    const auto &upperRight = margins.presets[0].position, &lowerLeft = margins.presets[1].position;
    const auto &upperLeft = margins.presets[2].position, &lowerRight = margins.presets[3].position;
    const auto &zero = margins.presets[4].position, &horizontal = margins.presets[5].position, &vertical = margins.presets[6].position;
    const auto &conflict = margins.presets[7].position, &invalidMargin = margins.presets[8].position, &centered = margins.presets[9].position;
    if (!check(!upperRight.centered && upperRight.horizontalAnchor == HorizontalAnchor::Right && upperRight.verticalAnchor == VerticalAnchor::Top &&
                   close(upperRight.horizontalMargin.value, 24) && close(upperRight.verticalMargin.value, 36) && !upperRight.horizontalMargin.isPercent &&
                   !upperRight.verticalMargin.isPercent,
               "explicit margins position and pixel units") ||
        !check(lowerLeft.horizontalAnchor == HorizontalAnchor::Left && lowerLeft.verticalAnchor == VerticalAnchor::Bottom && close(lowerLeft.horizontalMargin.value, .05f) &&
                   close(lowerLeft.verticalMargin.value, .1f) && lowerLeft.horizontalMargin.isPercent && lowerLeft.verticalMargin.isPercent,
               "omitted position and percent margins") ||
        !check(upperLeft.horizontalAnchor == HorizontalAnchor::Left && upperLeft.verticalAnchor == VerticalAnchor::Top && close(upperLeft.horizontalMargin.value, -8) &&
                   close(upperLeft.verticalMargin.value, -.02f) && !upperLeft.horizontalMargin.isPercent && upperLeft.verticalMargin.isPercent,
               "negative pixel and percent margins") ||
        !check(lowerRight.horizontalAnchor == HorizontalAnchor::Right && lowerRight.verticalAnchor == VerticalAnchor::Bottom && close(lowerRight.horizontalMargin.value, .015f) &&
                   close(lowerRight.verticalMargin.value, 12.5f) && lowerRight.horizontalMargin.isPercent && !lowerRight.verticalMargin.isPercent,
               "mixed units and fractional pixels") ||
        !check(zero.horizontalAnchor == HorizontalAnchor::Left && zero.verticalAnchor == VerticalAnchor::Bottom && zero.horizontalMargin.value == 0 &&
                   zero.verticalMargin.value == 0,
               "zero margins select the left/bottom anchors") ||
        !check(horizontal.horizontalAnchor == HorizontalAnchor::Left && horizontal.verticalAnchor == VerticalAnchor::Top && horizontal.verticalMargin.value == 0 &&
                   vertical.horizontalAnchor == HorizontalAnchor::Right && vertical.verticalAnchor == VerticalAnchor::Bottom && vertical.horizontalMargin.value == 0,
               "omitted axes default to right zero and top zero") ||
        !check(conflict.horizontalAnchor == HorizontalAnchor::Left && conflict.verticalAnchor == VerticalAnchor::Top && conflict.horizontalMargin.value == 0 &&
                   conflict.verticalMargin.value == 0 && conflict.verticalMargin.isPercent,
               "valid left/top margins take precedence without stretching") ||
        !check(invalidMargin.horizontalAnchor == HorizontalAnchor::Right && invalidMargin.verticalAnchor == VerticalAnchor::Top && invalidMargin.horizontalMargin.value == 0 &&
                   invalidMargin.verticalMargin.value == 0 && centered.centered && centered.horizontalMargin.value == 0 && centered.verticalMargin.value == 0,
               "invalid margins unset and center ignores all margins"))
        return 11;
    auto blank = fixture(R"ini(
[controls]
toggle =
cycle =
[presets]
order = blank, fallback
[preset.blank]
position = margins
margin_left =
margin_right = 2%
margin_top =
margin_bottom = 24px
[preset.fallback]
margin_left = invalid
margin_right = 42
margin_top = invalid
margin_bottom = 5%
)ini",
                         api);
    if (!check(blank.presets.size() == 2, "empty and invalid optional margins preserve their preset"))
        return 12;
    const auto &emptySide = blank.presets[0].position, &fallbackSide = blank.presets[1].position;
    if (!check(emptySide.horizontalAnchor == HorizontalAnchor::Right && emptySide.verticalAnchor == VerticalAnchor::Bottom && close(emptySide.horizontalMargin.value, .02f) &&
                   emptySide.horizontalMargin.isPercent && close(emptySide.verticalMargin.value, 24) && !emptySide.verticalMargin.isPercent,
               "blank values do not select or conflict with an anchor") ||
        !check(fallbackSide.horizontalAnchor == HorizontalAnchor::Right && fallbackSide.verticalAnchor == VerticalAnchor::Bottom &&
                   close(fallbackSide.horizontalMargin.value, 42) && close(fallbackSide.verticalMargin.value, .05f) && fallbackSide.verticalMargin.isPercent,
               "invalid selected side leaves the valid opposite side usable"))
        return 13;
    std::puts("PASS: real sectioned INI loading, shipped defaults, custom ordering, all controls/switches/preset fields, invalid values and old-format rejection.");
    std::puts("PASS: four corner anchors, pixel/percent/mixed/negative/zero margins, omitted/blank/invalid values, opposite-side precedence and center ignoring margins.");
    return 0;
}
