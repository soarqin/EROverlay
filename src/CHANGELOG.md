# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

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
