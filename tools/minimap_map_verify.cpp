// Check update-thread map/death snapshots using a synthetic, read-only view model.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

#include "minimap/data.hpp"
#include "minimap/defs/BonfireWarpParam.h"
#include "minimap/defs/WorldMapPointParam.h"
#include "minimap/resources.hpp"
#include "params/param.hpp"
#include "util/mapstate.hpp"

EROverlayAPI *api = nullptr;
namespace {
std::array<uint8_t, 1200> view{}, menu{}, owner{}, pointTable{}, graceTable{}, commonTable{};
uintptr_t menuPointer = reinterpret_cast<uintptr_t>(menu.data());
ERMapState state{};
bool valid = true;
template<typename T>
void put(std::array<uint8_t, 1200> &data, size_t offset, const T &value) {
    std::memcpy(data.data() + offset, &value, sizeof(value));
}
void prepare() {
    put(view, 0x280, uint64_t{2});
    uint8_t surface[]{0, 64, 28, 60}, dlc[]{0, 64, 28, 61};
    std::memcpy(view.data() + 0xF8 + 8, surface, 4);
    std::memcpy(view.data() + 0x128 + 8, dlc, 4);
    float settings[]{0, 0, 0, 128, 128, 1};
    std::memcpy(view.data() + 0xF8 + 12, settings, sizeof(settings));
    std::memcpy(view.data() + 0x128 + 12, settings, sizeof(settings));
    er::params::ParamEntryOffset entry{90000, 128, 0};
    put(pointTable, 0xA, uint16_t{1});
    put(pointTable, 0x40, entry);
    WorldMapPointParam point{};
    point.eventFlagId = 1;
    point.iconId = 3;
    point.areaNo = 60;
    point.gridXNo = 28;
    point.gridZNo = 64;
    point.dispMask00 = 1;
    point.angle = 90;
    put(pointTable, 128, point);
    entry.paramId = 111000;
    put(graceTable, 0xA, uint16_t{1});
    put(graceTable, 0x40, entry);
    BonfireWarpParam grace{};
    grace.iconId = 48;
    grace.areaNo = 11;
    grace.gridXNo = 10;
    put(graceTable, 128, grace);
    entry.paramId = 0;
    put(commonTable, 0xA, uint16_t{1});
    put(commonTable, 0x40, entry);
    put(commonTable, 128 + 0x278, uint32_t{111000});
    state.generation = 1;
    state.rawMapId = 0x3c1c4000;
    state.mapId = 0;
    state.x = 128;
    state.y = 128;
    state.viewModel = reinterpret_cast<uintptr_t>(view.data());
    state.deathValid = true;
    state.deathX = 148;
    state.deathY = 138;
    state.deathMapId = 0;
    put(view, 0x14, state.rawMapId);
    put(view, 0x24, state.mapId);
    put(view, 0x28, state.x);
    put(view, 0x2C, state.y);
    put(view, 0x34, 15.f);
    // Captured gameplay bytes at +0x30: 00 60 A6 32. Only 00 is the flag;
    // the old int32 read made the surface death map compare against 1.
    put(view, 0x30, uint32_t{0x32A66000});
    put(view, 0xA9, uint8_t{1});
    put(view, 0xAC, state.deathX);
    put(view, 0xB0, state.deathY);
    put(view, 0xB4, state.deathMapId);
}
} // namespace
int main() {
    prepare();
    EROverlayAPI legacy{};
    legacy.screenState = []() { return 0; };
    legacy.getGameAddresses = []() { return GameAddresses{reinterpret_cast<uintptr_t>(&menuPointer), 0, 0, 0, 0}; };
    api = &legacy;
    EROverlayNativeAPI native{};
    native.readMapState = [](ERMapState *out) {
        *out = state;
        return valid && er::util::readWorldMapView(reinterpret_cast<uintptr_t>(view.data()), *out);
    };
    native.findParamTable = [](uint32_t group) -> uintptr_t { return reinterpret_cast<uintptr_t>((group == 43 ? graceTable : group == 87 ? pointTable : commonTable).data()); };
    native.readEventFlag = [](uint32_t id) { return id == 1; };
    er::minimap::nativeApi = &native;
    er::minimap::Data data;
    data.update();
    auto first = data.snapshot();
    if (!first.valid || !first.state.deathValid || first.state.underground != 0 || first.state.deathMapId != 0 || first.decorations.size() != 1 || first.decorations[0].x != 128 ||
        first.decorations[0].y != 128 || first.decorations[0].iconId != 3)
        return 1;
    view[0x30] = 1;
    put(view, 0xB4, int32_t{1});
    data.update();
    if (data.snapshot().state.underground != 1 || data.snapshot().state.deathMapId != 1)
        return 5;
    put(view, 0x24, int32_t{10});
    view[0x30] = 0;
    put(view, 0xB4, int32_t{10});
    data.update();
    if (data.snapshot().state.mapId != 10 || data.snapshot().state.underground != 0)
        return 6;
    view[0xA9] = 0;
    data.update();
    if (data.snapshot().state.deathValid)
        return 2;
    state.generation = 2;
    put(view, 0x14, uint32_t{0x0b0a0000});
    data.update();
    if (!data.snapshot().roundtable)
        return 3;
    valid = false;
    data.update();
    if (data.snapshot().valid || !data.snapshot().decorations.empty())
        return 4;
    er::minimap::nativeApi = nullptr;
    std::puts("PASS: production byte-width view reads with nonzero padding, surface/underground/DLC deaths, marker conversion, death clearing, Home and invalidation.");
}
