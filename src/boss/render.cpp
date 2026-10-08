#include "render.hpp"

#include "data.hpp"

#include "api.h"
#include "util/string.hpp"

#include <imgui.h>

extern EROverlayAPI *api;

namespace fmt {
template<>
struct formatter<er::bosses::IntProxy>: formatter<int> {
    auto format(const er::bosses::IntProxy &value, format_context &ctx) const { return formatter<int>::format(value.value, ctx); }
};
} // namespace fmt

namespace er::bosses {

void Renderer::init(void *context, void *allocFunc, void *freeFunc, void *userData) {
    ImGui::SetCurrentContext((ImGuiContext *)context);
    ImGui::SetAllocatorFunctions((ImGuiMemAllocFunc)allocFunc, (ImGuiMemFreeFunc)freeFunc, userData);
    killText_ = api->configGet("boss.boss_kill_text");
    challengeText_ = api->configGet("boss.challenge_status_text");
    const std::string newlinePlaceholder = "$n";
    const std::string newline = "\n";
    const std::string igtPlaceholder = "{igt}";
    const std::string igtMinSec = "{igt:.0%M:%S}";
    const std::string igtHourMinSec = "{igt:.0%H:%M:%S}";
    util::replaceAll(killText_, newlinePlaceholder, newline);
    util::replaceAll(challengeText_, newlinePlaceholder, newline);
    // Duplicate before substituting igt format — hour variant uses %H:%M:%S
    killTextHour_ = killText_;
    challengeTextHour_ = challengeText_;
    util::replaceAll(killText_, igtPlaceholder, igtMinSec);
    util::replaceAll(challengeText_, igtPlaceholder, igtMinSec);
    util::replaceAll(killTextHour_, igtPlaceholder, igtHourMinSec);
    util::replaceAll(challengeTextHour_, igtPlaceholder, igtHourMinSec);
    allowRevive_ = api->configEnabled("boss.allow_revive");
    std::wstring language = api->configGetString("common.language", L"");
    if (language.empty())
        language = api->getGameLanguage();
    chinese_ = _wcsicmp(language.c_str(), L"zhoCN") == 0 || _wcsicmp(language.c_str(), L"zhoTW") == 0;
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

static float calculatePos(float w, float n) {
    if (n >= 1.f)
        return n;
    if (n >= 0.f)
        return w * n;
    if (n <= -1.f)
        return w + n;
    return w + w * n;
}

std::string Renderer::formatStatusText(int igt, bool challengeMode) const {
    if (challengeMode) {
        return fmt::vformat(igt < 3600000 ? challengeText_ : challengeTextHour_, args_);
    }
    return fmt::vformat(igt < 3600000 ? killText_ : killTextHour_, args_);
}

void Renderer::renderMini(const RenderState &state) {
    if (ImGui::Begin("##bosses_window", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        auto text = formatStatusText(state.inGameTime, state.challengeMode);
        ImGui::TextUnformatted(text.c_str());
        ImGui::SameLine();
        if (ImGui::ArrowButton("##bosses_arrow", ImGuiDir_Down)) {
            showFull_ = true;
        }
    }
}

void Renderer::renderFull(const RenderState &state) {
    auto *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(calculatePos(vp->Size.x, std::abs(width_)), calculatePos(vp->Size.y, std::abs(height_))), ImGuiCond_Always);
    if (ImGui::Begin("##bosses_window", nullptr, (ImGuiWindowFlags_NoDecoration & ~ImGuiWindowFlags_NoScrollbar) | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
        // Auto-expand the tree node for the current region (only on region change)
        int autoExpandRegion = -1;
        if (state.regionIndex != lastRegionIndex_) {
            autoExpandRegion = state.regionIndex;
            lastRegionIndex_ = state.regionIndex;
        }

        auto text = formatStatusText(state.inGameTime, state.challengeMode);
        ImGui::TextUnformatted(text.c_str());
        auto &style = ImGui::GetStyle();
        ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetFrameHeight() - style.WindowPadding.x - style.FramePadding.x);
        if (ImGui::ArrowButton("##bosses_arrow", ImGuiDir_Up)) {
            showFull_ = false;
        }

        const auto &bosses = gBossDataSet.bosses();
        bool popup = false;
        if (ImGui::BeginChild("##bosses_list", ImGui::GetContentRegionAvail())) {
            const auto &regions = gBossDataSet.regions();
            int sz = static_cast<int>(regions.size());
            for (int i = 0; i < sz; i++) {
                const auto &region = regions[i];
                auto bossCount = static_cast<int>(region.bosses.size());
                if (autoExpandRegion >= 0) {
                    ImGui::SetNextItemOpen(i == autoExpandRegion);
                }
                if (ImGui::TreeNode(&region, "%d/%d %s", state.regionCounts[i], bossCount, region.name.c_str())) {
                    for (int j = 0; j < bossCount; j++) {
                        auto &bd = bosses[region.bosses[j]];
                        bool on = state.dead[bd.index] != 0;
                        if (ImGui::Checkbox(bd.boss.c_str(), &on, on) && state.dead[bd.index] && allowRevive_) {
                            popupBossIndex_ = static_cast<int>(bd.index);
                            reviveFailed_ = false;
                            popup = true;
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s: %s", bd.boss.c_str(), bd.place.c_str());
                        }
                    }
                    ImGui::TreePop();
                }
            }
        }
        ImGui::EndChild();

        if (popup) {
            ImGui::OpenPopup("##bosses_revive_confirm");
            ImGui::SetNextWindowPos(ImVec2(vp->Size.x * 0.94f, vp->Size.y / 2.0f), ImGuiCond_Appearing, ImVec2(.5f, .5f));
        }
        renderRevivePopup();
    }
}

void Renderer::renderRevivePopup() {
    if (!allowRevive_)
        return;
    if (ImGui::BeginPopupModal("##bosses_revive_confirm", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto &bosses = gBossDataSet.bosses();
        if (popupBossIndex_ < 0 || static_cast<size_t>(popupBossIndex_) >= bosses.size()) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        ImGui::Text(chinese_ ? "复活 %s？" : "Revive %s?", bosses[popupBossIndex_].boss.c_str());
        ImGui::TextUnformatted(chinese_ ? "复活后请传送或重新加载场地。" : "Travel or reload the area after reviving.");
        switch (bosses[popupBossIndex_].flagId) {
            case 31000800:
                ImGui::TextUnformatted(chinese_ ? "帕奇会返回蒙流洞窟，重置求饶与所在位置进度。" : "Patches returns to Murkwater; his surrender and quest location are reset.");
                break;
            case 1252380800:
            case 1051360800:
                ImGui::TextUnformatted(chinese_ ? "会切换红狮子城的祭典与双人战共用场地。" : "This switches Redmane Castle's shared festival/duo arena.");
                break;
            case 12030850:
                ImGui::TextUnformatted(chinese_ ? "重新加载后会进入弗尔桑克斯的梦境。" : "Reloading enters Fortissax's dream.");
                break;
            case 2049440800:
                ImGui::TextUnformatted(chinese_ ? "重新加载后会进入丹恩的对战场地。" : "Reloading enters Dane's duel.");
                break;
        }
        if (ImGui::Button(chinese_ ? "复活" : "Yes!")) {
            if (gBossDataSet.revive(popupBossIndex_)) {
                ImGui::CloseCurrentPopup();
                popupBossIndex_ = -1;
            } else {
                reviveFailed_ = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(chinese_ ? "取消" : "NO!")) {
            ImGui::CloseCurrentPopup();
            popupBossIndex_ = -1;
        }
        if (reviveFailed_) {
            ImGui::TextUnformatted(chinese_ ? "复活失败，未修改 flag。请查看控制台，重新加载场地后重试。"
                                            : "Revival failed; no flags changed. Check the console, reload the area, and retry.");
        }
        ImGui::EndPopup();
    }
}

bool Renderer::render() {
    auto toggleFullModeKey = gBossDataSet.toggleFullModeKey();
    if (toggleFullModeKey != 0 && api->inputIsKeyPressed(toggleFullModeKey)) {
        showFull_ = !showFull_;
    }

    // Take a thread-safe snapshot of all mutable state (single mutex acquisition)
    gBossDataSet.fillRenderState(renderState_);
    kills_ = renderState_.count;
    total_ = gBossDataSet.total();
    deaths_ = renderState_.deaths;
    if (renderState_.challengeMode) {
        pb_ = renderState_.challengeBest;
        tries_ = renderState_.challengeTries;
    }
    igt_ = std::chrono::milliseconds(renderState_.inGameTime);

    auto *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(calculatePos(vp->Size.x, posX_), calculatePos(vp->Size.y, posY_)), ImGuiCond_Always, ImVec2(posX_ >= 0 ? 0.f : 1.f, posY_ >= 0 ? 0.f : 1.f));
    ImGui::SetNextWindowFocus();

    if (showFull_) {
        renderFull(renderState_);
    } else {
        renderMini(renderState_);
    }
    ImGui::End();
    return showFull_;
}

} // namespace er::bosses
