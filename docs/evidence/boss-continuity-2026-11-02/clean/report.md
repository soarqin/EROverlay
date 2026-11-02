# Independent clean-source reproduction

- Source: `0819f40da74f8fcaf916a1ba62d9b70f60e4e913`, `cindy/gentle-darwin`.
- Clone: `git clone --no-local --depth 1 --single-branch --branch cindy/gentle-darwin`; only committed content, shallow history, new clone-owned `target` directory. Source was clean before and after all checks.
- Environment: no `EROVERLAY_CPP_REFERENCE`, Boss resource inputs, fixture features, or shared `CARGO_TARGET_DIR`; normal Cargo registry cache remains available.
- Passed: workspace fmt, default workspace check, strict workspace/all-targets clippy, workspace tests, and root `build.ps1` (including a fresh workspace Release build).
- Actual test total: **183 passed, 0 failed, 11 ignored**, summed from 19 result lines in `clean-test.log`.
- Default production core and the actual Boss/Minimap DLLs passed **three same-process load/update/render/stop/unload cycles** in the default harness. No additional fixture DLL was supplied.
- Package manifest: **133 entries**, every byte count and SHA-256 verified; exact package file set matched before harness, all original entries stayed unchanged after harness, no extra package files were created.
- Normal DLL exports contain no test/fixture controls. Core exports `DllMain`, `eroverlay_render`, `eroverlay_start`, `eroverlay_stats`, `eroverlay_stop`; each overlay exports only `getEROverlayPluginV1`.

Commands, durations, DLL hashes and all result metadata are in `clean-source-reproduction.json`. Individual command output is in `clean-*.log`; every manifest entry is recorded in `manifest-verification.json`. `reproduce.py` preserves the executed procedure; it intentionally requires a fresh clone target before running.

The default offline harness has unavailable game state and hidden overlays. This proves independent build/package/default-DLL lifecycle reproduction, not visible overlap continuity or live-game acceptance. No game was started, installed into or injected. No archived assets, visible continuity matrix or microbenchmark was rerun. Clone paths may change binary bytes: these DLL hashes are independent evidence and do not replace the parent task's final packaged DLL identity. No production source or existing refs were modified.
