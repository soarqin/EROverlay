# ELDEN RING Overlay Hook

## Release Notes
* 2025-10-20: `Overlay Loader` v1.0.0
  + Refactorying: overlay functions are separated into modules with individual dlls in `overlays` folder.
    + `Boss` is an overlay module now.
    + New module: `Achievements` to tracking progression of Steam achievements.
    + New module: `Minimap` to show an in-game minimap.
  + Overlays have their individual versions now:
    + `Boss`: v1.5.0
    + `Achievements`: v0.1.0
    + `Minimap`: v0.1.0
  + Separate `EROverlay.ini` into individual `.ini`'s in `configs` folder, to flexibly support overlay modules.
  + Try to fix startup crashes, thanks to [samjviana](https://github.com/samjviana) for hinting in [#8](https://github.com/soarqin/EROverlay/issues/8).
* 2024-10-15: `Boss Overlay` v1.4.0
  + Try to read fonts from system font links, if character sets are not Latin or Cyrillic for current language and font file is not set properly.
  + Can set display text format manually now, check `EROverlay.ini` for details.
  + Add `Challenge mode`, which can be enabled by setting `challenge_mode` to `true` in `[boss]` section.
* 2024-07-03: `Boss Overlay` v1.3.0
  + Change data folder structure, to support multiple data files for different languages in future, and config entries are changed as follows:
    - `data` in `[boss]` section is changed to `data_file`, which indicates data filename.
    - `language` is added to `[common]` section.
  + Remove `boss_display` and `place_display` and in bosses data, as they are useless.
  + Resolve memory address by flag id for boss data now.
  + Fix some boss flag data.
  + Fix a mouse cursor locking issue after the mod is unloaded.
  + Fix a player location area determination issue.
* 2024-06-30: `Boss Overlay` v1.2.0
  + Improved DX12 hooking codes to fix some crashes and support for fullscreen mode (still get some rendering bugs, but at least you can do alt-tab now).
  + DLC bosses are not included in the list if you do not have the DLC installed.
  + Load data by game language, if you set `data` in `[boss]` section to empty. Please be note that you should set `font` and `charset` in `[common]` section manually to display non-latin characters.
  + `charset` in `[common]` section is empty by default now, which leads to use charset related to game language.
  + Add a new section `[style]` in config to customize the style of overlay panels.
  + Add a new section `[input]` in config to customize shortcut keys.
  + Use `Open Sans` as default font, and set default font size to 20 for better readability.
  + Prevent the mod from being loaded multiple times.
* 2024-06-24: `Boss Overlay` v1.1.3
  + Fix a crash caused by wrong memory deallocation.
  + Use [Pattern16](https://github.com/Dasaav-dsv/Pattern16) to improve signature scanning speed.
* 2024-06-23: `Boss Overlay` v1.1.2
  + Add a missing boss: `Divine Beast Dancing Lion` in `Ancient Ruins of Rauh`.
* 2024-06-23: `Boss Overlay` v1.1.1
  + Change codes for checking time to apply hooks, to avoid risk of crash.
  + Do not check boss data while on loading screen or returned to main menu now.
  + Add a note in README about not using with some overlays.
* 2024-06-22: `Boss Overlay` v1.1.0
  + Add support for Elden Ring v1.12 with DLC "Shadow of the Erdtree" with 41 new bosses (all my known bosses, but may be missing some optional bosses). 
  + Minor fixes to original boss data.
* 2024-01-13: `Boss Overlay` v1.0.1
  + Add `boss.panel_pos` to `EROverlay.ini` to adjust the position of boss panel.
  + Fix some bugs in boss data.
* 2023-08-27: `Boss Overlay` v1.0.0
  + Initial release.

## NOTE
* Please do not use this mod with some overlays like `FPS Counter` in `Nvidia GeForce Experience`, `MSI Afterburner`, `RivaTuner Statistics Server`, etc. They may cause the game to crash.
* You can use `EROverlay.dll` to load multiple overlays, I will keep the back compatibility so that you just need to make sure `Overlay Loader` version meets the minimal requirements for certain overlay.

## USAGE
* Modify .ini's inside `configs` folder to your liking.
* Inject the mod to Elden Ring, you can either:
  + Rename `EROverlay.dll` to `winhttp.dll` and put it aside `eldenring.exe` (don't forget folders `configs` and `data`).
  + Load `EROverlay.dll` with any mod loader ([EldenModLoader](https://www.nexusmods.com/eldenring/mods/117), [ModEngine2](https://github.com/soulsmods/ModEngine2) or [me3](https://github.com/garyttierney/me3)).
  + Run `injector.exe` to inject (not recommended, because that this method is not very stable and is blocked by some security softwares).
* Wait few seconds until the mod is fully loaded, press `=` to toggle mini/full mode.

## WRITE AN OVERLAY PLUGIN
* Read code comments in [src/api.h](src/api.h) for details.
* Read codes in [src/bosses/dllmain.cpp](src/bosses/dllmain.cpp) as an example.

## [LICENSE](https://github.com/soarqin/EROverlay/blob/master/LICENSE)

## CREDITS
* modified from [ELDEN RING Internal Menu](https://github.com/NightFyre/ELDENRING-INTERNAL)
* [minhook](https://github.com/TsudaKageyu/minhook)
* [imgui](https://github.com/ocornut/imgui)
* [stb](https://github.com/nothings/stb)
* [JSON for Modern C++](https://github.com/nlohmann/json)
* [inih](https://github.com/benhoyt/inih)
* [Pattern16](https://github.com/Dasaav-dsv/Pattern16)
* [Open Sans](https://fonts.google.com/specimen/Open+Sans)
