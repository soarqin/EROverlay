// Tests the actual patched provider with controlled visibility and real generated icon tags.
#include <atomic>
#include <limits>
#include <stdexcept>

#include "goblin_inject.hpp"
#include "goblin_map_api.hpp"

namespace {
std::atomic<uint32_t> scenario{0};
std::atomic<uint32_t> calls{0};
} // namespace
extern "C" __declspec(dllexport) void setTestScenario(uint32_t value) {
    scenario = value;
    goblin::map_api::publish_rows();
}
extern "C" __declspec(dllexport) uint32_t testCalls() { return calls; }

std::vector<goblin::NativeMarkerPoint> goblin::native_marker_snapshot(int layer, bool) {
    ++calls;
    if (scenario == 1)
        return {};
    if (scenario == 2)
        throw std::runtime_error("provider fixture");
    const uint8_t area = layer == 2 ? 61 : 60;
    std::vector<NativeMarkerPoint> result{{20, 6, area, static_cast<uint8_t>(layer), 28, 64, 141.f, 129.f, true, nullptr},
                                          {20 | NATIVE_CLEARED_KEY_BIT, 8, area, static_cast<uint8_t>(layer), 28, 64, 141.f, 129.f, true, nullptr},
                                          {NATIVE_HIGHLIGHT_KEY_BIT, 26, area, static_cast<uint8_t>(layer), 28, 64, 141.f, 129.f, true, nullptr},
                                          {21, 6, area, static_cast<uint8_t>(layer), 28, 64, 0, 0, false, nullptr},
                                          {22, 6, area, static_cast<uint8_t>(layer), 28, 64, std::numeric_limits<float>::quiet_NaN(), 0, true, nullptr},
                                          {23, 6, area, static_cast<uint8_t>(layer), 256, 64, 0, 0, true, nullptr}};
    if (scenario == 3)
        for (uint64_t i = 0; i < 600; ++i)
            result.push_back({100 + i, 6, area, static_cast<uint8_t>(layer), 28, 64, 141, 129, true, nullptr});
    if (scenario == 4) {
        map_api::record_depth(20, 200);
        map_api::record_depth(20 | NATIVE_CLEARED_KEY_BIT, 100);
        map_api::record_depth(NATIVE_HIGHLIGHT_KEY_BIT, 50000);
    }
    return result;
}
