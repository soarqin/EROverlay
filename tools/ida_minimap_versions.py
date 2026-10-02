"""Locate Minimap dependencies in historical Elden Ring EXEs with Python idapro.

Only IDA-loaded bytes and IDA's instruction decoder are used for code analysis.
The reference profile contains relocation-masked instruction windows; each hit
is checked against PE unwind function ranges and decoded again by IDA.
Database files and full exports belong under build/ida/versions (gitignored).
No debugger, game process, executable modification or global auto-analysis.
"""

from __future__ import annotations

import argparse
import bisect
import hashlib
import json
import re
import struct
import time
import warnings
from collections import Counter, defaultdict
from pathlib import Path


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def load_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


class Analysis:
    def __init__(self, exe: Path, database: Path):
        import idapro
        import ida_auto
        import ida_bytes
        import ida_funcs
        import ida_hexrays
        import ida_lines
        import ida_nalt
        import ida_segment
        import ida_ua
        import idaapi
        import idautils
        import idc

        warnings.filterwarnings("ignore", category=DeprecationWarning)
        self.idapro, self.bytes, self.funcs = idapro, ida_bytes, ida_funcs
        self.auto, self.hexrays, self.lines = ida_auto, ida_hexrays, ida_lines
        self.ua, self.idc, self.nalt = ida_ua, idc, ida_nalt
        self.exe, self.database = exe.resolve(), database.resolve()
        with exe.open("rb") as stream:
            self.sha256 = hashlib.file_digest(stream, "sha256").hexdigest()
        database.parent.mkdir(parents=True, exist_ok=True)
        source = database if database.exists() else exe
        options = " -a" if database.exists() else f'-a -o"{database.resolve()}"'
        result = idapro.open_database(str(source.resolve()), False, options)
        if result:
            raise RuntimeError(f"idapro.open_database returned {result}")
        actual = ida_nalt.retrieve_input_file_sha256()
        if actual is None or actual.hex() != self.sha256:
            current_input = ida_nalt.get_input_file_path()
            idapro.close_database(False)
            raise RuntimeError(f"IDA database SHA256 mismatch: expected={self.sha256}, actual={actual.hex() if actual else None}, input={current_input}")
        self.base = ida_nalt.get_imagebase()
        self.version = idaapi.get_kernel_version()
        self.segments = []
        for ea in idautils.Segments():
            segment = ida_segment.segment_info_t()
            if ida_segment.get_segment_info(segment, ea, ida_segment.GSI_NAME):
                name = segment.get_name()
                if name in (".text", ".rdata", ".data", ".pdata"):
                    data = ida_bytes.get_bytes(segment.start_ea, segment.end_ea - segment.start_ea)
                    if data:
                        self.segments.append((segment.start_ea, name, data))
        self.image_end = max(ea + len(data) for ea, _, data in self.segments)
        ranges = set()
        unwind_by_range = {}
        for ea, name, data in self.segments:
            if name != ".pdata":
                continue
            for offset in range(0, len(data) - 11, 12):
                start, end, unwind = struct.unpack_from("<III", data, offset)
                if 0 < start < end and end < self.image_end - self.base:
                    ranges.add((self.base + start, self.base + end))
                    unwind_by_range[self.base + start] = self.base + unwind
        self.ranges = sorted(ranges)
        self.starts = [start for start, _ in self.ranges]
        self.roots = {}
        self.function_ranges = defaultdict(list)
        for start, end in self.ranges:
            root = start
            for _ in range(16):
                unwind = unwind_by_range.get(root)
                if not unwind:
                    break
                header = ida_bytes.get_bytes(unwind, 4)
                if not header or not (header[0] >> 3) & 4:
                    break
                chain = unwind + 4 + ((header[2] + 1) & ~1) * 2
                chain_bytes = ida_bytes.get_bytes(chain, 12)
                if not chain_bytes:
                    break
                parent = self.base + struct.unpack_from("<I", chain_bytes)[0]
                if parent == root:
                    break
                root = parent
            self.roots[start] = root
            self.function_ranges[root].append((start, end))
        self.rip_refs = defaultdict(list)
        self.call_refs = defaultdict(list)
        self.strings = []

    def close(self, save: bool = False):
        self.idapro.close_database(save)

    def containing(self, ea: int):
        index = bisect.bisect_right(self.starts, ea) - 1
        if index >= 0 and ea < self.ranges[index][1]:
            start, end = self.ranges[index]
            root = self.roots[start]
            return root, max(b for _, b in self.function_ranges[root])
        function = self.funcs.get_func(ea)
        if function:
            return function.start_ea, function.end_ea
        return None

    def decode(self, ea: int, mask: bool = True):
        insn = self.ua.insn_t()
        size = self.ua.decode_insn(insn, ea)
        if not size:
            return None
        raw = self.bytes.get_bytes(ea, size)
        if raw is None:
            return None
        mask_bytes = bytearray(b"\xff" * size)
        operands = []
        for op in insn.ops:
            if op.type == self.ua.o_void:
                break
            operands.append({"type": op.type, "dtype": op.dtype, "reg": op.reg,
                             "addr": hex(op.addr), "value": hex(op.value), "offb": op.offb})
            if not mask or not op.offb:
                continue
            width = 0
            if op.type in (self.ua.o_near, self.ua.o_far):
                width = size - op.offb
            elif op.type == self.ua.o_mem and self.base <= op.addr < self.image_end:
                width = min(4, size - op.offb)
            elif op.type == self.ua.o_imm and self.base <= op.value < self.image_end:
                width = min(8, size - op.offb)
            elif op.type == self.ua.o_displ and 0x100000 <= op.addr < self.image_end - self.base:
                # Embedded image RVAs in MSVC switch jump-table addressing.
                width = min(4, size - op.offb)
            if 0 < width <= 8:
                mask_bytes[op.offb:op.offb + width] = b"\x00" * width
        return {"ea": hex(ea), "size": size, "bytes": raw.hex(), "mask": mask_bytes.hex(),
                "mnemonic": insn.get_canon_mnem() or "", "operands": operands}

    def decode_range(self, start: int, end: int):
        instructions = []
        ea = start
        while ea < end:
            item = self.decode(ea)
            if item is None:
                ea += 1
                continue
            instructions.append(item)
            ea += item["size"]
        return instructions

    def index_references(self):
        for base, name, data in self.segments:
            if name == ".text":
                for match in re.finditer(rb"[\x48-\x4f][\x8b\x8d][\x05\x0d\x15\x1d\x25\x2d\x35\x3d]....", data, re.DOTALL):
                    ea = base + match.start()
                    target = ea + 7 + struct.unpack_from("<i", match[0], 3)[0]
                    self.rip_refs[target].append(ea)
                for match in re.finditer(rb"[\xe8\xe9]....", data, re.DOTALL):
                    ea = base + match.start()
                    target = ea + 5 + struct.unpack_from("<i", match[0], 1)[0]
                    self.call_refs[target].append(ea)
            elif name in (".rdata", ".data"):
                for expression, encoding, term in [(rb"[\x20-\x7e]{5,}\x00", "ascii", 1),
                                                    (rb"(?:[\x20-\x7e]\x00){5,}\x00\x00", "utf-16-le", 2)]:
                    for match in re.finditer(expression, data):
                        self.strings.append({"ea": hex(base + match.start()),
                                             "text": match[0][:-term].decode(encoding), "encoding": encoding})
        string_by_address = {int(s["ea"], 16): s["text"] for s in self.strings}
        # Locate engine-submitted m60/m61/map-category constants and atlas path
        # tables independently of any version's RVA.
        self.native_tables = []
        for ea, name, data in self.segments:
            if name != ".rdata":
                continue
            for offset in range(0, len(data) - 15, 16):
                pointer = struct.unpack_from("<Q", data, offset)[0]
                text = string_by_address.get(pointer)
                if text not in ("71_MapTile", "01_Common"):
                    continue
                self.native_tables.append({"va": hex(ea + offset), "name": text,
                    "pointer": hex(pointer), "flags": data[offset + 8:offset + 16].hex()})

    def incoming(self, target: int, calls: bool = False):
        result = []
        for ea in (self.call_refs if calls else self.rip_refs).get(target, []):
            function = self.containing(ea)
            if not function:
                continue
            item = self.decode(ea)
            if item and any(op["type"] in (self.ua.o_near, self.ua.o_far, self.ua.o_mem)
                            and int(op["addr"], 16) == target for op in item["operands"]):
                result.append({"ea": hex(ea), "function": hex(function[0]), "bytes": item["bytes"]})
        return result

    def rtti(self, text: str):
        results = []
        for item in self.strings:
            if item["text"] != text:
                continue
            descriptor = int(item["ea"], 16) - 16
            tables = []
            for segment_ea, name, data in self.segments:
                if name != ".rdata":
                    continue
                for match in re.finditer(re.escape(struct.pack("<I", descriptor - self.base)), data):
                    offset = match.start() - 12
                    if offset < 0 or offset + 24 > len(data):
                        continue
                    fields = struct.unpack_from("<6I", data, offset)
                    locator = segment_ea + offset
                    if fields[0] != 1 or fields[5] != locator - self.base:
                        continue
                    for pointer in re.finditer(re.escape(struct.pack("<Q", locator)), data):
                        vtable = segment_ea + pointer.start() + 8
                        tables.append({"locator": hex(locator), "vtable": hex(vtable),
                                       "object_offset": fields[1], "references": self.incoming(vtable),
                                       "functions": [hex(self.bytes.get_qword(vtable + i * 8)) for i in range(16)]})
            results.append({"name": text, "type_descriptor": hex(descriptor), "vtables": tables})
        return results


def windows(instructions: list[dict]):
    candidates = []
    for index in range(len(instructions)):
        raw, mask = bytearray(), bytearray()
        previous = None
        for insn in instructions[index:index + 24]:
            ea = int(insn["ea"], 16)
            if previous is not None and ea != previous:
                break
            raw.extend(bytes.fromhex(insn["bytes"]))
            mask.extend(bytes.fromhex(insn["mask"]))
            previous = ea + insn["size"]
            if len(raw) >= 56:
                break
        if len(raw) < 7:
            continue
        literal_runs = list(re.finditer(rb"\xff+", mask))
        if not literal_runs:
            continue
        longest = max(literal_runs, key=lambda m: m.end() - m.start())
        if longest.end() - longest.start() < 5:
            continue
        candidates.append({"ea": instructions[index]["ea"], "offset": index, "bytes": raw.hex(),
                           "mask": mask.hex(), "anchor_offset": longest.start(),
                           "anchor": raw[longest.start():longest.end()].hex(), "weight": sum(bool(v) for v in mask)})
    selected = []
    # Spread windows through the function, retaining the strongest literal.
    for part in range(10):
        group = [w for w in candidates if w["offset"] * 10 // max(1, len(instructions)) == part]
        if group:
            selected.append(max(group, key=lambda w: (len(w["anchor"]), w["weight"])))
    if candidates and not any(w["offset"] == 0 for w in selected):
        first = next((w for w in candidates if w["offset"] == 0), None)
        if first:
            selected.append(first)
    return selected


def shape(instructions: list[dict]):
    value = bytearray()
    for item in instructions:
        raw, mask = bytes.fromhex(item["bytes"]), bytes.fromhex(item["mask"])
        value.extend(bytes(b & m for b, m in zip(raw, mask)))
        value.extend(mask)
    return hashlib.sha256(value).hexdigest()


def reference(args):
    a = Analysis(args.exe, args.database)
    try:
        query = load_json(args.queries)
        if query.get("sha256", a.sha256).lower() != a.sha256:
            raise RuntimeError("Reference query SHA256 differs from the executable")
        purposes = {}
        for path in (Path("docs/minimap-game-textures.evidence.json"), Path("docs/minimap-player-markers.evidence.json")):
            for item in load_json(path).get("functions", []):
                purposes[item["va"].lower()] = item.get("purpose", "")
        functions = []
        for va in query["functions"]:
            start = int(va, 16)
            f = a.funcs.get_func(start)
            if not f or f.start_ea != start:
                continue
            instructions = []
            # Reuse IDA's established function items, including chunk boundaries.
            import idautils
            for ea in idautils.FuncItems(start):
                if a.bytes.is_code(a.bytes.get_flags(ea)):
                    insn = a.decode(ea)
                    if insn:
                        instructions.append(insn)
            functions.append({"va": hex(start), "rva": hex(start - a.base), "end": hex(f.end_ea),
                              "purpose": purposes.get(hex(start), ""), "instruction_count": len(instructions),
                              "shape_sha256": shape(instructions), "windows": windows(instructions),
                              "instructions": [{"offset": int(i["ea"], 16) - start, "size": i["size"],
                                                "bytes": i["bytes"], "mask": i["mask"],
                                                "operands": i["operands"]} for i in instructions]})
        a.index_references()
        result = {"exe": str(a.exe), "sha256": a.sha256, "ida_version": a.version, "base": hex(a.base),
                  "functions": functions}
        write_json(args.output / "reference.json", result)
        print(f"Reference: {len(functions)} functions, {sum(len(f['windows']) for f in functions)} windows", flush=True)
    finally:
        a.close()


RTTI_NAMES = [
    ".?AVWorldMapViewModel@CS@@", ".?AVWorldMapMarkerData@CS@@",
    ".?AVWorldMapMarkerDataList@CS@@", ".?AVCSMenuMarkersSaveData@CS@@",
    ".?AVFD4FileManagerImp@FD4@@", ".?AVSoloParamRepositoryImp@CS@@",
    ".?AVCSMenuManImp@CS@@",
    ".?AVWorldMapWarpPinData@CS@@", ".?AVWorldMapPointPinData@CS@@",
    ".?AVWorldMapAreaConverter@CS@@", ".?AVWorldMapLegacyConverter@CS@@",
    ".?AVWorldMapTileBackReader@CS@@", ".?AVWorldMapWarpData@CS@@",
]

HOOK_PATTERNS = {
    "menu": "48 8B 0D ?? ?? ?? ?? 48 8B 49 08 E8 ?? ?? ?? ?? 48 8B D0 48 8B CE E8",
    "game_data": "48 8B 05 ?? ?? ?? ?? 48 85 C0 74 05 48 8B 40 58 C3 C3",
    "event_flags": "48 8B 3D ?? ?? ?? ?? 48 85 FF ?? ?? 32 C0 E9",
    "field": "48 8B 0D ?? ?? ?? ?? 48 ?? ?? ?? 44 0F B6 61 ?? E8 ?? ?? ?? ?? 48 63 87 ?? ?? ?? ?? 48 ?? ?? ?? 48 85 C0",
}


def compile_window(window: dict):
    raw, mask = bytes.fromhex(window["bytes"]), bytes.fromhex(window["mask"])
    expression = b"".join(re.escape(bytes((b,))) if m else b"." for b, m in zip(raw, mask))
    return re.compile(expression, re.DOTALL), bytes.fromhex(window["anchor"]), window["anchor_offset"]


def scan_window(a: Analysis, window: dict):
    expression, anchor, anchor_offset = compile_window(window)
    hits = []
    for base, name, data in a.segments:
        if name != ".text":
            continue
        search = 0
        while True:
            found = data.find(anchor, search)
            if found < 0:
                break
            search = found + 1
            offset = found - anchor_offset
            if offset >= 0 and expression.match(data, offset):
                ea = base + offset
                function = a.containing(ea)
                if not function and window.get("offset") == 0 and a.decode(ea):
                    # Leaf functions have no RUNTIME_FUNCTION record. A full
                    # entry window plus instruction decoding anchors them.
                    function = (ea, ea + len(bytes.fromhex(window["bytes"])))
                if function and a.decode(ea):
                    hits.append((function[0], function[1], ea))
                    if len(hits) > 120:
                        return []
    return hits


def analyze(args):
    started = time.monotonic()
    profile = load_json(args.profile)
    a = Analysis(args.exe, args.database)
    try:
        print(f"IDA {a.version}: loaded {args.label or args.exe} ({len(a.ranges)} unwind ranges)", flush=True)
        a.index_references()
        matches = []
        exports = {}
        translated = defaultdict(lambda: defaultdict(list))
        def same_reference(ref, start):
            ref_start, ref_end = int(ref["va"], 16), int(ref["end"], 16)
            for insn in ref.get("instructions", []):
                actual = a.bytes.get_bytes(start + insn["offset"], insn["size"])
                raw, mask = bytes.fromhex(insn["bytes"]), bytes.fromhex(insn["mask"])
                if not actual or any((x & m) != (y & m) for x, y, m in zip(actual, raw, mask)):
                    return False
                decoded = a.decode(start + insn["offset"])
                if not decoded or decoded["size"] != insn["size"]:
                    return False
                for source_op, target_op in zip(insn.get("operands", []), decoded["operands"]):
                    source = int(source_op["addr"], 16)
                    if source_op["type"] == a.ua.o_near and ref_start <= source < ref_end:
                        if int(target_op["addr"], 16) - start != source - ref_start:
                            return False
            return bool(ref.get("instructions"))
        for ref in profile["functions"]:
            scores, support, ends = Counter(), defaultdict(list), {}
            for index, window in enumerate(ref["windows"]):
                hits = scan_window(a, window)
                starts = set(start for start, _, _ in hits)
                for start, end, ea in hits:
                    ends[start] = end
                    support[start].append({"window": index, "ea": hex(ea),
                                           "reference_ea": window["ea"], "weight": window["weight"]})
                for start in starts:
                    scores[start] += window["weight"] / max(1, len(starts))
            best = scores.most_common(5)
            exact = [start for start, _ in best if same_reference(ref, start)]
            if len(exact) == 1:
                best.sort(key=lambda pair: (pair[0] != exact[0], -pair[1]))
            item = {"reference": ref.get("canonical_va", ref["va"]), "source_reference": ref["va"],
                    "source_version": ref.get("source_version", "1.17.1"), "purpose": ref["purpose"],
                    "candidates": [{"va": hex(start), "end": hex(ends[start]), "score": round(score, 3),
                                     "support": support[start]} for start, score in best]}
            if best and (len(exact) == 1 or len(best) == 1 or best[0][1] >= best[1][1] * 1.5):
                start, _ = best[0]
                if len(exact) == 1 or len(set(hit["window"] for hit in support[start])) >= 2 or len(ref["windows"]) <= 2:
                    item["va"], item["rva"] = hex(start), hex(start - a.base)
                    item["end"] = hex(ends[start])
                    identical = same_reference(ref, start)
                    instructions = exports.get(start)
                    if instructions is None:
                        if identical:
                            instructions = [a.decode(start + i["offset"]) for i in ref["instructions"]]
                            item["end"] = hex(start + int(ref["end"], 16) - int(ref["va"], 16))
                        else:
                            instructions = []
                            for chunk_start, chunk_end in a.function_ranges.get(start, [(start, ends[start])]):
                                instructions.extend(a.decode_range(chunk_start, chunk_end))
                        exports[start] = instructions
                    item["shape_sha256"] = shape(instructions)
                    item["same_shape_as_reference"] = identical
            matches.append(item)
            if "va" in item:
                ref_instructions = {int(ref["va"], 16) + insn["offset"]: insn for insn in ref.get("instructions", [])}
                used = set()
                mapping_hits = item["candidates"][0]["support"]
                if item["same_shape_as_reference"] and ref.get("instructions"):
                    mapping_hits = [{"window": -1, "reference_ea": ref["va"], "ea": item["va"]}]
                for hit in mapping_hits:
                    window = ref["windows"][hit["window"]] if hit["window"] >= 0 else None
                    ref_ea, target_ea = int(hit["reference_ea"], 16), int(hit["ea"], 16)
                    end = ref_ea + len(bytes.fromhex(window["bytes"])) if window else int(ref["end"], 16)
                    while ref_ea < end:
                        ref_insn = ref_instructions.get(ref_ea)
                        target_insn = a.decode(target_ea)
                        if not ref_insn or not target_insn or ref_insn["size"] != target_insn["size"]:
                            break
                        if ref_ea not in used:
                            for source_op, dest_op in zip(ref_insn.get("operands", []), target_insn["operands"]):
                                if source_op["type"] != dest_op["type"]:
                                    continue
                                if source_op["type"] in (a.ua.o_near, a.ua.o_far, a.ua.o_mem):
                                    source_value, dest_value = source_op["addr"], dest_op["addr"]
                                elif source_op["type"] == a.ua.o_imm:
                                    source_value, dest_value = source_op["value"], dest_op["value"]
                                else:
                                    continue
                                if int(profile["base"], 16) <= int(source_value, 16) < int(profile["base"], 16) + 0x7000000:
                                    translated[source_value][dest_value].append({"reference_function": ref["va"],
                                        "function": item["va"], "reference_instruction": hex(ref_ea),
                                        "instruction": hex(target_ea)})
                            used.add(ref_ea)
                        ref_ea += ref_insn["size"]
                        target_ea += target_insn["size"]
        # Different baseline versions contribute variants of the same semantic
        # function. Prefer a complete decoded-body match over partial windows.
        merged = {}
        for item in matches:
            key = item["reference"]
            old = merged.get(key)
            rank = lambda m: (m.get("same_shape_as_reference", False), "va" in m,
                              len(m["candidates"][0]["support"]) if m["candidates"] else 0)
            if old is None or rank(item) > rank(old):
                merged[key] = item
        matches = list(merged.values())
        selected = re.compile(r"WorldMap|MapTile|MenuMarkers|IsChangeableBaseMap|SB_MapCursor|71_MapTile|menu:.*(?:WorldMap|Common)|menutpfbnd|%02d|CSMenuMarkersSaveData\.cpp", re.IGNORECASE)
        string_evidence = [{**s, "references": a.incoming(int(s["ea"], 16))} for s in a.strings if selected.search(s["text"])]
        rtti_evidence = [entry for name in RTTI_NAMES for entry in a.rtti(name)]
        hooks = {}
        for name, pattern in HOOK_PATTERNS.items():
            tokens = pattern.split()
            raw = bytes(0 if t == "??" else int(t, 16) for t in tokens)
            mask = bytes(0 if t == "??" else 255 for t in tokens)
            longest = max(re.finditer(rb"\xff+", mask), key=lambda m: m.end() - m.start())
            window = {"bytes": raw.hex(), "mask": mask.hex(), "anchor": raw[longest.start():longest.end()].hex(), "anchor_offset": longest.start(), "offset": 0}
            hooks[name] = [{"ea": hex(ea), "function": hex(start),
                            "target": next((op["addr"] for op in a.decode(ea)["operands"] if op["type"] == a.ua.o_mem), None)}
                           for start, _, ea in scan_window(a, window)]
        write_json(args.output / "discovery.json", {"label": args.label, "exe": str(a.exe),
                    "sha256": a.sha256, "reference_sha256": profile["sha256"], "ida_version": a.version,
                    "analysis_mode": "targeted_no_global_auto", "base": hex(a.base), "functions": matches,
                    "hooks": hooks, "rtti": rtti_evidence, "strings": string_evidence,
                    "address_correspondences": {source: [{"target": target, "evidence": hits} for target, hits in destinations.items()]
                                               for source, destinations in translated.items()}})
        for start, instructions in exports.items():
            write_json(args.output / f"decoded_{start:x}.json", {"va": hex(start), "sha256": a.sha256,
                       "instructions": instructions})
        found = sum("va" in m for m in matches)
        same = sum(m.get("same_shape_as_reference", False) for m in matches)
        print(f"Located {found}/{len(matches)} functions, {same} identical relocation-masked bodies ({time.monotonic()-started:.1f}s)", flush=True)
        if args.export:
            request = load_json(args.export)
            chosen = request.get("references", [])
            addresses = request.get("functions", [])
            addresses.extend(m["va"] for m in matches if m["reference"] in chosen and "va" in m)
            export_functions(a, args.output, addresses)
        if args.save_database:
            a.close(True)
        else:
            a.close(False)
    except BaseException:
        a.close(False)
        raise


def export_functions(a: Analysis, output: Path, addresses: list[str]):
    initialized = a.hexrays.init_hexrays_plugin()
    for address in dict.fromkeys(addresses):
        start = int(address, 16)
        f = a.funcs.get_func(start)
        if not f:
            bounds = a.containing(start)
            a.ua.create_insn(start)
            a.funcs.add_func(start, bounds[1] if bounds and bounds[0] == start else a.idc.BADADDR)
            f = a.funcs.get_func(start)
        if not f:
            print(f"No function at {address}", flush=True)
            continue
        a.auto.plan_and_wait(f.start_ea, f.end_ea)
        import idautils
        instructions = []
        for ea in idautils.FuncItems(f.start_ea):
            if not a.bytes.is_code(a.bytes.get_flags(ea)):
                continue
            insn = a.decode(ea)
            if insn:
                insn["disassembly"] = a.lines.tag_remove(a.idc.generate_disasm_line(ea, 0) or "")
                instructions.append(insn)
        item = {"requested": address, "start": hex(f.start_ea), "end": hex(f.end_ea), "sha256": a.sha256,
                "callers": a.incoming(f.start_ea, True), "instructions": instructions}
        if initialized:
            try:
                cfunc = a.hexrays.decompile(f.start_ea, flags=a.hexrays.DECOMP_NO_WAIT)
                if cfunc:
                    item["pseudocode"] = "\n".join(a.lines.tag_remove(line.line) for line in cfunc.get_pseudocode())
            except a.hexrays.DecompilationFailure as error:
                item["decompile_error"] = str(error)
        write_json(output / f"function_{f.start_ea:x}.json", item)
        print(f"Exported {address} as {f.start_ea:#x}", flush=True)
    write_json(output / "export-summary.json", {"sha256": a.sha256,
               "requested": list(dict.fromkeys(addresses)), "database": str(a.database)})


def append_variants(args):
    """Build a reusable profile from already exported, hash-bound functions."""
    profile = load_json(args.profile)
    purposes = {f.get("canonical_va", f["va"]): f["purpose"] for f in profile["functions"]}
    for query in load_json(args.variants):
        function = load_json(Path(query["export"]))
        start = int(function["start"], 0)
        instructions = function["instructions"]
        profile["functions"].append({
            "va": hex(start), "canonical_va": query["reference"],
            "source_version": query["label"], "source_sha256": function["sha256"],
            "end": function["end"], "purpose": purposes.get(query["reference"], ""),
            "instruction_count": len(instructions), "shape_sha256": shape(instructions),
            "windows": windows(instructions),
            "instructions": [{"offset": int(i["ea"], 0) - start, "size": i["size"],
                              "bytes": i["bytes"], "mask": i["mask"], "operands": i["operands"]}
                             for i in instructions]})
    write_json(args.output / "reference.json", profile)
    print(f"Profile: {len(profile['functions'])} function entries", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("reference", "analyze", "export", "variants"))
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--database", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--queries", type=Path, default=Path("tools/ida_minimap_queries.json"))
    parser.add_argument("--profile", type=Path, default=Path("build/ida/versions/reference.json"))
    parser.add_argument("--label")
    parser.add_argument("--export", type=Path, help="JSON with reference VAs and/or discovered function VAs to decompile")
    parser.add_argument("--save-database", action="store_true")
    parser.add_argument("--memory-check", type=Path, help="JSON address lists for IDA-loaded data and RIP references")
    parser.add_argument("--variants", type=Path, help="JSON exports/reference/label list for variants mode")
    args = parser.parse_args()
    if args.mode == "variants":
        if not args.variants:
            parser.error("variants mode requires --variants")
        append_variants(args)
        return
    if args.exe is None or args.database is None:
        parser.error("IDA operations require --exe and --database")
    if args.mode == "reference":
        reference(args)
    elif args.mode == "analyze":
        analyze(args)
    else:
        a = Analysis(args.exe, args.database)
        try:
            a.index_references()
            request = load_json(args.export)
            addresses = request.get("functions", [])
            for address in request.get("caller_functions_of", []):
                addresses.extend(hit["function"] for hit in a.incoming(int(address, 0), True))
            for query in request.get("group_accessors", []):
                for hit in a.incoming(int(query["getter"], 0), True):
                    ea = int(hit["ea"], 0)
                    start = int(hit["function"], 0)
                    if ea - start > 256:
                        continue
                    instructions = a.decode_range(start, ea)
                    for insn in instructions[-8:]:
                        operands = insn["operands"]
                        if len(operands) < 2 or operands[0]["type"] != a.ua.o_reg or operands[0]["reg"] != 2:
                            continue
                        value = int(operands[1]["value"], 0) if operands[1]["type"] == a.ua.o_imm else int(operands[1]["addr"], 0)
                        if value in query["groups"]:
                            addresses.append(hit["function"])
            chosen = request.get("references", [])
            if chosen:
                discovery = load_json(args.output / "discovery.json")
                addresses.extend(m["va"] for m in discovery["functions"] if m["reference"] in chosen and "va" in m)
            export_functions(a, args.output, addresses)
            if args.memory_check:
                checks = load_json(args.memory_check)
                records = []
                for query in checks.get("addresses", []):
                    address = int(query["va"], 0) if isinstance(query, dict) else int(query, 0)
                    size = query.get("size", 256) if isinstance(query, dict) else 256
                    records.append({"va": hex(address), "bytes": (a.bytes.get_bytes(address, size) or b"").hex(),
                                    "references": a.incoming(address), "callers": a.incoming(address, True)})
                range_records = []
                for query in checks.get("ranges", []):
                    start, end = int(query["start"], 0), int(query["end"], 0)
                    range_records.append({"start": hex(start), "end": hex(end),
                                          "instructions": a.decode_range(start, end),
                                          "functions": [{"start": hex(s), "end": hex(e)} for s, e in a.ranges
                                                        if start <= s < end]})
                write_json(args.output / "memory-check.json", {"sha256": a.sha256, "records": records,
                    "ranges": range_records, "native_tables": a.native_tables})
        finally:
            a.close(args.save_database)


if __name__ == "__main__":
    main()
