"""Load the actual core, Minimap, and malformed DLL in isolated Windows processes."""
from __future__ import annotations

import argparse
import ctypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time


class StartOptions(ctypes.Structure):
    _fields_ = [
        ("size", ctypes.c_uint32), ("version", ctypes.c_uint32),
        ("native", ctypes.c_uint32), ("reserved", ctypes.c_uint32),
        ("directory", ctypes.c_void_p), ("directory_len", ctypes.c_size_t),
    ]


class RuntimeStats(ctypes.Structure):
    _fields_ = [
        ("size", ctypes.c_uint32), ("state", ctypes.c_uint32),
        ("plugins_loaded", ctypes.c_uint32), ("plugins_rejected", ctypes.c_uint32),
        ("updates", ctypes.c_uint64), ("renders", ctypes.c_uint64),
        ("meshes", ctypes.c_uint64), ("dropped_frames", ctypes.c_uint64),
        ("gpu_committed_bytes", ctypes.c_uint64), ("last_error", ctypes.c_int32),
        ("reserved", ctypes.c_uint32),
    ]


class FixtureCounts(ctypes.Structure):
    _fields_ = [("init", ctypes.c_uint32), ("destroy", ctypes.c_uint32), ("live", ctypes.c_uint32)]


def require(condition: bool, description: str) -> None:
    if not condition:
        raise AssertionError(description)


def run_case(args: argparse.Namespace) -> dict[str, object]:
    target = Path(__file__).resolve().parents[2] / "target"
    target.mkdir(exist_ok=True)
    # Child process environment is set before loading/starting any DLL worker.
    os.environ["EROVERLAY_RUST_OFFLINE_HOST"] = "1"
    os.environ["EROVERLAY_ABI_FIXTURE_MODE"] = args.mode
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.FreeLibrary.argtypes = [ctypes.c_void_p]
    kernel32.FreeLibrary.restype = ctypes.c_int
    with tempfile.TemporaryDirectory(prefix="abi-fixture-", dir=target, ignore_cleanup_errors=True) as scratch:
        directory = Path(scratch)
        require(directory.resolve().is_relative_to(target.resolve()), "scratch escaped Rust target directory")
        (directory / "overlays").mkdir()
        shutil.copy2(args.core, directory / "EROverlay.dll")
        shutil.copy2(args.minimap, directory / "overlays" / "Minimap.dll")
        fixture = args.missing_export if args.mode == "missing_export" else args.fixture
        require(fixture is not None, "missing-export case needs its separate DLL")
        shutil.copy2(fixture, directory / "overlays" / "BadPlugin.dll")
        bad = ctypes.WinDLL(str(directory / "overlays" / "BadPlugin.dll"))
        bad.fixture_counts.argtypes = [ctypes.POINTER(FixtureCounts)]
        bad.fixture_counts.restype = ctypes.c_int32
        core = ctypes.WinDLL(str(directory / "EROverlay.dll"))
        core.eroverlay_start.argtypes = [ctypes.POINTER(StartOptions)]
        core.eroverlay_start.restype = ctypes.c_int32
        core.eroverlay_stop.argtypes = []
        core.eroverlay_stop.restype = ctypes.c_int32
        core.eroverlay_stats.argtypes = [ctypes.POINTER(RuntimeStats)]
        core.eroverlay_stats.restype = ctypes.c_int32
        path = ctypes.create_unicode_buffer(str(directory))
        units = len(str(directory).encode("utf-16-le")) // 2
        options = StartOptions(ctypes.sizeof(StartOptions), 1, 0, 0, ctypes.addressof(path), units)
        started = False
        stopped = False
        try:
            code = core.eroverlay_start(ctypes.byref(options))
            require(code == 0, f"start failed: {code}")
            started = True
            stats = RuntimeStats(size=ctypes.sizeof(RuntimeStats))
            deadline = time.monotonic() + 5
            while True:
                require(core.eroverlay_stats(ctypes.byref(stats)) == 0, "stats query failed")
                if stats.updates > 0:
                    break
                require(time.monotonic() < deadline, "update thread did not run")
                time.sleep(0.01)
            valid = args.mode == "valid"
            require(stats.plugins_loaded == (2 if valid else 1), f"unexpected loaded count: {stats.plugins_loaded}")
            require(stats.plugins_rejected == (0 if valid else 1), f"unexpected rejected count: {stats.plugins_rejected}")
            counts = FixtureCounts()
            require(bad.fixture_counts(ctypes.byref(counts)) == 0, "fixture count query failed")
            before = (counts.init, counts.destroy, counts.live)
            expected = (1, 0, 1) if valid else (1, 1, 0) if args.mode == "init_failure" else (0, 0, 0)
            require(before == expected, f"initialization rollback counts {before}, expected {expected}")
            code = core.eroverlay_stop()
            require(code == 0, f"stop returned {code}; module must stay loaded")
            stopped = True
            require(core.eroverlay_stop() == 0, "second stop failed")
            require(core.eroverlay_stats(ctypes.byref(stats)) == 0 and stats.state == 0, "runtime survived stop")
            require(bad.fixture_counts(ctypes.byref(counts)) == 0, "post-stop fixture count query failed")
            after = (counts.init, counts.destroy, counts.live)
            expected = (1, 1, 0) if valid or args.mode == "init_failure" else (0, 0, 0)
            require(after == expected, f"destroy counts {after}, expected {expected}")
            require(kernel32.FreeLibrary(core._handle) != 0, "core FreeLibrary failed")
            require(kernel32.FreeLibrary(bad._handle) != 0, "fixture FreeLibrary failed")
            return {"mode": args.mode, "result": "PASS", "before": before, "after": after}
        except BaseException:
            # Never unload a DLL whose worker/native callback lifetime is uncertain.
            if started and not stopped:
                core.eroverlay_stop()
            raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("core", type=lambda value: Path(value).resolve())
    parser.add_argument("minimap", type=lambda value: Path(value).resolve())
    parser.add_argument("fixture", type=lambda value: Path(value).resolve())
    parser.add_argument("--missing-export", type=lambda value: Path(value).resolve())
    parser.add_argument("--mode", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.mode:
        print(json.dumps(run_case(args)))
        return 0
    modes = ["null", "misaligned", "short", "version", "magic", "capability", "missing_init", "missing_destroy", "init_failure", "valid"]
    if args.missing_export:
        modes.append("missing_export")
    for mode in modes:
        command = [sys.executable, str(Path(__file__).resolve()), str(args.core), str(args.minimap), str(args.fixture), "--mode", mode]
        if args.missing_export:
            command.extend(["--missing-export", str(args.missing_export)])
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        if result.returncode != 0:
            print(f"{mode}: FAIL\n{result.stdout}\n{result.stderr}", file=sys.stderr)
            return 1
        print(result.stdout.strip(), flush=True)
    print(f"{len(modes)} actual DLL load/rejection/rollback/shutdown scenarios passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
