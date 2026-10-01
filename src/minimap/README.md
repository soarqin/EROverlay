# ELDEN RING Overlay Hook

## NOTE
* Please do not use this mod with some overlays like `FPS Counter` in `Nvidia GeForce Experience`, `MSI Afterburner`, `RivaTuner Statistics Server`, etc. They may cause the game to crash.
* You can use `EROverlay.dll` to load multiple overlays, I will keep the back compatibility so that you just need to make sure `Overlay Loader` version meets the minimal requirements for certain overlay.

## USAGE

The minimap reads tiles and sprites from the game's archives and creates its own textures. External `data/map` files are no longer required. Native loading currently supports the verified Steam 1.17.1 executable; a hash mismatch disables game calls and shows a status message.

Dropped runes use the native death marker, enabled by default. Set `death_marker=0` in `minimap.ini` to hide it.

Numbered beacons placed in the game's world map appear as native downward arrows with digits 1–5. Positions, removal and map layers follow the game; arrows and digits stay upright on a rotating minimap. Set `player_markers=0` to hide them.

Set `full_map=1` to show all map-fragment terrain on the minimap, including the surface, underground and DLC. The default is `0`; omitting the key also follows actual fragment progress. This only changes the minimap display and does not modify the save. Grace and landmark markers still follow in-game discovery. Reload the plugin after changing the configuration.

* Modify `minimap.ini` inside `configs` folder to your liking.
* Inject the mod to Elden Ring, you can either:
  + Rename `EROverlay.dll` to `winhttp.dll` and put it beside `eldenring.exe`, along with `configs` and the `overlays` folder containing `Minimap.dll`.
  + Load `EROverlay.dll` with any mod loader ([EldenModLoader](https://www.nexusmods.com/eldenring/mods/117), [ModEngine2](https://github.com/soulsmods/ModEngine2) or [me3](https://github.com/garyttierney/me3)).
  + Run `injector.exe` to inject (not recommended, because that this method is not very stable and is blocked by some security softwares).

## [LICENSE](https://github.com/soarqin/EROverlay/blob/master/LICENSE)

## CREDITS
* modified from [ELDEN RING Internal Menu](https://github.com/NightFyre/ELDENRING-INTERNAL)
* [minhook](https://github.com/TsudaKageyu/minhook)
* [imgui](https://github.com/ocornut/imgui)
* [stb](https://github.com/nothings/stb)
* [JSON for Modern C++](https://github.com/nlohmann/json)
* [Pattern16](https://github.com/Dasaav-dsv/Pattern16)
