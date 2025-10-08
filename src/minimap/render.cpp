#include "render.hpp"

#define _USE_MATH_DEFINES
#include "data.hpp"

#include "util/string.hpp"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
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
static constexpr float dlcOffsetX = 3035.f;
static constexpr float dlcOffsetY = 1864.f;
static constexpr int textureSizeInt = 1024;
static constexpr float textureSize = (float)textureSizeInt;
static constexpr float textureArrowOriginX = 34.f;
static constexpr float textureArrowOriginY = 86.f;
static constexpr float texturePlayerOriginX = 34.f;
static constexpr float texturePlayerOriginY = 34.f;
static constexpr float texturePlayerScale = 0.33f;

void Renderer::init(void *context, void *allocFunc, void *freeFunc, void *userData) {
    minimapWidthRatio_ = std::clamp(api->configGetFloat("minimap.width_ratio", 0.3f), 0.1f, 0.5f);
    minimapHeightRatio_ = std::clamp(api->configGetFloat("minimap.height_ratio", 0.3f), 0.1f, 0.5f);
    mapScale_ = std::clamp(api->configGetFloat("minimap.scale", 0.75f), 0.3f, 1.5f);
    mapAlpha_ = std::clamp(api->configGetFloat("minimap.alpha", 0.8f), 0.f, 1.f);
    ImGui::SetCurrentContext((ImGuiContext *)context);
    ImGui::SetAllocatorFunctions((ImGuiMemAllocFunc)allocFunc, (ImGuiMemFreeFunc)freeFunc, userData);
    textures_.resize(300);
}

bool Renderer::render() {
    if (gData.onGUI()) {
        return false;
    }
    const auto &location = gData.location();
    if (location.x == 0.f) return false;
    auto *vp = ImGui::GetMainViewport();
    auto realHeight = vp->Size.x * .5625f >= vp->Size.y ? vp->Size.y : vp->Size.x * .5625f;
    minimapWidth_ = realHeight * minimapWidthRatio_;
    minimapHeight_ = realHeight * minimapHeightRatio_;
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec2 originalPadding = style.WindowPadding;
    style.WindowPadding = ImVec2(0, 0);
    ImGui::SetNextWindowPos(ImVec2(vp->Size.x - minimapWidth_, 0), ImGuiCond_Always, ImVec2(0.f, 0.f));
    ImGui::SetNextWindowSize(ImVec2(minimapWidth_, minimapHeight_));
    if (ImGui::Begin("##minimap_window", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings)) {
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
                dx = location.x - dlcOffsetX;
                dy = location.y - dlcOffsetY;
                break;
            default:
                ImGui::End();
                return false;
        }
        dx /= textureSize;
        dy /= textureSize;
        auto texSize = textureSize * mapScale_;
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
                renderMinimap(index0, nx, ny, mapScale_);
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
        auto boundMaxX = minimapWidth_ + 100.f;
        auto boundMaxY = minimapHeight_ + 100.f;
        ny = cy;
        auto bonfireSize = 30.f * mapScale_;
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
                    auto rx = bonfire->localX * mapScale_ + nx;
                    auto ry = bonfire->localY * mapScale_ + ny;
                    if (rx > -100.f && ry > -100.f && rx < boundMaxX && ry < boundMaxY) {
                        ImGui::SetCursorPos(ImVec2(rx + bonfireOffset, ry + bonfireOffset));
                        ImGui::ImageWithBg((ImTextureID)bonfireTexture_.gpuHandle, ImVec2(bonfireSize, bonfireSize));
                    }
                }
            }
        }
        if (drawRoundTable && roundTableTexture_.texture != nullptr) {
            ImGui::SetCursorPos(ImVec2(minimapWidth_ * .5f - roundTableTexture_.width * .25f, minimapHeight_ * .5f - roundTableTexture_.height * .25f));
            ImGui::ImageWithBg((ImTextureID)roundTableTexture_.gpuHandle, ImVec2(roundTableTexture_.width * .5f, roundTableTexture_.height * .5f));
        }
        renderPlayer();
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
    ImGui::ImageWithBg((ImTextureID)t.gpuHandle, ImVec2(width, height), ImVec2(rx / texWidth, ry / texHeight), ImVec2((rx + width) / texWidth, (ry + height) / texHeight), ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, mapAlpha_));
}

void Renderer::renderPlayer() {
    if (playerTexture_.texture == nullptr) {
        return;
    }
    auto halfWidth = minimapWidth_ * .5f;
    auto halfHeight = minimapHeight_ * .5f;
    ImGui::SetCursorPos(ImVec2(halfWidth - texturePlayerOriginX * texturePlayerScale, halfHeight - texturePlayerOriginY * texturePlayerScale));
    ImGui::ImageWithBg((ImTextureID)playerTexture_.gpuHandle, ImVec2(playerTexture_.width * texturePlayerScale, playerTexture_.height * texturePlayerScale));
    if (arrowTexture_.texture == nullptr) {
        return;
    }
    auto *drawList = ImGui::GetWindowDrawList();
    auto cursorPos = ImGui::GetWindowPos() + ImVec2(halfWidth, halfHeight);
    auto xRel0 = -textureArrowOriginX * texturePlayerScale;
    auto yRel0 = -textureArrowOriginY * texturePlayerScale;
    auto xRel1 = (arrowTexture_.width - textureArrowOriginX) * texturePlayerScale;
    auto yRel1 = (arrowTexture_.height - textureArrowOriginY) * texturePlayerScale;
    auto rad = gData.location().rad * (float)M_PI / 180.f;
    auto cosRad = std::cos(rad);
    auto sinRad = std::sin(rad);
    auto p1 = cursorPos + ImVec2(xRel0 * cosRad - yRel0 * sinRad, xRel0 * sinRad + yRel0 * cosRad);
    auto p2 = cursorPos + ImVec2(xRel1 * cosRad - yRel0 * sinRad, xRel1 * sinRad + yRel0 * cosRad);
    auto p3 = cursorPos + ImVec2(xRel0 * cosRad - yRel1 * sinRad, xRel0 * sinRad + yRel1 * cosRad);
    auto p4 = cursorPos + ImVec2(xRel1 * cosRad - yRel1 * sinRad, xRel1 * sinRad + yRel1 * cosRad);
    drawList->PushTexture((ImTextureID)arrowTexture_.gpuHandle);
    drawList->PrimReserve(6, 4); // 6 indices for 2 triangles, 4 vertices
    drawList->PrimQuadUV(p1, p2, p3, p4, ImVec2(0, 0), ImVec2(1, 0), ImVec2(0, 1), ImVec2(1, 1), IM_COL32_WHITE);
    drawList->PopTexture();
}

}
