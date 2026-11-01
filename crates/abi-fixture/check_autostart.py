"""Actual auto-entry DLL refcount regression; runs only in disposable Python hosts.

The non-game host deliberately fails native discovery. It covers delayed thread
startup, startup failure cleanup, external stop + immediate caller FreeLibrary,
and repeated unload/reload. It does not prove native gameplay teardown.
"""

import argparse
import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time


def child(core: Path, mode: str, iterations: int) -> None:
    os.environ.pop("EROVERLAY_RUST_OFFLINE_HOST", None)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.FreeLibrary.argtypes = [ctypes.c_void_p]
    kernel.FreeLibrary.restype = ctypes.c_int
    kernel.GetModuleHandleW.argtypes = [ctypes.c_wchar_p]
    kernel.GetModuleHandleW.restype = ctypes.c_void_p
    for _ in range(iterations):
        library = ctypes.WinDLL(str(core))
        if mode == "external-stop":
            stop = library.eroverlay_stop
            stop.argtypes = []
            stop.restype = ctypes.c_int
            deadline = time.monotonic() + 5
            while stop() != 0:
                if time.monotonic() >= deadline:
                    raise AssertionError("stop did not retire startup")
                time.sleep(0.001)
        if not kernel.FreeLibrary(library._handle):
            raise ctypes.WinError(ctypes.get_last_error())
        deadline = time.monotonic() + 5
        while kernel.GetModuleHandleW(str(core)):
            if time.monotonic() >= deadline:
                raise AssertionError("bootstrap reference leaked after startup failure/stop")
            time.sleep(0.001)
    print(f"PASS {mode}: {iterations} actual auto-start load/unload cycles")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("core", type=Path)
    parser.add_argument("--iterations", type=int, default=100)
    parser.add_argument("--child", choices=["release-before-bootstrap", "external-stop"])
    args = parser.parse_args()
    core = args.core.resolve(strict=True)
    if not 1 <= args.iterations <= 1000:
        parser.error("--iterations must be between 1 and 1000")
    if args.child:
        child(core, args.child, args.iterations)
        return
    target = Path(__file__).resolve().parents[2] / "target"
    target.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="auto-entry-", dir=target) as directory:
        staged = Path(directory) / "EROverlay.dll"
        shutil.copy2(core, staged)
        for mode in ["release-before-bootstrap", "external-stop"]:
            subprocess.run(
                [sys.executable, __file__, str(staged), "--iterations", str(args.iterations), "--child", mode],
                check=True,
                timeout=60,
            )


if __name__ == "__main__":
    main()
