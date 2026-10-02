#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

#include "nativeapi.h"

namespace er::util {

struct GameProfile {
    const char *label;
    std::array<uint8_t, 32> sha256;
    uint32_t submit, flush, enqueue, cancel;
    uint32_t manager, allocator, retirePoolOwner;
    uint32_t menu, gameData, eventFlags, field, repository, reveal;
    ERGameLayout layout;
};

inline constexpr GameProfile GAME_PROFILES[] = {
#include "gameprofiles.inc"
};

[[nodiscard]] inline const GameProfile *findGameProfile(std::span<const uint8_t, 32> digest) {
    for (const auto &profile: GAME_PROFILES)
        if (std::equal(profile.sha256.begin(), profile.sha256.end(), digest.begin()))
            return &profile;
    return nullptr;
}

} // namespace er::util
