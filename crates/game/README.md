# Rust game services

`GameServices` belongs to the update thread. It publishes copied `MapState`,
`DisplayState`, and `GameLayout` POD values. `read_display_state()` independently
checks the live display state without allocations, locks, or cached resource
dependencies. It hides on unavailable/invalid state, loading, LogoMenu, and the
full-screen map; unrelated menus do not hide the minimap.

`read_map_state()` rereads live map/location/camera fields and validates the live
owner/player against the last update snapshot, without discovery, flag traversal,
PARAM parsing or allocation. It keeps cached map-piece masks and generation only
while that context still matches. Consumers can recheck after reading game-owned
collections to detect an in-place map change; a replaced view/player fails closed
until the next update publishes its new generation.

`game_language()` is an initialization-only lookup through the already loaded
`steam_api64.dll`. It retains that module while calling the first-party flat
ISteamApps v008 exports and copying at most 64 bytes into owned Rust storage.
It never loads or initializes Steam and returns `None` if unavailable, malformed,
or unterminated. The core publishes the raw Steam language name as
`runtime.game_language`, using `english` when unavailable.

The Rust implementation owns memory reads, PARAM row bounds, cached event-flag
category lookup, map/camera state, native file request lifetime, selected TPF
members, container parsers, and DDS decoding. Runtime memory copies use bounded
`ReadProcessMemory` calls; `VirtualQuery` is reserved for initialization-time image
region discovery. No borrowed Rust reference points into mutable game memory. Event categories refresh each
second and map-piece masks each 200 ms, independently of rendering.

The scanner implements PE, signature, unwind, call-graph and RTTI verification in
Rust. Its compatibility rules use the C++ reference `master` at `afab7e1` as a
behavioral baseline; the old project is not present on `rust-rewrite`. No
executable hash or known-version list gates discovery. `GameBindings` contains
image-relative addresses and validated field offsets only. Scan errors disable
native game calls. The optional `runtime-read-fixture` feature compiles only the
explicitly requested C++ reader comparator, using an exported reference selected
by `EROVERLAY_CPP_REFERENCE`; normal builds do not compile it. See
[source layout and reference export](../../docs/source-layout.md).

Files use the game's validated virtual-file request functions, preserving mod
VFS behavior. `flags = 0x40` requests the game's native CPU DCX decompression;
uncompressed metadata uses zero. Requests are bounded to 128 outstanding tokens,
1024 UTF-16 path units, and 256 MiB **aggregate retained Rust file bytes**. Named
members queued for the same TPF in one update share one native archive read.
Only selected DDS members are copied; missing/duplicated entries fail that member
without failing valid siblings. Game buffers are released through the game's
allocator, while Rust-owned results are destroyed by Rust.

The callback allocation is separate from update-owned request bookkeeping. Its
immutable metadata is shared with atomics/mutex-protected completion data. A
request remains alive until both completion publication and native request-pool
generation retirement prove callback code has returned. `stop()` returns false
after five seconds if retirement cannot be proven; the core must remain loaded
and retry shutdown. Unloading anyway is unsafe.

DDS parsing validates dimensions, format, array/depth/cube restrictions, complete
mip byte ranges, and exact container length. BC1/BC2/BC3 decode uses Rust with
the established endpoint/interpolation rounding, including BC1 transparent mode
and BC2/BC3 opaque RGB mode. BC7 uses pinned `texture2ddecoder = 0.1.2` block
decoding. Decode allocates one bounded RGBA8 output buffer and uses stack block
scratch space. All container parsing and decode work belongs to update/workers.

Run these checks from the repository root:

```powershell
cargo test --locked -p eroverlay-game
cargo clippy --locked -p eroverlay-game --all-targets -- -D warnings
cargo run --release --locked -p eroverlay-game --bin game_probe -- archive D:/Games/EldenRingExe
cargo run --release --locked -p eroverlay-game --bin game_probe -- verify D:/Games/EldenRingExe/1.17.1
```

`game_probe` maps archived EXEs as data; it never loads or executes them.
`scan-evidence.csv` records the 2026-11-01 run of the former C++ scanner adapter:
30/30 native game/file bindings resolved across 1.02–1.17.2. It is historical
baseline evidence, not a new measurement of the Rust scanner. The measured scan time includes file reading and PE
mapping and occurs only at initialization. This result does not extend the
renderer compatibility floor or verify an in-game frame, live archive request,
GPU behavior, or end-to-end performance.
The historical additional `verify` run passed unknown-hash tolerance, rejection of a
modified native request ABI and truncated PE, and unchanged results after
relocating image/RTTI addresses by 8 GiB.

The Rust scanner's post-migration archive comparison is recorded separately in
`scan-evidence-rust.csv`, comparing every `Bindings` field against the explicitly
exported C++ baseline. Its execution result is reported in the
[root-layout validation record](../../docs/source-layout.md); the older CSV
above is not relabeled as Rust-scan evidence.
