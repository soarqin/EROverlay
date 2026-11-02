# Independent clean-source reproduction

Source: `9d623d0f4cc9063524b2538938f7505afdd1569f`, `cindy/gentle-darwin`.

The source was cloned with `--no-local --depth 1 --single-branch --branch cindy/gentle-darwin` into a new ignored directory. The clone had no target output before checks, used its own target directory and default features, and stayed source-clean before and after verification. No original C++ reference or game asset environment was supplied; Cargo registry caching remained available.

All steps passed: workspace fmt, default workspace check, strict workspace/all-targets clippy, workspace tests and root `build.ps1` (fresh workspace Release build plus packaging). Actual totals are **186 passed, 0 failed and 11 ignored**, summed from 19 test-result lines. The actual normal EROverlay/Boss/Minimap DLLs passed **three same-process default harness load/update/render/stop/unload cycles**.

The independently produced package contains **190 manifest entries**. Every size and SHA-256 matches, the initial package file set exactly matches the manifest, every original item is unchanged after the harness and no extra package files were generated. Both `third_party/arc-swap/LICENSE-MIT` and `third_party/rustversion/LICENSE-MIT` were copied and match their committed source bytes. The three normal DLLs have no test/fixture exports.

Exact commands, durations, DLL hashes/export names, package source receipt and license hashes are in `clean-source-reproduction.json`; per-file verification is in `manifest-verification.json`. Original command output is in `clean-*.log`. `reproduce.py` records the procedure and requires a fresh clone target.

The offline default harness has no valid game state and hidden overlays. This is independent source/build/package/default lifecycle evidence, not visible continuity or live-game acceptance. No pixel matrix, game assets, field checks or microbenchmark were rerun. No game installation/injection, production source edits, old-ref changes, commit or push was performed. Clone-path-dependent binary hashes are independent evidence only; they do not replace the parent task's final tested/packaged DLL identities. The 190-item manifest belongs to this independent code package, not a later documentation package.
