"""Inspect the installed Elden Ring executable through Python idapro/idalib.

Large IDA databases and generated evidence belong under build/ida (gitignored).
The executable itself is read from the Steam installation and is never modified.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import time
import warnings
from pathlib import Path


def find_steam_executable() -> Path:
    import winreg

    roots: list[Path] = []
    for hive, key, value in [
        (winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam", "SteamPath"),
        (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam", "InstallPath"),
    ]:
        try:
            with winreg.OpenKey(hive, key) as registry:
                roots.append(Path(winreg.QueryValueEx(registry, value)[0]))
        except OSError:
            continue
    for root in tuple(roots):
        library_file = root / "steamapps/libraryfolders.vdf"
        if library_file.exists():
            roots.extend(Path(match.replace("\\\\", "\\")) for match in
                         re.findall(r'"path"\s*"([^"]+)"', library_file.read_text(encoding="utf-8")))
    for root in dict.fromkeys(roots):
        manifest = root / "steamapps/appmanifest_1245620.acf"
        if not manifest.exists():
            continue
        match = re.search(r'"installdir"\s*"([^"]+)"', manifest.read_text(encoding="utf-8"))
        if match:
            executable = root / "steamapps/common" / match[1] / "Game/eldenring.exe"
            if executable.is_file():
                return executable
    raise FileNotFoundError("ELDEN RING (Steam app 1245620) was not found; provide --exe")


def save_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--database", type=Path, default=Path("build/ida/minimap.i64"))
    parser.add_argument("--output", type=Path, default=Path("build/ida/minimap"))
    parser.add_argument("--match", default=r"world.?map|map.?tile|menu.*(?:tex|map)|(?:tex|map).*menu|GRTexture|TextureResource|MapTexture|MapPiece|map.*\.tpf")
    parser.add_argument("--function", action="append", default=[], help="Function VA (hex); repeat to decompile several functions")
    parser.add_argument("--address", action="append", default=[], help="Data VA (hex); export its bytes and incoming references")
    parser.add_argument("--table", action="append", default=[], help="Pointer table VA:count (hex VA, decimal count)")
    parser.add_argument("--queries", type=Path, help="Run a JSON query profile, then save and exit")
    parser.add_argument("--request-dir", type=Path, help="Keep IDA open and process numbered JSON query files in this directory")
    parser.add_argument("--no-auto", action="store_true", help="Skip global auto-analysis (implied by --targeted)")
    parser.add_argument("--targeted", action="store_true", help="Analyze requested functions and discover references without global auto-analysis")
    args = parser.parse_args()
    if args.exe is None:
        args.exe = find_steam_executable()
    with args.exe.open("rb") as stream:
        source_sha256 = hashlib.file_digest(stream, "sha256").hexdigest()
    query_profile = json.loads(args.queries.read_text(encoding="utf-8")) if args.queries else None
    if query_profile and query_profile.get("sha256", source_sha256).lower() != source_sha256:
        parser.error("Query profile does not match the installed executable SHA256; locate new-version addresses before running it")
    args.output.mkdir(parents=True, exist_ok=True)
    args.database.parent.mkdir(parents=True, exist_ok=True)

    # Importing idapro initializes idalib. All subsequent calls stay on this thread.
    import idapro
    import ida_auto
    import ida_bytes
    import ida_funcs
    import ida_hexrays
    import ida_lines
    import ida_nalt
    import ida_segment
    import idaapi
    import ida_ua
    import idautils
    import idc

    args.no_auto = args.no_auto or args.targeted
    # IDA 9.4 still uses deprecated segment/function APIs inside idautils.
    warnings.filterwarnings("ignore", category=DeprecationWarning)
    started = time.monotonic()
    source = args.database if args.database.exists() else args.exe
    options = "" if args.database.exists() else f'-o"{args.database.resolve()}"'
    if args.no_auto:
        options += " -a"
    print(f"Opening {source} with IDA {idaapi.get_kernel_version()}", flush=True)
    result = idapro.open_database(str(source.resolve()), False, options)
    if result != 0:
        raise RuntimeError(f"idapro.open_database returned {result}")
    try:
        database_sha256 = ida_nalt.retrieve_input_file_sha256()
        if database_sha256 is None or database_sha256.hex() != source_sha256:
            raise RuntimeError("Database input SHA256 differs from the installed executable; use a new --database and --output")
        segments = []
        for ea in idautils.Segments():
            segment = ida_segment.segment_info_t()
            if ida_segment.get_segment_info(segment, ea, ida_segment.GSI_NAME):
                segments.append(segment)
        metadata = {
            "input_file": ida_nalt.get_input_file_path(),
            "database": str(args.database.resolve()),
            "ida_version": idaapi.get_kernel_version(),
            "image_base": hex(ida_nalt.get_imagebase()),
            "sha256": source_sha256,
            "analysis_mode": "targeted" if args.targeted else ("loader_only" if args.no_auto else "global_auto"),
            "segments": [{"name": segment.get_name(), "start": hex(segment.start_ea), "end": hex(segment.end_ea)} for segment in segments],
        }
        old_metadata = args.output / "metadata.json"
        if old_metadata.exists() and json.loads(old_metadata.read_text(encoding="utf-8")).get("sha256") != source_sha256:
            raise RuntimeError("Output cache belongs to another executable; use a new --output")
        save_json(args.output / "metadata.json", metadata)

        pattern = re.compile(args.match, re.IGNORECASE)
        string_cache = args.output / "strings_all.json"
        if string_cache.exists():
            all_strings = json.loads(string_cache.read_text(encoding="utf-8"))
        else:
            all_strings = []
            for segment in segments:
                if segment.get_name() not in (".rdata", ".data"):
                    continue
                # Read only IDA's loaded bytes; regex filtering avoids rebuilding
                # IDA's global string list on every targeted query.
                data = ida_bytes.get_bytes(segment.start_ea, segment.end_ea - segment.start_ea)
                if data is None:
                    continue
                for expression, encoding in [(rb"[\x20-\x7e]{5,}\x00", "ascii"),
                                             (rb"(?:[\x20-\x7e]\x00){5,}\x00\x00", "utf-16-le")]:
                    terminator_size = 2 if encoding == "utf-16-le" else 1
                    for match in re.finditer(expression, data):
                        all_strings.append({"ea": hex(segment.start_ea + match.start()),
                                            "text": match[0][:-terminator_size].decode(encoding), "encoding": encoding})
            save_json(string_cache, all_strings)
        selected = [(int(item["ea"], 16), item["text"]) for item in all_strings if pattern.search(item["text"])]
        save_json(args.output / "strings_initial.json", [{"ea": hex(ea), "text": value} for ea, value in selected])
        print(f"Found {len(selected)} candidate strings ({time.monotonic() - started:.1f}s)", flush=True)
        if not args.no_auto:
            print("Waiting for IDA auto-analysis", flush=True)
            if not ida_auto.auto_wait():
                raise RuntimeError("IDA auto-analysis was interrupted")
        print(f"Analysis ready: {ida_funcs.get_func_qty()} functions ({time.monotonic() - started:.1f}s)", flush=True)

        rip_references: dict[int, list[int]] = {}
        loaded_segments = []
        if args.targeted:
            for segment in segments:
                name = segment.get_name()
                if name not in (".text", ".rdata", ".data"):
                    continue
                data = ida_bytes.get_bytes(segment.start_ea, segment.end_ea - segment.start_ea)
                if data is None:
                    continue
                loaded_segments.append((segment.start_ea, name, data))
                if name == ".text":
                    for match in re.finditer(rb"[\x48-\x4f][\x8b\x8d][\x05\x0d\x15\x1d\x25\x2d\x35\x3d]....", data, re.DOTALL):
                        ea = segment.start_ea + match.start()
                        target = ea + 7 + struct.unpack_from("<i", match[0], 3)[0]
                        rip_references.setdefault(target, []).append(ea)
                    for match in re.finditer(rb"[\xe8\xe9]....", data, re.DOTALL):
                        ea = segment.start_ea + match.start()
                        target = ea + 5 + struct.unpack_from("<i", match[0], 1)[0]
                        rip_references.setdefault(target, []).append(ea)

        def function_at(ea: int):
            function = ida_funcs.func_entry_info_t()
            return function if ida_funcs.get_func_entry_info(function, ea) else None

        def reference(ea: int) -> dict:
            function = function_at(ea)
            instruction = ida_ua.insn_t()
            ida_ua.decode_insn(instruction, ea)
            return {
                "ea": hex(ea),
                "function": hex(function.start_ea) if function else None,
                "name": idc.get_func_name(ea) if function else idc.get_name(ea),
                "disassembly": ida_lines.tag_remove((ida_ua.print_insn_mnem(ea) or "") + " " + ", ".join(
                    idc.print_operand(ea, index) for index in range(3) if instruction.ops[index].type != ida_ua.o_void)),
            }

        def incoming(ea: int) -> list[dict]:
            candidates = set(rip_references.get(ea, []))
            validated = set()
            for source in candidates:
                if function_at(source) is None:
                    continue
                instruction = ida_ua.insn_t()
                if ida_ua.decode_insn(instruction, source) == 0:
                    continue
                if any(operand.type in (ida_ua.o_near, ida_ua.o_far, ida_ua.o_mem) and operand.addr == ea for operand in instruction.ops):
                    validated.add(source)
            return [reference(source) for source in sorted({x.frm for x in idautils.XrefsTo(ea)} | validated)]

        evidence = []
        for ea, value in selected:
            evidence.append({"ea": hex(ea), "text": value, "xrefs": incoming(ea)})
        save_json(args.output / "strings_xrefs.json", evidence)
        for requested in args.address:
            ea = int(requested, 0)
            save_json(args.output / f"address_{ea:x}.json", {
                "ea": hex(ea), "name": idc.get_name(ea),
                "bytes": (ida_bytes.get_bytes(ea, 128) or b"").hex(),
                "xrefs": incoming(ea),
            })
        for requested in args.table:
            address, count = requested.split(":")
            ea = int(address, 0)
            save_json(args.output / f"table_{ea:x}.json", {
                "ea": hex(ea), "name": idc.get_name(ea),
                "xrefs": incoming(ea),
                "pointers": [{"offset": hex(index * 8), "target": hex(ida_bytes.get_qword(ea + index * 8)),
                              **reference(ida_bytes.get_qword(ea + index * 8))} for index in range(int(count))],
            })
        def export_functions(requested_functions: list[str]) -> None:
            hexrays = ida_hexrays.init_hexrays_plugin()
            for requested in requested_functions:
                ea = int(requested, 0)
                function = function_at(ea)
                if function is None and args.targeted:
                    ida_ua.create_insn(ea)
                    ida_funcs.add_func(ea)
                    function = function_at(ea)
                if function is None:
                    print(f"No function at {requested}", flush=True)
                    continue
                if args.targeted and not ida_auto.plan_and_wait(function.start_ea, function.end_ea):
                    raise RuntimeError(f"Targeted analysis was interrupted at {function.start_ea:#x}")
                if function.start_ea != ea:
                    print(f"{requested} belongs to function {function.start_ea:#x}", flush=True)
                record = {"start": hex(function.start_ea), "rva": hex(function.start_ea - ida_nalt.get_imagebase()), "end": hex(function.end_ea),
                          "name": idc.get_func_name(function.start_ea),
                          "callers": incoming(function.start_ea),
                          "instructions": [{**reference(head), "bytes": (ida_bytes.get_bytes(head, idc.get_item_size(head)) or b"").hex()}
                                           for head in idautils.FuncItems(function.start_ea)]}
                if hexrays:
                    try:
                        cfunc = ida_hexrays.decompile(function.start_ea)
                        record["pseudocode"] = "\n".join(ida_lines.tag_remove(line.line) for line in cfunc.get_pseudocode()) if cfunc else None
                    except ida_hexrays.DecompilationFailure as error:
                        record["decompile_error"] = str(error)
                save_json(args.output / f"function_{function.start_ea:x}.json", record)
                print(f"Exported {record['name']} at {record['start']}", flush=True)
        if args.function:
            export_functions(args.function)
        def run_query(request: dict) -> None:
            if request.get("sha256", source_sha256).lower() != source_sha256:
                raise RuntimeError("Query profile does not match the installed executable SHA256")
            if request.get("functions"):
                export_functions(request["functions"])
            if request.get("match"):
                query = re.compile(request["match"], re.IGNORECASE)
                hits = [{"ea": item["ea"], "text": item["text"], "xrefs": incoming(int(item["ea"], 16))}
                        for item in all_strings if query.search(item["text"])]
                save_json(args.output / request.get("output", "query_strings.json"), hits)
            for requested in request.get("addresses", []):
                ea = int(requested, 0)
                save_json(args.output / f"address_{ea:x}.json", {
                    "ea": hex(ea), "name": idc.get_name(ea),
                    "bytes": (ida_bytes.get_bytes(ea, 256) or b"").hex(), "xrefs": incoming(ea),
                })
            for requested in request.get("tables", []):
                address, count = requested.split(":")
                ea = int(address, 0)
                save_json(args.output / f"table_{ea:x}.json", {
                    "ea": hex(ea), "name": idc.get_name(ea), "xrefs": incoming(ea),
                    "pointers": [{"offset": hex(index * 8), "target": hex(ida_bytes.get_qword(ea + index * 8)),
                                  **reference(ida_bytes.get_qword(ea + index * 8))} for index in range(int(count))],
                })
            if request.get("rtti") and not args.targeted:
                raise RuntimeError("RTTI queries require --targeted")
            for requested in request.get("rtti", []):
                type_ea = int(requested, 0) - 16
                base = ida_nalt.get_imagebase()
                tables = []
                for segment_ea, name, data in loaded_segments:
                    if name != ".rdata":
                        continue
                    for match in re.finditer(re.escape(struct.pack("<I", type_ea - base)), data):
                        offset = match.start() - 12
                        if offset < 0 or offset + 24 > len(data):
                            continue
                        fields = struct.unpack_from("<6I", data, offset)
                        locator = segment_ea + offset
                        if fields[0] != 1 or fields[5] != locator - base:
                            continue
                        for pointer in re.finditer(re.escape(struct.pack("<Q", locator)), data):
                            vtable = segment_ea + pointer.start() + 8
                            tables.append({"locator": hex(locator), "fields": [hex(v) for v in fields],
                                           "vtable": hex(vtable), "references": incoming(vtable),
                                           "functions": [hex(ida_bytes.get_qword(vtable + index * 8)) for index in range(16)]})
                save_json(args.output / f"rtti_{type_ea:x}.json", {"type_descriptor": hex(type_ea), "vtables": tables})

        if query_profile is not None:
            run_query(query_profile)
        if args.request_dir:
            args.request_dir.mkdir(parents=True, exist_ok=True)
            print(f"Ready for query files in {args.request_dir}", flush=True)
            while True:
                requests = sorted(args.request_dir.glob("*.request.json"))
                for request_path in requests:
                    request = json.loads(request_path.read_text(encoding="utf-8"))
                    if request.get("stop"):
                        save_json(request_path.with_suffix(".done.json"), {"stopped": True})
                        request_path.rename(request_path.with_suffix(".processed"))
                        return
                    run_query(request)
                    save_json(request_path.with_suffix(".done.json"), {"done": True})
                    request_path.rename(request_path.with_suffix(".processed"))
                    print(f"Completed {request_path.name}", flush=True)
                time.sleep(0.5)
    finally:
        print("Saving analysis database", flush=True)
        idapro.close_database(True)


if __name__ == "__main__":
    main()
