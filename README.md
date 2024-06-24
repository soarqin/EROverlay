# ELDEN RING Overlay Hook

## Release Notes
* 2024-06-24: v1.1.3
  + Fix a crash caused by wrong memory deallocation.
  + Use [Pattern16](https://github.com/Dasaav-dsv/Pattern16) to improve signature scanning speed.
* 2024-06-23: v1.1.2
  + Add a missing boss: `Divine Beast Dancing Lion` in `Ancient Ruins of Rauh`.
* 2024-06-23: v1.1.1
  + Change codes for checking time to apply hooks, to avoid risk of crash.
  + Do not check boss data while on loadin~~g~~ screen or returned to main menu now.
  + Add a note in README about not using with some overlays.
* 2024-06-22: v1.1.0
  + Add support for Elden Ring v1.12 with DLC "Shadow of the Erdtree" with 41 new bosses (all my known bosses, but may be missing some optional bosses). 
  + Minor fixes to original boss data.
* 2024-01-13: v1.0.1
  + Add `boss.panel_pos` to `EROverlay.ini` to adjust the position of boss panel.
  + Fix some bugs in boss data.
* 2023-08-27: v1.0.0
  + Initial release.

## NOTE
* Please do not use this mod with some overlays like `FPS Counter` in `Nvidia GeForce Experience`, `MSI Afterburner`, `RivaTuner Statistics Server`, etc. They may cause the game to crash.

## USAGE
* Modify `EROverlay.ini` to your liking.
* Inject the mod to Elden Ring, you can either:
  + Load `EROverlay.dll` with any mod loader ([EldenModLoader](https://www.nexusmods.com/eldenring/mods/117) or [ModEngine2](https://github.com/soulsmods/ModEngine2)).
  + Run `injector.exe` to inject.
* Wait few seconds until the mod is fully loaded, press `=` to toggle mini/full mode, press `-` to unload the mod.

## [LICENSE](https://github.com/soarqin/EROverlay/blob/master/LICENSE)

## CREDITS
* modified from [ELDEN RING Internal Menu](https://github.com/NightFyre/ELDENRING-INTERNAL)
* [minhook](https://github.com/TsudaKageyu/minhook)
* [imgui](https://github.com/ocornut/imgui)
* [stb](https://github.com/nothings/stb)
* [JSON for Modern C++](https://github.com/nlohmann/json)
* [inih](https://github.com/benhoyt/inih)
* [Pattern16](https://github.com/Dasaav-dsv/Pattern16)
