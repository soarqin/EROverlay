// Runs the official provider with controlled game state and real generated icon tags.
#include <atomic>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <thread>

#include "goblin_collected.hpp"
#include "goblin_config.hpp"
#include "goblin_inject.hpp"
#include "goblin_map_api.hpp"
#include "mfg_map_api.h"
#include "minimap_goblins.hpp"

extern "C" const MfgMapAPI *MfgGetMapAPI(uint32_t version) noexcept;

namespace {
using er::minimap::test::GoblinsScenario;
std::atomic<GoblinsScenario> scenarios[3]{};
std::atomic<uint32_t> epoch{0};
std::atomic<uint32_t> layerCalls[3]{};
} // namespace

extern "C" __declspec(dllexport) void setTestScenario(uint32_t layer, GoblinsScenario value) {
    if (layer > 2)
        return;
    scenarios[layer] = value;
    ++epoch;
    goblin::map_api::publish_rows();
}
extern "C" __declspec(dllexport) uint32_t testCalls() { return layerCalls[0].load() + layerCalls[1].load() + layerCalls[2].load(); }

// The test driver calls this sequentially; wait for a background refresh attempt, including failure.
// Request the layer on that thread so this helper never invalidates the reader's view.
extern "C" __declspec(dllexport) bool testPollLayer(uint32_t layer) {
    if (layer > 2)
        return false;
    bool refreshed = false;
    std::thread worker([&] {
        const auto *api = MfgGetMapAPI(MFG_MAP_ABI);
        const MfgMapPoint *points = nullptr;
        uint32_t count = 0;
        const auto before = layerCalls[layer].load();
        api->view_points(layer, &points, &count);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        do {
            goblin::map_api::maintain();
            refreshed = layerCalls[layer] != before;
            if (!refreshed)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (!refreshed && std::chrono::steady_clock::now() < deadline);
    });
    worker.join();
    return refreshed;
}

bool goblin::config::debugLogging = false;
bool goblin::collected::read_player_map_id(uint32_t &mapId) {
    mapId = 0x3C1C4000;
    return true;
}
uint32_t goblin::visibility_epoch() { return epoch; }

void goblin::api_marker_points(int layer, std::vector<NativeMarkerPoint> &out) {
    ++layerCalls[layer];
    out.clear();
    const auto current = scenarios[layer].load();
    if (current == GoblinsScenario::Hidden)
        return;
    if (current == GoblinsScenario::FailedBuild) {
        // Inject one failed build; a persistent lower-layer fault would starve later layers.
        scenarios[layer] = GoblinsScenario::Visible;
        throw std::runtime_error("provider fixture");
    }
    const uint8_t area = layer == 2 ? 61 : 60;
    out = {{20, 6, area, static_cast<uint8_t>(layer), 28, 64, 141.f, 129.f, true, nullptr},
           {20 | NATIVE_CLEARED_KEY_BIT, 8, area, static_cast<uint8_t>(layer), 28, 64, 141.f, 129.f, true, nullptr},
           {NATIVE_HIGHLIGHT_KEY_BIT, 26, area, static_cast<uint8_t>(layer), 28, 64, 141.f, 129.f, true, nullptr},
           {22, 6, area, static_cast<uint8_t>(layer), 28, 64, std::numeric_limits<float>::quiet_NaN(), 0, true, nullptr},
           {23, 6, area, static_cast<uint8_t>(layer), 256, 64, 0, 0, true, nullptr},
           {24, -1, area, static_cast<uint8_t>(layer), 28, 64, 0, 0, true, nullptr}};
    if (current == GoblinsScenario::NativeDepths) {
        map_api::record_depth(20, 200);
        map_api::record_depth(20 | NATIVE_CLEARED_KEY_BIT, 100);
        map_api::record_depth(NATIVE_HIGHLIGHT_KEY_BIT, 50000);
    }
    if (current == GoblinsScenario::NewChild)
        out.push_back({30, 6, area, static_cast<uint8_t>(layer), 28, 64, 141, 129, true, nullptr});
    if (current == GoblinsScenario::ManyPoints || current == GoblinsScenario::Overflow)
        for (uint64_t i = 0; i < (current == GoblinsScenario::Overflow ? MFG_MAP_MAX_POINTS : 600); ++i)
            out.push_back({100 + i, 6, area, static_cast<uint8_t>(layer), 28, 64, 141, 129, true, nullptr});
}
