# ELDEN RING Overlay Hook

## Release Notes
* 2025-10-20: `Overlay Loader` v1.0.0 + `Minimap Overlay` v0.1.0
  + Initial release.

## NOTE
* Please do not use this mod with some overlays like `FPS Counter` in `Nvidia GeForce Experience`, `MSI Afterburner`, `RivaTuner Statistics Server`, etc. They may cause the game to crash.
* You can use `EROverlay.dll` to load multiple overlays, I will keep the back compatibility so that you just need to make sure `Overlay Loader` version meets the minimal requirements for certain overlay.

## USAGE
* Modify `minimap.ini` inside `configs` folder to your liking.
* Inject the mod to Elden Ring, you can either:
  + Rename `EROverlay.dll` to `winhttp.dll` and put it aside `eldenring.exe` (don't forget folders `configs` and `data`).
  + Load `EROverlay.dll` with any mod loader ([EldenModLoader](https://www.nexusmods.com/eldenring/mods/117), [ModEngine2](https://github.com/soulsmods/ModEngine2) or [me3](https://github.com/garyttierney/me3)).
  + Run `injector.exe` to inject (not recommended, because that this method is not very stable and is blocked by some security softwares).

## [LICENSE](https://github.com/soarqin/EROverlay/blob/master/LICENSE)

## CREDITS
* modified from [ELDEN RING Internal Menu](https://github.com/NightFyre/ELDENRING-INTERNAL)
* [minhook](https://github.com/TsudaKageyu/minhook)
* [imgui](https://github.com/ocornut/imgui)
* [stb](https://github.com/nothings/stb)
* [JSON for Modern C++](https://github.com/nlohmann/json)
* [inih](https://github.com/benhoyt/inih)
* [Pattern16](https://github.com/Dasaav-dsv/Pattern16)
