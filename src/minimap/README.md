# Minimap

Display the current area's map, discovered sites of grace and landmarks, dropped runes, and numbered beacons 1–5 placed in the game's world map.

## Install or update

Place `EROverlay.dll`, `overlays/Minimap.dll` and `configs/` in your mod loader directory. Load the core DLL with [EldenModLoader](https://www.nexusmods.com/eldenring/mods/117), [ModEngine2](https://github.com/soulsmods/ModEngine2) or [me3](https://github.com/garyttierney/me3). For proxy loading, rename the core to `winhttp.dll` and place it alongside `eldenring.exe`.

When updating to the sectioned configuration format, back up your old `configs/minimap.ini` and replace it with the new file. Old flat keys and parallel comma-separated setting lists are ignored and are not converted automatically.

The minimap reads game archives and renders its own textures. External `data/map` images are not required. Avoid running it alongside MSI Afterburner, RivaTuner Statistics Server or Nvidia FPS Counter, which may hook the same DirectX interfaces.

## Start with the defaults

- Press **M** to cycle through the upper-right minimap, large centered map and hidden state.
- Press **N** to show or hide grace and landmark markers together.
- Death and numbered beacon markers are enabled, following rune recovery and in-game removal.
- Terrain follows your obtained map fragments.

Edit `configs/minimap.ini` with a text editor, save as UTF-8 and restart the game. The core reads the file when loading; rebuilding the renderer alone does not reload the file. Put comments on separate lines starting with `#` or `;`. Use `true` and `false` for switches.

## Find the setting

| Section | Purpose |
|---|---|
| `[controls]` | Show/hide, preset cycling and marker shortcuts |
| `[map]` | Fully revealed terrain |
| `[markers]` | Four marker visibility switches |
| `[border]` | Border color and pixel width shared by all presets |
| `[presets]` | Startup preset and cycling order |
| `[preset.name]` | One complete size, position, zoom, opacity and shape setup |
| `[diagnostics]` | Optional troubleshooting log |

### Shortcuts

| Key in `[controls]` | Default | Action |
|---|---|---|
| `toggle` | `M` | Show/hide the map; cycles presets when assigned the same key as `cycle` |
| `cycle` | `M` | Switch presets in the configured order |
| `toggle_graces` | `N` | Toggle grace markers |
| `toggle_landmarks` | `N` | Toggle landmark markers |

Use letters, digits, `F1`–`F24` or chords such as `CTRL+M`, `ALT+N` and `CTRL+SHIFT+F8`. Do not put spaces around `+`. See `configs/input.ini` for all key names. An empty value or `none` disables a shortcut.

One key can toggle multiple marker groups. When `toggle` and `cycle` share a nonempty key, a hidden step is appended unless a preset already has `zoom = 0`. Disabled keys do not add a hidden step. Minimap shortcuts respond while the game is in the foreground and its menus are closed.

To use M only for show/hide and F8 for cycling:

~~~ini
[controls]
toggle = M
cycle = F8
~~~

### Terrain and markers

| Section and key | Default | Meaning |
|---|---|---|
| `[map] full_map` | `false` | `true` reveals surface, underground and DLC terrain on the minimap; `false` follows fragment progress |
| `[markers] graces` | `true` | Show discovered graces; the shortcut can change visibility at runtime |
| `[markers] landmarks` | `true` | Show landmarks allowed by the game, including discovered distant-view markers |
| `[markers] death` | `true` | Show uncollected dropped runes on their map layer |
| `[markers] beacons` | `true` | Show world-map beacons 1–5, preserving their numbers |

Fully revealed terrain changes only the minimap display. It does not grant fragments, edit saves or discover graces and landmarks. Place and delete beacons in the game's world map; the minimap follows them.

### Border

`[border] color` takes four integers for red, green, blue and alpha, each from 0 to 255. The default `255, 255, 255, 100` is translucent white. Alpha 0 makes the border invisible. Border alpha is separate from preset `opacity`.

`[border] width` uses pixels directly. The default is `1.5`; use `0` to remove the border. Both settings apply to every preset.

## Edit or add display presets

`[presets] order = compact, large` starts with `compact`, then switches to `large`. Section positions in the file do not determine cycling order. Unlisted presets are unused.

Each preset is independent, with one value per setting. There is no inheritance or need to align multiple lists. The table lists defaults for omitted fields in any preset. The supplied `large` block explicitly sets 90% width/height, centered position, zoom 1.5 and 60% opacity.

| Key in `[preset.name]` | Default if omitted | Meaning |
|---|---|---|
| `width` / `height` | `30%` / `30%` | Relative to the game's 16:9 reference height; `30%` equals `0.3` |
| `position` | `margins` | Position using edge margins; `center` centers the map and ignores all margins |
| `margin_left` / `margin_right` | Right margin `0` if both are unset | Choose one horizontal edge; pixels such as `24` or `24px`, or screen-width percentages such as `2%` |
| `margin_top` / `margin_bottom` | Top margin `0` if both are unset | Choose one vertical edge; pixels such as `24` or `24px`, or screen-height percentages such as `2%` |
| `zoom` | `0.75` | Larger values enlarge terrain and show less area in the same window; `0` hides the preset |
| `opacity` | `80%` | `0%` is transparent, `100%` is opaque; decimals 0–1 also work |
| `rotate` | `false` | Keep camera-forward up, use a circle and show a compass |
| `shape` | `rect` | `rect`, `rounded` or `circle`; circles use the smaller width/height |
| `rounding` | `20%` | Rounded rectangles only: percentage of half the shorter side, or pixels such as `30px` or `30` |
| `map_scale` | `1` | Multiplies zoom, including icons that follow terrain zoom; effective zoom is limited to 0.01–16 |
| `decoration_scale` | `1` | Grace, ordinary landmark and Roundtable icons; area symbols still follow terrain zoom |
| `player_scale` | `1` | Player dot/arrow, death marker and numbered beacons |
| `compass_scale` | `1` | Compass size, visible only when rotation is enabled |

The size reference is `min(screen height, screen width × 9 / 16)`. At 1920×1080, 30% width/height gives 324×324 pixels; 90% gives 972×972 pixels.

A size multiplier of `1` is normal and `1.5` is 150%. Setting `decoration_scale`, `player_scale` or `compass_scale` to 0 hides that icon group. Zoom also affects map-scaled icons; to enlarge only player markers, change `player_scale`.

### Position with margins

Set `position = margins` in the preset you want to change, then choose one horizontal margin and one vertical margin. The corresponding map corner is the anchor. Margins measure its distance from the matching screen edges, and stay the same when the map changes size or becomes a circle.

| Margins to fill | Map anchor |
|---|---|
| `margin_right` + `margin_top` | Upper right |
| `margin_left` + `margin_top` | Upper left |
| `margin_right` + `margin_bottom` | Lower right |
| `margin_left` + `margin_bottom` | Lower left |

`24` or `24px` means 24 pixels. `2%` means 2% of the full screen width or height, independent of the map-size reference above. Horizontal and vertical margins can use different units. Positive values move inward and negative values move outward; portions outside the screen are clipped.

Omit or leave a margin empty to leave that edge unset. `0` explicitly selects the edge with no gap. When both edges on an axis are unset, the horizontal default is right `0` and the vertical default is top `0`. If both have valid values, left or top takes precedence and the console reports the conflict. The configured map dimensions are retained; margins do not stretch it. Invalid margins are treated as unset, so the other edge's valid value remains usable.

For a map 24 pixels from the left and 36 pixels from the bottom, replace the position settings in `[preset.compact]` and leave right/top empty:

~~~ini
position = margins
margin_left = 24px
margin_right =
margin_top =
margin_bottom = 36px
~~~

To return to the upper right, fill `margin_right` and `margin_top` and leave the other edges empty. `position = center` centers the map and ignores every margin.

### Add a preset

For an additional rotating circle 24 pixels from the upper-right edges:

~~~ini
[presets]
order = compact, rotating, large

[preset.rotating]
width = 35%
height = 35%
position = margins
margin_right = 24px
margin_top = 24px
zoom = 0.75
opacity = 90%
rotate = true
shape = circle
player_scale = 1.25
~~~

Merge the new `order` into the existing `[presets]` section, keep `[preset.compact]` and `[preset.large]`, and add `[preset.rotating]`. Do not create a second section with the same name.

Names use up to 64 letters, digits, underscores or hyphens and are case-sensitive. Keep at least one setting in each preset. Remove a name from `order` when deleting its section. Use `order = compact` to keep only the small map. Duplicate names are used once, undefined presets are skipped, and no usable presets restores the two built-in defaults.

To choose where the hidden step occurs, add `off` to `order` and define `[preset.off]` with `zoom = 0`.

## Troubleshooting

Empty or invalid ordinary numbers, shapes and switches use their field defaults; empty or invalid margins are treated as unset. Invalid values report the exact key in the console. Use a decimal point and a single value, rather than the former comma-separated lists. To see configuration errors, set `console = true` in `common.ini` and restart the game.

Set `log_file = D:/Logs/minimap.log` in `[diagnostics]` to record resource requests and map state. Create the parent folder first. Absolute paths are easier to locate; relative paths use the game's working directory. Leave it empty to disable file logging. Configuration errors are reported in the console, not this resource/state log.

| Symptom | Check |
|---|---|
| Changes do not apply | Edit the loader's `configs/minimap.ini`, save, then restart the game |
| Shortcut does not respond | Check for an empty/incorrect key, foreground focus or an open game menu |
| New preset is missing | Match the case of its section name and `order` entry |
| Margins do not match expectations | Use `position = margins` and leave the opposite edge empty on each axis; `0` also selects an edge |
| Map becomes hidden | Check the hidden step, `zoom = 0`, zero dimensions or `opacity = 0%` |
| Zoom feels reversed | Increase zoom for larger details; decrease it to see more area |
| Rounding has no effect | Use `shape = rounded`; rotation automatically selects a circle |

## Game and resource support

Native adapters cover hash-verified historical 1.02–1.17 EXEs and Steam 1.17.1. All supplied old EXEs passed offline address/layout checks; individual old-game visual testing remains pending. Unknown EXEs show a status and disable native calls. See the [compatibility report](../../docs/minimap-version-compatibility.md).

Atlas names and counts come from native GFX/TextureAtlas definitions, including atlas mods. Loading, drawing and retirement were verified with 12 atlases. Resources must be readable through the game's file layer and use supported PC GFX/TPF/DDS formats. Pre-DLC games require only surface/underground resources; M10 and DLC atlases are optional. Resource and texture budgets still apply.

## License and credits

[MIT License](https://github.com/soarqin/EROverlay/blob/master/LICENSE). Uses [ELDEN RING Internal Menu](https://github.com/NightFyre/ELDENRING-INTERNAL), [minhook](https://github.com/TsudaKageyu/minhook), [ImGui](https://github.com/ocornut/imgui), [stb](https://github.com/nothings/stb), [JSON for Modern C++](https://github.com/nlohmann/json) and [Pattern16](https://github.com/Dasaav-dsv/Pattern16).
