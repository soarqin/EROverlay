// Offline differential fixture only. Installation builds do not link these
// original C++ runtime readers; the production implementation remains Rust.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include "util/mapstate.hpp"
#include "util/gameflags.hpp"
#include "util/paramreader.hpp"
#include "util/gamescan.hpp"

// Optional test oracle only; production discovery executes the Rust scanner.
extern "C" bool er_cpp_scan_fixture(const uint8_t *image, size_t length, uintptr_t base,
                                      er::util::GameBindings *bindings, char *error, size_t capacity) noexcept {
    if (!image || !bindings || !error || !capacity) return false;
    *bindings = {};
    error[0] = 0;
    try {
        std::string message;
        const bool ok = er::util::gamescan::resolve(er::util::PeImage({image, length}, base), *bindings, message);
        std::memcpy(error, message.data(), std::min(message.size(), capacity - 1));
        error[std::min(message.size(), capacity - 1)] = 0;
        return ok;
    } catch (...) { return false; }
}

struct CppMapResult {
    uint32_t phase;
    uint32_t masks[3];
    uint32_t rawMapId;
    int32_t mapId;
    float x, y, angle;
    int32_t underground;
    uint32_t deathValid;
    float deathX, deathY;
    int32_t deathMapId;
    uintptr_t viewModel;
};
static_assert(sizeof(CppMapResult) == 64);

extern "C" void *er_cpp_flag_cache_fixture() noexcept {
    try { return new er::util::EventFlagCache; }
    catch (...) { return nullptr; }
}
extern "C" void er_cpp_drop_flag_cache_fixture(void *cache) noexcept {
    delete static_cast<er::util::EventFlagCache *>(cache);
}
extern "C" bool er_cpp_masks_cached_fixture(uintptr_t table, uintptr_t manager, void *cache, uint32_t *out) noexcept {
    if (!cache || !out) return false;
    try {
        uint32_t masks[3]{};
        const bool ok = er::util::readMapPieceMasks(table, manager, false, masks, static_cast<er::util::EventFlagCache *>(cache));
        for (unsigned i = 0; i < 3; ++i) out[i] = masks[i];
        return ok;
    } catch (...) { return false; }
}

extern "C" bool er_cpp_flag_fixture(uintptr_t manager, uint32_t id, uint32_t *value) noexcept {
    if (!value) return false;
    bool result = false;
    const bool success = er::util::readGameEventFlag(manager, id, result);
    *value = result ? 1u : 0u;
    return success;
}
extern "C" bool er_cpp_param_fixture(uintptr_t table, uint32_t id, size_t offset, uint32_t *value) noexcept {
    if (!value) return false;
    try {
        er::util::ParamRows rows;
        if (!rows.open(table)) return false;
        for (const auto &row: rows.rows()) {
            if (row.id == id) return rows.field(row, offset, *value);
        }
    } catch (...) {
    }
    return false;
}
extern "C" void er_cpp_read_map_fixture(uintptr_t base, uint32_t menuRva, uint32_t gameDataRva,
                                       const ERGameLayout *layout, uintptr_t table, uintptr_t manager,
                                       bool reveal, CppMapResult *out) noexcept {
    if (!out) return;
    *out = {};
    out->phase = 1;
    if (!layout) return;
    try {
        ERDisplayState display{};
        er::util::MapContext context;
        ERMapState state{};
        if (!er::util::readDisplayState(base + menuRva, base + gameDataRva, *layout, display)) return;
        out->phase = 2;
        if (display.loadingOrLogo || display.fullScreenMap) return;
        out->phase = 3;
        if (!er::util::readMapContext(base + menuRva, base + gameDataRva, *layout, context)) return;
        out->phase = 4;
        if (!er::util::readWorldMapView(context.view, state)) return;
        out->phase = 5;
        if (!er::util::readMapPieceMasks(table, manager, reveal, out->masks)) return;
        out->phase = 6;
        if (!er::util::mapContextUnchanged(context, *layout)) return;
        out->rawMapId = state.rawMapId;
        out->mapId = state.mapId;
        out->x = state.x;
        out->y = state.y;
        out->angle = state.oriDeg;
        out->underground = state.underground;
        out->deathValid = state.deathValid;
        out->deathX = state.deathX;
        out->deathY = state.deathY;
        out->deathMapId = state.deathMapId;
        out->viewModel = state.viewModel;
        out->phase = 0;
    } catch (...) {
        out->phase = 7;
    }
}
