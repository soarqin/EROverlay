# AGENTS.md — EROverlay Rust workspace

## Scope and layout

This branch contains the Rust/egui core and Minimap only. `Cargo.toml` and
`Cargo.lock` live at the repository root. `crates/` contains the stable C ABI,
core, game services, Minimap, renderer, harness and ABI fixtures. `configs/` and
`configs_CN/` contain the supported INI templates. `docs/` records design,
parity and validation. See `README.md` and `docs/source-layout.md` for commands.

The C++ application, ImGui, Boss, Achievements, injector, CMake project and old
release scripts belong to the separate C++ history; do not copy them back here.
Pure PE/signature/RTTI scanning is Rust. The native renderer bridge and vendored
MinHook are the minimal platform-specific exception. Optional differential tests
export a pinned C++ commit into ignored `target/` with an explicit tool command;
normal builds must not depend on that export or a sibling checkout.

## Build and validation

Use Windows x64, Rust MSVC 1.99+, Visual Studio C++ Build Tools and Windows SDK.
Run from the repository root:

```powershell
cargo fmt --all -- --check
cargo check --workspace --locked
cargo clippy --workspace --all-targets --locked -- -D warnings
cargo test --workspace --locked
./build.ps1
./target/release/eroverlay-harness.exe ./package 3
```

The `runtime-read-fixture` feature explicitly compiles original C++ readers for
comparison. Set `EROVERLAY_CPP_REFERENCE` to the export produced by
`tools/export_cpp_reference.py` before enabling it or running all-features checks.
The production DLLs do not link those readers. `package/` and `target/` are build
outputs. Do not inject, restart, replace or install into a running user's game.
Offline/WARP checks and archived-EXE scans do not prove live game visual or frame
performance equivalence. Report those evidence limits accurately.

## Runtime contracts

- Preserve `EROverlay.dll` + `overlays/Minimap.dll`, egui and native UI_SCENE only.
  Do not add DXGI/Present hooks or an independent window/event loop.
- Build UI/input only once per game queue. Minimap is below native UI; lower
  alpha stores scene transmittance, with RGB10A2 target and RGBA8 offscreen.
- No old plugin ABI or ImGui compatibility bridge. Cross-DLL values must be
  versioned `repr(C)` POD, explicit lengths, borrowed buffers or owned handles;
  no Rust/egui context, container or trait object may cross the boundary.
- Keep FFI panic barriers, size/version/capability validation and ownership rules.
  Do not block in DllMain. Annotate unsafe invariants.
- Native hooks PIN the core until process exit. Preserve `STOPPED_RESIDENT = 1`;
  do not promise same-process native unload/restart. Offline hosts may unload.
- Rendering must not perform disk IO, decompression, config parsing, whole-image
  scans, blocking update locks or ordinary per-frame GPU waits. Keep bounded
  queues, upload budgets, snapshots, retry backoff and fence-based retirement.
- Keep normal loading and individual tile errors silent on screen. Report real
  metadata/atlas failures, and log update-side failures only on attempts or state
  changes. Do not format/log repeatedly in the render path.
- Compare performance with C++ under the same workload. Absolute milliseconds
  are observations, not a fixed acceptance threshold. Preserve useful tests and
  distinguish inherited bottlenecks from new regressions.

## Change discipline

Use rustfmt, existing Rust conventions and `.clang-format` for the small C/C++
bridge. Prefer small direct changes; preserve configuration and native ABI
semantics. Keep third-party source licenses and notices. Update affected paths,
fixtures, commands and documentation when moving files. Never rely on ignored
old files or cached build output to validate a source-tree change.
