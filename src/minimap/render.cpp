#define NOMINMAX
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <numbers>

#include "render.hpp"
#include "util/string.hpp"

extern EROverlayAPI *api;
namespace er::minimap {
static constexpr float texturePlayerScale = 0.45f;
static constexpr float textureDecorationScale = 0.25f;
static constexpr float textureBearingRatio = 0.4f;

Renderer::~Renderer() noexcept {
    gResources.resetTextures();
    if (offscreen_)
        api->destroyOffscreen(offscreen_);
}

void Renderer::init(void *context, void *allocFunc, void *freeFunc, void *userData) {
    ImGui::SetCurrentContext((ImGuiContext *)context);
    ImGui::SetAllocatorFunctions((ImGuiMemAllocFunc)allocFunc, (ImGuiMemFreeFunc)freeFunc, userData);

    offscreen_ = api->createOffscreen();
    showDeath_ = api->configGetInt("minimap.death_marker", 1) != 0;
    showPlayerMarkers_ = api->configGetInt("minimap.player_markers", 1) != 0;

    toggleKey_ = api->configGetVirtualKey("minimap.toggle_key", 'M');
    scaleKey_ = api->configGetVirtualKey("minimap.scale_key", 'N');
    gracesKey_ = api->configGetVirtualKey("minimap.graces_key", 'N');
    showGraces_ = api->configGetInt("minimap.graces", 1) != 0;
    landmarksKey_ = api->configGetVirtualKey("minimap.landmarks_key", 'N');
    showLandmarks_ = api->configGetInt("minimap.landmarks", 1) != 0;
    widthRatios_ = util::strSplitToFloatVec(api->configGetString("minimap.width_ratio", L"30%,90%"));
    heightRatios_ = util::strSplitToFloatVec(api->configGetString("minimap.height_ratio", L"30%,90%"));
    auto sl = util::splitString(std::wstring(api->configGetString("minimap.scale", L"0.75,+1.5")), L',');
    scales_.clear();
    isCentered_.clear();
    for (auto &s: sl) {
        if (s.empty()) {
            scales_.push_back(0.f);
            isCentered_.push_back(false);
            continue;
        }
        if (s.front() == '+') {
            s.erase(0, 1);
            isCentered_.push_back(true);
        } else {
            isCentered_.push_back(false);
        }
        if (s.empty()) {
            scales_.push_back(0.f);
            continue;
        }
        if (s.back() == '%') {
            s.pop_back();
            scales_.push_back(std::stof(s) / 100.f);
        } else {
            scales_.push_back(std::stof(s));
        }
    }
    alphas_ = util::strSplitToFloatVec(api->configGetString("minimap.alpha", L"0.8,0.6"));
    auto shapeStrs = util::splitString(std::wstring(api->configGetString("minimap.shape", L"rect")), L',');
    shapes_.clear();
    for (auto &s: shapeStrs) {
        if (s == L"rounded")
            shapes_.push_back(Shape::Rounded);
        else if (s == L"circle")
            shapes_.push_back(Shape::Circle);
        else
            shapes_.push_back(Shape::Rect);
    }
    auto rotateStrs = util::splitString(std::wstring(api->configGetString("minimap.rotate", L"0")), L',');
    rotates_.clear();
    for (auto &s: rotateStrs) {
        rotates_.push_back(s == L"1" || s == L"yes" || s == L"true");
    }
    auto roundingStrs = util::splitString(std::wstring(api->configGetString("minimap.rounding", L"20%")), L',');
    roundings_.clear();
    roundingIsPercent_.clear();
    for (auto &s: roundingStrs) {
        if (s.empty()) {
            roundings_.push_back(0.f);
            roundingIsPercent_.push_back(true);
            continue;
        }
        if (s.back() == L'%') {
            auto sv = s.substr(0, s.size() - 1);
            roundings_.push_back(std::stof(sv) / 100.f);
            roundingIsPercent_.push_back(true);
        } else {
            roundings_.push_back(std::stof(s));
            roundingIsPercent_.push_back(false);
        }
    }
    // Parse border_color: "R,G,B,A" format (0-255 each), stored as ABGR (ImGui IM_COL32 format)
    {
        auto colorStr = std::wstring(api->configGetString("minimap.border_color", L"255,255,255,100"));
        auto parts = util::splitString(colorStr, L',');
        int r = 255, g = 255, b = 255, a = 100;
        if (parts.size() >= 1 && !parts[0].empty())
            r = std::clamp((int)std::stof(parts[0]), 0, 255);
        if (parts.size() >= 2 && !parts[1].empty())
            g = std::clamp((int)std::stof(parts[1]), 0, 255);
        if (parts.size() >= 3 && !parts[2].empty())
            b = std::clamp((int)std::stof(parts[2]), 0, 255);
        if (parts.size() >= 4 && !parts[3].empty())
            a = std::clamp((int)std::stof(parts[3]), 0, 255);
        borderColor_ = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
    }
    borderWidth_ = (float)api->configGetInt("minimap.border_width_x10", 15) / 10.f;
    extraTileScales_ = util::strSplitToFloatVec(api->configGetString("minimap.extra_tile_scale", L"1"));
    extraDecorationScales_ = util::strSplitToFloatVec(api->configGetString("minimap.extra_decoration_scale", L"1"));
    extraPlayerScales_ = util::strSplitToFloatVec(api->configGetString("minimap.extra_player_scale", L"1"));
    extraBearingScales_ = util::strSplitToFloatVec(api->configGetString("minimap.extra_bearing_scale", L"1"));
    auto maxCount = std::max({widthRatios_.size(), heightRatios_.size(), scales_.size(), alphas_.size(), shapes_.size(), roundings_.size(), rotates_.size(),
                              extraTileScales_.size(), extraDecorationScales_.size(), extraPlayerScales_.size(), extraBearingScales_.size()});
    if (maxCount == 0) {
        maxCount = 1;
        widthRatios_.push_back(0.3f);
        heightRatios_.push_back(0.3f);
        scales_.push_back(0.75f);
        alphas_.push_back(0.8f);
        isCentered_.push_back(false);
        shapes_.push_back(Shape::Rect);
        roundings_.push_back(0.2f);
        roundingIsPercent_.push_back(true);
        rotates_.push_back(false);
        extraTileScales_.push_back(1.f);
        extraDecorationScales_.push_back(1.f);
        extraPlayerScales_.push_back(1.f);
        extraBearingScales_.push_back(1.f);
    } else {
        if (widthRatios_.empty()) {
            widthRatios_.push_back(0.3f);
        }
        if (heightRatios_.empty()) {
            heightRatios_.push_back(0.3f);
        }
        if (scales_.empty()) {
            scales_.push_back(0.75f);
        }
        if (alphas_.empty()) {
            alphas_.push_back(0.8f);
        }
        if (isCentered_.empty()) {
            isCentered_.push_back(false);
        }
        if (shapes_.empty()) {
            shapes_.push_back(Shape::Rect);
        }
        if (roundings_.empty()) {
            roundings_.push_back(0.2f);
            roundingIsPercent_.push_back(true);
        }
        if (rotates_.empty()) {
            rotates_.push_back(false);
        }
        if (extraTileScales_.empty()) {
            extraTileScales_.push_back(1.f);
        }
        if (extraDecorationScales_.empty()) {
            extraDecorationScales_.push_back(1.f);
        }
        if (extraPlayerScales_.empty()) {
            extraPlayerScales_.push_back(1.f);
        }
        if (extraBearingScales_.empty()) {
            extraBearingScales_.push_back(1.f);
        }
        while (widthRatios_.size() < maxCount) {
            widthRatios_.push_back(widthRatios_.back());
        }
        while (heightRatios_.size() < maxCount) {
            heightRatios_.push_back(heightRatios_.back());
        }
        while (scales_.size() < maxCount) {
            scales_.push_back(scales_.back());
        }
        while (alphas_.size() < maxCount) {
            alphas_.push_back(alphas_.back());
        }
        while (isCentered_.size() < maxCount) {
            isCentered_.push_back(isCentered_.back());
        }
        while (shapes_.size() < maxCount) {
            shapes_.push_back(shapes_.back());
        }
        while (roundings_.size() < maxCount) {
            roundings_.push_back(roundings_.back());
            roundingIsPercent_.push_back(roundingIsPercent_.back());
        }
        while (rotates_.size() < maxCount) {
            rotates_.push_back(rotates_.back());
        }
        while (extraTileScales_.size() < maxCount) {
            extraTileScales_.push_back(extraTileScales_.back());
        }
        while (extraDecorationScales_.size() < maxCount) {
            extraDecorationScales_.push_back(extraDecorationScales_.back());
        }
        while (extraPlayerScales_.size() < maxCount) {
            extraPlayerScales_.push_back(extraPlayerScales_.back());
        }
        while (extraBearingScales_.size() < maxCount) {
            extraBearingScales_.push_back(extraBearingScales_.back());
        }
    }
    if (toggleKey_ == scaleKey_) {
        scales_.push_back(0.f);
        widthRatios_.push_back(widthRatios_.back());
        heightRatios_.push_back(heightRatios_.back());
        alphas_.push_back(alphas_.back());
        isCentered_.push_back(isCentered_.back());
        shapes_.push_back(shapes_.back());
        roundings_.push_back(roundings_.back());
        roundingIsPercent_.push_back(roundingIsPercent_.back());
        rotates_.push_back(rotates_.back());
        extraTileScales_.push_back(extraTileScales_.back());
        extraDecorationScales_.push_back(extraDecorationScales_.back());
        extraPlayerScales_.push_back(extraPlayerScales_.back());
        extraBearingScales_.push_back(extraBearingScales_.back());
    }
    currentWidthRatio_ = widthRatios_[0];
    currentHeightRatio_ = heightRatios_[0];
    currentScale_ = scales_[0];
    currentAlpha_ = alphas_[0];
    currentIsCentered_ = isCentered_[0];
    currentShape_ = shapes_[0];
    currentRounding_ = roundings_[0];
    currentRoundingIsPercent_ = roundingIsPercent_[0];
    currentRotate_ = rotates_[0];
    currentExtraTileScale_ = extraTileScales_[0];
    currentExtraDecorationScale_ = extraDecorationScales_[0];
    currentExtraPlayerScale_ = extraPlayerScales_[0];
    currentExtraBearingScale_ = extraBearingScales_[0];
}

bool Renderer::render() {
    gResources.prepareTextures();
    auto snapshot = gData.snapshot();
    if (snapshot.onGUI)
        return false;
    if (toggleKey_ && toggleKey_ != scaleKey_ && api->inputIsKeyPressed(toggleKey_))
        show_ = !show_;
    if (show_ && scaleKey_ && api->inputIsKeyPressed(scaleKey_)) {
        currentScaleIndex_ = (currentScaleIndex_ + 1) % scales_.size();
        auto i = currentScaleIndex_;
        currentWidthRatio_ = widthRatios_[i];
        currentHeightRatio_ = heightRatios_[i];
        currentScale_ = scales_[i];
        currentAlpha_ = alphas_[i];
        currentIsCentered_ = isCentered_[i];
        currentShape_ = shapes_[i];
        currentRounding_ = roundings_[i];
        currentRoundingIsPercent_ = roundingIsPercent_[i];
        currentRotate_ = rotates_[i];
        currentExtraTileScale_ = extraTileScales_[i];
        currentExtraDecorationScale_ = extraDecorationScales_[i];
        currentExtraPlayerScale_ = extraPlayerScales_[i];
        currentExtraBearingScale_ = extraBearingScales_[i];
    }
    if (!show_ || currentScale_ < 0.0001f || (!snapshot.valid && !*gResources.status()))
        return false;
    if (gracesKey_ && api->inputIsKeyPressed(gracesKey_))
        showGraces_ = !showGraces_;
    if (landmarksKey_ && api->inputIsKeyPressed(landmarksKey_))
        showLandmarks_ = !showLandmarks_;
    auto *vp = ImGui::GetMainViewport();
    float height = std::min(vp->Size.y, vp->Size.x * .5625f);
    minimapWidth_ = std::floor(height * currentWidthRatio_);
    minimapHeight_ = std::floor(height * currentHeightRatio_);
    if (currentRotate_)
        currentShape_ = Shape::Circle;
    if (currentShape_ == Shape::Circle)
        minimapWidth_ = minimapHeight_ = std::min(minimapWidth_, minimapHeight_);
    if (minimapWidth_ <= 0 || minimapHeight_ <= 0)
        return false;
    cachedRounding_ = currentRoundingIsPercent_ ? currentRounding_ * std::min(minimapWidth_, minimapHeight_) * .5f : currentRounding_;
    cachedRounding_ = std::clamp(cachedRounding_, 0.f, std::min(minimapWidth_, minimapHeight_) * .5f);
    effectiveScale_ = std::clamp(currentScale_ * currentExtraTileScale_, .01f, 16.f);
    effectivePlayerScale_ = texturePlayerScale * currentExtraPlayerScale_;
    effectiveDecorationScale_ = textureDecorationScale * currentExtraDecorationScale_;
    effectiveBearingRatio_ = textureBearingRatio * currentExtraBearingScale_;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImVec2 position =
        currentIsCentered_ ? ImVec2(std::floor((vp->Size.x - minimapWidth_) * .5f), std::floor((vp->Size.y - minimapHeight_) * .5f)) : ImVec2(vp->Size.x - minimapWidth_, 0);
    ImGui::SetNextWindowPos(position, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(minimapWidth_, minimapHeight_));
    if (ImGui::Begin("##minimap_window", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings)) {
        bool offscreen = offscreen_ && (currentAlpha_ < 1.f || currentRotate_ || currentShape_ != Shape::Rect);
        float alpha = currentAlpha_;
        if (offscreen) {
            if (!nativeApi || !nativeApi->beginOffscreen(offscreen_)) {
                ImGui::End();
                ImGui::PopStyleVar();
                return false;
            }
            currentAlpha_ = 1.f;
        }
        if (snapshot.valid)
            renderContent(snapshot);
        currentAlpha_ = alpha;
        if (offscreen)
            composite(alpha);
        auto *draw = ImGui::GetWindowDrawList();
        auto origin = ImGui::GetWindowPos();
        ImVec2 far = origin + ImVec2(minimapWidth_, minimapHeight_);
        if (borderWidth_ > 0) {
            if (currentShape_ == Shape::Circle)
                draw->AddCircle(origin + ImVec2(minimapWidth_ * .5f, minimapHeight_ * .5f), minimapWidth_ * .5f, borderColor_, 0, borderWidth_);
            else
                draw->AddRect(origin, far, borderColor_, currentShape_ == Shape::Rounded ? cachedRounding_ : 0.f, 0, borderWidth_);
        }
        if (*gResources.status())
            draw->AddText(origin + ImVec2(8, 8), IM_COL32(255, 255, 255, 255), gResources.status());
    }
    ImGui::End();
    ImGui::PopStyleVar();
    return false;
}

void Renderer::drawRecipe(const IconRecipe *recipe, Point center, float scale, float angle) {
    if (!recipe || scale <= 0 || !std::isfinite(angle))
        return;
    auto *draw = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetWindowPos();
    float cosine = std::cos(angle), sine = std::sin(angle);
    auto project = [&](const IconLayer &layer, Point point) {
        point = transform(layer.matrix, point);
        point.x *= scale;
        point.y *= scale;
        return origin + ImVec2(center.x + point.x * cosine - point.y * sine, center.y + point.x * sine + point.y * cosine);
    };
    for (const auto &layer: recipe->layers) {
        if (layer.bitmap()) {
            AtlasRegion region;
            ERTextureView texture;
            if (!gResources.layerView(layer, region, texture))
                continue;
            ImVec2 uv0(float(region.x) / texture.width, float(region.y) / texture.height);
            ImVec2 uv1(float(region.x + region.width) / texture.width, float(region.y + region.height) / texture.height);
            draw->AddImageQuad((ImTextureID)texture.gpuHandle, project(layer, {0, 0}), project(layer, {float(layer.width), 0}),
                               project(layer, {float(layer.width), float(layer.height)}), project(layer, {0, float(layer.height)}), uv0, ImVec2(uv1.x, uv0.y), uv1,
                               ImVec2(uv0.x, uv1.y), IM_COL32(255, 255, 255, int(currentAlpha_ * 255)));
        } else {
            Point previous;
            for (const auto &command: layer.shape.commands) {
                if (command.op && command.stroke && command.stroke <= layer.shape.strokes.size()) {
                    const auto &stroke = layer.shape.strokes[command.stroke - 1];
                    uint32_t color = (stroke.color & 0xffffff) | (uint32_t(float(stroke.color >> 24) * currentAlpha_) << 24);
                    float width = stroke.width * scale * std::sqrt(std::abs(layer.matrix[0] * layer.matrix[3] - layer.matrix[1] * layer.matrix[2]));
                    auto from = project(layer, previous), to = project(layer, command.to);
                    if (command.op == 1)
                        draw->AddLine(from, to, color, std::max(.5f, width));
                    else {
                        auto control = project(layer, command.control);
                        draw->AddBezierCubic(from, from + (control - from) * (2.f / 3.f), to + (control - to) * (2.f / 3.f), to, color, std::max(.5f, width));
                    }
                }
                previous = command.to;
            }
        }
    }
}

void Renderer::drawPlayerMarker(const PlayerMarkerInfo &marker, Point center, float scale) {
    const auto *arrow = gResources.special("marker");
    const auto *layout = gResources.playerMarkerText();
    if (!arrow || arrow->layers.empty() || !layout || marker.number < 1 || marker.number > 5 || !std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(scale) ||
        scale <= 0)
        return;
    AtlasRegion region;
    ERTextureView texture;
    if (!gResources.layerView(arrow->layers.front(), region, texture))
        return;
    drawRecipe(arrow, center, scale);
    // Keep the beacon and its digit upright while the map position rotates.
    // The native Text_0 is dynamic text, not a numbered bitmap frame.
    char text[]{static_cast<char>('0' + marker.number), 0};
    auto *font = ImGui::GetFont();
    float size = layout->fontHeight * scale;
    auto extent = font->CalcTextSizeA(size, FLT_MAX, 0, text);
    float left = layout->bounds.left + layout->leftMargin, right = layout->bounds.right - layout->rightMargin;
    float x = layout->align == 2 ? (left + right - extent.x / scale) * .5f : layout->align == 1 ? right - extent.x / scale : left + layout->indent;
    // SWF text fields include a two-pixel inset inside their bounds.
    Point local = transform(layout->matrix, {x, layout->bounds.top + 2.f});
    auto position = ImGui::GetWindowPos() + ImVec2(center.x + local.x * scale, center.y + local.y * scale);
    auto *draw = ImGui::GetWindowDrawList();
    uint32_t color = (layout->color & 0xffffff) | (uint32_t(float(layout->color >> 24) * currentAlpha_) << 24);
    uint32_t shadow = IM_COL32(26, 14, 0, int(currentAlpha_ * 255));
    float offset = std::max(.5f, scale);
    draw->AddText(font, size, position + ImVec2(-offset, 0), shadow, text);
    draw->AddText(font, size, position + ImVec2(offset, 0), shadow, text);
    draw->AddText(font, size, position + ImVec2(0, -offset), shadow, text);
    draw->AddText(font, size, position + ImVec2(0, offset), shadow, text);
    draw->AddText(font, size, position, color, text);
}

void Renderer::drawTile(const TileView &tile, Point player, float cosine, float sine) {
    ERTextureView texture;
    if (!nativeApi || nativeApi->pollTexture(tile.texture, &texture) != ER_TEXTURE_READY)
        return;
    auto origin = ImGui::GetWindowPos();
    auto point = [&](float x, float y) {
        x = (x - player.x) * effectiveScale_;
        y = (y - player.y) * effectiveScale_;
        return origin + ImVec2(minimapWidth_ * .5f + x * cosine - y * sine, minimapHeight_ * .5f + x * sine + y * cosine);
    };
    ImGui::GetWindowDrawList()->AddImageQuad((ImTextureID)texture.gpuHandle, point(tile.left, tile.top), point(tile.right, tile.top), point(tile.right, tile.bottom),
                                             point(tile.left, tile.bottom), ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1),
                                             IM_COL32(255, 255, 255, int(currentAlpha_ * 255)));
}

void Renderer::renderContent(const MapSnapshot &snapshot) {
    const auto &state = snapshot.state;
    Point center{minimapWidth_ * .5f, minimapHeight_ * .5f};
    Point player{state.x, state.y};
    float angle = currentRotate_ ? std::atan2(snapshot.camera.yawSin, snapshot.camera.yawCos) : 0.f;
    float cosine = std::cos(angle), sine = std::sin(angle);
    int map = state.mapId == 10 ? 2 : state.mapId == 0 ? ((state.underground & 1) ? 1 : 0) : -1;
    if (snapshot.roundtable) {
        drawRecipe(gResources.special("home"), center, currentScale_ * currentExtraTileScale_, angle);
        drawRecipe(gResources.icon(snapshot.homeIcon), center, effectiveDecorationScale_ * effectiveScale_ * 2.f, angle);
    } else if (map >= 0) {
        // Keep L0 as the default until native L1/L2 seam and position acceptance
        // is complete. Coarser assets and spans are already supported below.
        uint8_t level = 0;
        static constexpr float spans[] = {256, 342, 1288};
        static constexpr int counts[] = {41, 31, 9};
        float span = spans[level];
        int count = counts[level];
        gResources.beginFrame(state.generation, map, state.activeMasks, level);
        float radius = currentRotate_ ? std::hypot(minimapWidth_, minimapHeight_) * .5f : 0.f;
        float halfX = (currentRotate_ ? radius : minimapWidth_ * .5f) / effectiveScale_;
        float halfY = (currentRotate_ ? radius : minimapHeight_ * .5f) / effectiveScale_;
        int x0 = std::max(0, int(std::floor((player.x - halfX) / span)) - 1), x1 = std::min(count - 1, int(std::floor((player.x + halfX) / span)) + 1);
        int y0 = std::max(0, int(std::floor((player.y - halfY) / span)) - 1), y1 = std::min(count - 1, int(std::floor((player.y + halfY) / span)) + 1);
        auto drawLayer = [&](uint32_t layer) {
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    TileView tile;
                    if (gResources.tile(layer, x, count - 1 - y, tile))
                        drawTile(tile, player, cosine, sine);
                }
        };
        // Native Image_0 is the surface underlay; Image_1 is shown for the
        // underground view. M01 is translucent and needs the M00 base.
        if (map == 1)
            drawLayer(0);
        drawLayer(map);
        gResources.endFrame();
        for (const auto &marker: snapshot.decorations) {
            if (!(marker.maps & (1u << map)) || (marker.source == DecorationSource::Grace ? !showGraces_ : !showLandmarks_))
                continue;
            float x = (marker.x - player.x) * effectiveScale_, y = (marker.y - player.y) * effectiveScale_;
            Point screen{center.x + x * cosine - y * sine, center.y + x * sine + y * cosine};
            if (screen.x < -200 || screen.y < -200 || screen.x > minimapWidth_ + 200 || screen.y > minimapHeight_ + 200)
                continue;
            float size = marker.areaIcon ? effectiveScale_ : effectiveDecorationScale_ * effectiveScale_ * 2.f;
            drawRecipe(gResources.icon(marker.iconId), screen, size, marker.rotationRad + angle);
        }
        int markerMap = map == 2 ? 10 : map;
        if (showDeath_ && state.deathValid && state.deathMapId == markerMap) {
            float x = (state.deathX - player.x) * effectiveScale_, y = (state.deathY - player.y) * effectiveScale_;
            Point death{center.x + x * cosine - y * sine, center.y + x * sine + y * cosine};
            drawRecipe(gResources.special("death"), death, effectiveScale_ * 2.f * effectivePlayerScale_, angle);
        }
        if (showPlayerMarkers_)
            for (const auto &marker: snapshot.playerMarkers) {
                if (marker.map != markerMap)
                    continue;
                float x = (marker.x - player.x) * effectiveScale_, y = (marker.y - player.y) * effectiveScale_;
                Point screen{center.x + x * cosine - y * sine, center.y + x * sine + y * cosine};
                if (screen.x < -200 || screen.y < -200 || screen.x > minimapWidth_ + 200 || screen.y > minimapHeight_ + 200)
                    continue;
                drawPlayerMarker(marker, screen, effectiveScale_ * 2.f * effectivePlayerScale_);
            }
    }
    drawRecipe(gResources.special("player"), center, effectiveScale_ * effectivePlayerScale_);
    drawRecipe(gResources.special("arrow"), center, effectiveScale_ * effectivePlayerScale_, state.oriDeg * std::numbers::pi_v<float> / 180.f + angle);
    if (currentRotate_) {
        float size = std::min(minimapWidth_, minimapHeight_) * effectiveBearingRatio_;
        const auto *bearing = gResources.special("bearing");
        if (bearing && !bearing->layers.empty()) {
            auto width = float(bearing->layers[0].width);
            drawRecipe(bearing, {minimapWidth_ - size * .5f, size * .5f}, size / width, angle);
        }
    }
}

void Renderer::composite(float alpha) {
    void *handle = api->endOffscreen(offscreen_);
    if (!handle)
        return;
    auto origin = ImGui::GetWindowPos();
    auto *vp = ImGui::GetMainViewport();
    auto *draw = ImGui::GetWindowDrawList();
    ImVec2 far = origin + ImVec2(minimapWidth_, minimapHeight_);
    ImVec2 uv0(origin.x / vp->Size.x, origin.y / vp->Size.y), uv1(far.x / vp->Size.x, far.y / vp->Size.y);
    draw->PushTexture((ImTextureID)handle);
    int first = draw->VtxBuffer.Size;
    if (currentShape_ == Shape::Circle)
        draw->PathArcTo(origin + ImVec2(minimapWidth_ * .5f, minimapHeight_ * .5f), minimapWidth_ * .5f, 0, IM_PI * 2, 0);
    else
        draw->PathRect(origin, far, currentShape_ == Shape::Rounded ? cachedRounding_ : 0.f);
    draw->PathFillConvex(IM_COL32(255, 255, 255, int(alpha * 255)));
    ImGui::ShadeVertsLinearUV(draw, first, draw->VtxBuffer.Size, origin, far, uv0, uv1, false);
    draw->PopTexture();
}

bool Renderer::isPointInShape(float x, float y) const {
    if (currentShape_ == Shape::Rect)
        return true;
    float cx = minimapWidth_ * 0.5f;
    float cy = minimapHeight_ * 0.5f;
    if (currentShape_ == Shape::Circle) {
        float radius = cx; // Circle forces square, so cx == cy == radius
        float dx = x - cx;
        float dy = y - cy;
        return dx * dx + dy * dy <= radius * radius;
    }
    // Shape::Rounded — use cached rounding value
    float left = cachedRounding_;
    float right = minimapWidth_ - cachedRounding_;
    float top = cachedRounding_;
    float bottom = minimapHeight_ - cachedRounding_;
    // Inside the inner cross (no corner check needed)
    if (x >= left && x <= right && y >= 0.f && y <= minimapHeight_)
        return true;
    if (y >= top && y <= bottom && x >= 0.f && x <= minimapWidth_)
        return true;
    // Check the four corner arcs
    auto checkCorner = [](float px, float py, float cornerX, float cornerY, float r) {
        float dx = px - cornerX;
        float dy = py - cornerY;
        return dx * dx + dy * dy <= r * r;
    };
    if (x < left && y < top)
        return checkCorner(x, y, left, top, cachedRounding_);
    if (x > right && y < top)
        return checkCorner(x, y, right, top, cachedRounding_);
    if (x < left && y > bottom)
        return checkCorner(x, y, left, bottom, cachedRounding_);
    if (x > right && y > bottom)
        return checkCorner(x, y, right, bottom, cachedRounding_);
    return false;
}


} // namespace er::minimap
