#include "render.hpp"
#include "data.hpp"

#include "imgui.h"

namespace er::bosses {

void Render::render(bool &showFull) {
    counter_ = (counter_ + 1) & 0x1F;
    if (counter_ == 0) {
        gBossDataSet.update();
    }
    auto *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Size.x - 10, 10), ImGuiCond_Always, ImVec2(1.f, 0.f));
    if (!showFull) {
        ImGui::Begin("##bosses_window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
        {
            std::unique_lock lock(gBossDataSet.mutex());
            ImGui::Text("%d/%d", gBossDataSet.count(), gBossDataSet.total());
        }
        ImGui::SameLine();
        if (ImGui::ArrowButton("##bosses_arrow", ImGuiDir_Down)) {
            showFull = true;
        }
    } else {
        ImGui::SetNextWindowSize(ImVec2(vp->Size.x / 8.0f, vp->Size.y - 20.f), ImGuiCond_Always);
        ImGui::Begin("##bosses_window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
        std::unique_lock lock(gBossDataSet.mutex());
        auto regionIndex = gBossDataSet.regionIndex();
        if (regionIndex != lastRegionIndex_) {
            lastRegionIndex_ = regionIndex;
        } else {
            regionIndex = -1;
        }
        ImGui::Text("%d/%d", gBossDataSet.count(), gBossDataSet.total());
        ImGui::SameLine(ImGui::GetWindowContentRegionWidth() - 16.f);
        if (ImGui::ArrowButton("##bosses_arrow", ImGuiDir_Up)) {
            showFull = false;
        }
        ImGui::BeginChild("##bosses_list", ImGui::GetContentRegionAvail());
        const auto &bosses = gBossDataSet.bosses();
        const auto &regions = gBossDataSet.regions();
        const auto &alive = gBossDataSet.alive();
        int sz = (int)regions.size();
        for (int i = 0; i < sz; i++) {
            const auto &region = regions[i];
            auto bossCount = (int)region.bosses.size();
            if (regionIndex >= 0) {
                ImGui::SetNextItemOpen(i == regionIndex);
            }
            if (ImGui::TreeNode(&region, "%d/%d %s", region.count, bossCount, region.name.c_str())) {
                for (int j = 0; j < bossCount; j++) {
                    auto &bd = bosses[region.bosses[j]];
                    auto on = alive[bd.index];
                    ImGui::Checkbox(bd.displayName.c_str(), &on);
                    if (ImGui::IsItemHovered() && !bd.tip.empty()) {
                        ImGui::SetTooltip(bd.tip.c_str());
                    }
                }
                ImGui::TreePop();
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

}
