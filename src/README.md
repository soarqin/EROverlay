# ELDEN RING Overlay Loader

The core loader (`EROverlay.dll`) for ELDEN RING Overlay Hook. It hooks into the game's DirectX 12 rendering pipeline and renders ImGui-based overlays using a modular plugin architecture — discovering and loading individual overlay DLLs from the `overlays/` folder at runtime.

> 🇨🇳 [中文说明](README_CN.md)

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

- [Building from source](../docs/building.md)
- [Writing an overlay plugin](../docs/writing-plugins.md)

---

## License

[MIT License](../LICENSE)

---

## Credits

- Based on [ELDEN RING Internal Menu](https://github.com/NightFyre/ELDENRING-INTERNAL)
- [minhook](https://github.com/TsudaKageyu/minhook)
- [imgui](https://github.com/ocornut/imgui)
- [stb](https://github.com/nothings/stb)
- [JSON for Modern C++](https://github.com/nlohmann/json)
- [Pattern16](https://github.com/Dasaav-dsv/Pattern16)
- [Open Sans](https://fonts.google.com/specimen/Open+Sans)
