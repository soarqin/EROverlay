# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

### [1.6.0] - 2026-08-09

#### Added
- Random Run: a seeded race mode. Enter a numeric seed and a boss count in the full-mode panel, press `Randomize`, then press `Reveal bosses` once every player is ready. The selection stays hidden between those two steps so nobody gains planning time by randomizing first.
- Only the drawn bosses are listed while a run is active, and they check off automatically on kill. The in-game time freezes when the last one dies so runs can be compared.
- The same seed and boss count always draw the same list. Only base game bosses are drawn, so seeds are portable between players regardless of DLC ownership.
- Three list layouts selected by `random_layout`: `text`, `list` (boss portraits) and `grid` (card grid), with `random_grid_columns` and `random_image_size` to tune them.
- Optional boss portraits loaded from `data/boss_images/<flag_id>.png` (`.jpg` fallback). Missing images are skipped.
- Run state persists to `RandomRun.txt`, so a run survives restarting the game.

### [1.5.2] - 2026-06-30

#### Fixed
- Boss data now re-queries game memory addresses (`gameDataMan`, `fieldArea`) during `update()` if they were not yet resolved at init time. This fixes boss tracking failing to work when signature scanning runs before the game has fully loaded its code, mirroring the minimap fix.

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
