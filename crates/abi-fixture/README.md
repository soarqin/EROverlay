# Malformed plugin DLL fixture

This crate exists only for DLL loader integration checks. Do not put it in a
normal installation. Run all commands from the repository root. Build with
`cargo build -p eroverlay-abi-fixture --release`.

Before loading a fresh `BadPlugin.dll` module, set `EROVERLAY_ABI_FIXTURE_MODE`
to one of `null`, `misaligned`, `short`, `version`, `magic`, `capability`,
`missing_init`, `missing_destroy`, `init_failure`, `valid`, or `upper`. The short mode
returns a real 16-byte header allocation to verify that the core reads only the
fixed header before rejecting a small table. `init_failure` allocates an instance
then fails so the loader must call `destroy` before releasing its module handle.
The chosen full-table mode remains fixed while the module is loaded.

The test host can retain an additional `LoadLibrary` reference, then query
`fixture_counts(FixtureCounts*)` after the core accepted/rejected/stopped the
plugin. Counts contain three `u32`s: init, destroy, live. For `init_failure`,
expect `{1,1,0}` immediately after loading. For `valid`, expect `{1,0,1}` while
active and `{1,1,0}` after stop. All rejected header/callback/capability modes
expect `{0,0,0}`. Release the host's extra reference after querying counts.

To produce a DLL without the required export, build this crate separately with
`--features missing-export --target-dir target/fixture-noexport`; its
`fixture_counts` export remains available, while `getEROverlayPluginV1` is absent.

Run all 11 real-DLL cases (fresh child process and isolated staging per case):

```powershell
python crates/abi-fixture/check_rejections.py `
  E:/path/to/EROverlay.dll E:/path/to/Minimap.dll target/release/BadPlugin.dll `
  --missing-export target/fixture-noexport/release/BadPlugin.dll
```

The runner copies only the given new DLLs into temporary directories under
`target`, starts an explicit offline host, and checks rejection counts,
initialization-failure cleanup, valid-plugin destruction, and actual DLL unload.
It never touches the game, an existing installation, or a separate C++ reference checkout.

`upper` is a valid above-UI rendering fixture for the existing standalone WARP
host. It creates one texture through the actual Host API, waits for readiness,
then submits a triangle. Its separate `fixture_render_counts` export reports
four `u32`s (resets, renders, ready draws, texture creations) followed by one
`u64` generation; the original `fixture_counts` layout is unchanged.
Prepare a disposable package with `EROverlay.dll`, `overlays/Minimap.dll`, and
`overlays/BadPlugin.dll`, then run from the repository root:

```powershell
target/release/eroverlay-harness.exe E:/absolute/test-package 3 0 E:/absolute/test-package/overlays/BadPlugin.dll
```

The optional fourth argument preloads this fixture and selects `upper` for the
controlled host process. Alternating above/below viewport sizes must leave its
generation, reset count and texture creation count at one, with a real ready
draw and stable committed resources after warmup. A final actual draw is held
behind a GPU gate, and stop must wait for that work before unloading. Without
the fixture argument, the host retains its unused-layer checks; an offline,
hidden Minimap produces no draw and does not claim GPU-retained shutdown.

Verified on 2026-11-02: both modes passed three real-DLL cycles with D3D12
ERROR/CORRUPTION validation checks. This is controlled WARP coverage, not an
in-game performance measurement or proof of native-hook DLL unmapping.

Verified on 2026-11-01 against the new x64 Release DLLs: all 11 cases passed.
`init_failure` produced `{1,1,0}`; `valid` changed from `{1,0,1}` while active to
`{1,1,0}` after shutdown. These checks cover the actual DLL boundary and owned
instance rollback, rather than only Rust struct layout checks.

The core's automatic DLL-entry thread has a separate refcount regression runner:

```powershell
python crates/abi-fixture/check_autostart.py target/release/EROverlay.dll
```

It starts fresh disposable Python hosts with automatic entry enabled, deliberately
fails native discovery outside the game, and repeats two real load/unload cases:
immediately release the caller's reference before bootstrap runs; or call external
`eroverlay_stop`, retry until `OK`, then immediately release the caller's reference.
Both require the module to disappear after bootstrap cleanup. This covers startup
failure and loader/thread reference lifetime; it does not validate in-game callback
or GPU teardown. The core acquires its bootstrap reference before `CreateThread`
and releases it only with `FreeLibraryAndExitThread` after local cleanup.

External hosts retain their own `LoadLibrary` reference through `stop()==OK` and
then release only that reference. `ERR_BUSY` requires retaining it and retrying.
Disable `input.unload` when controlling lifetime externally: external exports or
`FreeLibrary` must not race key-driven self-unload. The configured key path still
consumes the original injector reference and then the bootstrap reference.

Verified on 2026-11-01 against both the Debug and final Release core: 100 cycles
of each case passed per build (200 cycles per build).
