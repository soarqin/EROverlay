# ELDEN RING Overlay Hook

A Windows DLL overlay mod for Elden Ring that hooks into the DirectX 12 rendering pipeline and renders ImGui-based overlays. It uses a modular plugin architecture — the core loader (`EROverlay.dll`) discovers and loads individual overlay DLLs from the `overlays/` folder at runtime.

**Included overlays:**
- **Boss** — tracks boss kill counts, supports challenge mode and custom display formats
- **Achievements** — tracks Steam achievement progression in real time
- **Minimap** — renders an in-game minimap with configurable size, scale, and shape

> 📋 Changelogs: [Loader](src/CHANGELOG.md) · [Boss](src/boss/CHANGELOG.md) · [Achievements](src/achievements/CHANGELOG.md) · [Minimap](src/minimap/CHANGELOG.md) · 🇨🇳 [中文说明](README_CN.md)

---

## ⚠️ Compatibility Note

Do **not** use this mod alongside overlays such as `FPS Counter` in Nvidia GeForce Experience, `MSI Afterburner`, or `RivaTuner Statistics Server`. They hook the same DirectX APIs and will cause the game to crash.

---

## Installation

1. Edit the `.ini` files in the `configs/` folder to your liking (see [Configuration](#configuration) below).
2. Inject `EROverlay.dll` into Elden Ring using one of the following methods:

   | Method | Instructions |
   |---|---|
   | **Proxy DLL** *(recommended)* | Rename `EROverlay.dll` to `winhttp.dll` and place it next to `eldenring.exe`. Also copy the `configs/` and `data/` folders to the same directory. |
   | **Mod loader** | Load `EROverlay.dll` via [EldenModLoader](https://www.nexusmods.com/eldenring/mods/117), [ModEngine2](https://github.com/soulsmods/ModEngine2), or [me3](https://github.com/garyttierney/me3). |
   | **Injector** | Run the bundled `injector.exe`. *(Not recommended — unstable and may be blocked by security software.)* |

3. Wait a few seconds for the mod to finish loading. Press `=` to toggle between mini and full mode.

The loader maintains backward compatibility — you only need to ensure the Overlay Loader version meets the minimum requirement for any given overlay module.

---

## Configuration

The loader reads INI files from `configs/`. Save edits as UTF-8 and restart the game to apply them. Put comments on separate lines.

`configs/` contains English comments; `configs_CN/` contains Chinese-comment templates with the same filenames, keys and default values. To use Chinese comments, back up any customized file, copy its matching template into `configs/` and reapply your settings. Editing `configs_CN/` alone does not affect the running mod. The comment language does not select the game or data language.

### `common.ini` — Global Settings

| Key | Default | Description |
|---|---|---|
| `console` | `false` | Enable debug console output. |
| `font` | *(empty)* | Path to a font file in the `data/` folder (or an absolute path). Leave empty to use the built-in Latin font or fall back to system fonts for other languages. |
| `font_size` | `20` | Font size in pixels. |
| `charset` | *(empty)* | Character set for font loading (`enUS`, `jaJP`, `koKR`, `zhCN`, `ruRU`, etc.). Leave empty to auto-detect from the game language. |
| `language` | *(empty)* | Language used to load data files. Leave empty to use the game language. |

### `boss.ini` — Boss Overlay

| Key | Default | Description |
|---|---|---|
| `toggle_full_mode` | `=` | Shortcut key to toggle full/mini mode. |
| `data_file` | `bosses.json` | Boss data filename, located in `data/<language>/`. |
| `allow_revive` | `false` | Allow reviving defeated bosses. |
| `panel_pos` | `-10,10,15%,90%` | Panel position and size: `x, y, width, height`. Values can be pixels or percentages; negative x/y are relative to the right/bottom edge. |
| `boss_kill_text` | `{kills}/{total}` | Display format for boss kill count. Supports `{kills}`, `{total}`, `{deaths}`, `{igt}`, `$n` (newline). |
| `challenge_mode` | `false` | Track a personal best within the death limit; exceeding it stops best-score updates while the current kill count still updates. |
| `challenge_death_count` | `0` | Maximum allowed deaths in challenge mode. |
| `challenge_status_text` | `PB: {pb}/{total}  Tries: {tries}$nCurrent: {kills}/{total}` | Display format for challenge mode status. Supports `{kills}`, `{total}`, `{deaths}`, `{igt}`, `{pb}`, `{tries}`, `$n`. |

Revival also restores the arena and phase flags configured for that boss. Travel or reload the area afterwards. Radahn/Redmane and Patches involve shared arena or quest state; see the [revival schema and reverse-engineering evidence](docs/boss-revival.md).

### `achievements.ini` — Achievements Overlay

| Key | Default | Description |
|---|---|---|
| `max_achievements` | `20` | Maximum pending entries; newly unlocked notices may still appear in addition. |
| `panel_pos` | `0,12%,0,60%` | `x, y, maximum width, maximum height`; 0 removes a size limit. Positions use the same anchors as `boss.ini`. |

### `minimap.ini` — Minimap Overlay

The new file groups shortcuts, terrain, markers and borders into sections, with one `[preset.name]` block per display preset. Replace old files with the supplied [minimap.ini](configs/minimap.ini); the previous flat keys and parallel lists are no longer supported. Save as UTF-8 and restart the game after changes.

| Section | What to change |
|---|---|
| `[controls]` | `toggle` / `cycle` default to M; grace/landmark toggles default to N. Empty values disable shortcuts. |
| `[map]` | `full_map = true` shows fully revealed terrain without editing the save. |
| `[markers]` | `graces`, `landmarks`, `death` and `beacons` default to `true`. |
| `[border]` | RGBA `color` and direct pixel `width` (default `1.5`). |
| `[presets]` | `order = compact, large` controls startup and cycling order. |
| `[preset.name]` | Size, centered or edge-margin position, zoom, opacity, rotation, shape, rounding and four element-size multipliers. |
| `[diagnostics]` | Optional `log_file` for resource/map-state troubleshooting. |

By default, M cycles small map → large centered map → hidden. Increasing `zoom` enlarges details and shows less area. See the [complete Minimap configuration guide](src/minimap/README.md) for every option, defaults and preset examples.

### `input.ini` — Global Shortcuts

| Key | Default | Description |
|---|---|---|
| `unload` | *(empty)* | Shortcut key to unload the mod. |

Key names support modifiers with `+` (e.g., `CTRL+SHIFT+[`). See `input.ini` for the full list of available key names.

### `style.ini` — Visual Style

Customizes the ImGui color theme used by all overlay panels. All color values are `R,G,B,A` (0–255 each).

| Key | Description |
|---|---|
| `text_color` | Text color |
| `check_mark_color` | Checkbox check mark color |
| `bg_color` | Panel background color |
| `border_color` | Panel border color |
| `button_color` / `button_hover_color` / `button_press_color` | Button states |
| `node_color` / `node_hover_color` / `node_press_color` | Tree node states |
| `scroll_bg_color` / `scroll_color` / `scroll_hover_color` / `scroll_press_color` | Scrollbar states |
| `border_width` | Border width in pixels |
| `rounding` | Window corner rounding radius |

---

## Documentation

- [Building from source](docs/building.md)
- [Writing an overlay plugin](docs/writing-plugins.md)

---

## License

[MIT License](LICENSE)

---

## Credits

- Based on [ELDEN RING Internal Menu](https://github.com/NightFyre/ELDENRING-INTERNAL)
- [minhook](https://github.com/TsudaKageyu/minhook)
- [imgui](https://github.com/ocornut/imgui)
- [stb](https://github.com/nothings/stb)
- [JSON for Modern C++](https://github.com/nlohmann/json)
- [Pattern16](https://github.com/Dasaav-dsv/Pattern16)
- [Open Sans](https://fonts.google.com/specimen/Open+Sans)
