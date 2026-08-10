#pragma once

#include "data.hpp"

#include "api.h"

#include <fmt/args.h>
#include <fmt/chrono.h>
#include <fmt/format.h>

#include <string>
#include <unordered_map>

namespace er::bosses {

// How the revealed boss list is drawn. Set by boss.random_layout.
enum class RandomLayout {
    Text,
    List,
    Grid,
};

// A lazily loaded boss portrait. `attempted` stops a missing file from being reopened every
// frame, since loadTexture() reports failure by returning a zeroed context.
struct BossImage {
    TextureContext texture = {};
    bool attempted = false;
};

// Wrapper to allow fmt::dynamic_format_arg_store to hold stable references to int values.
// The store captures a reference to IntProxy; updating IntProxy::value is reflected on next format call.
struct IntProxy {
    int value = 0;
    explicit operator int() const { return value; }
    IntProxy &operator=(int val) {
        value = val;
        return *this;
    }
};

class Renderer {
public:
    ~Renderer();
    void init(void *context, void *allocFunc, void *freeFunc, void *userData);
    bool render();

private:
    [[nodiscard]] std::string formatStatusText(bool challengeMode) const;
    void renderMini(const RenderState &state);
    void renderFull(const RenderState &state);
    void renderRevivePopup();
    void renderRegionTree(const RenderState &state, bool &popup);

    // Random run UI
    void renderRandomPanel(const RenderState &state);
    void renderRandomBosses(const RenderState &state, bool &popup);
    void renderRandomText(const RenderState &state, bool &popup);
    void renderRandomList(const RenderState &state, bool &popup);
    void renderRandomGrid(const RenderState &state);
    void applyRandomize();
    [[nodiscard]] TextureContext *bossImage(uint32_t flagId);
    void unloadImages();

    bool showFull_ = false;
    bool allowRevive_ = false;
    int lastRegionIndex_ = -1;
    int popupBossIndex_ = -1;
    float posX_ = -10.f;
    float posY_ = 10.f;
    float width_ = 0.12f;
    float height_ = 0.9f;

    // Random run controls
    RandomLayout randomLayout_ = RandomLayout::List;
    int randomColumns_ = 3;
    float randomImageSize_ = 48.f;
    char seedInput_[kSeedMaxDigits + 1] = {};
    int countInput_ = 10;
    std::unordered_map<uint32_t, BossImage> images_;

    // Format templates (with $n → \n and {igt} → chrono specifiers already applied)
    std::string killText_;
    std::string challengeText_;

    // Dynamic format args — stable references via IntProxy members
    fmt::dynamic_format_arg_store<fmt::format_context> args_;
    std::chrono::milliseconds igt_{};
    IntProxy kills_;
    IntProxy total_;
    IntProxy deaths_;
    IntProxy pb_;
    IntProxy tries_;

    // In-game time actually displayed: live IGT, or the frozen finish time once a run completes.
    int effectiveIgt_ = 0;

    // Cached render state (reused across frames to avoid per-frame vector allocation)
    RenderState renderState_;
};

}
