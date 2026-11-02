# Presenter lock audit (read-only)

Audited baseline: `5ee822c78921cd29e7c7252ed3810e563663bd54`.
Fixed C++ comparison: `afab7e14db2b62ae0bede980d5d0c96634321350`.
Line references below describe those fixed inputs, not concurrent edits after the audit began.

## Conclusion

A contended Presenter lock does **not** prove that the current NativeFrame's sequence/RTV has been composited. Both the outer handle mutex and the inner Presenter mutex can be held before any GPU submission, or solely for metadata/invalidation/lifetime work. The baseline returns from the current callback after the failed try-lock without another presentation path.

The native adapter permits concurrent submit invocations: its shared SRW gate protects lifetime, not exclusive rendering. This makes a second native invocation a supported entry path to the lock collision. It is a reachable missing-submission case in the code's allowed concurrency, **not evidence that this game's latest reported flicker was uniquely caused by parallel native submissions**. No game execution, thread trace, or new test was performed in this audit. If the game happens to consume every relevant native submission on one strictly sequential CPU thread, ordinary L/H callbacks alone do not create this particular lock collision.

No production background update or texture worker acquires Presentation. A new test thread holding the real lock is a controlled way to expose this scheduling boundary; it must not be described as an existing production background-maintenance thread.

## Two locks and their holders

| Lock / holder | Fixed source | Can it hold the lock without compositing this caller's current target? |
|---|---|---|
| Outer `Runtime.presentation: Mutex<Option<PresentationHandle>>` | `crates/core/src/lib.rs:60`; acquired by `rendering.rs:21` | Yes. It only protects retrieval/publication of the Arc. It has no sequence/RTV receipt. |
| Same-identity renderer preparation | `crates/core/src/rendering.rs:82-93`, especially `90-92` | Yes. Every normal preparation can replace the outer handle with another Arc clone before plugin drawing. |
| New renderer publication | `crates/core/src/rendering.rs:121-123` | Yes. Handle publication is not image submission. |
| Core invalidation / replay | `crates/core/src/rendering.rs:20-31`, `33-68` | Yes. Invalidation only retires metadata/resources; replay submits for its own borrowed frame, not any other caller. |
| Normal AboveUI copy/publication | `crates/renderer/src/gpu.rs:1135-1148` through `1221` | Yes. The mutex is acquired before `record_copy`, including possible resource/descriptor preparation. Queue Execute is only at `1211`, Signal at `1218`, and ready publication at `1221`. The mutex stays held across these steps. |
| Empty/invisible group invalidation | `crates/renderer/src/gpu.rs:693-699` | Yes. No target draw is implied. |
| Renderer fallback replay | `crates/renderer/src/gpu.rs:600-617` | It submits only if it acquires the inner lock and finds a usable image/slot. A failed try-lock leaves the dropped result unchanged. |
| Renderer `wait_idle` | `crates/renderer/src/gpu.rs:1332-1343` | Yes. It takes the same mutex and waits for the two fence domains. This is reset/shutdown work, not current-target composition. |
| Presentation destruction | `crates/renderer/src/gpu/presentation.rs:461-475`; Renderer Drop `gpu.rs:1368-1385` | Destruction requires exclusive ownership or the Renderer wait path; it does not create current-frame pixels. Bounded timeout retention protects in-flight storage. |

The core's `with_presentation` returns `false` when either try-lock fails (`rendering.rs:20-31`). `retain_upper` ignores that boolean (`40-68`), so this particular failure has neither a presentation submission nor its own dropped-counter increment. `invalidate_group` does record deferred invalidation (`33-38`); that protects hiding semantics, but does not provide visible-frame continuity.

## Concrete allowed schedule

1. Native invocation A enters the AboveUI callback for frame A and owns the CPU paint/render locks.
2. A acquires Presenter at `gpu.rs:1137`, records its image copy, and is descheduled before `ExecuteCommandLists` at `1211`.
3. Native invocation B enters for a later sequence/current RTV. The shared native lifetime gate permits B. B fails Core's paint or render try-lock and calls `retain_upper` (`rendering.rs:177-180` or `205-209`).
4. B fails the Presenter try-lock in `with_presentation` and returns without compositing B's current target.
5. A resumes and submits the command list that explicitly bound **A's** `frame.target` (`gpu.rs:1150-1153`). That submission is not a receipt for B's RTV/sequence. Same device/queue identity alone does not establish target or native-U ordering equivalence.

There is an even shorter metadata window: A holds only the outer handle mutex while cloning/replacing the Arc in same-identity preparation; B cannot acquire that mutex to replay. Again, the holder has not necessarily recorded any target command.

## Native callback and lifetime evidence

- `crates/renderer/native/scene.cpp:36-43`: `SceneReadLock` uses `AcquireSRWLockShared`; the exclusive form is reserved for install/stop.
- `scene.cpp:182-194`: submit holds the shared gate and allocates a unique sequence with `fetch_add`. It does not take a render-thread mutex.
- `scene.cpp:221-239`: each invocation registers its own callbacks and consumes them synchronously inside `original_`. The per-invocation counter prevents duplicate execution **within that invocation**; it is not cross-invocation exclusion.
- `scene.cpp:246-287`: resource/RTV, queue, dimensions and sequence are read into the current stack NativeFrame and passed synchronously to Core. The frame is not a cross-frame owned target.
- `scene.cpp:124-148`: stop disables acceptance and drains the shared gate with an exclusive gate. `core/src/lib.rs:700-732` additionally stops acceptance before native stop, worker join, callback write exclusion, and Renderer wait/drop.
- `core/src/rendering.rs:40-48`: replay checks `accepting` before and after acquiring Presenter. This recheck is necessary: a caller that started before stop may acquire the independent handle after GPU drain has begun. A replacement design must preserve it.

Retries must not retain a borrowed game RTV after its native callback returns. Any independent replay storage must own only overlay images and command resources, and use the current callback's target.

## Threads that do not hold Presentation

- The update worker is spawned at `core/src/lib.rs:594-615` and invokes `Runtime::update` under the callback read gate (`621-628`). Its ordinary game/plugin update path does not access Presentation.
- Worker diagnostics can take `Runtime.render.try_lock` (`lib.rs:672-678`), which makes the independent replay path useful, but `Renderer::diagnostics` queries the main fence/slot counters without taking Presenter.
- Texture preparation/synchronization owns the texture registry and Renderer data, not the Presentation handle. `Renderer::committed_bytes` reads the shared retained-image byte counter atomically (`gpu.rs:1230-1231`).
- The direct mode-6 Presenter hold is confined to `core/src/layer_fixture.rs:252-262`. It is not present in a normal build.

## Fixed C++ difference

The C++ native bridge also uses a shared lifecycle lock (`src/uiscene.cpp:19`, submit at `161`) and invokes `renderer_.overlayScene(frame)` at `264`. However, `src/d3drenderer.cpp:188-189` takes `std::lock_guard lock(deviceMutex_)` at the start of **every** overlayScene call. `deviceMutex_` is `std::recursive_mutex` (`src/d3drenderer.hpp:196`). Normal frame recording/submission therefore serializes on that lock, including `renderCommands` at `364` and Execute at `480`.

Rust deliberately replaces blocking render exclusion with try-lock + retained-image fallback. Adding another contended exclusive gate to that fallback and treating its failure as already-presented does not preserve the C++ serialization outcome. The smallest safe direction is to keep stable published image ownership readable independently of normal copy/maintenance, while preserving bounded command storage, per-use fence retirement, stop acceptance checks and current-target borrowing. This audit does not prescribe the implementation or relax those safety rules.

## Scope

Read-only source review. No files outside this ignored report were edited. No tests, GPU stalls, game runs, injection, new assets, dependency installation or ref changes were performed. The parent/renderer agent owns the fix and its old-red/new-green test.
