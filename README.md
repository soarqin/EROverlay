# ELDEN RING Overlay Hook

A Windows DLL overlay mod for Elden Ring that hooks into the DirectX 12 rendering pipeline and renders ImGui-based overlays. It uses a modular plugin architecture — the core loader (`EROverlay.dll`) discovers and loads individual overlay DLLs from the `overlays/` folder at runtime.

**Included overlays:**
- **Boss** — tracks boss kill counts, supports challenge mode and custom display formats
- **Achievements** — tracks Steam achievement progression in real time
- **Minimap** — renders an in-game minimap with configurable size, scale, and shape

> 📋 [Changelog](CHANGELOG.md) · 🇨🇳 [中文说明](README_CN.md)

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

All configuration files are located in the `configs/` folder. Edit them with any text editor before launching the game.

### `common.ini` — Global Settings

| Key | Default | Description |
|---|---|---|
| `console` | `false` | Enable debug console output. |
| `font` | *(empty)* | Path to a font file in the `data/` folder (or an absolute path). Leave empty to use the built-in Latin font or fall back to system fonts for other languages. |
| `font_size` | `20` | Font size in points. |
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
| `challenge_mode` | `false` | Enable challenge mode — stops recording kills if death count exceeds the allowed value. |
| `challenge_death_count` | `0` | Maximum allowed deaths in challenge mode. |
| `challenge_status_text` | `PB: {pb}/{total}  Tries: {tries}$nCurrent: {kills}/{total}` | Display format for challenge mode status. Supports `{kills}`, `{total}`, `{deaths}`, `{igt}`, `{pb}`, `{tries}`, `$n`. |

### `achievements.ini` — Achievements Overlay

| Key | Default | Description |
|---|---|---|
| `max_achievements` | `20` | Maximum number of achievements to display at once. |
| `panel_pos` | `0,12%,0,60%` | Panel position and size (same format as `boss.ini`). |

### `minimap.ini` — Minimap Overlay

| Key | Default | Description |
|---|---|---|
| `toggle_key` | `M` | Key to show/hide the minimap. |
| `scale_key` | `M` | Key to cycle through scale/ratio/alpha presets. If equal to `toggle_key`, an extra hidden state is added to the cycle. |
| `graces_key` | `N` | Key to toggle grace markers on the minimap. |
| `graces` | `1` | Show grace markers by default (`1` = yes, `0` = no). |
| `landmarks_key` | `N` | Key to toggle landmark markers on the minimap. |
| `landmarks` | `1` | Show landmark markers by default (`1` = yes, `0` = no). |
| `width_ratio` | `30%,90%` | Comma-separated list of minimap width ratios (relative to screen height) for each scale state. |
| `height_ratio` | `30%,90%` | Comma-separated list of minimap height ratios for each scale state. |
| `scale` | `0.75,+1.5` | Comma-separated scale values. A leading `+` centers the minimap on screen; `0` hides it. |
| `alpha` | `0.8,0.6` | Comma-separated opacity values (0.0–1.0) for each scale state. |
| `rotate` | `0` | Set `1` to rotate the minimap to match the player's facing direction (forces circle shape). Comma-separated per scale state. |
| `shape` | `rect` | Minimap shape per state: `rect`, `rounded`, or `circle`. |
| `rounding` | `20%` | Corner radius for `rounded` shape (percentage of half the shorter side, or pixels). |
| `border_color` | `255,255,255,100` | Border color as `R,G,B,A` (0–255 each). |
| `border_width_x10` | `15` | Border width multiplied by 10 (e.g., `15` = 1.5 px). Set to `0` to disable. |

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
- [inih](https://github.com/benhoyt/inih)
- [Pattern16](https://github.com/Dasaav-dsv/Pattern16)
- [Open Sans](https://fonts.google.com/specimen/Open+Sans)
