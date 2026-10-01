"""Capture numbered map beacons with read-only process access, without a debugger.

Offsets are established by Python idapro against the verified Steam executable.
This tool only copies a stable snapshot; it neither calls nor modifies the game.
"""

from __future__ import annotations

import argparse
import ctypes as ct
from ctypes import wintypes as wt
from datetime import datetime
import hashlib
import json
from pathlib import Path
import struct

from ida_minimap_research import find_steam_executable


EXACT_HASH = "1a3547101327f65d0c76da2f9190ac0aa66871ea42bae2aecc61e11a8b597891"


class ModuleEntry(ct.Structure):
    _fields_ = [("size", wt.DWORD), ("module_id", wt.DWORD), ("pid", wt.DWORD),
                ("global_usage", wt.DWORD), ("process_usage", wt.DWORD),
                ("base", ct.c_void_p), ("bytes", wt.DWORD), ("module", wt.HMODULE),
                ("name", wt.WCHAR * 256), ("path", wt.WCHAR * 260)]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", required=True, type=int)
    parser.add_argument("--output", type=Path, default=Path("build/ida/player-marker-live"))
    args = parser.parse_args()
    source = find_steam_executable()
    with source.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    if digest != EXACT_HASH:
        raise RuntimeError("当前 Steam EXE 不匹配已确认的编号标记布局，需要重新分析 RVA。")

    kernel = ct.WinDLL("kernel32", use_last_error=True)
    kernel.CreateToolhelp32Snapshot.argtypes = [wt.DWORD, wt.DWORD]
    kernel.CreateToolhelp32Snapshot.restype = wt.HANDLE
    kernel.Module32FirstW.argtypes = [wt.HANDLE, ct.POINTER(ModuleEntry)]
    kernel.Module32FirstW.restype = wt.BOOL
    kernel.Module32NextW.argtypes = [wt.HANDLE, ct.POINTER(ModuleEntry)]
    kernel.Module32NextW.restype = wt.BOOL
    kernel.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
    kernel.OpenProcess.restype = wt.HANDLE
    kernel.ReadProcessMemory.argtypes = [wt.HANDLE, ct.c_void_p, ct.c_void_p, ct.c_size_t, ct.POINTER(ct.c_size_t)]
    kernel.ReadProcessMemory.restype = wt.BOOL
    kernel.CloseHandle.argtypes = [wt.HANDLE]

    modules = kernel.CreateToolhelp32Snapshot(0x08 | 0x10, args.pid)
    if modules == ct.c_void_p(-1).value:
        raise OSError(ct.get_last_error(), "无法枚举游戏模块；请确认 --pid 对应运行中的游戏。")
    base = 0
    try:
        entry = ModuleEntry()
        entry.size = ct.sizeof(entry)
        current = kernel.Module32FirstW(modules, ct.byref(entry))
        while current:
            if entry.name.lower() == "eldenring.exe" and Path(entry.path).resolve() == source.resolve():
                base = entry.base
                break
            current = kernel.Module32NextW(modules, ct.byref(entry))
    finally:
        kernel.CloseHandle(modules)
    if not base:
        raise RuntimeError("指定进程没有加载对应的 Steam EXE；请重新确认 --pid。")
    process = kernel.OpenProcess(0x410, False, args.pid)  # QUERY_INFORMATION | VM_READ.
    if not process:
        raise OSError(ct.get_last_error(), "无法取得只读游戏句柄；请确认进程仍在运行。")

    def read(address: int, size: int) -> bytes:
        buffer = ct.create_string_buffer(size)
        copied = ct.c_size_t()
        if not address or not kernel.ReadProcessMemory(process, address, buffer, size, ct.byref(copied)) or copied.value != size:
            raise OSError(ct.get_last_error(), f"无法读取 {address:#x}；请在游戏加载完成后重试。")
        return buffer.raw

    def pointer(address: int) -> int:
        return struct.unpack("<Q", read(address, 8))[0]

    try:
        menu = pointer(base + 0x3D6F820)
        owner = pointer(menu + 0x80)
        view = pointer(owner + 0x250)
        save = pointer(view + 0x338)
        header = read(save, 80)
        slots, capacity = struct.unpack_from("<QQ", header, 8)
        count = struct.unpack_from("<Q", header, 0x40)[0]
        if not slots or not 0 < capacity <= 10 or count > capacity:
            raise RuntimeError("编号标记列表尚未就绪；请在游戏加载完成后重试。")
        records = read(slots, capacity * 16)
        view_bytes = read(view, 1200)
        if (pointer(menu + 0x80) != owner or pointer(owner + 0x250) != view or
                pointer(view + 0x338) != save or read(save, 80) != header or read(slots, capacity * 16) != records):
            raise RuntimeError("采样期间地图或标记发生变化；请在状态稳定时重试。")
        markers = []
        for slot in range(capacity):
            raw = records[slot * 16:(slot + 1) * 16]
            identity, x, y, map_id, icon, _ = struct.unpack("<iffBBH", raw)
            markers.append({"slot": slot, "number": slot + 1, "id": identity, "x": x, "y": y,
                            "map": map_id, "icon": icon, "bytes": raw.hex()})
        if sum(marker["id"] >= 0 for marker in markers) != count:
            raise RuntimeError("标记计数尚未同步；请重试只读采样。")
        map_id, x, y, underground = struct.unpack_from("<iffB", view_bytes, 0x24)
        result = {"captured_at": datetime.now().astimezone().isoformat(), "pid": args.pid,
                  "method": "ReadProcessMemory, PROCESS_QUERY_INFORMATION | PROCESS_VM_READ; no debugger",
                  "executable": str(source), "sha256": digest, "base": hex(base), "view": hex(view),
                  "save": hex(save), "slots": hex(slots), "capacity": capacity, "active": count,
                  "player": {"map": map_id, "x": x, "y": y, "underground": underground},
                  "markers": markers}
        args.output.mkdir(parents=True, exist_ok=True)
        for name, value in [("view.bin", view_bytes), ("save.bin", header), ("slots.bin", records)]:
            (args.output / name).write_bytes(value)
        (args.output / "snapshot.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        active = [marker["number"] for marker in markers if marker["id"] >= 0]
        print(f"已只读采样：PID {args.pid}，{count} 个有效编号标记 {active}，保存至 {args.output}。")
    finally:
        kernel.CloseHandle(process)


if __name__ == "__main__":
    main()
