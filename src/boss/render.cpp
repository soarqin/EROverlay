#include "render.hpp"

#include "data.hpp"

#include "api.h"
#include "util/string.hpp"

#include <imgui.h>

extern EROverlayAPI *api;

namespace fmt {
template<>
struct formatter<er::bosses::IntProxy> : formatter<string_view> {
    auto format(const er::bosses::IntProxy &value, format_context &ctx) const {
        return formatter<string_view>::format(std::to_string(value.value), ctx);
    }
};
}

namespace er::bosses {

void Renderer::init(void *context, void *allocFunc, void *freeFunc, void *userData) {
    ImGui::SetCurrentContext((ImGuiContext *)context);
    ImGui::SetAllocatorFunctions((ImGuiMemAllocFunc)allocFunc, (ImGuiMemFreeFunc)freeFunc, userData);
    killText_ = api->configGet("boss.boss_kill_text");
    challengeText_ = api->configGet("boss.challenge_status_text");
    const std::string from = "$n";
    const std::string to = "\n";
    const std::string from2 = "{igt}";
    const std::string to2 = "{igt:.0%M:%S}";
    const std::string from3 = "{igt}";
    const std::string to3 = "{igt:.0%H:%M:%S}";
    util::replaceAll(killText_, from, to);
    util::replaceAll(challengeText_, from, to);
    killTextHour_ = killText_;
    challengeTextHour_ = challengeText_;
    util::replaceAll(killText_, from2, to2);
    util::replaceAll(challengeText_, from2, to2);
    util::replaceAll(killTextHour_, from3, to3);
    util::replaceAll(challengeTextHour_, from3, to3);
    allowRevive_ = api->configEnabled("boss.allow_revive");
    const auto &pos = api->configGet("boss.panel_pos");
    auto posVec = util::strSplitToFloatVec(pos);
    if (posVec.size() >= 4) {
        posX_ = posVec[0];
        posY_ = posVec[1];
        width_ = posVec[2];
        height_ = posVec[3];
    }
    args_.clear();
    args_.push_back(fmt::arg("kills", std::cref(kills_)));
    args_.push_back(fmt::arg("total", std::cref(total_)));
    args_.push_back(fmt::arg("deaths", std::cref(deaths_)));
    args_.push_back(fmt::arg("pb", std::cref(pb_)));
    args_.push_back(fmt::arg("tries", std::cref(tries_)));
    args_.push_back(fmt::arg("igt", std::cref(igt_)));
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
    auto toggleFullModeKey = gBossDataSet.toggleFullModeKey();
    if (toggleFullModeKey != 0 && ImGui::IsKeyChordPressed(toggleFullModeKey)) {
        showFull_ = !showFull_;
    }
    auto challengeMode = gBossDataSet.challengeMode();
    {
        std::unique_lock lock(gBossDataSet.mutex());
        kills_ = gBossDataSet.count();
        total_ = gBossDataSet.total();
    }
    deaths_ = gBossDataSet.deaths();
    if (challengeMode) {
        pb_ = gBossDataSet.challengeBest();
        tries_ = gBossDataSet.challengeTries();
    }
    auto igt = gBossDataSet.readInGameTime();
    igt_ = std::chrono::milliseconds(igt);

    auto *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(calculatePos(vp->Size.x, posX_), calculatePos(vp->Size.y, posY_)),
                            ImGuiCond_Always,
                            ImVec2(posX_ >= 0 ? 0.f : 1.f, posY_ >= 0 ? 0.f : 1.f));
    if (!showFull_) {
        if (ImGui::Begin("##bosses_window", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            if (challengeMode) {
                auto text = fmt::vformat(igt < 3600000 ? challengeText_ : challengeTextHour_, args_);
                ImGui::TextUnformatted(text.c_str());
            } else {
                auto text = fmt::vformat(igt < 3600000 ? killText_ : killTextHour_, args_);
                ImGui::TextUnformatted(text.c_str());
            }
            ImGui::SameLine();
            if (ImGui::ArrowButton("##bosses_arrow", ImGuiDir_Down)) {
                showFull_ = true;
            }
        }
    } else {
        ImGui::SetNextWindowSize(ImVec2(calculatePos(vp->Size.x, std::abs(width_)), calculatePos(vp->Size.y, std::abs(height_))), ImGuiCond_Always);
        if (ImGui::Begin("##bosses_window", nullptr,
                         (ImGuiWindowFlags_NoDecoration & ~ImGuiWindowFlags_NoScrollbar) | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
            std::unique_lock lock(gBossDataSet.mutex());
            auto regionIndex = gBossDataSet.regionIndex();
            if (regionIndex != lastRegionIndex_) {
                lastRegionIndex_ = regionIndex;
            } else {
                regionIndex = -1;
            }
            if (gBossDataSet.challengeMode()) {
                auto text = fmt::vformat(igt < 3600000 ? challengeText_ : challengeTextHour_, args_);
                ImGui::TextUnformatted(text.c_str());
            } else {
                auto text = fmt::vformat(igt < 3600000 ? killText_ : killTextHour_, args_);
                ImGui::TextUnformatted(text.c_str());
            }
            auto &style = ImGui::GetStyle();
            ImGui::SameLine(
                ImGui::GetWindowWidth() - ImGui::GetFrameHeight() - style.WindowPadding.x - style.FramePadding.x);
            if (ImGui::ArrowButton("##bosses_arrow", ImGuiDir_Up)) {
                showFull_ = false;
            }
            const auto &bosses = gBossDataSet.bosses();
            bool popup = false;
            if (ImGui::BeginChild("##bosses_list", ImGui::GetContentRegionAvail())) {
                const auto &regions = gBossDataSet.regions();
                const auto &regionCounts = gBossDataSet.regionCounts();
                const auto &dead = gBossDataSet.dead();
                int sz = (int)regions.size();
                for (int i = 0; i < sz; i++) {
                    const auto &region = regions[i];
                    auto bossCount = (int)region.bosses.size();
                    if (regionIndex >= 0) {
                        ImGui::SetNextItemOpen(i == regionIndex);
                    }
                    if (ImGui::TreeNode(&region, "%d/%d %s", regionCounts[i], bossCount, region.name.c_str())) {
                        for (int j = 0; j < bossCount; j++) {
                            auto &bd = bosses[region.bosses[j]];
                            bool on = dead[bd.index];
                            if (ImGui::Checkbox(bd.boss.c_str(), &on, on) && dead[bd.index] && allowRevive_) {
                                popupBossIndex_ = (int)bd.index;
                                popup = true;
                            }
                            if (ImGui::IsItemHovered() && !bd.tip.empty()) {
                                ImGui::SetTooltip("%s", bd.tip.c_str());
                            }
                        }
                        ImGui::TreePop();
                    }
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
                if (ImGui::BeginPopupModal("##bosses_revive_confirm", nullptr,
                                           ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("Revive %s?", bosses[popupBossIndex_].boss.c_str());
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
    }
    ImGui::End();
    return showFull_;
}

}
