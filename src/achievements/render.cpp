#include "render.hpp"

#include "data.hpp"

#include "api.h"
#include "util/string.hpp"

#include <imgui.h>

extern EROverlayAPI *api;

namespace er::achievements {

void Renderer::init(void *context, void *allocFunc, void *freeFunc, void *userData) {
    ImGui::SetCurrentContext((ImGuiContext *)context);
    ImGui::SetAllocatorFunctions((ImGuiMemAllocFunc)allocFunc, (ImGuiMemFreeFunc)freeFunc, userData);

    maxAchievements_ = api->configGetInt("achievements.max_achievements", 20);
    const auto &pos = api->configGet("achievements.panel_pos");
    auto posVec = util::strSplitToFloatVec(pos);
    if (posVec.size() >= 4) {
        posX_ = posVec[0];
        posY_ = posVec[1];
        width_ = posVec[2];
        height_ = posVec[3];
    }
}

inline static float calculatePos(float w, float n) {
    if (n >= 1.f) {
        return n;
    }
    if (n >= 0.f) {
        return w * n;
    }
    if (n <= -1.f) {
        return w + n;
    }
    return w + w * n;
}

bool Renderer::render() {
    auto *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(calculatePos(vp->Size.x, posX_), calculatePos(vp->Size.y, posY_)), ImGuiCond_Always, ImVec2(posX_ >= 0 ? 0.f : 1.f, posY_ >= 0 ? 0.f : 1.f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(-1, -1), ImVec2(calculatePos(vp->Size.x, std::abs(width_)), calculatePos(vp->Size.y, std::abs(height_))));

    if (ImGui::Begin("##achievements_window", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        const auto &achievements = gData.achievements();
        std::lock_guard lock(gData.mutex());
        const auto &locked = gData.locked();
        auto total = achievements.size();
        auto sz = locked.size();
        ImGui::Text("%zu/%zu", total - sz, total);
        ImGui::Spacing();
        if (sz > maxAchievements_) sz = size_t(maxAchievements_);
        for (size_t i = 0; i < sz; i++) {
            const auto &ach = achievements[locked[i]];
            ImGui::TextUnformatted(ach.displayName);
        }
    }
    ImGui::End();

    return false;
}

}
