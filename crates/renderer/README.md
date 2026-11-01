# Native DX12 backend

`Renderer` is a Rust DX12 mesh renderer using the locked `windows = 0.62.2`
bindings. It accepts only the core-owned C ABI mesh views; no egui object or Rust
allocation crosses a DLL boundary. `NativeFrame` borrows the game's DIRECT queue,
device and render-target descriptor for one synchronous UI_SCENE callback. The
renderer keeps its own device/queue COM references and never retains a game target
resource. There is no window, event loop, DXGI hook or Present hook.

The small C++ adapter in `native/scene.cpp` covers the engine's native UI_SCENE
queue ABI, safe process-memory reads and MinHook. `scene_scan.rs` validates the
UI-specific rules using the Rust game library's shared PE/signature/RTTI view.
The adapter copies only the validated 60-byte address record and module base;
it has no old C++ utility headers or Pattern16 dependency. The checked-in C
files in `third_party/minhook` are linked
directly. None of the old C++ renderer,
ImGui backend, plugin loader, asset loader or configuration implementation is
linked. Native bridge exceptions are caught at `noexcept` C exports; Rust callbacks
must catch their own panics. The native stop gate drains synchronous callbacks
before their context is released. It does not prove that every thread has left
the detour's entry/return code. Before enabling either native hook, the adapter
pins its owning module with `GetModuleHandleExW(FROM_ADDRESS | PIN)`. That module
remains loaded until process exit, including after partial-install rollback.
`NativeBridge::requires_module_retention()` exposes this permanent state to core.
Stopping a native session releases callbacks and renderer/plugin resources but
does not provide complete DLL unmapping or hot replacement. Offline sessions
that never enable native hooks do not acquire this pin.

Every created hook is recorded before enabling it. Failed installation rolls
back through the same stop path. If patch restoration fails, the bridge first
stops accepting callbacks and drains its user-state accesses, then retains its
small inert native bridge/trampolines; it does not free reachable hook storage or
fall back into an entry that might still be patched.

## Rendering and ownership

- Four independent command allocators/lists use persistently mapped, bounded
  vertex/index buffers. Each starts with 8,192 vertices and 16,384 indices and
  grows by doubling only after its own fence completes; stable frames reuse it.
  A busy fence slot is skipped, never waited on by `render`.
- Each slot has a reusable RGBA8 offscreen target sized to the group's clipped
  region. It is resized only after that slot completes. The shader handles group
  opacity, circular/rounded clipping and an independently opaque inside border.
- Premultiplied sRGBA geometry and RGBA textures use ONE / INV_SRC_ALPHA blending.
  The core worker premultiplies straight RGBA input. DDS keeps original
  BC1/BC2/BC3/BC7/RGBA formats and every mip, with a per-texture straight-alpha
  sampling flag; it is never expanded to RGBA on the render thread. DX10 DDS
  premultiplied-alpha metadata is honored. `PreparedTexture::from_dds` runs on the
  worker, then `upload_prepared` moves the validated bytes without parsing.
  Font atlas patches are assembled by the plugin before full-image replacement.
- Below-UI composition preserves the game's contract:
  `ui.rgb += overlay.rgb * ui.a; ui.a *= 1 - overlay.a`. Above-UI composition uses
  ordinary premultiplied blending. Both RGBA8 and RGB10A2 target pipelines exist;
  offscreen alpha retains eight bits independently of the HDR target.
- The texture table has 768 user entries plus built-in white texture 0, a 64 MiB
  queued CPU-source ceiling, a 32 MiB single-source ceiling, a 512 MiB
  committed GPU-texture ceiling, and 2,048 descriptors. Replacements allocate a
  fresh descriptor and retire the old resource/descriptor behind its draw fence.
  Dirty IDs use a work queue; stable frames do not scan the texture table. Source
  image bytes are released as soon as copied into fence-owned upload staging.
- Upload work follows the [documented resource budget](../../docs/adr-0001.md): at most four images and
  approximately 4 MiB of source bytes per game queue sequence, shared across
  below/above-UI callbacks and plugin groups. Four slots share at most eight
  in-flight images and a 16 MiB committed upload-staging target, including row
  padding and heap alignment. A single source larger than the normal batch
  budget (up to 32 MiB) may proceed only when all uploads are idle; it remains
  alone until its fence completes. Budget pressure defers work without waiting.
  There is no separate copy-queue flush or ordinary-frame GPU/CPU wait. Core-local
  `upload_rgba_owned` moves decoded pixels into the renderer without a frame copy.
- Hidden, clean frames do not reset allocators, submit command lists, touch the
  target, or allocate. Completed retired resources and upload staging are still
  released while hidden; once clean, even the fence query is skipped.
  Core/plugin work before this call is measured separately.
- Viewport dimensions belong to each native callback, not renderer identity.
  The core retains the same device/queue renderer and plugin texture generation
  across per-layer viewport differences and resizes. After its fence completes,
  an individual slot can resize its group target without resetting textures.
- `wait_idle(0)` only reads the completed fence value; it never creates an event
  or registers a completion notification. Rebuild polling is nonblocking.
  Positive-timeout waits are only for shutdown and explicit diagnostics. A failed render is
  poisoned and must be rebuilt by the owner after callbacks are excluded. Drop
  waits at most five seconds; if a GPU remains wedged, live bounded resources are
  deliberately leaked until process exit rather than freed under executing work.

`RenderStats.resident_bytes` and `committed_bytes()` describe committed D3D12
resource bytes, including vertex/index buffers, in-flight uploads, offscreen
targets and retired textures. They are not DXGI process residency/VRAM samples.

## Verification and measurement

From the repository root:

```powershell
cargo test -p eroverlay-renderer
cargo clippy -p eroverlay-renderer --all-targets -- -D warnings
cargo run -p eroverlay-renderer --release --example gpu_perf -- --samples 600
cargo run -p eroverlay-renderer --release --example gpu_perf -- --samples 600 --egui
cargo run -p eroverlay-renderer --release --example gpu_perf -- --samples 600 --egui-batched
cargo run -p eroverlay-renderer --release --example gpu_perf -- --samples 600 --egui-batched --hdr --no-gpu-timing
cargo run -p eroverlay-renderer --release --example gpu_perf -- --samples 600 --warp
cargo run -p eroverlay-renderer --release --example scene_probe -- D:/Games/EldenRingExe
$env:EROVERLAY_SCENE_EXE = "D:/Games/EldenRingExe/1.17.2/Game/eldenring.exe"
cargo test -p eroverlay-renderer archived_contract_rejects_mutations_and_survives_relocation -- --ignored --test-threads=1
```

WARP tests read actual rendered pixels for group opacity over overlapping
geometry, circular clipping, independent border opacity and the game's native
transmittance blend. They also cover RGB10A2 above-UI output, a deliberately
blocked GPU queue dropping frames without a CPU wait, cross-layer frame upload
budgets, aggregate in-flight count/committed-byte caps, exclusive oversized
uploads, retirement,
hidden cleanup, buffer growth/reuse, GPU timestamps, and C/C++ frame layout.
A real BC1 texture with ten differently colored mips proves LOD sampling and
compressed memory accounting; BC2 pixels verify straight-alpha handling.
The D3D12 validation layer
is enabled when installed; tests reject stored ERROR/CORRUPTION messages.
The blocked-queue test also issues 1,000 incomplete zero-timeout polls and checks
that no wait event was created; a positive timeout still uses the event path.

`scene_probe` maps archived EXEs as data only, using the same Rust UI resolver
as native installation. On 2026-11-02, all 30 available archives matched the
previous C++ resolver from `afab7e1` exactly: 21 supported images (1.05–1.17.2)
matched all 15 addresses/offsets; nine older images kept the same rejection
reason. [scan-evidence.csv](scan-evidence.csv) records the comparison. The
opt-in archived test passed on 1.05, 1.16 and 1.17.2, covering duplicate submit
signatures, redirected enqueue/native-composition calls, changed consumer
control flow, missing RTTI, truncated images, an unknown file hash, and an
8 GiB relocated image. Scanning uses a temporary owned image only at startup;
it never runs inside a render callback. These static contracts do not establish
real-game visual behavior.

`native/checks.inc` tests the adapted production bridge using real MinHook detours
on controlled local entries: lock-protected submit/compositor teardown, restored
entries, fixture reinstall, and validated/stale/missing native UI boundaries.
It additionally pauses both detours before shared-lock acquisition and after
shared-lock release (three repetitions of all four windows). Stop returns while
the producer remains inside detour code, demonstrating why the module must stay
resident. These probes temporarily hook the Windows SRW entry in the standalone
test process; production detours have no extra pause probe. Three real MinHook
disable-error branches also verify that an inert bridge/trampoline stays alive
and its original entry remains callable without user callbacks or recursion.
The tests neither unmap the module nor establish safe real-game DLL unloading.
They do not launch or inject into the game. Function-level linking discards this
test entry from release products when it is not referenced.

The performance example prints JSON Lines for hidden, stable 64-quad, large
rotated/transparent 1,024-quad, region-resize and texture-burst scenarios. It
selects the high-performance adapter by default and prints its name. `--warp`
selects software rendering. GPU durations come from queue timestamp queries;
diagnostic timestamp instrumentation is disabled by default in the library.
`--hdr` selects RGB10A2 for a matched target-format comparison.
`--no-gpu-timing` disables queries and reports GPU timings as `null`.

Default CPU timing includes the renderer call only. `--egui` additionally times
`Context::run`, per-quad convex shapes without edge feathering, tessellation and
copying into preallocated ABI staging. `--egui-batched` instead clones one mesh
per texture/group before the same egui/tessellation/staging path, avoiding a
separate allocation for every polygon. Rust allocations are counted over that
selected CPU scope; driver/C allocations are not counted. The harness drains the
queue outside each measured call and excludes the game, native bridge, dynamic
plugin dispatch and update/asset workers. These figures cannot establish a
whole-game or whole-Minimap performance win. Use the C++ fixture with the same
adapter, geometry, resolution and pacing for a scoped comparison, then collect
matched in-game traces for final acceptance.

API references: [Microsoft windows-rs DX12 bindings](https://microsoft.github.io/windows-docs-rs/doc/windows/Win32/Graphics/Direct3D12/index.html),
[DX12 resource barriers](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states),
[DX12 fences](https://learn.microsoft.com/en-us/windows/win32/direct3d12/user-mode-heap-synchronization).
