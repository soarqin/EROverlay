// Tests the actual patched provider with controlled visibility and real generated icon tags.
#include <atomic>
#include <limits>
#include <stdexcept>

#include "goblin_inject.hpp"
#include "goblin_map_api.hpp"
#include "mfg_map_api.h"

// Capture the provider's borrowed point view for pointer-identity checks.
extern "C" const MfgMapAPI *MfgGetMapAPI(uint32_t version) noexcept;
#undef MfgGetMapAPI

namespace {
std::atomic<uint32_t> scenario{0};
std::atomic<uint32_t> calls{0};
std::atomic<const MfgMapPoint *> lastPointView{nullptr};

int viewPoints(uint32_t layer, const MfgMapPoint **points, uint32_t *count) {
    const int result = MfgGetMapAPIProvider(MFG_MAP_ABI)->view_points(layer, points, count);
    lastPointView = points ? *points : nullptr;
    return result;
}
} // namespace
extern "C" __declspec(dllexport) const MfgMapAPI *MfgGetMapAPI(uint32_t version) noexcept {
    if (!MfgGetMapAPIProvider(version))
        return nullptr;
    static const MfgMapAPI current = [] {
        auto api = *MfgGetMapAPIProvider(MFG_MAP_ABI);
        api.view_points = viewPoints;
        return api;
    }();
    return &current;
}
extern "C" __declspec(dllexport) void setTestScenario(uint32_t value) {
    scenario = value;
    goblin::map_api::publish_rows();
}
extern "C" __declspec(dllexport) uint32_t testCalls() { return calls; }
extern "C" __declspec(dllexport) const MfgMapPoint *testLastPointView() { return lastPointView; }

void goblin::visit_native_markers(int layer, NativeMarkerVisitor visitor, void *context) {
    ++calls;
    if (scenario == 1)
        return;
    if (scenario == 2)
        throw std::runtime_error("provider fixture");
    const uint8_t area = layer == 2 ? 61 : 60;
    const NativeMarkerPoint result[]{{20, 6, area, static_cast<uint8_t>(layer), 28, 64, 141.f, 129.f, true, nullptr},
                                     {20 | NATIVE_CLEARED_KEY_BIT, 8, area, static_cast<uint8_t>(layer), 28, 64, 141.f, 129.f, true, nullptr},
                                     {NATIVE_HIGHLIGHT_KEY_BIT, 26, area, static_cast<uint8_t>(layer), 28, 64, 141.f, 129.f, true, nullptr},
                                     {21, 6, area, static_cast<uint8_t>(layer), 28, 64, 0, 0, false, nullptr},
                                     {22, 6, area, static_cast<uint8_t>(layer), 28, 64, std::numeric_limits<float>::quiet_NaN(), 0, true, nullptr},
                                     {23, 6, area, static_cast<uint8_t>(layer), 256, 64, 0, 0, true, nullptr}};
    if (scenario == 4) {
        map_api::record_depth(20, 200);
        map_api::record_depth(20 | NATIVE_CLEARED_KEY_BIT, 100);
        map_api::record_depth(NATIVE_HIGHLIGHT_KEY_BIT, 50000);
    }
    for (const auto &point: result)
        visitor(point, context);
    if (scenario == 3 || scenario == 5)
        for (uint64_t i = 0; i < (scenario == 5 ? MFG_MAP_MAX_POINTS : 600); ++i)
            visitor({100 + i, 6, area, static_cast<uint8_t>(layer), 28, 64, 141, 129, true, nullptr}, context);
}
