# Retained AboveUI presentation evidence

The renderer keeps the last complete image for each nonzero AboveUI plugin group. A missing new atlas or a busy four-slot geometry ring can therefore composite the previous complete image into the current callback target. Empty successful paint, viewport mismatch, explicit reset and hide invalidate it. Native ABI and target borrowing are unchanged.

The independent presentation ring has 16 allocator/list pairs and no geometry buffers. It never resets or re-executes in-flight storage, never waits in rendering, and never retains the game RTV. Image resources and immutable descriptors retire only after both the main-copy fence and presentation fence complete. Presenter lock contention or a completely occupied presentation ring can still drop a present. Shutdown covers both fences; a wedged GPU retains bounded in-flight storage rather than releasing it unsafely.

## Checks

Default and `native-scene-fixture` check/clippy, renderer fmt, default tests (13 passed, 1 ignored) and fixture tests (14 passed, 1 ignored) passed. `strict-checks.log`, `renderer-default-tests.log` and `renderer-fixture-tests.log` contain the commands/results. The archived EXE scanner remains opt-in and ignored.

## Actual WARP pixels and lifetime

`retained-pixels.log` records two new tests. A missing new atlas leaves a freshly cleared target red on the baseline path and green with the previous complete image. An artificially blocked GPU fills all four geometry slots: 17 independently cleared/read-back presents are all red without retention, while retention yields 16 green frames followed by one red frame at the explicit presentation-ring limit. There are zero render wait events. A second test invalidates an image while copy and replay are separately blocked: copy completion alone does not release it; replay completion permits retirement. Hide and resize invalidate, and the D3D12 debug layer reports no errors/corruption.

This deliberately exposes the hard bound. It is not unlimited continuity and is not live game or two-plugin integration acceptance; those are separate parent-task checks.

## Normal frame copy cost

The derived benchmark uses the existing renderer example with AboveUI, RGB10A2, 1920x1080 full-viewport and 64 stable quads. Release RTX 4080, 32 warmups and 300 samples per invocation, three baseline/retained pairs; round two reverses execution order. The queue is drained outside timing. It includes the normal extra CopyResource and cache bookkeeping, not fallback work or pool construction.

Across all three pairs GPU p50/p95 increases from 21.504 to 34.816 us (+13.312 us). CPU p50 medians are 19.4 and 22.6 us; raw CPU p95 varies, including a 50.0 us retained run. Both modes have zero measured Rust allocations, zero drops, zero uploads and two draws per frame. One full-viewport image adds 8,388,608 committed resource bytes (8 MiB); each copy payload is 8,294,400 bytes. Allocator/list/descriptor driver allocations are not included in resource-byte accounting. No fixed CPU affinity was applied.

Run `./target/boss-presentation-final/run-copy-perf.ps1` to reproduce the ignored standalone derivation. Raw per-pair JSONL and exact source hashes are collected in `renderer-evidence.json`. These numbers are renderer-only and do not establish live-game FPS or C++ performance parity.
