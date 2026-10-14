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
std::array<uint8_t, 96> memoSave{};
std::array<uint8_t, 1600> memoSlots{};
uintptr_t menuPointer = reinterpret_cast<uintptr_t>(menu.data());
ERMapState state{};
bool valid = true;
bool completion = false;
template<typename T>
void put(std::array<uint8_t, 1200> &data, size_t offset, const T &value) {
    std::memcpy(data.data() + offset, &value, sizeof(value));
}
void prepare() {
    for (auto *table: {&pointTable, &graceTable, &commonTable}) {
        (*table)[0x2D] = 4;
        (*table)[0x2E] = 2;
    }
    put(pointTable, 0, uint32_t{128 + sizeof(WorldMapPointParam)});
    put(graceTable, 0, uint32_t{128 + sizeof(BonfireWarpParam)});
    put(commonTable, 0, uint32_t{128 + 0x280});
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
    point.clearedEventFlagId = 2;
    point.areaNo = 60;
    point.gridXNo = 28;
    point.gridZNo = 64;
    point.dispMask00 = 1;
    point.angle = 90;
    put(pointTable, 128, point);
    for (size_t text = 1; text < 8; ++text)
        put(pointTable, 128 + 48 + text * 12, int32_t{-1});
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
    native.readEventFlag = [](uint32_t id) { return id == 1 || (id == 2 && completion); };
    er::minimap::nativeApi = &native;
    er::minimap::Data data;
    data.update();
    auto first = data.snapshot();
    if (!first.valid || !first.state.deathValid || first.state.underground != 0 || first.state.deathMapId != 0 || first.decorations.size() != 1 || first.decorations[0].x != 128 ||
        first.decorations[0].y != 128 || first.decorations[0].iconId != 3 || first.decorations[0].cleared)
        return 1;
    completion = true;
    ++state.generation;
    data.update();
    if (data.snapshot().decorations.size() != 1 || !data.snapshot().decorations[0].cleared)
        return 7;
    completion = false;
    ++state.generation;
    data.update();
    if (data.snapshot().decorations.size() != 1 || data.snapshot().decorations[0].cleared)
        return 8;
    // A mod point with no opening condition is controlled by its text flags.
    // Reconfiguring visibility must also invalidate the 200 ms snapshot cache.
    put(pointTable, 0x40, er::params::ParamEntryOffset{42, 128, 0});
    put(pointTable, 128 + offsetof(WorldMapPointParam, eventFlagId), uint32_t{0});
    put(pointTable, 128 + offsetof(WorldMapPointParam, textDisableFlagId1), uint32_t{2});
    ++state.generation;
    data.update();
    if (!data.snapshot().decorations.empty())
        return 9;
    data.setShowOthers(true);
    data.update();
    if (data.snapshot().decorations.size() != 1 || data.snapshot().decorations[0].source != er::minimap::DecorationSource::Other || first.decorations.size() != 1)
        return 10;
    put(pointTable, 128 + offsetof(WorldMapPointParam, iconId), uint16_t{0});
    put(pointTable, 128 + offsetof(WorldMapPointParam, altIconId), uint16_t{17});
    put(pointTable, 128 + offsetof(WorldMapPointParam, textType1), uint8_t{1});
    ++state.generation;
    data.update();
    if (data.snapshot().decorations.size() != 1 || data.snapshot().decorations[0].iconId != 17)
        return 17;
    put(pointTable, 128 + offsetof(WorldMapPointParam, altIconId), uint16_t{0});
    put(pointTable, 128 + offsetof(WorldMapPointParam, iconId), uint16_t{3});
    put(pointTable, 128 + offsetof(WorldMapPointParam, textType1), uint8_t{0});
    for (size_t slot = 0; slot < 100; ++slot) {
        int32_t id = -1;
        std::memcpy(memoSlots.data() + slot * 16, &id, sizeof(id));
    }
    auto putMemo = [&](size_t offset, const auto &value) { std::memcpy(memoSlots.data() + offset, &value, sizeof(value)); };
    auto saveMemo = [&](size_t offset, const auto &value) { std::memcpy(memoSave.data() + offset, &value, sizeof(value)); };
    put(view, 0x378, reinterpret_cast<uintptr_t>(memoSave.data()));
    saveMemo(8, reinterpret_cast<uintptr_t>(memoSlots.data()));
    saveMemo(16, uint64_t{100});
    saveMemo(0x40, uint64_t{2});
    putMemo(0, int32_t{11});
    putMemo(4, 180.f);
    putMemo(8, 190.f);
    putMemo(12, uint8_t{0});
    putMemo(13, uint8_t{1});
    putMemo(99 * 16, int32_t{12});
    putMemo(99 * 16 + 4, 380.f);
    putMemo(99 * 16 + 8, 390.f);
    putMemo(99 * 16 + 12, uint8_t{10});
    putMemo(99 * 16 + 13, uint8_t{10});
    data.update();
    auto memos = data.snapshot();
    if (memos.memoMarkers.size() != 2 || memos.memoMarkers[0].x != 180 || memos.memoMarkers[1].icon != 10 || memos.memoMarkers[1].map != 10)
        return 14;
    saveMemo(0x40, uint64_t{3}); // A torn count must discard the whole sample.
    data.update();
    if (!data.snapshot().memoMarkers.empty() || memos.memoMarkers.size() != 2)
        return 15;
    saveMemo(0x40, uint64_t{2});
    putMemo(13, uint8_t{11}); // Unsupported stamp frame is not rendered.
    data.update();
    if (data.snapshot().memoMarkers.size() != 1)
        return 16;
    completion = true;
    ++state.generation;
    data.update();
    if (!data.snapshot().decorations.empty())
        return 11;
    completion = false;
    put(pointTable, 0x40, er::params::ParamEntryOffset{90000, 128, 0});
    for (uint16_t icon: {80, 87, 88}) {
        put(pointTable, 128 + offsetof(WorldMapPointParam, iconId), icon);
        ++state.generation;
        data.update();
        if (data.snapshot().decorations.size() != 1 || data.snapshot().decorations[0].source != er::minimap::DecorationSource::Other ||
            data.snapshot().decorations[0].iconId != icon)
            return 12;
    }
    data.setShowOthers(false);
    data.update();
    if (!data.snapshot().decorations.empty() || !data.snapshot().memoMarkers.empty())
        return 13;
    put(pointTable, 128 + offsetof(WorldMapPointParam, iconId), uint16_t{3});
    ++state.generation;
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
    ++state.generation;
    put(view, 0x14, uint32_t{0x0b0a0000});
    data.update();
    if (!data.snapshot().roundtable)
        return 3;
    valid = false;
    data.update();
    if (data.snapshot().valid || !data.snapshot().decorations.empty() || !data.snapshot().memoMarkers.empty())
        return 4;
    er::minimap::nativeApi = nullptr;
    std::puts("PASS: production view reads, deaths, marker conversion, NPC/quest/mod point visibility, text-driven removal, cache reconfiguration, Home and invalidation.");
}
