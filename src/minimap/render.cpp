#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "render.hpp"

#include "util/string.hpp"
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>
#include <windows.h>
#include <cmath>
#include <numbers>

extern EROverlayAPI *api;

namespace er::minimap {

// constant values for the minimap texture
static constexpr float roundTableMapX0 = 2740.f;
static constexpr float roundTableMapY0 = 7510.f;
static constexpr float roundTableMapX1 = 2940.f;
static constexpr float roundTableMapY1 = 7710.f;
static constexpr float roundTableAltPosX = 800.f;
static constexpr float roundTableAltPosY = 9119.f - 800.f;
static constexpr float dlcMapOffsetX = 3035.f;
static constexpr float dlcMapOffsetY = 1864.f;
static constexpr int textureSizeInt = 1024;
static constexpr float textureSize = (float)textureSizeInt;
static constexpr float texturePlayerScale = 0.45f;
static constexpr float textureDecorationScale = 0.25f;
static constexpr float textureBearingRatio = 0.4f;

void Renderer::init(void *context, void *allocFunc, void *freeFunc, void *userData) {
    ImGui::SetCurrentContext((ImGuiContext *)context);
    ImGui::SetAllocatorFunctions((ImGuiMemAllocFunc)allocFunc, (ImGuiMemFreeFunc)freeFunc, userData);

    offscreen_ = api->createOffscreen();
    textures_.resize(300);

    toggleKey_ = api->configGetImGuiKey("minimap.toggle_key", ImGuiKey_M);
    scaleKey_ = api->configGetImGuiKey("minimap.scale_key", ImGuiKey_N);
    gracesKey_ = api->configGetImGuiKey("minimap.graces_key", ImGuiKey_N);
    showGraces_ = api->configGetInt("minimap.graces", 1) != 0;
    landmarksKey_ = api->configGetImGuiKey("minimap.landmarks_key", ImGuiKey_N);
    showLandmarks_ = api->configGetInt("minimap.landmarks", 1) != 0;
    widthRatios_ = util::strSplitToFloatVec(api->configGetString("minimap.width_ratio", L"30%,90%"));
    heightRatios_ = util::strSplitToFloatVec(api->configGetString("minimap.height_ratio", L"30%,90%"));
    auto sl = util::splitString(std::wstring(api->configGetString("minimap.scale", L"0.75,+1.5")), L',');
    scales_.clear();
    isCentered_.clear();
    for (auto &s : sl) {
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
    for (auto &s : shapeStrs) {
        if (s == L"rounded") shapes_.push_back(Shape::Rounded);
        else if (s == L"circle") shapes_.push_back(Shape::Circle);
        else shapes_.push_back(Shape::Rect);
    }
    auto rotateStrs = util::splitString(std::wstring(api->configGetString("minimap.rotate", L"0")), L',');
    rotates_.clear();
    for (auto &s : rotateStrs) {
        rotates_.push_back(s == L"1" || s == L"yes" || s == L"true");
    }
    auto roundingStrs = util::splitString(std::wstring(api->configGetString("minimap.rounding", L"20%")), L',');
    roundings_.clear();
    roundingIsPercent_.clear();
    for (auto &s : roundingStrs) {
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
        if (parts.size() >= 1 && !parts[0].empty()) r = std::clamp((int)std::stof(parts[0]), 0, 255);
        if (parts.size() >= 2 && !parts[1].empty()) g = std::clamp((int)std::stof(parts[1]), 0, 255);
        if (parts.size() >= 3 && !parts[2].empty()) b = std::clamp((int)std::stof(parts[2]), 0, 255);
        if (parts.size() >= 4 && !parts[3].empty()) a = std::clamp((int)std::stof(parts[3]), 0, 255);
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
    if (gData.onGUI()) return false;
    if (toggleKey_ != 0 && toggleKey_ != scaleKey_ && ImGui::IsKeyChordPressed(static_cast<ImGuiKey>(toggleKey_))) {
        show_ = !show_;
    }
    if (scaleKey_ != 0 && show_ && ImGui::IsKeyChordPressed(static_cast<ImGuiKey>(scaleKey_))) {
        currentScaleIndex_ = (currentScaleIndex_ + 1) % scales_.size();
        currentWidthRatio_ = widthRatios_[currentScaleIndex_];
        currentHeightRatio_ = heightRatios_[currentScaleIndex_];
        currentScale_ = scales_[currentScaleIndex_];
        currentAlpha_ = alphas_[currentScaleIndex_];
        currentIsCentered_ = isCentered_[currentScaleIndex_];
        currentShape_ = shapes_[currentScaleIndex_];
        currentRounding_ = roundings_[currentScaleIndex_];
        currentRoundingIsPercent_ = roundingIsPercent_[currentScaleIndex_];
        currentRotate_ = rotates_[currentScaleIndex_];
        currentExtraTileScale_ = extraTileScales_[currentScaleIndex_];
        currentExtraDecorationScale_ = extraDecorationScales_[currentScaleIndex_];
        currentExtraPlayerScale_ = extraPlayerScales_[currentScaleIndex_];
        currentExtraBearingScale_ = extraBearingScales_[currentScaleIndex_];
    }
    if (!show_ || currentScale_ < 0.0001f) {
        return false;
    }
    if (gracesKey_ != 0 && ImGui::IsKeyChordPressed(static_cast<ImGuiKey>(gracesKey_))) {
        showGraces_ = !showGraces_;
    }
    if (landmarksKey_ != 0 && ImGui::IsKeyChordPressed(static_cast<ImGuiKey>(landmarksKey_))) {
        showLandmarks_ = !showLandmarks_;
    }

    const auto &location = gData.location();
    if (location.x == 0.f) return false;
    auto *vp = ImGui::GetMainViewport();
    auto realHeight = vp->Size.x * .5625f >= vp->Size.y ? vp->Size.y : vp->Size.x * .5625f;
    minimapWidth_ = std::floor(realHeight * currentWidthRatio_);
    minimapHeight_ = std::floor(realHeight * currentHeightRatio_);
    if (currentRotate_) {
        currentShape_ = Shape::Circle;
    }
    if (currentShape_ == Shape::Circle) {
        float side = std::min(minimapWidth_, minimapHeight_);
        minimapWidth_ = side;
        minimapHeight_ = side;
    }
    // Cache rounding value for this frame (used by renderShapedMinimap, border drawing, isPointInShape)
    cachedRounding_ = currentRoundingIsPercent_
        ? currentRounding_ * std::min(minimapWidth_, minimapHeight_) * 0.5f
        : currentRounding_;
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec2 originalPadding = style.WindowPadding;
    style.WindowPadding = ImVec2(0, 0);
    if (currentIsCentered_) {
        ImGui::SetNextWindowPos(ImVec2(std::floor((vp->Size.x - minimapWidth_) * .5f), std::floor((vp->Size.y - minimapHeight_) * .5f)), ImGuiCond_Always, ImVec2(0.f, 0.f));
        ImGui::SetNextWindowSize(ImVec2(minimapWidth_, minimapHeight_));
    } else {
        ImGui::SetNextWindowPos(ImVec2(vp->Size.x - minimapWidth_, 0), ImGuiCond_Always, ImVec2(0.f, 0.f));
        ImGui::SetNextWindowSize(ImVec2(minimapWidth_, minimapHeight_));
    }
    if (ImGui::Begin("##minimap_window", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings)) {
        // Begin offscreen rendering: all content renders at alpha=1.0 to avoid double-alpha blending.
        // Save actual alpha for compositing, temporarily set to 1.0 so all sub-methods render opaque.
        // In rotation mode, offscreen is always required for circular compositing (tiles are drawn as
        // rotated quads and the circle mask is applied during the compositing step).
        bool useOffscreen = offscreen_ != nullptr && (currentAlpha_ < 1.0f || currentRotate_);
        float savedAlpha = currentAlpha_;
        if (useOffscreen) {
            api->beginOffscreen(offscreen_);
            currentAlpha_ = 1.f;
        }
        float dx, dy;
        bool drawRoundTable = false;
        int layer;
        switch (location.mapId) {
            case 0: {
                layer = (location.underground & 1) ? 1 : 0;
                dx = location.x;
                dy = location.y;
                if (dx >= roundTableMapX0 && dx < roundTableMapX1 && dy >= roundTableMapY0 && dy < roundTableMapY1) {
                    drawRoundTable = true;
                    dx = roundTableAltPosX;
                    dy = roundTableAltPosY;
                }
                break;
            }
            case 10:
                layer = 2;
                dx = location.x - dlcMapOffsetX;
                dy = location.y - dlcMapOffsetY;
                break;
            default:
                if (useOffscreen) currentAlpha_ = savedAlpha;
                ImGui::End();
                return false;
        }
        // Compute per-frame effective scale values based on current state
        effectiveScale_ = currentScale_ * currentExtraTileScale_;
        effectivePlayerScale_ = texturePlayerScale * currentExtraPlayerScale_;
        effectiveDecorationScale_ = textureDecorationScale * currentExtraDecorationScale_;
        effectiveBearingRatio_ = textureBearingRatio * currentExtraBearingScale_;

        // Save player world position before converting to tile units
        float playerWorldX = dx;
        float playerWorldY = dy;

        // Map rotation parameters (rotation = negative player facing so "forward" points up)
        float mapRad = 0.f, cosMapRot = 1.f, sinMapRot = 0.f;
        if (currentRotate_) {
            const auto &camera = gData.camera();
            cosMapRot = camera.yawCos;
            sinMapRot = camera.yawSin;
            // Calculate map rotation from camera yaw
            mapRad = std::atan2(sinMapRot, cosMapRot);
        }

        dx /= textureSize;
        dy /= textureSize;
        auto texSize = textureSize * effectiveScale_;
        auto halfWidth = minimapWidth_ * 0.5f;
        auto halfHeight = minimapHeight_ * 0.5f;

        if (currentRotate_) {
            // === ROTATED TILE RENDERING ===
            // Tile range covers the circle diameter symmetrically around the player
            auto halfTileRange = halfWidth / texSize + 1.f;
            auto x0 = (int)std::floor(dx - halfTileRange);
            auto y0 = (int)std::floor(dy - halfTileRange);
            auto x1 = (int)std::ceil(dx + halfTileRange);
            auto y1 = (int)std::ceil(dy + halfTileRange);
            for (auto y = y0; y <= y1; y++) {
                for (auto x = x0; x <= x1; x++) {
                    if (x < 0 || x > 9 || y < 0 || y > 9) continue;
                    auto index = layer * 100 + y * 10 + x;
                    float tileOffsetX = (x * textureSize - playerWorldX) * effectiveScale_;
                    float tileOffsetY = (y * textureSize - playerWorldY) * effectiveScale_;
                    renderRotatedTile(index, tileOffsetX, tileOffsetY, cosMapRot, sinMapRot);
                }
            }

            if (playerSprite_ == nullptr) {
                gAtlas.loadTextures();
                playerSprite_ = gAtlas.findSprite("Player");
                arrowSprite_ = gAtlas.findSprite("Arrow");
                roundTableSprite_ = gAtlas.findSprite("RoundTable");
                bearingSprite_ = gAtlas.findSprite("Bearing");
            }

            // === ROTATED DECORATION RENDERING ===
            if ((showGraces_ || showLandmarks_) && gData.paramsLoaded()) {
                auto boundMaxX = minimapWidth_ + 100.f;
                auto boundMaxY = minimapHeight_ + 100.f;
                auto decorationScale = effectiveDecorationScale_ * effectiveScale_;
                for (auto y = y0; y <= y1; y++) {
                    for (auto x = x0; x <= x1; x++) {
                        if (x < 0 || x > 9 || y < 0 || y > 9) continue;
                        auto p = gData.decorationsAround(layer, x, y);
                        auto *begin = std::get<0>(p);
                        if (begin == nullptr) continue;
                        auto *end = std::get<1>(p);
                        for (auto *decoration = begin; decoration < end; decoration++) {
                            switch (decoration->source) {
                                case DecorationSource::Grace:
                                    if (!showGraces_) continue;
                                    break;
                                case DecorationSource::Landmark:
                                    if (!showLandmarks_) continue;
                                    break;
                            }
                            if (!decoration->isUnlocked()) continue;
                            auto *sprite = decoration->sprite;
                            // Position: world-relative to player, then rotate, then offset to center
                            float decoRelX = (decoration->x - playerWorldX) * effectiveScale_;
                            float decoRelY = (decoration->y - playerWorldY) * effectiveScale_;
                            auto rx = decoRelX * cosMapRot - decoRelY * sinMapRot + halfWidth;
                            auto ry = decoRelX * sinMapRot + decoRelY * cosMapRot + halfHeight;
                            if (rx > -100.f && ry > -100.f && rx < boundMaxX && ry < boundMaxY) {
                                auto width = sprite->width * decorationScale;
                                auto height = sprite->height * decorationScale;
                                auto spriteCenterX = sprite->centerX * decorationScale;
                                auto spriteCenterY = sprite->centerY * decorationScale;
                                if (!isPointInShape(rx, ry)) continue;
                                auto rad = decoration->rotationRad;
                                if (rad == 0.f) {
                                    // rotationRad==0: position rotates with map, but visual orientation stays fixed on screen
                                    ImGui::SetCursorPos(ImVec2(rx - spriteCenterX, ry - spriteCenterY));
                                    ImGui::ImageWithBg((ImTextureID)sprite->texture->gpuHandle, ImVec2(width, height), ImVec2(sprite->u0, sprite->v0), ImVec2(sprite->u1, sprite->v1), ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, currentAlpha_));
                                } else {
                                    // rotationRad!=0: visual rotation adjusted by map rotation
                                    rad += mapRad;
                                    auto *drawList = ImGui::GetWindowDrawList();
                                    auto xRel0 = -spriteCenterX;
                                    auto yRel0 = -spriteCenterY;
                                    auto xRel1 = (sprite->width - sprite->centerX) * decorationScale;
                                    auto yRel1 = (sprite->height - sprite->centerY) * decorationScale;
                                    auto cosRad = std::cos(rad);
                                    auto sinRad = std::sin(rad);
                                    auto pos = ImGui::GetWindowPos();
                                    auto absX = rx + pos.x;
                                    auto absY = ry + pos.y;
                                    auto p1 = ImVec2(absX + xRel0 * cosRad - yRel0 * sinRad, absY + xRel0 * sinRad + yRel0 * cosRad);
                                    auto p2 = ImVec2(absX + xRel1 * cosRad - yRel0 * sinRad, absY + xRel1 * sinRad + yRel0 * cosRad);
                                    auto p3 = ImVec2(absX + xRel1 * cosRad - yRel1 * sinRad, absY + xRel1 * sinRad + yRel1 * cosRad);
                                    auto p4 = ImVec2(absX + xRel0 * cosRad - yRel1 * sinRad, absY + xRel0 * sinRad + yRel1 * cosRad);
                                    drawList->PushTexture((ImTextureID)sprite->texture->gpuHandle);
                                    drawList->PrimReserve(6, 4);
                                    drawList->PrimQuadUV(p1, p2, p3, p4, ImVec2(sprite->u0, sprite->v0), ImVec2(sprite->u1, sprite->v0), ImVec2(sprite->u1, sprite->v1), ImVec2(sprite->u0, sprite->v1), IM_COL32(255, 255, 255, (int)(currentAlpha_ * 255.f)));
                                    drawList->PopTexture();
                                }
                            }
                        }
                    }
                }
            }
        } else {
            // === ORIGINAL (NON-ROTATED) RENDERING ===
            auto u = minimapWidth_ / texSize;
            auto v = minimapHeight_ / texSize;
            dx -= u * 0.5f;
            dy -= v * 0.5f;
            auto cx = std::floor((std::floor(dx) - dx) * texSize);
            auto cy = std::floor((std::floor(dy) - dy) * texSize);
            auto x0 = (int)dx;
            auto y0 = (int)dy;
            auto x1 = (int)(dx + u);
            auto y1 = (int)(dy + v);
            auto ny = cy;
            for (auto y = y0; y <= y1; y++, ny += texSize) {
                auto index0 = layer * 100 + y * 10 + x0;
                auto nx = cx;
                for (auto x = x0; x <= x1; x++, nx += texSize, index0++) {
                    if (currentShape_ == Shape::Rect) {
                        renderMinimap(index0, nx, ny, effectiveScale_);
                    } else {
                        renderShapedMinimap(index0, nx, ny, effectiveScale_);
                    }
                }
            }
            if (playerSprite_ == nullptr) {
                gAtlas.loadTextures();
                playerSprite_ = gAtlas.findSprite("Player");
                arrowSprite_ = gAtlas.findSprite("Arrow");
                roundTableSprite_ = gAtlas.findSprite("RoundTable");
                bearingSprite_ = gAtlas.findSprite("Bearing");
            }
            if ((showGraces_ || showLandmarks_) && gData.paramsLoaded()) {
                auto boundMaxX = minimapWidth_ + 100.f;
                auto boundMaxY = minimapHeight_ + 100.f;
                ny = cy;
                auto decorationScale = effectiveDecorationScale_ * effectiveScale_;
                for (auto y = y0; y <= y1; y++, ny += texSize) {
                    auto index0 = layer * 100 + y * 10 + x0;
                    auto nx = cx;
                    for (auto x = x0; x <= x1; x++, nx += texSize, index0++) {
                        auto p = gData.decorationsAround(layer, x, y);
                        auto *begin = std::get<0>(p);
                        if (begin == nullptr) continue;
                        auto *end = std::get<1>(p);
                        for (auto *decoration = begin; decoration < end; decoration++) {
                            switch (decoration->source) {
                                case DecorationSource::Grace:
                                    if (!showGraces_) continue;
                                    break;
                                case DecorationSource::Landmark:
                                    if (!showLandmarks_) continue;
                                    break;
                            }
                            if (!decoration->isUnlocked()) continue;
                            auto *sprite = decoration->sprite;
                            auto rx = decoration->localX * effectiveScale_ + nx;
                            auto ry = decoration->localY * effectiveScale_ + ny;
                            if (rx > -100.f && ry > -100.f && rx < boundMaxX && ry < boundMaxY) {
                                auto width = sprite->width * decorationScale;
                                auto height = sprite->height * decorationScale;
                                auto centerX = sprite->centerX * decorationScale;
                                auto centerY = sprite->centerY * decorationScale;
                                if (currentShape_ != Shape::Rect) {
                                    if (!isPointInShape(rx + centerX, ry + centerY)) continue;
                                }
                                auto rad = decoration->rotationRad;
                                if (rad == 0.f) {
                                    ImGui::SetCursorPos(ImVec2(rx - centerX, ry - centerY));
                                    ImGui::ImageWithBg((ImTextureID)sprite->texture->gpuHandle, ImVec2(width, height), ImVec2(sprite->u0, sprite->v0), ImVec2(sprite->u1, sprite->v1), ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, currentAlpha_));
                                } else {
                                    auto *drawList = ImGui::GetWindowDrawList();
                                    auto xRel0 = -centerX;
                                    auto yRel0 = -centerY;
                                    auto xRel1 = (sprite->width - sprite->centerX) * decorationScale;
                                    auto yRel1 = (sprite->height - sprite->centerY) * decorationScale;
                                    auto cosRad = std::cos(rad);
                                    auto sinRad = std::sin(rad);
                                    auto pos = ImGui::GetWindowPos();
                                    rx += pos.x;
                                    ry += pos.y;
                                    // PrimQuadUV expects the same order as PrimRectUV: TL, TR, BR, BL (triangles TL-TR-BR and TL-BR-BL).
                                    auto p1 = ImVec2(rx + xRel0 * cosRad - yRel0 * sinRad, ry + xRel0 * sinRad + yRel0 * cosRad);
                                    auto p2 = ImVec2(rx + xRel1 * cosRad - yRel0 * sinRad, ry + xRel1 * sinRad + yRel0 * cosRad);
                                    auto p3 = ImVec2(rx + xRel1 * cosRad - yRel1 * sinRad, ry + xRel1 * sinRad + yRel1 * cosRad);
                                    auto p4 = ImVec2(rx + xRel0 * cosRad - yRel1 * sinRad, ry + xRel0 * sinRad + yRel1 * cosRad);
                                    drawList->PushTexture((ImTextureID)sprite->texture->gpuHandle);
                                    drawList->PrimReserve(6, 4); // 6 indices for 2 triangles, 4 vertices
                                    drawList->PrimQuadUV(p1, p2, p3, p4, ImVec2(sprite->u0, sprite->v0), ImVec2(sprite->u1, sprite->v0), ImVec2(sprite->u1, sprite->v1), ImVec2(sprite->u0, sprite->v1), IM_COL32(255, 255, 255, (int)(currentAlpha_ * 255.f)));
                                    drawList->PopTexture();
                                }
                            }
                        }
                    }
                }
            }
        }
        if (drawRoundTable && roundTableSprite_ != nullptr) {
            if (currentShape_ == Shape::Rect || isPointInShape(minimapWidth_ * 0.5f, minimapHeight_ * 0.5f)) {
                ImGui::SetCursorPos(ImVec2(minimapWidth_ * .5f - roundTableSprite_->width * .25f, minimapHeight_ * .5f - roundTableSprite_->height * .25f));
                ImGui::ImageWithBg((ImTextureID)roundTableSprite_->texture->gpuHandle, ImVec2(roundTableSprite_->width * .5f, roundTableSprite_->height * .5f), ImVec2(roundTableSprite_->u0, roundTableSprite_->v0), ImVec2(roundTableSprite_->u1, roundTableSprite_->v1), ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, currentAlpha_));
            }
        }
        renderPlayer(location.oriDeg * std::numbers::pi_v<float> / 180.f + mapRad);
        // End offscreen rendering and composite the result with the actual alpha
        if (useOffscreen) {
            currentAlpha_ = savedAlpha;
            auto *gpuHandle = api->endOffscreen(offscreen_);
            if (gpuHandle) {
                auto winPos = ImGui::GetWindowPos();
                auto *vp = ImGui::GetMainViewport();
                float u0 = winPos.x / vp->Size.x;
                float v0 = winPos.y / vp->Size.y;
                float u1 = (winPos.x + minimapWidth_) / vp->Size.x;
                float v1 = (winPos.y + minimapHeight_) / vp->Size.y;
                if (currentRotate_) {
                    // Circular compositing: sample the offscreen texture through a circle shape.
                    // Tiles were drawn as rotated quads (perfect tiling, no gaps/overlaps),
                    // and this step masks the result to the circle boundary.
                    auto *drawList = ImGui::GetWindowDrawList();
                    drawList->PushTexture((ImTextureID)gpuHandle);
                    int vs = drawList->VtxBuffer.Size;
                    float radius = minimapWidth_ * 0.5f;
                    ImVec2 center = {winPos.x + radius, winPos.y + radius};
                    drawList->PathArcTo(center, radius, 0.0f, IM_PI * 2.0f, 0);
                    drawList->PathFillConvex(IM_COL32(255, 255, 255, (int)(currentAlpha_ * 255.f)));
                    int ve = drawList->VtxBuffer.Size;
                    ImGui::ShadeVertsLinearUV(drawList, vs, ve, winPos, {winPos.x + minimapWidth_, winPos.y + minimapHeight_}, ImVec2(u0, v0), ImVec2(u1, v1), false);
                    drawList->PopTexture();
                } else {
                    ImGui::SetCursorPos(ImVec2(0, 0));
                    ImGui::ImageWithBg((ImTextureID)gpuHandle, ImVec2(minimapWidth_, minimapHeight_), ImVec2(u0, v0), ImVec2(u1, v1), ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, currentAlpha_));
                }
            }
        }
        // Draw shape border
        if (borderWidth_ > 0.f) {
            auto *drawList = ImGui::GetWindowDrawList();
            auto winPos = ImGui::GetWindowPos();
            ImVec2 shapeMin = winPos;
            ImVec2 shapeMax = {winPos.x + minimapWidth_, winPos.y + minimapHeight_};
            if (currentShape_ == Shape::Circle) {
                float radius = minimapWidth_ * 0.5f;
                ImVec2 center = {shapeMin.x + radius, shapeMin.y + radius};
                drawList->AddCircle(center, radius, borderColor_, 0, borderWidth_);
            } else if (currentShape_ == Shape::Rounded) {
                drawList->AddRect(shapeMin, shapeMax, borderColor_, cachedRounding_, 0, borderWidth_);
            } else {
                drawList->AddRect(shapeMin, shapeMax, borderColor_, 0.f, 0, borderWidth_);
            }
        }
        // Render bearing compass in top-right corner (rotation mode only)
        if (currentRotate_ && bearingSprite_ != nullptr && bearingSprite_->texture != nullptr) {
            auto *drawList = ImGui::GetWindowDrawList();
            auto winPos = ImGui::GetWindowPos();
            float bearingScale = minimapWidth_ * effectiveBearingRatio_ / (float)bearingSprite_->width;
            float bw = bearingSprite_->width * bearingScale;
            float bh = bearingSprite_->height * bearingScale;
            float bcx = bearingSprite_->centerX * bearingScale;
            float bcy = bearingSprite_->centerY * bearingScale;
            float margin = minimapWidth_ * 0.04f;
            float absX = winPos.x + minimapWidth_ - bw * 0.5f - margin;
            float absY = winPos.y + bh * 0.5f + margin;
            float cosRad = std::cos(mapRad);
            float sinRad = std::sin(mapRad);
            auto xRel0 = -bcx;
            auto yRel0 = -bcy;
            auto xRel1 = bw - bcx;
            auto yRel1 = bh - bcy;
            auto p1 = ImVec2(absX + xRel0 * cosRad - yRel0 * sinRad, absY + xRel0 * sinRad + yRel0 * cosRad);
            auto p2 = ImVec2(absX + xRel1 * cosRad - yRel0 * sinRad, absY + xRel1 * sinRad + yRel0 * cosRad);
            auto p3 = ImVec2(absX + xRel1 * cosRad - yRel1 * sinRad, absY + xRel1 * sinRad + yRel1 * cosRad);
            auto p4 = ImVec2(absX + xRel0 * cosRad - yRel1 * sinRad, absY + xRel0 * sinRad + yRel1 * cosRad);
            drawList->PushTexture((ImTextureID)bearingSprite_->texture->gpuHandle);
            drawList->PrimReserve(6, 4);
            drawList->PrimQuadUV(p1, p2, p3, p4,
                ImVec2(bearingSprite_->u0, bearingSprite_->v0), ImVec2(bearingSprite_->u1, bearingSprite_->v0),
                ImVec2(bearingSprite_->u1, bearingSprite_->v1), ImVec2(bearingSprite_->u0, bearingSprite_->v1),
                IM_COL32(255, 255, 255, (int)(currentAlpha_ * 255.f)));
            drawList->PopTexture();
        }
    }
    ImGui::End();
    style.WindowPadding = originalPadding;
    return false;
}

bool Renderer::prepareTile(int index, float &posX, float &posY, float scale, TileInfo &out) {
    auto &t = textures_[index];
    if (!t.loaded) {
        wchar_t path[256];
        wsprintfW(path, L"%ls\\data\\map\\%02d\\%d_%d.png", api->getModulePath(), index / 100, index % 10, (index % 100) / 10);
        t = api->loadTexture(path);
        if (t.texture == nullptr) {
            // Fallback to JPG if PNG is not found
            wsprintfW(path, L"%ls\\data\\map\\%02d\\%d_%d.jpg", api->getModulePath(), index / 100, index % 10, (index % 100) / 10);
            t = api->loadTexture(path);
        }
    }
    if (t.texture == nullptr) {
        return false;
    }
    auto width = (float)t.width * scale;
    auto height = (float)t.height * scale;
    out.texWidth = width;
    out.texHeight = height;
    out.clipU = 0.f;
    out.clipV = 0.f;
    if (posX < 0) {
        out.clipU = -posX;
        width += posX;
        posX = 0.f;
    }
    if (posY < 0) {
        out.clipV = -posY;
        height += posY;
        posY = 0.f;
    }
    if (posX + width > minimapWidth_) {
        width = minimapWidth_ - posX;
    }
    if (posY + height > minimapHeight_) {
        height = minimapHeight_ - posY;
    }
    if (width <= 0.f || height <= 0.f) return false;
    out.texture = &t;
    out.posX = posX;
    out.posY = posY;
    out.width = width;
    out.height = height;
    return true;
}

void Renderer::renderMinimap(int index, float posX, float posY, float scale) {
    TileInfo tile;
    if (!prepareTile(index, posX, posY, scale, tile)) return;
    ImGui::SetCursorPos(ImVec2(tile.posX, tile.posY));
    ImGui::ImageWithBg((ImTextureID)tile.texture->gpuHandle, ImVec2(tile.width, tile.height),
        ImVec2(tile.clipU / tile.texWidth, tile.clipV / tile.texHeight),
        ImVec2((tile.clipU + tile.width) / tile.texWidth, (tile.clipV + tile.height) / tile.texHeight),
        ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, currentAlpha_));
}

void Renderer::renderShapedMinimap(int index, float posX, float posY, float scale) {
    TileInfo tile;
    if (!prepareTile(index, posX, posY, scale, tile)) return;

    auto *drawList = ImGui::GetWindowDrawList();
    auto winPos = ImGui::GetWindowPos();

    // Shape screen coordinates (full minimap bounds)
    ImVec2 shapeMin = winPos;
    ImVec2 shapeMax = {winPos.x + minimapWidth_, winPos.y + minimapHeight_};

    // Tile screen coordinates
    ImVec2 tileScreenMin = {winPos.x + tile.posX, winPos.y + tile.posY};
    ImVec2 tileScreenMax = {tileScreenMin.x + tile.width, tileScreenMin.y + tile.height};

    // PushClipRect to tile area intersected with minimap bounds
    drawList->PushClipRect(
        ImVec2(std::max(tileScreenMin.x, shapeMin.x), std::max(tileScreenMin.y, shapeMin.y)),
        ImVec2(std::min(tileScreenMax.x, shapeMax.x), std::min(tileScreenMax.y, shapeMax.y)),
        true);

    drawList->PushTexture((ImTextureID)tile.texture->gpuHandle);
    int vs = drawList->VtxBuffer.Size;
    if (currentShape_ == Shape::Circle) {
        float radius = minimapWidth_ * 0.5f;
        ImVec2 center = {shapeMin.x + radius, shapeMin.y + radius};
        drawList->PathArcTo(center, radius, 0.0f, IM_PI * 2.0f, 0);
    } else {
        drawList->PathRect(shapeMin, shapeMax, cachedRounding_);
    }
    drawList->PathFillConvex(IM_COL32(255, 255, 255, (int)(currentAlpha_ * 255.f)));
    int ve = drawList->VtxBuffer.Size;

    // Map tile texture UVs: texOriginScreen is the screen position of UV (0,0) of this tile
    ImVec2 texOriginScreen = {winPos.x + tile.posX - tile.clipU, winPos.y + tile.posY - tile.clipV};
    ImGui::ShadeVertsLinearUV(drawList, vs, ve, texOriginScreen, {texOriginScreen.x + tile.texWidth, texOriginScreen.y + tile.texHeight}, ImVec2(0, 0), ImVec2(1, 1), false);

    drawList->PopTexture();
    drawList->PopClipRect();
}

void Renderer::renderRotatedTile(int index, float tileOffsetX, float tileOffsetY, float cosRot, float sinRot) {
    if (index < 0 || index >= (int)textures_.size()) return;
    auto &t = textures_[index];
    if (!t.loaded) {
        wchar_t path[256];
        wsprintfW(path, L"%ls\\data\\map\\%02d\\%d_%d.png", api->getModulePath(), index / 100, index % 10, (index % 100) / 10);
        t = api->loadTexture(path);
        if (t.texture == nullptr) {
            wsprintfW(path, L"%ls\\data\\map\\%02d\\%d_%d.jpg", api->getModulePath(), index / 100, index % 10, (index % 100) / 10);
            t = api->loadTexture(path);
        }
    }
    if (t.texture == nullptr) return;

    float texPixelW = (float)t.width * effectiveScale_;
    float texPixelH = (float)t.height * effectiveScale_;

    auto *drawList = ImGui::GetWindowDrawList();
    auto winPos = ImGui::GetWindowPos();
    float cx = winPos.x + minimapWidth_ * 0.5f;
    float cy = winPos.y + minimapHeight_ * 0.5f;

    // Compute 4 rotated corners in screen space (TL, TR, BR, BL)
    float offsets[4][2] = {
        {tileOffsetX, tileOffsetY},
        {tileOffsetX + texPixelW, tileOffsetY},
        {tileOffsetX + texPixelW, tileOffsetY + texPixelH},
        {tileOffsetX, tileOffsetY + texPixelH},
    };
    ImVec2 p[4];
    float minX = 1e10f, minY = 1e10f, maxX = -1e10f, maxY = -1e10f;
    for (int i = 0; i < 4; i++) {
        p[i] = {cx + offsets[i][0] * cosRot - offsets[i][1] * sinRot,
                cy + offsets[i][0] * sinRot + offsets[i][1] * cosRot};
        minX = std::min(minX, p[i].x);
        minY = std::min(minY, p[i].y);
        maxX = std::max(maxX, p[i].x);
        maxY = std::max(maxY, p[i].y);
    }

    // Early-out: bounding box completely outside minimap window
    if (maxX < winPos.x || maxY < winPos.y || minX > winPos.x + minimapWidth_ || minY > winPos.y + minimapHeight_) {
        return;
    }

    // Draw tile as a rotated textured quad.
    // Adjacent tiles share edges exactly, so there are no gaps or overlaps.
    // Circular clipping is handled by the offscreen compositing step.
    drawList->PushTexture((ImTextureID)t.gpuHandle);
    drawList->PrimReserve(6, 4);
    drawList->PrimQuadUV(p[0], p[1], p[2], p[3],
        ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1),
        IM_COL32(255, 255, 255, (int)(currentAlpha_ * 255.f)));
    drawList->PopTexture();
}

void Renderer::renderPlayer(float deltaRad) {
    if (playerSprite_ == nullptr) {
        return;
    }
    auto halfWidth = minimapWidth_ * .5f;
    auto halfHeight = minimapHeight_ * .5f;
    ImGui::SetCursorPos(ImVec2(halfWidth - playerSprite_->centerX * effectiveScale_ * effectivePlayerScale_, halfHeight - playerSprite_->centerY * effectiveScale_ * effectivePlayerScale_));
    ImGui::ImageWithBg((ImTextureID)playerSprite_->texture->gpuHandle, ImVec2(playerSprite_->width * effectiveScale_ * effectivePlayerScale_, playerSprite_->height * effectiveScale_ * effectivePlayerScale_), ImVec2(playerSprite_->u0, playerSprite_->v0), ImVec2(playerSprite_->u1, playerSprite_->v1), ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, currentAlpha_));
    if (arrowSprite_ == nullptr) {
        return;
    }
    if (deltaRad == 0.f) {
        // In rotation mode the map already faces the player direction — arrow points up (no rotation)
        ImGui::SetCursorPos(ImVec2(halfWidth - arrowSprite_->centerX * effectiveScale_ * effectivePlayerScale_, halfHeight - arrowSprite_->centerY * effectiveScale_ * effectivePlayerScale_));
        ImGui::ImageWithBg((ImTextureID)arrowSprite_->texture->gpuHandle, ImVec2(arrowSprite_->width * effectiveScale_ * effectivePlayerScale_, arrowSprite_->height * effectiveScale_ * effectivePlayerScale_), ImVec2(arrowSprite_->u0, arrowSprite_->v0), ImVec2(arrowSprite_->u1, arrowSprite_->v1), ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, currentAlpha_));
    } else {
        auto *drawList = ImGui::GetWindowDrawList();
        auto cursorPos = ImGui::GetWindowPos() + ImVec2(halfWidth, halfHeight);
        auto xRel0 = -arrowSprite_->centerX * effectiveScale_ * effectivePlayerScale_;
        auto yRel0 = -arrowSprite_->centerY * effectiveScale_ * effectivePlayerScale_;
        auto xRel1 = (arrowSprite_->width - arrowSprite_->centerX) * effectiveScale_ * effectivePlayerScale_;
        auto yRel1 = (arrowSprite_->height - arrowSprite_->centerY) * effectiveScale_ * effectivePlayerScale_;
        auto cosRad = std::cos(deltaRad);
        auto sinRad = std::sin(deltaRad);
        // PrimQuadUV: TL, TR, BR, BL (matches PrimRectUV / AddImage winding).
        auto p1 = cursorPos + ImVec2(xRel0 * cosRad - yRel0 * sinRad, xRel0 * sinRad + yRel0 * cosRad);
        auto p2 = cursorPos + ImVec2(xRel1 * cosRad - yRel0 * sinRad, xRel1 * sinRad + yRel0 * cosRad);
        auto p3 = cursorPos + ImVec2(xRel1 * cosRad - yRel1 * sinRad, xRel1 * sinRad + yRel1 * cosRad);
        auto p4 = cursorPos + ImVec2(xRel0 * cosRad - yRel1 * sinRad, xRel0 * sinRad + yRel1 * cosRad);
        drawList->PushTexture((ImTextureID)arrowSprite_->texture->gpuHandle);
        drawList->PrimReserve(6, 4); // 6 indices for 2 triangles, 4 vertices
        drawList->PrimQuadUV(p1, p2, p3, p4, ImVec2(arrowSprite_->u0, arrowSprite_->v0), ImVec2(arrowSprite_->u1, arrowSprite_->v0), ImVec2(arrowSprite_->u1, arrowSprite_->v1), ImVec2(arrowSprite_->u0, arrowSprite_->v1), IM_COL32(255, 255, 255, (int)(currentAlpha_ * 255.f)));
        drawList->PopTexture();
    }
}

bool Renderer::isPointInShape(float x, float y) const {
    if (currentShape_ == Shape::Rect) return true;
    float cx = minimapWidth_ * 0.5f;
    float cy = minimapHeight_ * 0.5f;
    if (currentShape_ == Shape::Circle) {
        float radius = cx;  // Circle forces square, so cx == cy == radius
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
    if (x >= left && x <= right && y >= 0.f && y <= minimapHeight_) return true;
    if (y >= top && y <= bottom && x >= 0.f && x <= minimapWidth_) return true;
    // Check the four corner arcs
    auto checkCorner = [](float px, float py, float cornerX, float cornerY, float r) {
        float dx = px - cornerX;
        float dy = py - cornerY;
        return dx * dx + dy * dy <= r * r;
    };
    if (x < left && y < top) return checkCorner(x, y, left, top, cachedRounding_);
    if (x > right && y < top) return checkCorner(x, y, right, top, cachedRounding_);
    if (x < left && y > bottom) return checkCorner(x, y, left, bottom, cachedRounding_);
    if (x > right && y > bottom) return checkCorner(x, y, right, bottom, cachedRounding_);
    return false;
}

}
