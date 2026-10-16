#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "plugin.hpp"

namespace er {

// One ImGui frame feeds both native render stages. Input, plugin callbacks,
// atlas updates and animations are evaluated once, including legacy plugins.
class DrawLayers {
public:
    void begin() {
        for (auto &entry: lists_) {
            entry.above.resize(0);
            entry.below.resize(0);
        }
        count_ = 0;
        started_.clear();
        pending_ = false;
    }

    static void observe(RenderLayer layer, bool before, void *context) {
        if (layer != RenderLayer::BelowUI)
            return;
        auto &self = *static_cast<DrawLayers *>(context);
        eachList([&](ImDrawList *list) {
            if (before) {
                list->AddCallback(&layerMarker, reinterpret_cast<void *>(uintptr_t{1}));
                self.started_.push_back(list);
            } else {
                if (std::find(self.started_.begin(), self.started_.end(), list) == self.started_.end()) {
                    // The plugin created this window/list during its callback.
                    // Command offsets refer to index buffers, so inserting a
                    // marker does not change any geometry or callback data.
                    ImDrawCmd marker{};
                    marker.UserCallback = &layerMarker;
                    marker.UserCallbackData = reinterpret_cast<void *>(uintptr_t{1});
                    list->CmdBuffer.insert(list->CmdBuffer.begin(), marker);
                }
                list->AddCallback(&layerMarker, reinterpret_cast<void *>(uintptr_t{2}));
            }
        });
        if (!before)
            self.started_.clear();
    }

    void finish(ImDrawData *source) {
        source_ = source;
        for (auto *list: source->CmdLists) {
            if (count_ == lists_.size())
                lists_.emplace_back();
            auto &entry = lists_[count_++];
            entry.list = list;
            auto layer = RenderLayer::AboveUI;
            for (const auto &command: list->CmdBuffer) {
                if (command.UserCallback == &layerMarker &&
                    (command.UserCallbackData == reinterpret_cast<void *>(uintptr_t{1}) || command.UserCallbackData == reinterpret_cast<void *>(uintptr_t{2})))
                    layer = command.UserCallbackData == reinterpret_cast<void *>(uintptr_t{1}) ? RenderLayer::BelowUI : RenderLayer::AboveUI;
                else
                    (layer == RenderLayer::BelowUI ? entry.below : entry.above).push_back(command);
            }
        }
        pending_ = true;
    }

    // Temporarily substitute only the command buffers; vertex/index buffers,
    // texture requests and draw order remain those of the single ImGui frame.
    [[nodiscard]] ImDrawData *select(RenderLayer layer) {
        selected_.Clear();
        selected_.Valid = source_->Valid;
        selected_.DisplayPos = source_->DisplayPos;
        selected_.DisplaySize = source_->DisplaySize;
        selected_.FramebufferScale = source_->FramebufferScale;
        selected_.OwnerViewport = source_->OwnerViewport;
        selected_.Textures = source_->Textures;
        selectedLayer_ = layer;
        for (size_t i = 0; i < count_; ++i) {
            auto &entry = lists_[i];
            auto &commands = layer == RenderLayer::BelowUI ? entry.below : entry.above;
            entry.list->CmdBuffer.swap(commands);
            // User callbacks can submit their own GPU work without any ImGui
            // geometry. Keep them in their layer even in callback-only lists.
            bool hasCommands = std::any_of(entry.list->CmdBuffer.begin(), entry.list->CmdBuffer.end(), [](const ImDrawCmd &cmd) { return cmd.ElemCount != 0 || cmd.UserCallback; });
            if (hasCommands)
                selected_.AddDrawList(entry.list);
        }
        return &selected_;
    }

    void restore() {
        for (size_t i = 0; i < count_; ++i) {
            auto &entry = lists_[i];
            entry.list->CmdBuffer.swap(selectedLayer_ == RenderLayer::BelowUI ? entry.below : entry.above);
        }
    }

    [[nodiscard]] bool pending() const { return pending_; }
    void discard() { pending_ = false; }

private:
    // One callback plus distinct tags: Release /OPT:ICF can merge two empty
    // marker functions, so their function addresses cannot encode the layer.
    static void layerMarker(const ImDrawList *, const ImDrawCmd *) {}
    template<typename F>
    static void eachList(F &&visit) {
        auto &g = *ImGui::GetCurrentContext();
        for (auto *window: g.Windows)
            if (window->LastFrameActive == g.FrameCount)
                visit(window->DrawList);
        for (auto *viewport: g.Viewports)
            for (int i = 0; i < 2; ++i)
                if (viewport->BgFgDrawLists[i] && viewport->BgFgDrawListsLastFrame[i] == g.FrameCount)
                    visit(viewport->BgFgDrawLists[i]);
    }
    struct Commands {
        ImDrawList *list = nullptr;
        ImVector<ImDrawCmd> above, below;
    };
    std::vector<ImDrawList *> started_;
    std::vector<Commands> lists_;
    size_t count_ = 0;
    ImDrawData *source_ = nullptr;
    ImDrawData selected_;
    RenderLayer selectedLayer_ = RenderLayer::AboveUI;
    bool pending_ = false;
};

} // namespace er
