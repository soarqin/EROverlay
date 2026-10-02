#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cmath>
#include <cstddef>

#include "mapstate.hpp"

namespace er::util {
namespace {
template<typename T>
bool read(uintptr_t address, T &value) {
    SIZE_T copied = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void *>(address), &value, sizeof(value), &copied) && copied == sizeof(value);
}
struct Location {
    int32_t map;
    float x, y;
    // RVA 0x887F37 writes only one byte. Padding is not initialized by that
    // write and must not participate in the underground/death-map predicate.
    uint8_t underground;
    uint8_t padding[3];
    float angle;
};
static_assert(sizeof(Location) == 20 && offsetof(Location, underground) == 12 && offsetof(Location, angle) == 16);
struct Death {
    float x, y;
    int32_t map;
};
} // namespace

bool readMapContext(uintptr_t menuGlobal, uintptr_t gameDataGlobal, const ERGameLayout &layout, MapContext &context) {
    context = {};
    uint16_t screen = 1;
    bool menuRead = read(menuGlobal, context.menu) && context.menu && read(context.menu + layout.menuOwnerOffset, context.owner) && context.owner &&
                    read(context.owner + layout.ownerViewOffset, context.view);
    bool playerRead = read(gameDataGlobal, context.gameData) && context.gameData && read(context.gameData + 0x58, context.player);
    if (!context.menu || !read(context.menu + layout.screenStateOffset, screen) || screen)
        context.player = 0;
    return menuRead && playerRead && context.view && context.player;
}
bool mapContextUnchanged(const MapContext &context, const ERGameLayout &layout) {
    uintptr_t owner = 0, view = 0, player = 0;
    uint16_t screen = 1;
    return context.menu && context.owner && context.gameData && read(context.menu + layout.menuOwnerOffset, owner) && owner == context.owner &&
           read(owner + layout.ownerViewOffset, view) && view == context.view && read(context.gameData + 0x58, player) && player == context.player &&
           read(context.menu + layout.screenStateOffset, screen) && !screen;
}

bool readWorldMapView(uintptr_t view, ERMapState &state) {
    state.deathValid = false;
    Location location;
    Death death;
    uint32_t raw = 0;
    uint8_t valid = 0;
    if (!view || !read(view + 0x24, location) || !read(view + 0x14, raw) || !read(view + 0xA9, valid) || !read(view + 0xAC, death) || !std::isfinite(location.x) ||
        !std::isfinite(location.y) || !std::isfinite(location.angle))
        return false;
    state.rawMapId = raw;
    state.mapId = location.map;
    state.x = location.x;
    state.y = location.y;
    state.underground = location.underground != 0;
    state.oriDeg = location.angle;
    state.deathValid = valid && std::isfinite(death.x) && std::isfinite(death.y);
    state.deathX = death.x;
    state.deathY = death.y;
    state.deathMapId = death.map;
    state.viewModel = view;
    return true;
}

} // namespace er::util
