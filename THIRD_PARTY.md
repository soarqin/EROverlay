# Third-party components

The project source is MIT licensed; see [LICENSE](LICENSE).

## Vendored native code

`third_party/minhook/` contains only the MinHook implementation used by the
UI_SCENE bridge: `buffer.c`, `hook.c`, `trampoline.c`, `hde64.c`, their headers,
README, authors and license. It is preserved byte-for-byte from the EROverlay
C++ baseline `afab7e14db2b62ae0bede980d5d0c96634321350` (`deps/minhook/`).
Upstream: https://github.com/TsudaKageyu/minhook. MinHook and HDE notices are in
[LICENSE.txt](third_party/minhook/LICENSE.txt); distribute that notice with DLLs.
No ImGui, Pattern16, fmt, nlohmann/json, bcdec or Steamworks SDK is vendored here.

`crates/renderer/native/scene.cpp` and `scene.hpp` are project-owned platform
bridges for MinHook callbacks, native queue boundaries and module retention.
They do not implement game signature discovery, assets or overlay UI.
`checks.inc` is an offline lifecycle fixture. `crates/game/native/read_checks.cpp`
and `tools/cpp_render_perf.cpp` are optional comparison fixtures: they use C++
reference sources only from an explicit ignored export, never a production build.

## Rust dependencies and public ABI

Cargo dependencies, including egui/epaint, windows, texture2ddecoder and cc, are
pinned in [Cargo.lock](Cargo.lock). Minimap and Boss each embed only Ubuntu Light from
`epaint_default_fonts`; the full egui default font collection is disabled.
The upstream font collection's MIT, OFL-1.1 and Ubuntu-font-1.0 notices are retained under
[font-licenses](third_party/font-licenses/) and copied into the installation package.
Their source packages retain their own license
files and Cargo license metadata; they are not copied from the legacy project.

Renderer uses `arc-swap = 1.9.3` for safe publication and reclamation of immutable
retained images. Its only added transitive dependency is the compile-time
`rustversion = 1.0.23`. Both offer MIT OR Apache-2.0; this distribution uses their
MIT licenses, copied unchanged from those pinned source packages under
[`third_party/arc-swap`](third_party/arc-swap/LICENSE-MIT) and
[`third_party/rustversion`](third_party/rustversion/LICENSE-MIT).

[MapForGoblins ABI](docs/api/mfg_map_api.h) is an MIT public C interface declaration
retained for interoperation documentation, not the old Minimap implementation.
Game executables and game assets are not distributed.
