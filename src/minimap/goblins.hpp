#pragma once

#include <span>

#include "compat/mfg_map_api.h"

namespace er::minimap {

// Update-thread client. Borrowed point views are consumed before publishing Data;
// render only sees Data's owned snapshots and textures.
class Goblins {
public:
    Goblins() = default;
    Goblins(const Goblins &) = delete;
    Goblins &operator=(const Goblins &) = delete;
    Goblins(Goblins &&) = delete;
    Goblins &operator=(Goblins &&) = delete;
    // Published snapshot; empty/stale until the provider's background refresh.
    // Valid until this thread rereads the same layer, exits, or stop() releases the module.
    [[nodiscard]] std::span<const MfgMapPoint> viewPoints(uint32_t layer);
    void stop();

private:
    [[nodiscard]] bool connect();
    void *module_ = nullptr;
    const MfgMapAPI *api_ = nullptr;
    uint64_t retry_ = 0;
};

extern Goblins gGoblins;

} // namespace er::minimap
