"""Collect Elden Ring event-interpreter evidence with Python idapro/idalib.

The executable is opened read-only. IDA databases, public archive keys and full
decompilations stay under build/ida/boss (gitignored); no game process is used.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import re
from pathlib import Path

from ida_minimap_research import find_steam_executable
from ida_minimap_versions import Analysis, write_json


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--database", type=Path, default=Path("build/ida/boss/boss.i64"))
    parser.add_argument("--output", type=Path, default=Path("build/ida/boss"))
    parser.add_argument("--function", action="append", default=[])
    parser.add_argument("--address", action="append", default=[])
    parser.add_argument("--range", action="append", default=[], metavar="VA:SIZE")
    parser.add_argument("--callers", action="append", default=[])
    parser.add_argument("--find-bytes", action="append", default=[])
    parser.add_argument("--revival-baseline", action="store_true", help="Export the reviewed 1.17.1 interpreter/storage evidence")
    args = parser.parse_args()
    if args.revival_baseline:
        from collect_boss_revival_evidence import BASELINE_SHA256, NATIVE
        args.function.extend("0x" + row[0] for row in NATIVE.values())
        args.range.append("0x143B35FF0:5952")
        args.address.extend(("0x142A70008", "0x142A70028"))
    executable = args.exe or find_steam_executable()
    if args.revival_baseline:
        with executable.open("rb") as stream:
            sample = hashlib.file_digest(stream, "sha256").hexdigest()
        if sample != BASELINE_SHA256:
            parser.error("--revival-baseline addresses only apply to the reviewed 1.17.1 executable hash; locate functions for this sample first")
    args.output.mkdir(parents=True, exist_ok=True)
    print(f"Opening {executable} with Python idapro", flush=True)
    analysis = Analysis(executable, args.database)
    try:
        analysis.index_references()
        write_json(args.output / "metadata.json", {
            "input_file": str(executable.resolve()), "sha256": analysis.sha256,
            "file_size": executable.stat().st_size,
            "ida_version": analysis.version, "idapro_version": importlib.metadata.version("idapro"),
            "image_base": hex(analysis.base), "analysis_mode": "targeted_static",
        })
        pattern = re.compile(r"emevd|event.?flag|event.?script|event.?command|EventMan|EventControl|EventSystem|CSEvent|Ec.*Flag|Emk|\.bhd|Data[0-3]|BEGIN RSA PUBLIC", re.I)
        strings = []
        for entry in analysis.strings:
            if not pattern.search(entry["text"]):
                continue
            address = int(entry["ea"], 16)
            refs = analysis.rip_refs.get(address, [])
            strings.append({**entry, "references": [hex(ea) for ea in refs],
                            "functions": sorted({hex(analysis.containing(ea)[0]) for ea in refs if analysis.containing(ea)})})
        write_json(args.output / "strings.json", strings)
        keys = []
        for base, name, data in analysis.segments:
            if name != ".rdata":
                continue
            for match in re.finditer(rb"-----BEGIN RSA PUBLIC KEY-----.*?-----END RSA PUBLIC KEY-----", data, re.S):
                address = base + match.start()
                target = args.output / f"archive-key-{address:x}.pem"
                target.write_bytes(match[0] + b"\n")
                refs = analysis.rip_refs.get(address, [])
                keys.append({"ea": hex(address), "file": target.name,
                             "references": [hex(ea) for ea in refs],
                             "functions": sorted({hex(analysis.containing(ea)[0]) for ea in refs if analysis.containing(ea)})})
        write_json(args.output / "archive-keys.json", keys)
        print(f"IDA {analysis.version}, SHA256={analysis.sha256}, {len(strings)} event strings, {len(keys)} public archive keys", flush=True)
        for requested in args.find_bytes:
            query = bytes.fromhex(requested)
            matches = []
            for base, name, data in analysis.segments:
                cursor = 0
                while (offset := data.find(query, cursor)) >= 0:
                    ea = base + offset
                    ranges = analysis.containing(ea)
                    matches.append({"ea": hex(ea), "segment": name, "function": hex(ranges[0]) if ranges else None})
                    cursor = offset + 1
            print(f"Byte query {requested}: {matches}", flush=True)
        for requested in args.callers:
            ea = int(requested, 0)
            write_json(args.output / f"callers-{ea:x}.json", {
                "ea": hex(ea), "sha256": analysis.sha256,
                "callers": [{"ea": hex(x), "function": hex(analysis.containing(x)[0]) if analysis.containing(x) else None,
                             "instructions": analysis.decode_range(max(analysis.base, x - 24), x + 16)} for x in analysis.call_refs.get(ea, [])],
            })
        for requested in args.address:
            ea = int(requested, 0)
            write_json(args.output / f"address-{ea:x}.json", {
                "ea": hex(ea), "bytes": analysis.bytes.get_bytes(ea, 256).hex(),
                "references": [hex(x) for x in analysis.rip_refs.get(ea, [])],
            })
        for requested in args.range:
            address, size = (int(part, 0) for part in requested.split(":"))
            data = analysis.bytes.get_bytes(address, size)
            if data is None or len(data) != size:
                raise ValueError(f"IDA could not read range {requested}")
            write_json(args.output / f"range-{address:x}.json", {
                "ea": hex(address), "size": size, "sha256": analysis.sha256, "bytes": data.hex(),
            })
        if args.function:
            analysis.hexrays.init_hexrays_plugin()
            for requested in args.function:
                ea = int(requested, 0)
                ranges = analysis.containing(ea)
                if not ranges:
                    analysis.ua.create_insn(ea)
                    analysis.funcs.add_func(ea)
                    function = analysis.funcs.get_func(ea)
                    if function is None:
                        raise ValueError(f"IDA could not define leaf function {ea:#x}")
                    ranges = function.start_ea, function.end_ea
                start, end = ranges
                analysis.ua.create_insn(start)
                if not analysis.funcs.get_func(start):
                    analysis.funcs.add_func(start, end)
                analysis.auto.plan_and_wait(start, end)
                function = analysis.funcs.get_func(start)
                if function is None:
                    raise ValueError(f"IDA could not define function {start:#x}")
                instructions = analysis.decode_range(start, end)
                function_bytes = analysis.bytes.get_bytes(start, end - start)
                disassembly = [analysis.lines.tag_remove(analysis.idc.generate_disasm_line(int(item["ea"], 16), 0) or "") for item in instructions]
                decompiled = analysis.hexrays.decompile(start)
                write_json(args.output / f"function-{start:x}.json", {
                    "ea": hex(start), "end": hex(end), "sha256": analysis.sha256,
                    "function_sha256": hashlib.sha256(function_bytes).hexdigest(),
                    "instructions": [{**item, "text": text} for item, text in zip(instructions, disassembly)],
                    "decompilation": str(decompiled) if decompiled else None,
                })
                print(f"Exported function {start:#x}: {len(instructions)} instructions", flush=True)
    finally:
        analysis.close(True)


if __name__ == "__main__":
    main()
