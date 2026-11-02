# Boss input and production DLL probe

The Boss plugin uses the game's existing Win32 window and the version 1 C ABI.
There is no extra window, event loop, ImGui context or Present/DXGI hook in the
production input path. The fixture's hidden message-only window is test-only.

## ABI and input lifetime

`HostApi` keeps its original 176-byte prefix. `CAP_UI_INPUT = 64` adds three
function pointers through `UI_INPUT_HOST_SIZE = 200`: `ui_input_poll`,
`ui_input_clear` and `ui_input_capture`. `CAP_BOSS_STATE = 128` appends Boss state,
batch flags and the explicit revival request through `BOSS_STATE_HOST_SIZE = 224`.
Size/version/capability checks precede reads of each appendix. The original
Minimap binary can use the longer host table. Boss rejects the old short table.
The production DLL probe also initializes and destroys the new sibling
`Minimap.dll` with exactly 176 readable host bytes ending at a `PAGE_NOACCESS`
guard page. The same guarded prefix must be rejected by Boss before extension
reads.

`UiInput` is a 32-byte POD containing `size`, `kind`, `code`, `flags`, physical
client-pixel `x/y`, `value` and `reserved`. It carries pointer movement, five mouse
buttons, horizontal/vertical wheel lines, Win32 virtual keys, Unicode scalar
text, focus and reset. Text commits combine UTF-16 surrogate pairs; control
characters and orphan surrogates are not typed into egui.

The native window callback writes into a fixed 256-event queue using `try_lock`.
Polling is nonblocking. Overflow or contention requests a reset instead of
leaving a held key/button in egui. Reset polling also carries the current focus
snapshot: opening the panel does not cause a new `WM_SETFOCUS` for an already
focused game window.

Capture is explicit: mouse bit 0, keyboard bit 1, zero releases. The full panel
renews a 750 ms lease; the compact panel does not capture. Hiding, loading,
full-screen map, focus loss, renderer reset and plugin destruction release
capture. Lease expiry makes detours pass through even if a render callback
stops renewing. The renderer consumes events once per native sequence.

Window-message suppression is supplemented with DirectInput keyboard/mouse
`GetDeviceState` and `GetDeviceData` interception. Device types are checked so
controller input is preserved. Cursor clipping and game recenter requests are
suppressed only while mouse capture is active. Boss draws a small egui cursor
without changing the game's `ShowCursor` counter.

Input hooks are installed only for native sessions containing a plugin that
requires `CAP_UI_INPUT`, after the existing native bridge pins the core. Offline
normal loading installs none. Stop disables capture before restoring hook
entries and the window-procedure chain where still owned. Trampolines remain
allocated for in-flight callbacks; native `STOPPED_RESIDENT = 1` is preserved.

## Native DLL input fixture

Build the opt-in core fixture from the repository root:

```powershell
cargo build -p eroverlay-core --features native-lifecycle-fixture --locked
```

Load this DLL into an explicitly offline host and call
`eroverlay_test_ui_input()`. For example, the following Python script uses a
fresh process and never opens or injects the game:

```python
import ctypes
import os

os.environ["EROVERLAY_RUST_OFFLINE_HOST"] = "1"
dll = ctypes.WinDLL(os.path.abspath("target/debug/EROverlay.dll"))
assert dll.eroverlay_test_ui_input() == 0
assert dll.eroverlay_stop() == 1
```

The export exists only with `native-lifecycle-fixture`. It first exercises the
native detour lifetime fixture and PIN contract. It then installs the real
DirectInput and Win32 cursor detours and subclasses a real hidden HWND. Actual
`SendMessageW` calls verify pointer/buttons/wheel/key input, Chinese text,
supplementary Unicode, suppression, release, focus loss and restoration of the
original window procedure. The test never acquires foreground focus or moves
the user's cursor.

This proves hook installation and controlled Win32 behavior. It does not prove
that every live game input path is intercepted, or that a particular game's
cooperative input mode behaves identically.

## Production Boss DLL and WARP

The optional probe loads `Boss.dll` and sibling `Minimap.dll` through their real exports and uses Boss's real
init/update/render/reset/destroy callbacks. It does not compile a second Boss
implementation into the host.

```powershell
cargo build -p eroverlay-boss --release --locked
cargo build -p eroverlay-harness --bin boss_probe --release --locked
./target/release/boss_probe.exe ./target/release/Boss.dll ./target/research E:/Projects/EROverlay/build/ida/boss/events
```

The resource directories are explicit optional inputs. The raw PARAM directory
contains `GameAreaParam.param`, `BonfireWarpParam.param`,
`BonfireWarpTabParam.param`, `BonfireWarpSubCategoryParam.param`,
`WorldMapPointParam.param`, `MapDefaultInfoParam.param`, `item_dlc02.bnd` and
`menu_dlc02.bnd`. The EMEVD directory contains uncompressed `.emevd` resources.
Normal production builds do not need these directories or an installed game.

The fixture maps the original PARAM bytes with their native allocation-size
prefix and serves archived EMEVD and FMG binders. Save state, flag values, input,
and write callbacks are controlled fixtures. A successful revival test means
that the UI queued the expected request; it does not mutate a game save.

The host uses a real 1280 × 900 RGB10A2 WARP target and production renderer.
Font atlas copies go through the production upload path, copied meshes go
through its real GPU submission, and target readback produces BMP evidence in
the printed scratch directory. Each frame starts with a cleared black target;
hidden output must contain zero nonblack RGB pixels. D3D12 validation errors
fail the probe. Readback and GPU waits belong to this controlled host, never
the production render callback.

The original pre-scope probe checked a 211-entry static catalogue, flags, short-host rejection,
full/compact switching, search geometry differences, settings persistence on
update, Cancel preserving flags, exactly one explicitly confirmed revival
request, wheel input, full-screen-map/loading/hide capture release, renderer
reset and duplicate-sequence suppression. The test clicks the captured default
English layout; coordinate assertions must change if that layout changes.
The current [207-entry scope](boss-207-scope-2026-11-02.md) and
[strict native-path/loading checks](boss-loading-fix-2026-11-02.md) have separate evidence.

It also reports production DLL render CPU samples for compact/full/hidden and
500 ms update samples. Samples exclude GPU submission/drain and readback; the
update samples use a controlled flag service. Steady samples assert the observed
absence of repeated font uploads and file requests. These measurements are
offline observations, not game FPS, a fixed acceptance threshold, or a fair
same-scene comparison with the C++ renderer. Live visual/input behavior and
same-scene C++ performance remain separate in-game checks.
