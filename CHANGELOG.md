# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [Unreleased]

---

## Overlay Loader

### [1.1.0] - 2026-03-21

#### Fixed
- Fixed `backBuffer_` not being zero-initialized, which could cause crashes when a swap chain buffer fails to acquire.
- Fixed a file handle leak in `LoadTextureFromFile` when the file size query fails.
- Fixed COM object leaks in `createDevice` (adapter and intermediate swap chain not released on all error paths).
- Added a guard in `HeapDescriptorAlloc` to prevent out-of-bounds access when the descriptor pool is exhausted.
- Fixed a thread-safety issue in the plugin config API: value buffers are now `thread_local` to prevent data races.
- Fixed `screenState` returning an inconsistent value when the game address is null.
- Fixed a typo in a plugin loader log message.
- Added TIER 1 device-loss defensive checks for NVIDIA Optimus (hybrid GPU) graceful degradation: the overlay now silently disables itself instead of crashing the game when a D3D12 device-removed event is detected.

### [1.0.0] - 2025-10-20

#### Added
- Overlay functions are now separated into individual DLL modules placed in the `overlays/` folder.
- New module: **Achievements** (`v0.1.0`) — tracks Steam achievement progression.
- New module: **Minimap** (`v0.1.0`) — displays an in-game minimap.
- Each overlay module now carries its own independent version number.
- `EROverlay.ini` has been split into per-module `.ini` files inside the `configs/` folder for flexible configuration.

#### Fixed
- Attempted fix for startup crashes (thanks to [samjviana](https://github.com/samjviana) for the hint in [#8](https://github.com/soarqin/EROverlay/issues/8)).

---

## Boss Overlay

### [1.5.1] - 2025-11-13

#### Fixed
- `Death Count` was always shown as zero when `challenge_mode` was not enabled.

### [1.5.0] - 2025-10-20

#### Changed
- Boss overlay is now a standalone module (`Boss.dll`) loaded by the Overlay Loader.

### [1.4.0] - 2024-10-15

#### Added
- Challenge mode: set `challenge_mode = true` in the `[boss]` section to enable it.
- Display text format can now be customized manually — see `boss.ini` for details.

#### Changed
- Font loading now falls back to system font links when the character set is not Latin or Cyrillic and no font file is explicitly configured.

### [1.3.0] - 2024-07-03

#### Added
- `language` key added to the `[common]` section to select the data language independently of the game language.

#### Changed
- Data folder structure updated to support multiple language data files.
- `data` key in `[boss]` section renamed to `data_file` (now specifies the data filename).
- Boss memory addresses are now resolved by flag ID.

#### Removed
- `boss_display` and `place_display` fields removed from boss data (unused).

#### Fixed
- Mouse cursor remained locked after the mod was unloaded.
- Player location area determination was incorrect in some cases.
- Various boss flag data corrections.

### [1.2.0] - 2024-06-30

#### Added
- New `[style]` section in config to customize overlay panel appearance.
- New `[input]` section in config to customize shortcut keys.
- DLC bosses are excluded from the list when the DLC is not installed.
- Data is now loaded by game language when `data` in `[boss]` is left empty (note: `font` and `charset` in `[common]` must be set manually for non-Latin characters).

#### Changed
- Default font changed to **Open Sans** with a default size of 20 for better readability.
- `charset` in `[common]` now defaults to empty, which auto-selects the charset based on game language.

#### Fixed
- Improved DX12 hooking to reduce crashes and add fullscreen mode support (alt-tab now works, though minor rendering artifacts may remain).
- The mod can no longer be loaded multiple times simultaneously.

### [1.1.3] - 2024-06-24

#### Fixed
- Crash caused by incorrect memory deallocation.

#### Changed
- Switched to [Pattern16](https://github.com/Dasaav-dsv/Pattern16) for faster signature scanning.

### [1.1.2] - 2024-06-23

#### Fixed
- Missing boss: `Divine Beast Dancing Lion` in `Ancient Ruins of Rauh` was not listed.

### [1.1.1] - 2024-06-23

#### Fixed
- Timing logic for applying hooks revised to reduce crash risk.
- Boss data is no longer checked during loading screens or when returning to the main menu.

### [1.1.0] - 2024-06-22

#### Added
- Support for Elden Ring v1.12 with DLC "Shadow of the Erdtree" — 41 new bosses added (all known bosses; some optional bosses may be missing).

#### Fixed
- Minor corrections to existing boss data.

### [1.0.1] - 2024-01-13

#### Added
- `boss.panel_pos` config key to adjust the position of the boss panel.

#### Fixed
- Various bugs in boss data.

### [1.0.0] - 2023-08-27

#### Added
- Initial release.

---

## Minimap Overlay

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
