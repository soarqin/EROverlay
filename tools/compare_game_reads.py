"""Compare actual game update reads with 20fce70 using the same allocated-memory fixture.

Runs no game or injection. The archived crate and its C++ dependencies are
exported from an explicit historical commit below target/. An independent clone
can use --repository pointing to a checkout containing that history.
The fixture measures update_snapshot/read_map_state, not rendering or game FPS.
"""

import argparse
import os
from pathlib import Path
import subprocess
from export_cpp_reference import export_reference


def run(command: list[str], directory: Path, environment: dict[str, str]) -> str:
    result = subprocess.run(command, cwd=directory, env=environment, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"{' '.join(command)}\n{result.stdout}\n{result.stderr}")
    return result.stdout


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--samples", type=int, default=1000)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--repository", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--baseline-commit", default="20fce70339795538bd17b2914adbba985d5b07f4")
    args = parser.parse_args()
    if not 100 <= args.samples <= 100000 or not 1 <= args.runs <= 10:
        parser.error("samples must be 100..100000 and runs 1..10")
    rust = Path(__file__).resolve().parents[1]
    target = rust / "target" / "game-read-comparison"
    target.mkdir(parents=True, exist_ok=True)
    import tempfile
    baseline = Path(tempfile.mkdtemp(prefix="baseline-", dir=target)) / "source"
    baseline_commit = export_reference(args.repository, args.baseline_commit, baseline)
    print(f"Historical Rust baseline: {baseline_commit}", flush=True)
    workspace = baseline / "rust"
    (workspace / "Cargo.toml").write_text('''[workspace]
members = ["crates/game", "crates/abi"]
resolver = "2"
[workspace.package]
edition = "2024"
rust-version = "1.99"
license = "MIT"
version = "0.1.0"
[workspace.dependencies]
eroverlay-abi = { path = "crates/abi" }
[workspace.lints.rust]
unsafe_op_in_unsafe_fn = "deny"
[profile.release]
lto = "thin"
codegen-units = 1
debug = 1
panic = "unwind"
''', encoding="utf-8")
    fixture = (rust / "crates/game/src/runtime_perf.rs").read_text(encoding="utf-8")
    (workspace / "crates/game/src/runtime_perf.rs").write_text(fixture, encoding="utf-8")
    lib = workspace / "crates/game/src/lib.rs"
    lib.write_text(lib.read_text(encoding="utf-8") + '\n#[cfg(test)]\nmod runtime_perf;\n', encoding="utf-8")
    environment = os.environ.copy()
    environment["EROVERLAY_READ_PROFILE_SAMPLES"] = str(args.samples)
    command = ["cargo", "test", "--release", "--offline", "-p", "eroverlay-game", "--lib",
               "runtime_production_read_profile", "--", "--ignored", "--nocapture", "--test-threads=1"]
    # Serial alternating pairs, no overlapping compiler or performance fixture.
    for iteration in range(1, args.runs + 1):
        for name, directory in ((baseline_commit[:7], workspace), ("fixed", rust)):
            output = run(command, directory, environment)
            (target / f"{name}-{iteration}.txt").write_text(output, encoding="utf-8")
            records = [line[line.index("runtime_read_perf "):] for line in output.splitlines()
                       if "runtime_read_perf " in line]
            if not records:
                raise RuntimeError(f"fixture produced no runtime_read_perf records: {name}")
            for record in records:
                print(f"{name} run={iteration} {record}", flush=True)
    print(f"Evidence: {target}")


if __name__ == "__main__":
    main()
