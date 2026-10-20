/* Read-only MapForGoblins map views. MIT, Copyright (c) 2026 EROverlay contributors.
 * Windows x64, default packing and calling convention.
 * Caller retains the provider module while using any returned pointer.
 * No STL objects, heap ownership or exceptions cross the DLL boundary.
 *
 * Getting the table: MapForGoblins exports MfgGetMapAPI (no import library):
 *     HMODULE m = GetModuleHandleW(L"MapForGoblins.dll");
 *     MfgGetMapAPIFn get = m ? (MfgGetMapAPIFn)GetProcAddress(m, "MfgGetMapAPI") : NULL;
 *     const MfgMapAPI *api = get ? get(MFG_MAP_ABI) : NULL;
 * NULL = another ABI, or a build without native markers. Load order is not fixed: ask again
 * later rather than once at your own start. Nothing is computed for the API until the first
 * MfgGetMapAPI call, so a game without a reader pays nothing.
 *
 * Freshness: the points come from a snapshot MapForGoblins rebuilds on its own background
 * thread (every 100 ms it checks; ~2 ms per rebuild) for each layer asked for within the
 * last 3 s - within ~0.35 s of a change it knows about (a category toggle, a focus, a
 * manual hide, a collected piece), and at least once a second for what only an event flag
 * says (an item picked up, a boss killed). view_points itself only hands that snapshot
 * out, so calling it every frame costs nothing. The first call for a layer, or the first
 * after a pause of more than 3 s, may return an empty or old view; the next ~100 ms bring
 * the current one.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { MFG_MAP_ABI = 1, MFG_MAP_MAX_POINTS = 32768, MFG_MAP_MAX_ICONS = 512 };
enum { MFG_MAP_OK = 0, MFG_MAP_INVALID = 1, MFG_MAP_FAILED = 2 };

typedef struct MfgMapPoint {
    /* WorldMapPointParam row id of the marker. Bit 63 set: the completion badge drawn over
     * that row (defeated boss, NPC, ...). Bit 62 set: a focus ring, low bits = pool index. */
    uint64_t id;
    uint32_t icon_id; /* Stable source id, independent of runtime GFX frame ids. */
    /* area << 24 | gridX << 16 | gridZ << 8, the tile the position below is relative to. The
     * low (variant) byte is always 0: a world-state version of a tile shares its markers. */
    uint32_t raw_map_id;
    /* Display coordinates, including the provider's de-overlap: the row's posX/posZ in
     * that tile, moved aside where markers would cover each other. */
    float x, z;
    uint32_t draw_order; /* Ascending child depth inside the native ItemList. */
    uint32_t reserved;
} MfgMapPoint;

typedef struct MfgMapIcon {
    const uint8_t *lossless2; /* Immutable SWF DefineBitsLossless2 body. */
    uint32_t lossless2_size;
    uint32_t source_icon_id;
    const uint8_t *matrix; /* Immutable SWF placement MATRIX. */
    uint32_t matrix_size;
    uint32_t reserved;
} MfgMapIcon;

typedef struct MfgMapAPI {
    uint32_t size;
    uint32_t abi_version;
    uint32_t point_size;
    uint32_t icon_size;
    uint32_t icon_count; /* Immutable enumeration, valid indices [0, icon_count). */
    uint32_t reserved;
    /* Any thread. layer: surface=0, underground=1, DLC=2.
     * Returns a borrowed read-only view of visible points, including completion
     * badges and focus rings, sorted by ascending native child draw_order.
     * Before child creation, draw_order follows native row order in the normal
     * and focus-ring depth bands; existing children use their actual depths
     * (recorded from the next map open after the first MfgGetMapAPI call), and
     * a point that has no child yet comes after all of them.
     * The view is valid until the calling thread next calls view_points for the
     * same layer, exits, or the provider is unloaded. Other threads and other
     * layers cannot invalidate the view.
     * Consume synchronously without reentering view_points. Do not modify,
     * free, or retain the view beyond its lifetime.
     * Empty/not ready (markers not injected yet, all icons switched off, the
     * layer's first snapshot not built yet): MFG_MAP_OK with NULL/0.
     * Failure: non-OK with NULL/0 (MFG_MAP_FAILED: more than MFG_MAP_MAX_POINTS).
     */
    int (*view_points)(uint32_t layer, const MfgMapPoint **points, uint32_t *count);
    /* Any thread. The icon art: every point's icon_id is the source_icon_id of one
     * entry here, badges and focus rings included. */
    int (*get_icon)(uint32_t index, MfgMapIcon *icon);
} MfgMapAPI;

typedef const MfgMapAPI *(*MfgGetMapAPIFn)(uint32_t abi_version);

#ifdef __cplusplus
}
#endif
