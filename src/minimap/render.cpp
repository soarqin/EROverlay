#define _USE_MATH_DEFINES
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "render.hpp"

#include "data.hpp"

#include "util/string.hpp"
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>
#include <windows.h>
#include <cmath>
#undef _USE_MATH_DEFINES

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
static constexpr float textureArrowOriginX = 34.f;
static constexpr float textureArrowOriginY = 86.f;
static constexpr float texturePlayerOriginX = 34.f;
static constexpr float texturePlayerOriginY = 34.f;
static constexpr float texturePlayerScale = 0.44f;

void Renderer::init(void *context, void *allocFunc, void *freeFunc, void *userData) {
    ImGui::SetCurrentContext((ImGuiContext *)context);
    ImGui::SetAllocatorFunctions((ImGuiMemAllocFunc)allocFunc, (ImGuiMemFreeFunc)freeFunc, userData);
    textures_.resize(300);

    toggleKey_ = api->configGetImGuiKey("minimap.toggle_key", ImGuiKey_M);
    scaleKey_ = api->configGetImGuiKey("minimap.scale_key", ImGuiKey_N);
    gracesKey_ = api->configGetImGuiKey("minimap.graces_key", ImGuiKey_N);
    showGraces_ = api->configGetInt("minimap.graces", 1) != 0;
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
        if (s == L"rounded") shapes_.push_back(Shape::Rounded);
        else if (s == L"circle") shapes_.push_back(Shape::Circle);
        else shapes_.push_back(Shape::Rect);
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
    // Parse border_color: "R,G,B,A" format (0-255 each)
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
    auto maxCount = std::max({widthRatios_.size(), heightRatios_.size(), scales_.size(), alphas_.size(), shapes_.size(), roundings_.size()});
    if (maxCount == 0) {
        maxCount = 1;
        widthRatios_.push_back(0.3f);
        heightRatios_.push_back(0.3f);
        scales_.push_back(0.75f);
        alphas_.push_back(0.8f);
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
        if (shapes_.empty()) {
            shapes_.push_back(Shape::Rect);
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
         while (shapes_.size() < maxCount) {
             shapes_.push_back(shapes_.back());
         }
         if (roundings_.empty()) {
             roundings_.push_back(0.2f);
             roundingIsPercent_.push_back(true);
         }
         while (roundings_.size() < maxCount) {
             roundings_.push_back(roundings_.back());
             roundingIsPercent_.push_back(roundingIsPercent_.back());
         }
     }
     if (toggleKey_ == scaleKey_) {
         scales_.push_back(0.f);
         widthRatios_.push_back(widthRatios_.back());
         heightRatios_.push_back(heightRatios_.back());
         shapes_.push_back(shapes_.back());
         roundings_.push_back(roundings_.back());
         roundingIsPercent_.push_back(roundingIsPercent_.back());
     }
     currentWidthRatio_ = widthRatios_[0];
     currentHeightRatio_ = heightRatios_[0];
     currentScale_ = scales_[0];
     currentAlpha_ = alphas_[0];
     currentIsCentered_ = isCentered_[0];
     currentShape_ = shapes_[0];
     currentRounding_ = roundings_[0];
     currentRoundingIsPercent_ = roundingIsPercent_[0];
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
     }
    if (!show_ || currentScale_ < 0.0001f) {
        return false;
    }
    if (gracesKey_ != 0 && ImGui::IsKeyChordPressed(static_cast<ImGuiKey>(gracesKey_))) {
        showGraces_ = !showGraces_;
    }

    const auto &location = gData.location();
    if (location.x == 0.f) return false;
    auto *vp = ImGui::GetMainViewport();
    auto realHeight = vp->Size.x * .5625f >= vp->Size.y ? vp->Size.y : vp->Size.x * .5625f;
    minimapWidth_ = realHeight * currentWidthRatio_;
    minimapHeight_ = realHeight * currentHeightRatio_;
    if (currentShape_ == Shape::Circle) {
        float side = std::min(minimapWidth_, minimapHeight_);
        minimapWidth_ = side;
        minimapHeight_ = side;
    }
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec2 originalPadding = style.WindowPadding;
    style.WindowPadding = ImVec2(0, 0);
    if (currentIsCentered_) {
        ImGui::SetNextWindowPos(ImVec2((vp->Size.x - minimapWidth_) * .5f, (vp->Size.y - minimapHeight_) * .5f), ImGuiCond_Always, ImVec2(0.f, 0.f));
        ImGui::SetNextWindowSize(ImVec2(minimapWidth_, minimapHeight_));
    } else {
        ImGui::SetNextWindowPos(ImVec2(vp->Size.x - minimapWidth_, 0), ImGuiCond_Always, ImVec2(0.f, 0.f));
        ImGui::SetNextWindowSize(ImVec2(minimapWidth_, minimapHeight_));
    }
    if (ImGui::Begin("##minimap_window", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings)) {
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
                ImGui::End();
                return false;
        }
        dx /= textureSize;
        dy /= textureSize;
        auto texSize = textureSize * currentScale_;
        auto u = minimapWidth_ / texSize;
        auto v = minimapHeight_ / texSize;
        dx -= u * 0.5f;
        dy -= v * 0.5f;
        auto cx = (std::floor(dx) - dx) * texSize;
        auto cy = (std::floor(dy) - dy) * texSize;
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
                    renderMinimap(index0, nx, ny, currentScale_);
                } else {
                    renderShapedMinimap(index0, nx, ny, currentScale_);
                }
            }
        }
        if (!playerTexture_.loaded) {
            wchar_t path[256];
            wsprintfW(path, L"%ls\\data\\map\\player.png", api->getModulePath());
            playerTexture_ = api->loadTexture(path);
            wsprintfW(path, L"%ls\\data\\map\\arrow.png", api->getModulePath());
            arrowTexture_ = api->loadTexture(path);
            wsprintfW(path, L"%ls\\data\\map\\roundtable.png", api->getModulePath());
            roundTableTexture_ = api->loadTexture(path);
            wsprintfW(path, L"%ls\\data\\map\\bonfire.png", api->getModulePath());
            bonfireTexture_ = api->loadTexture(path);
        }
        if (showGraces_ && gData.paramsLoaded()) {
            auto boundMaxX = minimapWidth_ + 100.f;
            auto boundMaxY = minimapHeight_ + 100.f;
            ny = cy;
            auto bonfireSize = 39.f * currentScale_;
            auto bonfireOffset = -bonfireSize * .5f;
            for (auto y = y0; y <= y1; y++, ny += texSize) {
                auto index0 = layer * 100 + y * 10 + x0;
                auto nx = cx;
                for (auto x = x0; x <= x1; x++, nx += texSize, index0++) {
                    auto p = gData.bonfiresAround(layer, x, y);
                    auto *begin = std::get<0>(p);
                    if (begin == nullptr) continue;
                    auto *end = std::get<1>(p);
                    for (auto *bonfire = begin; bonfire < end; bonfire++) {
                         if (!bonfire->isUnlocked()) continue;
                         auto rx = bonfire->localX * currentScale_ + nx;
                         auto ry = bonfire->localY * currentScale_ + ny;
                         if (rx > -100.f && ry > -100.f && rx < boundMaxX && ry < boundMaxY) {
                             if (currentShape_ != Shape::Rect) {
                                 auto iconCenter = rx + bonfireSize * 0.5f;
                                 auto iconCenterY = ry + bonfireSize * 0.5f;
                                 if (!isPointInShape(iconCenter, iconCenterY)) continue;
                             }
                             ImGui::SetCursorPos(ImVec2(rx + bonfireOffset, ry + bonfireOffset));
                             ImGui::ImageWithBg((ImTextureID)bonfireTexture_.gpuHandle, ImVec2(bonfireSize, bonfireSize), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, currentAlpha_));
                         }
                     }
                }
            }
        }
        if (drawRoundTable && roundTableTexture_.texture != nullptr) {
             if (currentShape_ == Shape::Rect || isPointInShape(minimapWidth_ * 0.5f, minimapHeight_ * 0.5f)) {
                 ImGui::SetCursorPos(ImVec2(minimapWidth_ * .5f - roundTableTexture_.width * .25f, minimapHeight_ * .5f - roundTableTexture_.height * .25f));
                 ImGui::ImageWithBg((ImTextureID)roundTableTexture_.gpuHandle, ImVec2(roundTableTexture_.width * .5f, roundTableTexture_.height * .5f), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, currentAlpha_));
             }
         }
         renderPlayer();
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
                 float rounding = currentRoundingIsPercent_
                     ? currentRounding_ * std::min(minimapWidth_, minimapHeight_) * 0.5f
                     : currentRounding_;
                 drawList->AddRect(shapeMin, shapeMax, borderColor_, rounding, 0, borderWidth_);
             } else {
                 drawList->AddRect(shapeMin, shapeMax, borderColor_, 0.f, 0, borderWidth_);
             }
         }
     }
     ImGui::End();
     style.WindowPadding = originalPadding;
     return false;
}

void Renderer::renderMinimap(int index, float posX, float posY, float scale) {
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
        return;
    }
    auto width = (float)t.width * scale;
    auto height = (float)t.height * scale;
    auto texWidth = width;
    auto texHeight = height;
    auto rx = 0.f;
    auto ry = 0.f;
    if (posX < 0) {
        rx -= posX;
        width += posX;
        posX = 0.f;
    }
    if (posY < 0) {
        ry -= posY;
        height += posY;
        posY = 0.f;
    }
    if (posX + width > minimapWidth_) {
        width = minimapWidth_ - posX;
    }
    if (posY + height > minimapHeight_) {
        height = minimapHeight_ - posY;
    }
    ImGui::SetCursorPos(ImVec2(posX, posY));
    ImGui::ImageWithBg((ImTextureID)t.gpuHandle, ImVec2(width, height), ImVec2(rx / texWidth, ry / texHeight), ImVec2((rx + width) / texWidth, (ry + height) / texHeight), ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, currentAlpha_));
}

void Renderer::renderShapedMinimap(int index, float posX, float posY, float scale) {
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
        return;
    }
    auto width = (float)t.width * scale;
    auto height = (float)t.height * scale;
    auto texWidth = width;
    auto texHeight = height;
    auto rx = 0.f;
    auto ry = 0.f;
    if (posX < 0) {
        rx -= posX;
        width += posX;
        posX = 0.f;
    }
    if (posY < 0) {
        ry -= posY;
        height += posY;
        posY = 0.f;
    }
    if (posX + width > minimapWidth_) {
        width = minimapWidth_ - posX;
    }
    if (posY + height > minimapHeight_) {
        height = minimapHeight_ - posY;
    }
    if (width <= 0.f || height <= 0.f) return;

    float rounding = currentRoundingIsPercent_ ? currentRounding_ * std::min(minimapWidth_, minimapHeight_) * 0.5f : currentRounding_;

    auto *drawList = ImGui::GetWindowDrawList();
    auto winPos = ImGui::GetWindowPos();

    // Shape screen coordinates (full minimap bounds)
    ImVec2 shapeMin = winPos;
    ImVec2 shapeMax = {winPos.x + minimapWidth_, winPos.y + minimapHeight_};

    // Tile screen coordinates
    ImVec2 tileScreenMin = {winPos.x + posX, winPos.y + posY};
    ImVec2 tileScreenMax = {tileScreenMin.x + width, tileScreenMin.y + height};

    // PushClipRect to tile area intersected with minimap bounds
    drawList->PushClipRect(
        ImVec2(std::max(tileScreenMin.x, shapeMin.x), std::max(tileScreenMin.y, shapeMin.y)),
        ImVec2(std::min(tileScreenMax.x, shapeMax.x), std::min(tileScreenMax.y, shapeMax.y)),
        true);

    drawList->PushTexture((ImTextureID)t.gpuHandle);
    int vs = drawList->VtxBuffer.Size;
    if (currentShape_ == Shape::Circle) {
        float radius = minimapWidth_ * 0.5f;
        ImVec2 center = {shapeMin.x + radius, shapeMin.y + radius};
        drawList->PathArcTo(center, radius, 0.0f, IM_PI * 2.0f, 0);
    } else {
        drawList->PathRect(shapeMin, shapeMax, rounding);
    }
    drawList->PathFillConvex(IM_COL32(255, 255, 255, (int)(currentAlpha_ * 255.f)));
    int ve = drawList->VtxBuffer.Size;

    // Map tile texture UVs: texOriginScreen is the screen position of UV (0,0) of this tile
    ImVec2 texOriginScreen = {winPos.x + posX - rx, winPos.y + posY - ry};
    ImGui::ShadeVertsLinearUV(drawList, vs, ve, texOriginScreen, {texOriginScreen.x + texWidth, texOriginScreen.y + texHeight}, ImVec2(0, 0), ImVec2(1, 1), true);

    drawList->PopTexture();
    drawList->PopClipRect();
}

void Renderer::renderPlayer() {
    if (playerTexture_.texture == nullptr) {
        return;
    }
    auto halfWidth = minimapWidth_ * .5f;
    auto halfHeight = minimapHeight_ * .5f;
    ImGui::SetCursorPos(ImVec2(halfWidth - texturePlayerOriginX * currentScale_ * texturePlayerScale, halfHeight - texturePlayerOriginY * currentScale_ * texturePlayerScale));
    ImGui::ImageWithBg((ImTextureID)playerTexture_.gpuHandle, ImVec2(playerTexture_.width * currentScale_ * texturePlayerScale, playerTexture_.height * currentScale_ * texturePlayerScale), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, currentAlpha_));
    if (arrowTexture_.texture == nullptr) {
        return;
    }
    auto *drawList = ImGui::GetWindowDrawList();
    auto cursorPos = ImGui::GetWindowPos() + ImVec2(halfWidth, halfHeight);
    auto xRel0 = -textureArrowOriginX * currentScale_ * texturePlayerScale;
    auto yRel0 = -textureArrowOriginY * currentScale_ * texturePlayerScale;
    auto xRel1 = (arrowTexture_.width - textureArrowOriginX) * currentScale_ * texturePlayerScale;
    auto yRel1 = (arrowTexture_.height - textureArrowOriginY) * currentScale_ * texturePlayerScale;
    auto rad = gData.location().rad * (float)M_PI / 180.f;
    auto cosRad = std::cos(rad);
    auto sinRad = std::sin(rad);
    auto p1 = cursorPos + ImVec2(xRel0 * cosRad - yRel0 * sinRad, xRel0 * sinRad + yRel0 * cosRad);
    auto p2 = cursorPos + ImVec2(xRel1 * cosRad - yRel0 * sinRad, xRel1 * sinRad + yRel0 * cosRad);
    auto p3 = cursorPos + ImVec2(xRel0 * cosRad - yRel1 * sinRad, xRel0 * sinRad + yRel1 * cosRad);
    auto p4 = cursorPos + ImVec2(xRel1 * cosRad - yRel1 * sinRad, xRel1 * sinRad + yRel1 * cosRad);
    drawList->PushTexture((ImTextureID)arrowTexture_.gpuHandle);
    drawList->PrimReserve(6, 4); // 6 indices for 2 triangles, 4 vertices
    drawList->PrimQuadUV(p1, p2, p3, p4, ImVec2(0, 0), ImVec2(1, 0), ImVec2(0, 1), ImVec2(1, 1), IM_COL32(255, 255, 255, (int)(currentAlpha_ * 255.f)));
    drawList->PopTexture();
}

bool Renderer::isPointInShape(float x, float y) const {
    if (currentShape_ == Shape::Rect) return true;
    float cx = minimapWidth_ * 0.5f;
    float cy = minimapHeight_ * 0.5f;
    if (currentShape_ == Shape::Circle) {
        float radius = cx;  // 已强制正方形，cx == cy == radius
        float dx = x - cx;
        float dy = y - cy;
        return dx * dx + dy * dy <= radius * radius;
    }
    // Shape::Rounded
    float rounding = currentRoundingIsPercent_
        ? currentRounding_ * std::min(minimapWidth_, minimapHeight_) * 0.5f
        : currentRounding_;
    // 点在圆角矩形内：先检查内矩形，再检查角弧
    float left = rounding;
    float right = minimapWidth_ - rounding;
    float top = rounding;
    float bottom = minimapHeight_ - rounding;
    // 在内矩形内（无需检查角）
    if (x >= left && x <= right && y >= 0.f && y <= minimapHeight_) return true;
    if (y >= top && y <= bottom && x >= 0.f && x <= minimapWidth_) return true;
    // 检查四个角弧
    auto checkCorner = [](float px, float py, float cornerX, float cornerY, float r) {
        float dx = px - cornerX;
        float dy = py - cornerY;
        return dx * dx + dy * dy <= r * r;
    };
    if (x < left && y < top) return checkCorner(x, y, left, top, rounding);
    if (x > right && y < top) return checkCorner(x, y, right, top, rounding);
    if (x < left && y > bottom) return checkCorner(x, y, left, bottom, rounding);
    if (x > right && y > bottom) return checkCorner(x, y, right, bottom, rounding);
    return false;
}

}
