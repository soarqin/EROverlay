#pragma once

#include "../renderbase.hpp"

#include <fmt/args.h>
#include <fmt/chrono.h>
#include <fmt/format.h>

#include <string>
#include <cstdint>

namespace er::bosses {

struct IntProxy {
    int value = 0;
    explicit inline operator int() const { return value; }
    inline IntProxy &operator=(int val) {
        value = val;
        return *this;
    }
};

class Render : public RenderBase {
public:
    void init() override;
    void render(bool &showFull) override;

private:
    bool allowRevive_ = false;
    int lastRegionIndex_ = -1;
    int popupBossIndex_ = -1;
    float posX_ = -10.f;
    float posY_ = 10.f;
    float width_ = 0.12f;
    float height_ = 0.9f;

    /* dynamic format args */
    std::string killText_, killTextHour_;
    std::string challengeText_, challengeTextHour_;
    fmt::dynamic_format_arg_store<fmt::format_context> args_;

    std::chrono::milliseconds igt_;
    IntProxy kills_;
    IntProxy total_;
    IntProxy deaths_;
    IntProxy pb_;
    IntProxy tries_;
};

}