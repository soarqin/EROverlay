# Unconsumed native layer regression

Historical `acdcf56` evidence. The later cumulative review also removed viewport
size from renderer lifetime identity; its two-consumer regression and current
resize/stop behavior are recorded in [the cumulative review](../../../docs/review-2026-11-02.md).

Recorded on 2026-11-02 with actual Release core and Minimap DLLs in a controlled
offline WARP process. This does not launch or inject into the user's game.

The runtime originally compared device/queue/viewport identity and rebuilt its
renderer before checking whether any plugin consumed the native layer. With
only below-UI Minimap loaded, an upper-UI viewport of another size could recreate
the renderer, compile shaders and reset Minimap even though nothing rendered in
that layer.

The modified production harness first reproduced this against the unfixed core:

```text
unused above-UI layer changed core renderer state:
before_renders=1, before_bytes=1179648
after: renders=2, gpu_committed_bytes=1048576, last_error=0
```

The runtime now caches its accepted plugin-layer mask and returns for an
unconsumed layer before acquiring render state or comparing/rebuilding identity.
Hidden callbacks in a consumed layer still process uploads and retire completed
resources. Pending bounded work waits for a consuming callback or shutdown if
the native boundary is unavailable. Fresh display/hiding checks are unchanged.

The harness calls the actual `eroverlay_render` export with the real Minimap
loaded. Each cycle interleaves 16 upper callbacks with alternating 192×128 and
32×32 viewports and below callbacks, checks that upper callbacks cannot change
render count or committed bytes, then covers real resize, blocked-GPU shutdown
and DLL unloading in offline mode. Three cycles passed with 19 consumed renders
per cycle. Native mode retains its existing process-lifetime PIN policy.

An optional third harness argument observes the production path long enough for
the configured diagnostics file to report:

```powershell
target/release/eroverlay-harness.exe <absolute-fixture-directory> 1 3
```

The three-second run passed with 204 consumed renders. At update 121, the
appended log recorded two expected renderer creations (64×64 then 128×96), 132
unconsumed-layer skips, zero render-lock/callback-gate skips, generation 2, zero
pending/in-flight uploads, and submitted/completed fence values both 1. Thus
stable hidden production calls submitted no further GPU work. A sampled initial
renderer creation took about 11 ms in this WARP/debug-layer fixture; this is not
a real-game renderer timing or a C++/Rust performance comparison.

The logger samples CPU stages and thread-local memory system-call counters one
call in sixteen, and formats reports only in the update worker (state changes
after at least 60 updates, or 120 stable updates). Stage scopes overlap total
scopes and are diagnostic samples, not latency percentiles. Native C++ adapter
memory reads fall outside the Rust memory counter. Queue reports distinguish
file waits, map/flag failures, texture preparation, GPU upload backpressure and
renderer recreation, without inferring a cause from a generic waiting message.
