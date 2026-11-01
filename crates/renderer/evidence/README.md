# Offline measurements

Recorded on 2026-11-01 on this development computer:

These are historical measurements from before the workspace moved to the
repository root. Source paths in raw JSONL remain unchanged; the commands below
use the current layout. They do not validate the newly translated Rust scanners.

- Intel Core i9-13900KF, NVIDIA GeForce RTX 4080.
- NVIDIA Windows driver `32.0.16.2101`.
- Windows 11 Pro build `26300`.
- Rust `1.99.1`, MSVC `14.51.36231`, workspace Release profile (thin LTO).
- 1920 × 1080 RGBA8 target, no validation layer in the performance program.
- Hardware runs: 32 warm-up frames and 600 measured calls per scenario.
- WARP run: 32 warm-up frames and 100 measured calls per scenario.

Each JSONL file records its actual adapter, frontend scope, elapsed sample-batch
wall time, CPU/GPU percentiles, Rust allocations, draw calls, upload payload and
peak committed backend resources. GPU timestamps were enabled for these files.
`rtx4080-renderer.jsonl` measures the backend alone;
`rtx4080-egui-renderer.jsonl` adds per-quad egui shapes;
`rtx4080-egui-batched-renderer.jsonl` uses a batched egui mesh.
`warp-renderer.jsonl` is software rendering and is not a hardware performance
substitute. Background development/build activity was not excluded from these
exploratory runs. The [parent performance report](../../../docs/performance.md)
records separate serial matched C++/Rust comparison runs against the external
`master` reference at `afab7e1`.

The upload limits were subsequently restored to four images/about 4 MiB per
native queue sequence and eight images/16 MiB of aggregate in-flight staging,
with an idle-only oversized-image exception. Stable/hidden/large/resize records
contain no measured uploads, so this change does not alter their workload.
The archived upload-burst fixture submits only one 1 MiB image every 30 calls and
drains outside each call; it stays below both sets of limits and does not measure
backpressure. Blocked-queue WARP tests now exercise the actual count, aligned-byte,
shared-sequence and oversized-image limits; these synthetic performance records
must not be used as evidence of burst-load bounds.

The diagnostic drains the queue outside every timed call. These short synthetic
batches do not reproduce sixty-fps game pacing, game/overlay GPU contention,
native callbacks, DLL plugin dispatch, asset/update workers or the actual Minimap
workload. Rust allocation counters do not include driver/C allocations. Committed
D3D12 resources are not process VRAM residency. Do not interpret these numbers as
an end-to-end performance acceptance result.

For a CPU-only comparator with the C++ RGB10A2 fixture, use:

```powershell
cargo run -p eroverlay-renderer --release --example gpu_perf -- --samples 600 --egui-batched --hdr --no-gpu-timing
```

`--no-gpu-timing` reports GPU percentiles as `null`; use the default separate
timestamp-enabled run for GPU measurements.

Native stop-lifecycle evidence recorded on 2026-11-02 is described in
[`native-stop-lifecycle.md`](native-stop-lifecycle.md). It confirms the pre-lock
and post-unlock detour race and the process-lifetime module-retention policy;
it does not claim successful real-game DLL unmapping.

The 2026-11-02 actual-DLL empty-layer regression and low-frequency production
diagnostics check are recorded in
[`empty-layer-runtime.md`](empty-layer-runtime.md). They demonstrate a fixed
recreation trigger with a controlled alternating-viewport fixture; they do not
establish the user's live-game frame cost or the only cause of resource waits.
