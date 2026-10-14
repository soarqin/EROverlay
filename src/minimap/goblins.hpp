#pragma once

#include <span>
#include <vector>

#include "compat/mfg_map_api.h"

namespace er::minimap {

// Update-thread client. Render only sees copied snapshots and owned textures.
class Goblins {
public:
    Goblins() = default;
    Goblins(const Goblins &) = delete;
    Goblins &operator=(const Goblins &) = delete;
    Goblins(Goblins &&) = delete;
    Goblins &operator=(Goblins &&) = delete;
    [[nodiscard]] std::span<const MfgMapPoint> points(uint32_t layer);
    void stop();

private:
    [[nodiscard]] bool connect();
    void *module_ = nullptr;
    const MfgMapAPI *api_ = nullptr;
    uint64_t retry_ = 0;
    std::vector<MfgMapPoint> buffer_;
};

extern Goblins gGoblins;

} // namespace er::minimap
