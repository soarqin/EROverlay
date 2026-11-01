"""Verify native detour retention in a disposable DLL host; never starts the game.

Requires a core build with --features native-lifecycle-fixture in a separate
target directory. The installation build does not export its local-hook check.
"""

import argparse
import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def child(core: Path) -> None:
    os.environ["EROVERLAY_RUST_OFFLINE_HOST"] = "1"
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.FreeLibrary.argtypes = [ctypes.c_void_p]
    kernel.FreeLibrary.restype = ctypes.c_int
    kernel.GetModuleHandleW.argtypes = [ctypes.c_wchar_p]
    kernel.GetModuleHandleW.restype = ctypes.c_void_p
    library = ctypes.WinDLL(str(core))
    stop = library.eroverlay_stop
    stop.restype = ctypes.c_int
    assert stop() == 0, "offline mode unexpectedly retained code before hooks"
    check = library.eroverlay_test_native_lifecycle
    check.restype = ctypes.c_int
    assert check() == 0, "controlled native detour checks failed"
    assert stop() == 1, "native stop must report STOPPED_RESIDENT"
    assert stop() == 1, "repeated stop lost resident state"
    class StartOptions(ctypes.Structure):
        _fields_ = [
            ("size", ctypes.c_uint32), ("version", ctypes.c_uint32),
            ("native", ctypes.c_uint32), ("reserved", ctypes.c_uint32),
            ("directory", ctypes.c_void_p), ("directory_len", ctypes.c_size_t),
        ]
    directory = ctypes.create_unicode_buffer(str(core.parent))
    options = StartOptions(
        ctypes.sizeof(StartOptions), 1, 1, 0,
        ctypes.cast(directory, ctypes.c_void_p), len(directory.value),
    )
    start = library.eroverlay_start
    start.argtypes = [ctypes.POINTER(StartOptions)]
    start.restype = ctypes.c_int
    assert start(ctypes.byref(options)) == -3, "native restart must require a fresh process"
    handle = library._handle
    assert kernel.FreeLibrary(handle), ctypes.WinError(ctypes.get_last_error())
    assert kernel.GetModuleHandleW(str(core)) == handle, "pinned code was unmapped"
    # Resolve again through the resident module rather than using a dead object.
    assert stop() == 1, "resident stop export no longer callable"
    print("PASS: actual DLL local detours, resident stop=1, caller FreeLibrary leaves code mapped")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("core", type=Path)
    parser.add_argument("--child", action="store_true")
    args = parser.parse_args()
    core = args.core.resolve(strict=True)
    if args.child:
        child(core)
        return
    target = Path(__file__).resolve().parents[2] / "target"
    target.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="native-retention-", dir=target) as directory:
        staged = Path(directory) / "EROverlayNativeCheck.dll"
        shutil.copy2(core, staged)
        result = subprocess.run(
            [sys.executable, __file__, str(staged), "--child"], capture_output=True, timeout=60
        )
        sys.stdout.write(result.stdout.decode("utf-8", errors="replace"))
        if result.returncode:
            # MSVC wide stderr in a redirected process uses UTF-16LE.
            encoding = "utf-16le" if b"\x00" in result.stderr else "utf-8"
            sys.stderr.write(result.stderr.decode(encoding, errors="replace"))
        result.check_returncode()


if __name__ == "__main__":
    main()
