# Native stop lifecycle

Recorded on 2026-11-02 in the isolated development worktree. This is a controlled
standalone process check, without launching or injecting into Elden Ring.
The result predates the root-workspace migration; commands below use the current
layout. Scanner validation is recorded separately.

```powershell
# From the repository root.
cargo test -p eroverlay-renderer native_hook_teardown_and_exact_layer_boundary -- --test-threads=1 --nocapture
```

Before changing the stop policy, a new fixture reproduced all four windows:
submit before acquiring the shared gate, submit after releasing it, composite
before acquisition, and composite after release. Each window was repeated three
times. Real MinHook detours reached the production scene functions; a temporary
detour on the Windows SRW entry paused only the selected producer thread and
scene lock. No sleep was used to infer that producer code had exited.

In all 12 windows, `stop()` returned with the bridge removed while the producer
had not exited its detour. Its suspended call still had a return address in the
scene module. The test resumed and joined the producer without unmapping code.
This establishes a gap in bridge-lock draining; it does not attempt a deliberate
use-after-unmap crash or treat MinHook IP relocation as a thread-drain guarantee.

The implemented policy pins the owning module before either hook can be enabled.
The pin persists through normal stop and failed-install rollback. Callback/user
accesses still drain behind the exclusive gate; renderer and plugin resources
can be stopped independently of the retained native code. The core reads the
process-lifetime `NativeBridge::requires_module_retention()` flag and must report
a stopped-but-resident result instead of claiming complete unloading.

The same 12 windows pass after the policy change and assert that retention is
active. An additional three iterations exercise a real MinHook disable error:
the fixture temporarily supplies an unregistered target to obtain
`MH_ERROR_NOT_CREATED` while its actual submit hook remains installed. Stop
leaves an `accepting=false` bridge and valid trampoline alive. Calling the actual
entry reaches the original function without user callbacks or recursion. The
fixture then restores the target metadata and successfully removes both hooks.
This verifies failure ownership; it does not simulate every possible Windows
thread-suspension failure.

Focused Debug test result after the change: one test passed, with these checks:

```text
PASS: two real MinHook detours drain lock-protected callbacks; restored local entries and fixture reinstall work without unmapping the module.
CONFIRMED: 12 real-detour pre-acquire/post-release pauses outlive stop(); a drained bridge does not prove its module can be unmapped.
PASS: three real MinHook disable-error branches retain an inert bridge/trampoline, skip user callbacks, and permit later successful cleanup.
PASS: exact native UI command boundary survives later native work; stale/missing boundaries suppress only the lower stage.
```

Fixture reinstall occurs while its process module remains loaded. It is not a
test of hot replacing a DLL. Real-game visual behavior and complete native DLL
unmapping remain unverified; the product intentionally requires process exit to
release code once native hooks have been reachable.

The integrated Release DLL fixture also passed:

```powershell
cargo build -p eroverlay-core --release --locked --features native-lifecycle-fixture --target-dir target/native-retention
python crates/abi-fixture/check_native_retention.py target/native-retention/release/EROverlay.dll
```

This runs in a disposable Python child with automatic game discovery disabled.
Before any hooks, core stop returns `0`. After the same native checks, stop
returns `STOPPED_RESIDENT = 1` repeatedly, native restart returns unavailable,
and releasing the caller's `LoadLibrary` reference leaves the DLL mapped and its
stop export callable. No executing code is deliberately unmapped. The ordinary
installation DLL was checked separately and has no lifecycle-fixture export.
