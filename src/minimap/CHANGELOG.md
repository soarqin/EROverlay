# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

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
