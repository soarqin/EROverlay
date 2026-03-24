#include "render.hpp"

#include "data.hpp"

#include "api.h"
#include "util/string.hpp"

#include <imgui.h>

extern EROverlayAPI *api;

namespace er::achievements {

// UI color constants
static constexpr ImVec4 kProgressTextColor{0.8f, 0.8f, 0.8f, 1.0f};
static constexpr ImVec4 kUnlockBaseColor{0.3f, 1.0f, 0.5f, 1.0f};

void Renderer::init(void *context, void *allocFunc, void *freeFunc, void *userData) {
    ImGui::SetCurrentContext((ImGuiContext *)context);
    ImGui::SetAllocatorFunctions((ImGuiMemAllocFunc)allocFunc, (ImGuiMemFreeFunc)freeFunc, userData);

    maxAchievements_ = size_t(api->configGetInt("achievements.max_achievements", 20));
    const auto &pos = api->configGet("achievements.panel_pos");
    auto posVec = util::strSplitToFloatVec(pos);
    if (posVec.size() >= 4) {
        posX_ = posVec[0];
        posY_ = posVec[1];
        width_ = posVec[2];
        height_ = posVec[3];
    }
}

static float calculatePos(float w, float n) {
    if (n >= 1.f) return n;
    if (n >= 0.f) return w * n;
    if (n <= -1.f) return w + n;
    return w + w * n;
}

bool Renderer::render() {
    auto *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(calculatePos(vp->Size.x, posX_), calculatePos(vp->Size.y, posY_)), ImGuiCond_Always, ImVec2(posX_ >= 0 ? 0.f : 1.f, posY_ >= 0 ? 0.f : 1.f));
    float maxW = width_ != 0.f ? calculatePos(vp->Size.x, std::abs(width_)) : FLT_MAX;
    float maxH = height_ != 0.f ? calculatePos(vp->Size.y, std::abs(height_)) : FLT_MAX;
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(maxW, maxH));

    if (ImGui::Begin("##achievements_window", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings)) {
        const auto &achievements = gData.achievements();
        std::lock_guard lock(gData.mutex());
        const auto &locked = gData.locked();
        const auto &unlocking = gData.unlocking();
        auto total = achievements.size();
        auto sz = locked.size();
        ImGui::PushStyleColor(ImGuiCol_Text, kProgressTextColor);
        ImGui::Text("%zu/%zu", total - sz, total);
        ImGui::PopStyleColor();
        ImGui::Spacing();
        if (sz > maxAchievements_) sz = maxAchievements_;
        if (!unlocking.empty()) {
            gData.updateUnlockingStatus();
            for (const auto &n : unlocking) {
                const auto &ach = achievements[n.index];
                ImVec4 color = kUnlockBaseColor;
                color.w = n.alpha;
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::Text("\xe2\x98\x91 %s", ach.displayName);
                ImGui::PopStyleColor();
            }
        }
        for (size_t i = 0; i < sz; i++) {
            auto &l = locked[i];
            const auto &ach = achievements[l.index];
            auto flagTotal = int(ach.collectionFlags.size());
            if (flagTotal == 0) {
                ImGui::TextUnformatted(ach.displayName);
                continue;
            }
            ImGui::Text("%s [%d/%d]", ach.displayName, l.flagCount, flagTotal);
        }
    }
    ImGui::End();
    return false;
}

}
