#pragma once

#include <cstdint>

#include "nativeapi.h"

namespace er::util {

struct MapContext {
    uintptr_t menu = 0;
    uintptr_t owner = 0;
    uintptr_t view = 0;
    uintptr_t gameData = 0;
    uintptr_t player = 0;
};

[[nodiscard]] bool readMapContext(uintptr_t menuGlobal, uintptr_t gameDataGlobal, const ERGameLayout &layout, MapContext &context);
[[nodiscard]] bool mapContextUnchanged(const MapContext &context, const ERGameLayout &layout);

// Read the exact view layout for the supported executable. The caller owns
// generation/activeMasks and checks that the view identity remains current.
[[nodiscard]] bool readWorldMapView(uintptr_t view, ERMapState &state);

} // namespace er::util
