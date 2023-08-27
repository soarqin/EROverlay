#include "render.hpp"
#include "data.hpp"

#include "../config.hpp"

#include "imgui.h"

namespace er::bosses {

void Render::init() {
    allowRevive_ = gConfig.enabled("boss.allow_revive");
}

void Render::render(bool &showFull) {
    auto *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Size.x - 10, 10), ImGuiCond_Always, ImVec2(1.f, 0.f));
    if (!showFull) {
        ImGui::Begin("##bosses_window",
                     nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                         | ImGuiWindowFlags_AlwaysAutoResize);
        {
            std::unique_lock lock(gBossDataSet.mutex());
            ImGui::Text("%d/%d", gBossDataSet.count(), gBossDataSet.total());
        }
        ImGui::SameLine();
        if (ImGui::ArrowButton("##bosses_arrow", ImGuiDir_Down)) {
            showFull = true;
        }
    } else {
        ImGui::SetNextWindowSize(ImVec2(vp->Size.x * 0.12f, vp->Size.y * .9f), ImGuiCond_Always);
        ImGui::Begin("##bosses_window",
                     nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
        std::unique_lock lock(gBossDataSet.mutex());
        auto regionIndex = gBossDataSet.regionIndex();
        if (regionIndex != lastRegionIndex_) {
            lastRegionIndex_ = regionIndex;
        } else {
            regionIndex = -1;
        }
        ImGui::Text("%d/%d", gBossDataSet.count(), gBossDataSet.total());
        auto &style = ImGui::GetStyle();
        ImGui::SameLine(
            ImGui::GetWindowWidth() - ImGui::GetFrameHeight() - style.WindowPadding.x - style.FramePadding.x);
        if (ImGui::ArrowButton("##bosses_arrow", ImGuiDir_Up)) {
            showFull = false;
        }
        ImGui::BeginChild("##bosses_list", ImGui::GetContentRegionAvail());
        const auto &bosses = gBossDataSet.bosses();
        const auto &regions = gBossDataSet.regions();
        const auto &dead = gBossDataSet.dead();
        int sz = (int)regions.size();
        bool popup = false;
        for (int i = 0; i < sz; i++) {
            const auto &region = regions[i];
            auto bossCount = (int)region.bosses.size();
            if (regionIndex >= 0) {
                ImGui::SetNextItemOpen(i == regionIndex);
            }
            if (ImGui::TreeNode(&region, "%d/%d %s", region.count, bossCount, region.name.c_str())) {
                for (int j = 0; j < bossCount; j++) {
                    auto &bd = bosses[region.bosses[j]];
                    auto on = dead[bd.index];
                    if (ImGui::Checkbox(bd.displayName.c_str(), &on) && dead[bd.index] && allowRevive_) {
                        popupBossIndex_ = (int)bd.index;
                        popup = true;
                    }
                    if (ImGui::IsItemHovered() && !bd.tip.empty()) {
                        ImGui::SetTooltip(bd.tip.c_str());
                    }
                }
                ImGui::TreePop();
            }
        }
        ImGui::EndChild();
        if (allowRevive_) {
            if (popup) {
                ImGui::OpenPopup("##bosses_revive_confirm");
                ImGui::SetNextWindowPos(ImVec2(vp->Size.x * 0.94f, vp->Size.y / 2.0f),
                                        ImGuiCond_Appearing,
                                        ImVec2(.5f, .5f));
            }
            if (ImGui::BeginPopupModal("##bosses_revive_confirm",
                                       nullptr,
                                       ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                                           | ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Revive %s?", bosses[popupBossIndex_].displayName.c_str());
                if (ImGui::Button("Yes!")) {
                    gBossDataSet.revive(popupBossIndex_);
                    ImGui::CloseCurrentPopup();
                    popupBossIndex_ = -1;
                }
                ImGui::SameLine();
                if (ImGui::Button("NO!")) {
                    ImGui::CloseCurrentPopup();
                    popupBossIndex_ = -1;
                }
                ImGui::EndPopup();
            }
        }
    }
    ImGui::End();
}

}
