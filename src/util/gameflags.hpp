#pragma once

#include <cstdint>

namespace er::util {

// These readers inspect published CPU data only. Failure is distinct from a
// readable flag whose value is false, so unavailable progress cannot select
// the unexplored map variant.
[[nodiscard]] bool readGameEventFlag(uintptr_t manager, uint32_t id, bool &value);
[[nodiscard]] bool readMapPieceMasks(uintptr_t table, uintptr_t manager, bool reveal, uint32_t (&masks)[3]);

} // namespace er::util
