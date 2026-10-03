// Exercise every hash-selected layout and bounded PARAM variant in owned
// fixture memory. Guard pages detect short-row overreads without a debugger.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

#include "minimap/data.hpp"
#include "minimap/defs/BonfireWarpParam.h"
#include "minimap/defs/WorldMapPointParam.h"
#include "minimap/layout.hpp"
#include "minimap/resources.hpp"
#include "params/param.hpp"
#include "util/gameflags.hpp"
#include "util/gameprofile.hpp"
#include "util/mapstate.hpp"
#include "util/paramreader.hpp"

EROverlayAPI *api = nullptr;
namespace {
const er::util::GameProfile *profile = nullptr;
std::array<uint8_t, 2048> menu{}, owner{}, view{}, gameData{}, player{}, graces{}, commonTable{};
std::array<uint8_t, 80> markerSave{};
struct Record {
    int32_t id;
    float x, y;
    uint8_t map, icon;
    uint16_t padding;
};
std::array<Record, 10> slots;
uintptr_t menuPointer = reinterpret_cast<uintptr_t>(menu.data()), gameDataPointer = reinterpret_cast<uintptr_t>(gameData.data());
uintptr_t graceTable = 0, pointTable = 0;
uint64_t generation = 0;
bool alternateFlag = false;
template<typename T>
void put(uintptr_t address, const T &value) {
    std::memcpy(reinterpret_cast<void *>(address), &value, sizeof(value));
}
template<size_t N, typename T>
void put(std::array<uint8_t, N> &bytes, size_t offset, const T &value) {
    put(reinterpret_cast<uintptr_t>(bytes.data()) + offset, value);
}
struct GuardTable {
    uint8_t *allocation = nullptr;
    uintptr_t table = 0;
    GuardTable(size_t rows, size_t rowSize, uint8_t format = 4, bool wide = true) {
        allocation = static_cast<uint8_t *>(VirtualAlloc(nullptr, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!allocation)
            return;
        DWORD old;
        if (!VirtualProtect(allocation + 4096, 4096, PAGE_NOACCESS, &old))
            return;
        size_t size = 256 + rows * rowSize;
        table = reinterpret_cast<uintptr_t>(allocation + 4096 - size);
        put(table, static_cast<uint32_t>(size));
        put(table + 0xA, static_cast<uint16_t>(rows));
        put(table + 0x2D, format);
        put(table + 0x2E, uint8_t(wide ? 2 : 0));
        size_t start = (format & 0x7F) == 2 ? 0x30 : 0x40;
        for (size_t i = 0; i < rows; ++i)
            if (wide)
                put(table + start + i * 24, er::params::ParamEntryOffset{static_cast<uint64_t>(i + 1), static_cast<intptr_t>(256 + i * rowSize), 0});
            else {
                std::array<uint32_t, 3> entry{static_cast<uint32_t>(i + 1), static_cast<uint32_t>(256 + i * rowSize), 0};
                put(table + start + i * 12, entry);
            }
    }
    ~GuardTable() noexcept {
        if (allocation)
            VirtualFree(allocation, 0, MEM_RELEASE);
    }
    GuardTable(const GuardTable &) = delete;
    GuardTable &operator=(const GuardTable &) = delete;
};

bool checkParamFormats() {
    using er::util::ParamRows;
    for (auto [format, wide]: {std::pair{uint8_t{2}, false}, {uint8_t{3}, false}, {uint8_t{4}, false}, {uint8_t{4}, true}, {uint8_t{5}, false}, {uint8_t{0x85}, true}}) {
        GuardTable table(2, 48, format, wide);
        ParamRows rows;
        if (!table.table || !rows.open(table.table) || rows.rows().size() != 2 || rows.rows()[0].size != 48 || rows.rows()[1].size != 48)
            return false;
        BonfireWarpParam value{};
        if (!rows.read(rows.rows().back(), value, 48) || rows.read(rows.rows().back(), value) || rows.field(rows.rows().back(), 47, value.iconId))
            return false;
        put(table.table + 0x2C, uint8_t{1});
        if (rows.open(table.table))
            return false;
        put(table.table + 0x2C, uint8_t{0});
        put(table.table + 0x2D, uint8_t{0});
        if (rows.open(table.table))
            return false;
        put(table.table + 0x2D, format);
        put(table.table, uint32_t{0});
        put(table.table - 16, uint32_t{256 + 2 * 48});
        if (!rows.open(table.table) || rows.rows().back().size != 48 || !rows.read(rows.rows().back(), value, 48))
            return false;
    }
    return true;
}
bool checkPieceFormats() {
    std::array<uint8_t, 128> manager{}, sentinel{}, node{};
    std::array<uint8_t, 32> flags{};
    put(manager, 0x1C, uint32_t{100});
    put(manager, 0x38, reinterpret_cast<uintptr_t>(sentinel.data()));
    put(sentinel, 8, reinterpret_cast<uintptr_t>(node.data()));
    sentinel[0x19] = 1;
    put(node, 0, reinterpret_cast<uintptr_t>(sentinel.data()));
    put(node, 0x10, reinterpret_cast<uintptr_t>(sentinel.data()));
    put(node, 0x20, uint32_t{1});
    put(node, 0x28, uint32_t{2});
    put(node, 0x30, reinterpret_cast<uintptr_t>(flags.data()));
    flags[0] = 0x80;
    for (auto [format, wide]: {std::pair{uint8_t{2}, false}, {uint8_t{4}, true}}) {
        GuardTable table(2, 8, format, wide);
        size_t start = format == 2 ? 0x30 : 0x40, stride = wide ? 24 : 12;
        put(table.table + start, uint32_t{0});
        put(table.table + start + stride, uint32_t{103});
        put(table.table + 256 + 4, int32_t{100});
        put(table.table + 264 + 4, int32_t{100});
        uint32_t masks[3]{};
        if (!er::util::readMapPieceMasks(table.table, reinterpret_cast<uintptr_t>(manager.data()), false, masks) || masks[0] != 1 || masks[1] != 8 || masks[2])
            return false;
    }
    return true;
}

bool checkProfile() {
    const auto &layout = profile->layout;
    bool early = !layout.alternateIcons;
    GuardTable grace(1, early ? 168 : sizeof(BonfireWarpParam));
    GuardTable point(1, early ? 184 : sizeof(WorldMapPointParam));
    if (!grace.table || !point.table)
        return false;
    graceTable = grace.table;
    pointTable = point.table;
    menu.fill(0);
    owner.fill(0);
    view.fill(0);
    gameData.fill(0);
    graces.fill(0);
    commonTable.fill(0);
    put(menu, layout.menuOwnerOffset, reinterpret_cast<uintptr_t>(owner.data()));
    put(owner, layout.ownerViewOffset, reinterpret_cast<uintptr_t>(view.data()));
    put(gameData, 0x58, reinterpret_cast<uintptr_t>(player.data()));
    put(view, 0x14, uint32_t{0x3C1C4000});
    put(view, 0x24, int32_t{0});
    put(view, 0x28, 128.f);
    put(view, 0x2C, 128.f);
    put(view, 0x30, uint32_t{0x32A66000});
    put(view, 0xA9, uint8_t{1});
    put(view, 0xAC, 138.f);
    put(view, 0xB0, 140.f);
    put(view, 0xB4, int32_t{0});
    put(view, 0x280, uint64_t{2});
    uint8_t origin[]{0, 64, 28, 60};
    std::memcpy(view.data() + 0x100, origin, 4);
    float settings[]{0, 0, 0, 128, 128, 1};
    std::memcpy(view.data() + 0x104, settings, sizeof(settings));
    std::memcpy(view.data() + 0x134, settings, sizeof(settings));
    view[0x133] = 61;
    put(view, 0x2E8, reinterpret_cast<uintptr_t>(graces.data()));
    put(view, 0x2F0, reinterpret_cast<uintptr_t>(graces.data()) + 2 * layout.graceStride);
    for (size_t i = 0; i < 2; ++i) {
        size_t offset = i * layout.graceStride;
        put(graces, offset + 0x238, uint32_t(i ? 99999 : 111000));
        put(graces, offset + layout.graceNormalOffset, uint8_t{0});
        put(graces, offset + 0x288, uint32_t(i ? 98 : 77));
        if (!early)
            put(graces, offset + 0x308, uint32_t(i ? 97 : 88));
    }
    BonfireWarpParam graceRow{};
    graceRow.eventflagId = 1;
    graceRow.iconId = 1;
    graceRow.areaNo = 60;
    graceRow.gridXNo = 28;
    graceRow.gridZNo = 64;
    graceRow.dispMask00 = 1;
    graceRow.textId1 = 10;
    graceRow.textType1 = 1;
    graceRow.textEnableFlagId1 = 2;
    graceRow.altIconId = 99;
    std::memcpy(reinterpret_cast<void *>(graceTable + 256), &graceRow, early ? 168 : sizeof(graceRow));
    put(graceTable + 0x40, uint64_t{111000});
    WorldMapPointParam pointRow{};
    pointRow.eventFlagId = 1;
    pointRow.distViewEventFlagId = 2;
    pointRow.iconId = 3;
    pointRow.distViewIconId = 49;
    pointRow.altIconId = 84;
    pointRow.textId1 = 12;
    pointRow.textType1 = 1;
    pointRow.textEnableFlagId1 = 2;
    pointRow.areaNo = 60;
    pointRow.gridXNo = 28;
    pointRow.gridZNo = 64;
    pointRow.dispMask00 = 1;
    std::memcpy(reinterpret_cast<void *>(pointTable + 256), &pointRow, early ? 184 : sizeof(pointRow));
    put(pointTable + 0x40, uint64_t{90000});
    put(commonTable, 0, uint32_t{256 + 0x280});
    put(commonTable, 0xA, uint16_t{1});
    commonTable[0x2D] = 4;
    commonTable[0x2E] = 2;
    put(commonTable, 0x40, er::params::ParamEntryOffset{0, 256, 0});
    put(commonTable, 256 + 0x278, uint32_t{123456});
    for (auto &slot: slots)
        slot.id = -1;
    slots[0] = {4, 120, 132, 0, 1, 0};
    slots[4] = {5, 135, 128, 0, 1, 0};
    put(view, 0x338, reinterpret_cast<uintptr_t>(markerSave.data()));
    put(markerSave, 8, reinterpret_cast<uintptr_t>(slots.data()));
    put(markerSave, 16, uint64_t{10});
    put(markerSave, 0x40, uint64_t{2});
    alternateFlag = true;
    ++generation;
    er::minimap::Data data;
    data.update();
    auto first = data.snapshot();
    if (!first.valid || !first.state.deathValid || first.state.underground || first.decorations.size() != 2 || first.playerMarkers.size() != 2 ||
        first.playerMarkers[1].number != 5 || first.decorations[0].iconId != (early ? 77 : 88) || first.decorations[1].iconId != (early ? 3 : 84))
        return false;
    alternateFlag = false;
    ++generation;
    data.update();
    auto base = data.snapshot();
    if (base.decorations.size() != 2 || base.decorations[0].iconId != 77 || base.decorations[1].iconId != 3)
        return false;
    // Verify the actual menu/view chain reader at both sides of a transition.
    er::util::MapContext context;
    if (!er::util::readMapContext(reinterpret_cast<uintptr_t>(&menuPointer), reinterpret_cast<uintptr_t>(&gameDataPointer), layout, context) ||
        !er::util::mapContextUnchanged(context, layout))
        return false;
    put(menu, layout.screenStateOffset, uint16_t{1});
    if (er::util::mapContextUnchanged(context, layout))
        return false;
    ++generation;
    data.update();
    if (data.snapshot().valid || !data.snapshot().playerMarkers.empty() || !data.snapshot().decorations.empty())
        return false;
    put(menu, layout.screenStateOffset, uint16_t{0});
    view[0xA9] = 0;
    slots[0].id = -1;
    put(markerSave, 0x40, uint64_t{1});
    ++generation;
    data.update();
    if (!data.snapshot().valid || data.snapshot().state.deathValid || data.snapshot().playerMarkers.size() != 1 || data.snapshot().playerMarkers[0].number != 5)
        return false;
    // An uninitialized non-native converter must not convert a legacy point.
    view[0x103] = 59;
    view[0x133] = 59;
    ++generation;
    data.update();
    return data.snapshot().decorations.empty();
}
} // namespace

int main() {
    EROverlayAPI legacy{};
    legacy.screenState = [] { return 0; };
    legacy.getGameAddresses = [] { return GameAddresses{reinterpret_cast<uintptr_t>(&menuPointer), reinterpret_cast<uintptr_t>(&gameDataPointer), 0, 0, 0}; };
    api = &legacy;
    EROverlayNativeAPI native{};
    native.size = sizeof(native);
    native.version = 1;
    native.readGameLayout = [](ERGameLayout *layout) {
        *layout = profile->layout;
        return true;
    };
    native.readMapState = [](ERMapState *state) {
        *state = {};
        state->generation = generation;
        er::util::MapContext context;
        return er::util::readMapContext(reinterpret_cast<uintptr_t>(&menuPointer), reinterpret_cast<uintptr_t>(&gameDataPointer), profile->layout, context) &&
               er::util::readWorldMapView(context.view, *state) && er::util::mapContextUnchanged(context, profile->layout);
    };
    native.findParamTable = [](uint32_t group) -> uintptr_t { return group == 43 ? graceTable : group == 87 ? pointTable : reinterpret_cast<uintptr_t>(commonTable.data()); };
    native.readEventFlag = [](uint32_t id) { return id == 1 || (id == 2 && alternateFlag); };
    er::minimap::nativeApi = &native;
    native.size = ER_NATIVE_API_V1_SIZE;
    if (er::minimap::gameLayout().graceStride != 0x350 || er::minimap::gameLayout().mapMask != 7)
        return 4;
    native.size = sizeof(native);
    if (!checkParamFormats() || !checkPieceFormats())
        return 1;
    native.size = offsetof(EROverlayNativeAPI, queueDdsTexture);
    profile = &er::util::GAME_PROFILES[0];
    if (er::minimap::gameLayout().graceStride != profile->layout.graceStride || er::minimap::gameLayout().mapMask != profile->layout.mapMask)
        return 5;
    native.size = sizeof(native);
    std::array<uint8_t, 32> unknown{};
    if (er::util::findGameProfile(unknown))
        return 2;
    for (const auto &item: er::util::GAME_PROFILES) {
        profile = &item;
        if (er::util::findGameProfile(item.sha256) != profile || !checkProfile()) {
            std::fprintf(stderr, "FAIL profile=%s\n", profile->label);
            return 3;
        }
        std::printf("PASS profile=%s stride=%x maps=%x\n", profile->label, profile->layout.graceStride, profile->layout.mapMask);
    }
    er::minimap::nativeApi = nullptr;
    std::puts("PASS: all hash profiles, old/new menu chains, guard-page short rows, six PARAM formats, native grace selection, deaths and sparse numbered slots.");
}
