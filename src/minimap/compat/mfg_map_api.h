/* Read-only MapForGoblins map views. MIT, Copyright (c) 2026 EROverlay contributors.
 * Windows x64, default packing and calling convention.
 * Caller retains the provider module while using any returned pointer.
 * No STL objects, heap ownership or exceptions cross the DLL boundary.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { MFG_MAP_ABI = 1, MFG_MAP_MAX_POINTS = 32768, MFG_MAP_MAX_ICONS = 512 };
enum { MFG_MAP_OK = 0, MFG_MAP_INVALID = 1, MFG_MAP_FAILED = 2 };

typedef struct MfgMapPoint {
    uint64_t id;
    uint32_t icon_id; /* Stable source id, independent of runtime GFX frame ids. */
    uint32_t raw_map_id; /* area/gridX/gridZ/variant, matching the game's map id. */
    float x, z; /* Display coordinates, including the provider's de-overlap. */
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
    /* Update thread only. layer: surface=0, underground=1, DLC=2.
     * Returns a borrowed read-only view of visible points, including completion
     * badges and focus rings, sorted by ascending native child draw_order.
     * Before child creation, draw_order follows native row order in the normal
     * and focus-ring depth bands; existing children use their actual depths.
     * The view is valid until the calling thread next calls view_points, exits,
     * or the provider is unloaded. Other threads cannot invalidate the view.
     * Consume synchronously without reentering view_points. Do not modify,
     * free, or retain the view beyond its lifetime.
     * Empty/not ready: MFG_MAP_OK with NULL/0. Failure: non-OK with NULL/0.
     */
    int (*view_points)(uint32_t layer, const MfgMapPoint **points, uint32_t *count);
    int (*get_icon)(uint32_t index, MfgMapIcon *icon);
} MfgMapAPI;

typedef const MfgMapAPI *(*MfgGetMapAPIFn)(uint32_t abi_version);

#ifdef __cplusplus
}
#endif
