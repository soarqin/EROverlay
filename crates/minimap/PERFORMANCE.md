# Minimap CPU probe

Measured on 2026-11-02 with an Intel Core i9-13900KF, Windows x64, Rust 1.99.1, release ThinLTO and pinned egui 0.33.3. The machine reports an NVIDIA GeForce RTX 4080, but this probe does not execute GPU work.

These measurements predate the repository-root migration. C++ references describe `master` at `afab7e1` or the separately identified historical Rust package, not sources retained on this branch. Current commands run from the repository root.

The opt-in test uses the production GFX/TextureAtlas parser, native recipe drawing, egui frame/tessellation and POD vertex conversion. Already triangulated egui meshes submit directly; non-mesh runs pass through egui's tessellator in their original order. A counting ABI sink receives meshes; it does not emulate D3D12. Positions and the four terrain-shaped quads are controlled synthetic geometry using archived native artwork. It excludes game memory reads, archive I/O, update-side marker collection, core staging copies, actual GPU uploads/composition and game rendering. Therefore these figures must not be compared with the existing C++ resource-cache microbenchmark as if they measured the same complete pipeline.

1920×1080, 100 warm-up frames and 1,000 measured frames per scenario, one test thread:

| Scenario | Markers | CPU p50 / p95 / p99 (µs) | Allocations / frame | Allocated bytes / frame | Meshes / frame | Vertices / indices / frame |
|---|---:|---:|---:|---:|---:|---:|
| Hidden | 0 | 0 / 0.1 / 0.1 | 0 | 0 | 0 | 0 / 0 |
| Stable compact | 117 | 43.2 / 47.5 / 63.0 | 427 | 213,708 | 81 | 3,645 / 9,381 |
| Large, rotated, alpha 0.6 | 117 | 42.3 / 45.3 / 57.7 | 427 | 213,708 | 81 | 3,649 / 9,387 |
| Large, many markers | 1,000 | 344.8 / 391.0 / 479.5 | 3,505 | 1,743,060 | 672 | 30,559 / 78,759 |

All measured steady scenarios queued zero texture uploads after warm-up. Allocation counts include allocations and reallocations on the measuring thread; bytes are total requested bytes, not live/resident memory. OS scheduling and concurrent development activity affect timings. These are observations, not game-frame acceptance limits or FPS claims.

The archived evidence decoded 118 native recipes, 148 resolved texture regions and ten stamp frames. Its four BC7 atlas DDS payloads total 7,340,624 bytes. Their dimensions are 1024×1024, 2048×1024, 1024×2048 and 1024×2048. Actual GPU allocation and fence-delayed residency must be measured through the renderer harness.

Separate production-path regressions verify that a repeated native frame sequence advances the private egui context only once, and native/provider texture preparation shares a single four-image / approximately 4 MiB allowance. These checks are correctness evidence, not additional timing measurements. The probe does not exercise live hot reload; controlled unload/reload belongs to the core lifecycle harness.

The wait/stutter repair also tests actual OS-backed marker collection and the original C++ game readers. Its timings and syscall counts are recorded separately in [the runtime read review](../../docs/runtime-read-review.md); this draw-only table does not include those costs.

## Reproduce

Provide legally owned, decompressed asset fixtures outside source control:

```powershell
$env:EROVERLAY_GFX='E:/Projects/EROverlay/build/ida/sprite-probe/run-24636-34293203/worldmap.gfx'
$env:EROVERLAY_LAYOUTS='E:/Projects/EROverlay/build/ida/probes/run-26836-32694765/common-layouts.bin'
$env:EROVERLAY_TPF='E:/Projects/EROverlay/build/ida/probes/run-26836-32694765/common-atlas.tpf'
cargo test --release -p eroverlay-minimap archived -- --ignored --nocapture --test-threads=1
```

The first ignored test verifies the real recipe/atlas relationship. The second reports production egui CPU costs and asserts hidden frames allocate/draw nothing, steady frames create no textures and these workloads do not truncate geometry at the ABI frame limits.

## Outstanding performance acceptance

Record the complete C++ and Rust+egui pipelines on the same hardware, map, resolution, marker count and sample duration. Include core/update/render CPU p50/p95/p99, GPU timestamp p50/p95/p99, allocations, draw calls, upload volume, active/retired residency and context-switch/load behavior. Cover hidden, stable, large/rotating/transparent, surface/underground/DLC switching, fragments, marker changes and a large marker load. The probe above supplies partial CPU evidence only; it cannot establish the user's whole-pipeline performance requirement by itself.
