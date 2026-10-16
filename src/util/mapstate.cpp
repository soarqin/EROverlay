#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>

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

template<size_t N>
bool menuTypeMatches(uintptr_t menu, const char (&type)[N], bool &matches, bool primary = false) {
    static_assert(N >= 4);
    matches = false;
    uintptr_t vtable = 0, locator = 0;
    std::array<uint32_t, 6> col{};
    // An allocated/readable object may still be zero-filled during boot.
    // Require a constructed MSVC RTTI chain before interpreting its state.
    // COL.self and TypeDescriptor are image-relative, including under ASLR.
    if (!read(menu, vtable) || vtable < sizeof(uintptr_t) || !read(vtable - sizeof(uintptr_t), locator) || !read(locator, col) || col[0] != 1 || (primary && col[1] != 0) ||
        !col[5] || !col[3] || locator < col[5])
        return false;
    std::array<char, N> name{};
    if (!read(locator - col[5] + col[3] + 16, name) || name[0] != '.' || name[1] != '?' || name[2] != 'A' || (name[3] != 'V' && name[3] != 'U'))
        return false;
    matches = std::memcmp(name.data(), type, N) == 0;
    return true;
}
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

bool readDisplayState(uintptr_t menuGlobal, uintptr_t gameDataGlobal, const ERGameLayout &layout, ERDisplayState &state) {
    state = {true, false};
    uintptr_t menu = 0, gameData = 0, player = 0, owner = 0, view = 0;
    if (!read(menuGlobal, menu))
        return false;
    if (!menu)
        return true; // Menu system has not finished booting.
    bool matches = false;
    if (!menuTypeMatches(menu, ".?AVCSMenuManImp@CS@@", matches, true) || !matches || !menuTypeMatches(menu + layout.menuInfoOffset, ".?AVLoadingScreenData@CS@@", matches, true) ||
        !matches)
        return false;
    uint16_t loading = 0;
    uint8_t loadingMenu = 0, nowLoading = 0, worldMap = 0;
    // Menu-control bytes follow the menu owner by 0x10. Native constructors
    // identify LoadingScreen=6, NowLoading=21 and WorldMapDialog=61.
    auto controls = menu + layout.menuOwnerOffset + 0x10;
    if (!read(menu + layout.screenStateOffset, loading) || !read(controls + 6, loadingMenu) || !read(controls + 21, nowLoading) || !read(controls + 61, worldMap))
        return false;
    if (!read(gameDataGlobal, gameData) || (gameData && !read(gameData + 0x58, player)))
        return false;
    // Early boot may have no character data. Hide resource status then;
    // explicit LogoMenu is checked below even when data is already allocated.
    ERDisplayState next{loading != 0 || ((loadingMenu | nowLoading) & 1) != 0 || !gameData || !player, (worldMap & 1) != 0};
    if (!next.loadingOrLogo) {
        // Loading flags begin at zero and character data can exist before the
        // startup black/logo screens finish. Do not infer "visible" until the
        // UI owner and its view have been published. This does not require map
        // coordinates, a minimap snapshot, or overlay resources to be ready.
        if (!read(menu + layout.menuOwnerOffset, owner) || !owner || !read(owner + layout.ownerViewOffset, view) || !view)
            return false;
        // CSMenuMan's inline root stack ends immediately before menuOwner.
        // Its update dispatches directly to LogoMenu while logos are active.
        // Check this separately: allocating character data does not end logo.
        constexpr uint32_t ROOTS = 0x30;
        auto end = layout.menuOwnerOffset - sizeof(uintptr_t);
        uint64_t count = 0;
        if (end < ROOTS || end > ROOTS + 16 * sizeof(uintptr_t) || !read(menu + end, count) || count > (end - ROOTS) / sizeof(uintptr_t))
            return false;
        for (uint64_t i = 0; i < count; ++i) {
            uintptr_t root = 0;
            if (!read(menu + ROOTS + i * sizeof(uintptr_t), root))
                return false;
            if (!root || !menuTypeMatches(root, ".?AVLogoMenu@@", matches))
                return false;
            next.loadingOrLogo |= matches;
        }
    }
    // Publishing/replacing the UI can race these independent reads. Keep the
    // default hidden state instead of exposing a mixture of two generations.
    uintptr_t current = 0;
    if (!read(menuGlobal, current) || current != menu || !read(gameDataGlobal, current) || current != gameData ||
        (gameData && (!read(gameData + 0x58, current) || current != player)) ||
        (owner && (!read(menu + layout.menuOwnerOffset, current) || current != owner || !read(owner + layout.ownerViewOffset, current) || current != view)))
        return false;
    state = next;
    return true;
}

} // namespace er::util
