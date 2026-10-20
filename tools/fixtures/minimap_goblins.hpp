#pragma once

#include <cstdint>

namespace er::minimap::test {

enum class GoblinsScenario : uint32_t { Visible, Hidden, FailedBuild, ManyPoints, NativeDepths, Overflow, NewChild };

} // namespace er::minimap::test
