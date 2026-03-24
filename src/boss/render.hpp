#pragma once

#include "data.hpp"

#include <fmt/args.h>
#include <fmt/chrono.h>
#include <fmt/format.h>

#include <string>

namespace er::bosses {

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
    void init(void *context, void *allocFunc, void *freeFunc, void *userData);
    bool render();

private:
    [[nodiscard]] std::string formatStatusText(int igt, bool challengeMode) const;
    void renderMini(const RenderState &state);
    void renderFull(const RenderState &state);
    void renderRevivePopup();

    bool showFull_ = false;
    bool allowRevive_ = false;
    int lastRegionIndex_ = -1;
    int popupBossIndex_ = -1;
    float posX_ = -10.f;
    float posY_ = 10.f;
    float width_ = 0.12f;
    float height_ = 0.9f;

    // Format templates (with $n → \n and {igt} → chrono specifiers already applied)
    std::string killText_;
    std::string killTextHour_;
    std::string challengeText_;
    std::string challengeTextHour_;

    // Dynamic format args — stable references via IntProxy members
    fmt::dynamic_format_arg_store<fmt::format_context> args_;
    std::chrono::milliseconds igt_{};
    IntProxy kills_;
    IntProxy total_;
    IntProxy deaths_;
    IntProxy pb_;
    IntProxy tries_;

    // Cached render state (reused across frames to avoid per-frame vector allocation)
    RenderState renderState_;
};

}
