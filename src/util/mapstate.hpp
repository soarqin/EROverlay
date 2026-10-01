#pragma once

#include <cstdint>

#include "nativeapi.h"

namespace er::util {

// Read the exact view layout for the supported executable. The caller owns
// generation/activeMasks and checks that the view identity remains current.
[[nodiscard]] bool readWorldMapView(uintptr_t view, ERMapState &state);

} // namespace er::util
