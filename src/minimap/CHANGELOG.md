# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

### [Unreleased]

#### Configuration format change

- Replace flat keys and parallel comma-separated lists with sections and independent named display presets. All existing controls, marker switches and display adjustments remain configurable.
- Separate position from zoom, accept border width directly in pixels, and rewrite the INI comments and configuration guides. Invalid values use field defaults without throwing.
- Position presets at the center or using horizontal/vertical edge margins anchored to the matching map corner. Margins support pixels, screen percentages and negative values, retaining the anchor through size changes and circle mode.
- Replace old minimap.ini files with the new template; old configuration keys are no longer read.

#### Added

- Native dropped-runes marker, enabled by default with `[markers] death = true`. The marker follows the game's death record and is hidden after rune recovery or on a different map layer.
- Player-placed numbered map beacons 1–5, drawn with native downward arrows and dynamic digits. Enabled by default with `[markers] beacons = true`; placement and removal follow the game, and remaining beacons retain their numbers.
- Optional fully revealed minimap terrain with `[map] full_map = true`, covering the surface, underground and DLC. Disabled by default; this changes only the minimap display, leaving the save and grace/landmark discovery unchanged.

#### Changed

- Map tiles, icon atlases and GFX layouts now load through the game's file layer and render with Overlay-owned textures. External `data/map` files are no longer required or packaged.
- Surface, underground, DLC and Roundtable Hold rendering now uses native map coordinates, icon aliases and resource layouts.
- Minimap shortcuts now use the loader's shared Virtual-Key input handling, including modifier combinations and shared key presses.
- Native adapters cover hash-verified 1.02–1.17 historical EXEs and Steam 1.17.1, selecting menu, map, grace and parameter layouts by hash. Pre-DLC versions do not require M10 or DLC atlases.
- Atlas names and counts are discovered from GFX/TextureAtlas references, supporting mods with more than three atlases. Missing or malformed individual atlases no longer block valid siblings.

#### Fixed

- Keep circular minimap borders and their antialiasing inside the map bounds, preventing the top, bottom, left and right edges from being clipped. The minimap no longer inherits ImGui's window-border clipping inset.
- Corrected map-fragment progress synchronization and tile-variant selection so obtained fragments show explored terrain instead of the unexplored map layer.
- Added the surface underlay beneath the translucent underground map.
- Fixed surface death markers being hidden because padding bytes were interpreted as part of the underground flag.
- Corrected icon pivots, layered state graphics and Roundtable Hold asset selection.

#### Testing

- In-game testing with the latest ELDEN RING version was confirmed successful on 2026-10-02.
- Historical adapters passed offline checks for all 28 old directories and the latest EXE, plus three-layout, bounded short-row, death/beacon/full-map regressions. Per-version old-game testing remains pending.
- Twelve atlases passed production callback extraction, texture/UV drawing for 117 visible icon recipes, failure isolation and reset/retirement checks.
- Twelve BC1 atlases passed production D3D12 uploads, per-texture GPU readback comparisons and complete SRV retirement.

### [1.1.3] - 2026-06-30

#### Fixed
- Minimap GPU resources (textures, atlas sprites, offscreen target) are now properly destroyed and recreated during D3D12 device recovery and renderer resets. A new `Atlas::unloadTextures()` and a `Renderer` destructor clean up resources, and `prepareTile()` now bounds-checks the tile index to avoid crashes when the texture list is reset.
- Game memory addresses (`csMenuManImp`, `fieldArea`) are now re-queried during `update()` if they were unresolved at init time, so the minimap works even when signature scanning runs before the game code is fully loaded.
- Added a null-renderer guard in the plugin `render()` entry point.

### [1.1.2] - 2026-03-27

#### Fixed
- Fixed decorations (grace icons, landmarks) rendering outside the visible area in `circle` and `rounded` minimap shapes. The offscreen compositing now applies proper shape masking for all non-rect modes.
- Fixed decorations near the shape boundary being completely hidden instead of partially visible. The strict center-point shape check is removed since offscreen compositing now handles clipping.

### [1.1.1] - 2026-03-26

#### Added
- Per-state extra scale multipliers for rendering elements: `extra_tile_scale`, `extra_decoration_scale`, `extra_player_scale`, and `extra_bearing_scale` config options. Each accepts comma-separated values per scale state (cycled via `scale_key`), following the same pattern as `scale`/`alpha`/`width_ratio`.

#### Changed
- Increased bearing compass indicator size ratio from 0.25 to 0.4 for better visibility.

### [1.1.0] - 2026-03-23

#### Added
- Rotation mode: set `rotate=1` to rotate the minimap so the camera's facing direction always points up. When enabled, the shape is forced to circle. Configurable per scale state (comma-separated).
- A bearing compass indicator is shown in the top-right corner of the minimap when rotation mode is active.
- Camera yaw is now read from game memory for accurate map rotation.
- Landmark markers now appear on the minimap, with rotation support for directional icons.
- `landmarks_key` config option to set the key for toggling landmark marker visibility (default: `N`).
- `landmarks` config option to show/hide landmark markers by default (default: `1`).

#### Fixed
- Fixed translucent minimap elements (markers, tiles) appearing washed out due to double-alpha blending.
- Fixed crash when cycling minimap presets with mismatched config list lengths.
- Fixed pixel-snapping issues that could cause sub-pixel jitter on minimap edges.
- Various internal code fixes and optimizations.

### [1.0.0] - 2026-03-21

#### Added
- Configurable minimap shape: `rect` (default), `rounded`, or `circle`, set per scale state via `shape=` in `minimap.ini`.
- Configurable rounding radius for the `rounded` shape: supports percentage (e.g. `20%` of the shorter side) or absolute pixel values (e.g. `30`).
- Configurable shape border: `border_color` (R,G,B,A, 0–255 each) and `border_width_x10` (width × 10, e.g. `15` = 1.5 px). Set `border_width_x10=0` to disable.
- In `circle` mode, the minimap is automatically constrained to a square (the smaller of width and height).
- Bonfire/grace icons and the Roundtable Hold marker are now clipped to the minimap shape boundary in non-rect modes.

#### Fixed
- Fixed tile texture stretching in shaped (`rounded`/`circle`) rendering caused by incorrect UV clamping in `ShadeVertsLinearUV`.
- Fixed a null pointer dereference when `csMenuManImp_` address is zero.
- Removed leftover debug CSV dump from data loading.

### [0.1.0] - 2025-10-20

#### Added
- Initial release.
