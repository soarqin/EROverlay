# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

### [2.0.0] - 2026-10-20

#### Added

- Add the optional `getRenderLayer()` plugin export to draw above or below game UI without changing the existing plugin ABI. Plugins without this export, including older Boss and Achievements DLLs, remain above UI.
- Add `readDisplayState()` to the native API to query loading screens, logos and the full-screen map independently of map-resource readiness.

#### Changed

- Replace the DXGI renderer with the game's native `UI_SCENE` render queue, resolving game hooks through signatures and RTTI instead of hooking DXGI and `ExecuteCommandLists`.
- Support the game's RGB10A2 render targets used in SDR and HDR, retaining RGBA8 offscreen textures to preserve alpha precision. Composite lower-layer overlays behind native UI with its transparency preserved.
- Build one ImGui frame across both UI layers, reuse GPU submission slots without per-frame CPU waits, and keep one texture per offscreen context to reduce video memory use.
- Reduce overlay work on the game's render thread: frames with nothing to display submit no GPU commands, font atlas updates no longer wait for the game's submitted GPU work, and controller input is no longer polled.

#### Fixed

- Fix crashes caused by empty or malformed numbers and colors in INI files; the defaults are used instead.
- Fix fonts being loaded again and duplicated after changing resolution or window mode.
- Fix rendering and input recovery after the game recreates its window or swap chain.
- Fix plugin renderer destruction callbacks running before creation or more than once during cleanup; drain active render callbacks before releasing plugins and GPU resources.
- Preserve ImGui draw lists containing only callbacks, including offscreen rendering callbacks, when submitting either UI layer.

#### Upgrade notes

- Static address checks pass for archived game versions 1.05–1.17.1; versions 1.02–1.04.1 no longer render overlays. Static checks do not replace in-game verification for each version.
- `UI_SCENE` is the only renderer. The old `common.renderer` setting is ignored; the DXGI backend and fallback have been removed.
- Update `overlays/Boss.dll` when upgrading from Boss builds that still use ImGui key codes: the earlier virtual-key input API migration changed shortcut-value semantics even though the function-table slot stayed in place.

### [1.4.0] - 2026-10-03

#### Added

- Add `EROverlayNativeAPI` and its retrieval interface, used to deal with game native resources, which is used for `minimap` overlay.

#### Changed

- Shortcuts support Ctrl, Alt, Shift and Win combinations. Multiple overlays can share the same key; shortcuts respond only while the game is in the foreground.

#### Fixed

- Fix ghosting in translucent overlays.
- Fix overlays failing to appear due to incorrect game-window detection.

### [1.3.0] - 2026-06-30

#### Fixed
- Improved D3D12 device-loss recovery: the renderer now detects device-removed/reset events from `Present`, `Present1`, `GetDeviceRemovedReason`, and command allocator/list failures, releases all device resources (including plugin renderers via `pluginsDestroyRenderers()`), and waits for a new device instead of permanently disabling the overlay.
- Stabilized DX12 command queue capture: only direct command-list-type queues are captured, and the `ExecuteCommandLists` hook is now installed/removed independently so it can be re-armed after a queue capture reset. Swap-chain creation hooks capture the queue from the device when present.
- Fixed SRV descriptor heap allocation using the wrong descriptor size (`rtvDescriptorSize_` instead of `srvDescriptorSize_`), and enlarged the SRV heap to 1024 descriptors.
- Hardened hook installation with per-hook error logging and `MH_RemoveHook` cleanup on disable; `hook()` now returns `bool` and the loader retries hooking until it succeeds.
- `screenState()` now reads the stable 16-bit screen state value.
- Added a mutex around plugin load/update/render calls and a `renderersLoaded` flag so `pluginsRender()` no-ops while renderers are being (re)loaded.
- `SetSourceSize` hook signature corrected to use `IDXGISwapChain2`.

### [1.2.0] - 2026-03-23

#### Added
- Offscreen rendering API in plugin interface (`createOffscreen`, `destroyOffscreen`, `beginOffscreen`, `endOffscreen`), enabling plugins to composite content with a single alpha pass.

#### Changed
- Updated dependencies: fmt v12.1.0, ImGui v1.92.6, inih latest HEAD, MinHook latest HEAD.

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
