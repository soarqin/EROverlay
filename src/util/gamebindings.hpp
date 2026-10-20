#pragma once

#include <cstdint>

#include "nativeapi.h"

namespace er::util {

// Resolved once from the loaded image, then published unchanged to consumers.
// Function/global values are RVAs relative to that image, including under ASLR.
struct GameBindings {
    uint32_t submit = 0, flush = 0, enqueue = 0, cancel = 0;
    uint32_t manager = 0, allocator = 0, retirePoolOwner = 0;
    uint32_t menu = 0, gameData = 0, eventFlags = 0, field = 0, repository = 0, reveal = 0;
    ERGameLayout layout{};
};

} // namespace er::util
