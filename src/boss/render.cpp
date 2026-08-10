#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "render.hpp"

#include "data.hpp"

#include "api.h"
#include "util/string.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

extern EROverlayAPI *api;

namespace fmt {
template<>
struct formatter<er::bosses::IntProxy> : formatter<int> {
    auto format(const er::bosses::IntProxy &value, format_context &ctx) const {
        return formatter<int>::format(value.value, ctx);
    }
};
} // namespace fmt

namespace er::bosses {

namespace {

constexpr ImVec4 kTintNormal = ImVec4(1.f, 1.f, 1.f, 1.f);
constexpr ImVec4 kTintKilled = ImVec4(1.f, 1.f, 1.f, 0.4f);

// Restrict the seed box to digits so both players always type the same thing.
int seedCharFilter(ImGuiInputTextCallbackData *data) {
    if (data->EventChar < '0' || data->EventChar > '9') return 1;
    return 0;
}

// Never returns 0: std::clamp() is undefined when the low bound exceeds the high one, which
// would happen here if the boss data failed to load.
int maxRandomCount() {
    auto poolSize = gBossDataSet.randomPoolSize();
    return poolSize > 0 ? poolSize : 1;
}

// Finds how much of the text fits on one line without splitting a word.
// Returns nullptr when even the first word is wider than the available width.
const char *fitWholeWords(const char *lineStart, float wrapWidth) {
    const char *lineEnd = nullptr;
    const char *probe = lineStart;
    while (true) {
        const char *wordEnd = probe;
        while (*wordEnd != '\0' && *wordEnd != ' ') wordEnd++;
        if (ImGui::CalcTextSize(lineStart, wordEnd).x > wrapWidth) break;
        lineEnd = wordEnd;
        if (*wordEnd == '\0') break;
        probe = wordEnd + 1;
    }
    return lineEnd;
}

// Wraps on spaces rather than mid-word. ImGui's own wrapping splits a word in half as soon as
// the word alone exceeds the wrap width, which is the common case in a narrow grid cell.
// A single word too long to fit still falls back to ImGui wrapping, so nothing gets clipped.
void textWordWrapped(const char *text, float wrapWidth, bool disabled) {
    if (disabled) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    }
    const char *lineStart = text;
    while (*lineStart != '\0') {
        const char *lineEnd = fitWholeWords(lineStart, wrapWidth);
        if (lineEnd == nullptr) {
            const char *wordEnd = lineStart;
            while (*wordEnd != '\0' && *wordEnd != ' ') wordEnd++;
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapWidth);
            ImGui::TextUnformatted(lineStart, wordEnd);
            ImGui::PopTextWrapPos();
            lineStart = wordEnd;
        } else {
            ImGui::TextUnformatted(lineStart, lineEnd);
            lineStart = lineEnd;
        }
        while (*lineStart == ' ') lineStart++;
    }
    if (disabled) {
        ImGui::PopStyleColor();
    }
}

// Draws a check mark over a killed boss portrait, sized to the image rect.
void drawKilledMark(const ImVec2 &min, const ImVec2 &max) {
    auto *drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(min, max, IM_COL32(0, 0, 0, 110));
    auto width = max.x - min.x;
    auto height = max.y - min.y;
    auto size = (width < height ? width : height) * 0.5f;
    ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    ImVec2 a(center.x - size * 0.5f, center.y);
    ImVec2 b(center.x - size * 0.15f, center.y + size * 0.35f);
    ImVec2 c(center.x + size * 0.5f, center.y - size * 0.4f);
    auto color = ImGui::GetColorU32(ImGuiCol_CheckMark);
    auto thickness = size * 0.18f;
    drawList->AddLine(a, b, color, thickness);
    drawList->AddLine(b, c, color, thickness);
}

}

Renderer::~Renderer() {
    unloadImages();
}

void Renderer::unloadImages() {
    for (auto &pair : images_) {
        if (pair.second.texture.texture != nullptr) {
            api->destroyTexture(&pair.second.texture);
        }
    }
    images_.clear();
}

TextureContext *Renderer::bossImage(uint32_t flagId) {
    auto &image = images_[flagId];
    if (!image.attempted) {
        image.attempted = true;
        wchar_t path[MAX_PATH];
        // _TRUNCATE rather than swprintf_s: an over-long module path should drop the image,
        // not trip the invalid parameter handler and take the game down.
        _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%ls\\data\\boss_images\\%u.png", api->getModulePath(), flagId);
        image.texture = api->loadTexture(path);
        if (image.texture.texture == nullptr) {
            _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%ls\\data\\boss_images\\%u.jpg", api->getModulePath(), flagId);
            image.texture = api->loadTexture(path);
        }
    }
    return image.texture.texture != nullptr ? &image.texture : nullptr;
}

void Renderer::init(void *context, void *allocFunc, void *freeFunc, void *userData) {
    ImGui::SetCurrentContext((ImGuiContext *)context);
    ImGui::SetAllocatorFunctions((ImGuiMemAllocFunc)allocFunc, (ImGuiMemFreeFunc)freeFunc, userData);
    killText_ = api->configGet("boss.boss_kill_text");
    challengeText_ = api->configGet("boss.challenge_status_text");
    const std::string newlinePlaceholder = "$n";
    const std::string newline = "\n";
    const std::string igtPlaceholder = "{igt}";
    // Always hh:mm:ss. The old behaviour switched to mm:ss under an hour, which made the
    // header jump in width mid-run and gave two runs different-looking finish times.
    const std::string igtFormat = "{igt:.0%H:%M:%S}";
    util::replaceAll(killText_, newlinePlaceholder, newline);
    util::replaceAll(challengeText_, newlinePlaceholder, newline);
    util::replaceAll(killText_, igtPlaceholder, igtFormat);
    util::replaceAll(challengeText_, igtPlaceholder, igtFormat);
    allowRevive_ = api->configEnabled("boss.allow_revive");

    std::string layout = api->configGet("boss.random_layout");
    util::toLower(layout);
    if (layout == "text") {
        randomLayout_ = RandomLayout::Text;
    } else if (layout == "grid") {
        randomLayout_ = RandomLayout::Grid;
    } else {
        randomLayout_ = RandomLayout::List;
    }
    randomColumns_ = std::clamp(api->configGetInt("boss.random_grid_columns", 3), 1, 8);
    randomImageSize_ = std::max(api->configGetFloat("boss.random_image_size", 48.f), 8.f);
    countInput_ = std::clamp(api->configGetInt("boss.random_count", 10), 1, maxRandomCount());

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
    if (n >= 1.f) return n;
    if (n >= 0.f) return w * n;
    if (n <= -1.f) return w + n;
    return w + w * n;
}

std::string Renderer::formatStatusText(bool challengeMode) const {
    return fmt::vformat(challengeMode ? challengeText_ : killText_, args_);
}

void Renderer::renderMini(const RenderState &state) {
    if (ImGui::Begin("##bosses_window", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        auto text = formatStatusText(state.challengeMode);
        ImGui::TextUnformatted(text.c_str());
        ImGui::SameLine();
        if (ImGui::ArrowButton("##bosses_arrow", ImGuiDir_Down)) {
            showFull_ = true;
        }
    }
}

void Renderer::renderFull(const RenderState &state) {
    auto *vp = ImGui::GetMainViewport();
    // Challenge mode is the original tracker and always lists every boss. Mercenary Melee only
    // has a list once a run exists, so when idle the panel shows just the controls and a zero
    // height lets it shrink to fit them rather than reserving the configured height for nothing.
    const bool showList = state.challengeMode || state.runState != RunState::Idle;
    const float panelWidth = calculatePos(vp->Size.x, std::abs(width_));
    const float maxHeight = calculatePos(vp->Size.y, std::abs(height_));
    // Height 0 means fit the content, and the constraint caps that at the configured height,
    // so panel_pos acts as a ceiling rather than a fixed size: a short boss list gets a short
    // panel, and only a list taller than the ceiling starts scrolling.
    ImGui::SetNextWindowSize(ImVec2(panelWidth, 0.f), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.f, 0.f), ImVec2(FLT_MAX, maxHeight));
    if (ImGui::Begin("##bosses_window", nullptr,
                     (ImGuiWindowFlags_NoDecoration & ~ImGuiWindowFlags_NoScrollbar) | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings)) {
        auto text = formatStatusText(state.challengeMode);
        ImGui::TextUnformatted(text.c_str());
        auto &style = ImGui::GetStyle();
        ImGui::SameLine(
            ImGui::GetWindowWidth() - ImGui::GetFrameHeight() - style.WindowPadding.x - style.FramePadding.x);
        if (ImGui::ArrowButton("##bosses_arrow", ImGuiDir_Up)) {
            showFull_ = false;
        }

        // Challenge mode owns the whole list, so the random run controls stay out of its way.
        if (!state.challengeMode) {
            renderRandomPanel(state);
        }

        if (showList) {
            ImGui::Separator();
            bool popup = false;
            // AutoResizeY rather than GetContentRegionAvail(): the window height now follows its
            // content, so asking for the remaining space would be circular. Capping the child at
            // whatever is left of the configured height keeps overflow scrolling inside the list,
            // so the status line and controls above it stay pinned instead of scrolling away.
            const float childMax = std::max(maxHeight - ImGui::GetCursorPosY() - style.WindowPadding.y, 1.f);
            ImGui::SetNextWindowSizeConstraints(ImVec2(0.f, 0.f), ImVec2(FLT_MAX, childMax));
            if (ImGui::BeginChild("##bosses_list", ImVec2(0.f, 0.f), ImGuiChildFlags_AutoResizeY)) {
                if (state.challengeMode) {
                    renderRegionTree(state, popup);
                } else if (state.runState == RunState::Armed) {
                    // Revealing is per client, not synchronised between players, so this has to
                    // read as an instruction rather than a promise the mod cannot keep.
                    ImGui::TextWrapped("Keep this hidden until everyone is ready, then reveal together.");
                } else {
                    renderRandomBosses(state, popup);
                }
            }
            ImGui::EndChild();

            if (popup) {
                ImGui::OpenPopup("##bosses_revive_confirm");
                ImGui::SetNextWindowPos(ImVec2(vp->Size.x * 0.94f, vp->Size.y / 2.0f),
                                        ImGuiCond_Appearing, ImVec2(.5f, .5f));
            }
            renderRevivePopup();
        }
    }
}

void Renderer::renderRegionTree(const RenderState &state, bool &popup) {
    // Auto-expand the tree node for the current region (only on region change)
    int autoExpandRegion = -1;
    if (state.regionIndex != lastRegionIndex_) {
        autoExpandRegion = state.regionIndex;
        lastRegionIndex_ = state.regionIndex;
    }

    const auto &bosses = gBossDataSet.bosses();
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

void Renderer::renderRandomPanel(const RenderState &state) {
    ImGui::SeparatorText("Mercenary Melee");
    switch (state.runState) {
        case RunState::Idle: {
            ImGui::TextUnformatted("Seed");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##random_seed", seedInput_, sizeof(seedInput_),
                             ImGuiInputTextFlags_CallbackCharFilter, seedCharFilter);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Digits only. Leave empty and Randomize will roll one first.");
            }
            const int maxCount = maxRandomCount();
            ImGui::TextUnformatted("Bosses");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputInt("##random_count", &countInput_);
            ImGui::SetNextItemWidth(-FLT_MIN);
            // AlwaysClamp so ctrl+click typing into the slider cannot escape the range either.
            ImGui::SliderInt("##random_count_slider", &countInput_, 1, maxCount, "%d",
                             ImGuiSliderFlags_AlwaysClamp);
            // Clamped every frame rather than only on edit, so no path (typing, the +/- step
            // buttons, a stale config value) can leave the count outside 1..pool size.
            countInput_ = std::clamp(countInput_, 1, maxCount);
            // Rolling the seed is separate from drawing the bosses, so a new number can be
            // agreed on before anyone commits to a selection.
            if (ImGui::Button("Reroll seed", ImVec2(-FLT_MIN, 0.f))) {
                setSeedInput(BossDataSet::generateSeed());
            }
            if (ImGui::Button("Randomize", ImVec2(-FLT_MIN, 0.f))) {
                applyRandomize();
            }
            break;
        }
        case RunState::Armed:
            ImGui::TextWrapped("Randomized - Seed: %llu - Bosses: %d", state.runSeed, state.runCount);
            if (ImGui::Button("Reveal bosses", ImVec2(-FLT_MIN, 0.f))) {
                gBossDataSet.revealRandomRun();
            }
            break;
        case RunState::Running:
            ImGui::TextWrapped("Seed: %llu - Bosses: %d/%d", state.runSeed, state.runKilled, state.runCount);
            break;
        default:
            ImGui::TextWrapped("Finished - Seed: %llu - Bosses: %d", state.runSeed, state.runCount);
            break;
    }
    if (state.runState != RunState::Idle) {
        // Ends immediately rather than via a confirmation popup: the popup rendered detached
        // from this panel and was easy to miss. Nothing is lost by ending, since the seed box
        // still holds the seed and re-entering it redraws the same bosses.
        if (ImGui::Button("End run", ImVec2(-FLT_MIN, 0.f))) {
            gBossDataSet.endRandomRun();
        }
    }
}

void Renderer::setSeedInput(uint64_t seed) {
    std::snprintf(seedInput_, sizeof(seedInput_), "%llu", seed);
}

void Renderer::applyRandomize() {
    uint64_t seed = 0;
    if (seedInput_[0] != '\0') {
        seed = std::strtoull(seedInput_, nullptr, 10);
    }
    // An empty box means roll a seed first, then draw with it.
    if (seed == 0) {
        seed = BossDataSet::generateSeed();
    }
    countInput_ = std::clamp(countInput_, 1, maxRandomCount());
    gBossDataSet.startRandomRun(seed, countInput_);
    // Echo the seed back into the box so a rolled one can be read off and shared.
    setSeedInput(seed);
}

void Renderer::renderRandomBosses(const RenderState &state, bool &popup) {
    switch (randomLayout_) {
        case RandomLayout::Text:
            renderRandomText(state, popup);
            break;
        case RandomLayout::Grid:
            renderRandomGrid(state);
            break;
        default:
            renderRandomList(state, popup);
            break;
    }
}

void Renderer::renderRandomText(const RenderState &state, bool &popup) {
    const auto &bosses = gBossDataSet.bosses();
    for (auto index : state.runSelected) {
        const auto &bd = bosses[index];
        bool on = state.dead[index] != 0;
        // Several bosses share a name across regions, so the label alone is not a unique id.
        ImGui::PushID(static_cast<int>(index));
        if (ImGui::Checkbox(bd.boss.c_str(), &on, on) && state.dead[index] && allowRevive_) {
            popupBossIndex_ = static_cast<int>(index);
            popup = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s: %s", bd.boss.c_str(), bd.place.c_str());
        }
        ImGui::PopID();
    }
}

void Renderer::renderRandomList(const RenderState &state, bool &popup) {
    const auto &bosses = gBossDataSet.bosses();
    for (auto index : state.runSelected) {
        const auto &bd = bosses[index];
        bool on = state.dead[index] != 0;
        ImGui::PushID(static_cast<int>(index));
        ImGui::BeginGroup();
        if (ImGui::Checkbox("##kill", &on, on) && state.dead[index] && allowRevive_) {
            popupBossIndex_ = static_cast<int>(index);
            popup = true;
        }
        ImGui::SameLine();
        auto *texture = bossImage(bd.flagId);
        if (texture != nullptr) {
            auto width = randomImageSize_;
            if (texture->height > 0) {
                width = randomImageSize_ * static_cast<float>(texture->width) / static_cast<float>(texture->height);
            }
            auto min = ImGui::GetCursorScreenPos();
            ImGui::ImageWithBg((ImTextureID)texture->gpuHandle, ImVec2(width, randomImageSize_), ImVec2(0.f, 0.f),
                               ImVec2(1.f, 1.f), ImVec4(0.f, 0.f, 0.f, 0.f), on ? kTintKilled : kTintNormal);
            if (on) {
                drawKilledMark(min, ImVec2(min.x + width, min.y + randomImageSize_));
            }
            ImGui::SameLine();
        }
        ImGui::BeginGroup();
        if (on) {
            ImGui::TextDisabled("%s", bd.boss.c_str());
        } else {
            ImGui::TextUnformatted(bd.boss.c_str());
        }
        ImGui::TextDisabled("%s", bd.place.c_str());
        ImGui::EndGroup();
        ImGui::EndGroup();
        ImGui::PopID();
    }
}

void Renderer::renderRandomGrid(const RenderState &state) {
    const auto &bosses = gBossDataSet.bosses();
    if (!ImGui::BeginTable("##random_grid", randomColumns_, ImGuiTableFlags_SizingStretchSame)) {
        return;
    }
    for (auto index : state.runSelected) {
        ImGui::TableNextColumn();
        const auto &bd = bosses[index];
        bool killed = state.dead[index] != 0;
        auto cellWidth = ImGui::GetContentRegionAvail().x;
        ImGui::PushID(static_cast<int>(index));
        auto *texture = bossImage(bd.flagId);
        if (texture != nullptr) {
            auto height = cellWidth;
            if (texture->width > 0) {
                height = cellWidth * static_cast<float>(texture->height) / static_cast<float>(texture->width);
            }
            auto min = ImGui::GetCursorScreenPos();
            ImGui::ImageWithBg((ImTextureID)texture->gpuHandle, ImVec2(cellWidth, height), ImVec2(0.f, 0.f),
                               ImVec2(1.f, 1.f), ImVec4(0.f, 0.f, 0.f, 0.f), killed ? kTintKilled : kTintNormal);
            if (killed) {
                drawKilledMark(min, ImVec2(min.x + cellWidth, min.y + height));
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s: %s", bd.boss.c_str(), bd.place.c_str());
            }
        }
        // Grouped so the hover test covers every wrapped line, not just the last one.
        ImGui::BeginGroup();
        textWordWrapped(bd.boss.c_str(), cellWidth, killed);
        ImGui::EndGroup();
        if (texture == nullptr && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s: %s", bd.boss.c_str(), bd.place.c_str());
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void Renderer::renderRevivePopup() {
    if (!allowRevive_) return;
    if (ImGui::BeginPopupModal("##bosses_revive_confirm", nullptr,
                               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto &bosses = gBossDataSet.bosses();
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

bool Renderer::render() {
    auto toggleFullModeKey = gBossDataSet.toggleFullModeKey();
    if (toggleFullModeKey != 0 && api->inputIsKeyPressed(toggleFullModeKey)) {
        showFull_ = !showFull_;
    }

    // Take a thread-safe snapshot of all mutable state (single mutex acquisition)
    gBossDataSet.fillRenderState(renderState_);
    if (renderState_.runState != RunState::Idle) {
        // During a run the status line tracks the run, not the full boss roster.
        kills_ = renderState_.runKilled;
        total_ = renderState_.runCount;
    } else {
        kills_ = renderState_.count;
        total_ = gBossDataSet.total();
    }
    deaths_ = renderState_.deaths;
    if (renderState_.challengeMode) {
        pb_ = renderState_.challengeBest;
        tries_ = renderState_.challengeTries;
    }
    // A finished run freezes the clock at the final kill so players can compare times.
    effectiveIgt_ = renderState_.runState == RunState::Finished ? renderState_.runFinalIgt : renderState_.inGameTime;
    igt_ = std::chrono::milliseconds(effectiveIgt_);

    auto *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(calculatePos(vp->Size.x, posX_), calculatePos(vp->Size.y, posY_)),
                            ImGuiCond_Always,
                            ImVec2(posX_ >= 0 ? 0.f : 1.f, posY_ >= 0 ? 0.f : 1.f));
    ImGui::SetNextWindowFocus();

    if (showFull_) {
        renderFull(renderState_);
    } else {
        renderMini(renderState_);
    }
    ImGui::End();
    return showFull_;
}

}
