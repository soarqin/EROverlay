"""Verify a hash-independent Minimap scanner using Python idapro-loaded EXEs.

Two shared masked anchors identify each dependency. Runtime-function chains,
rel32 calls, RIP globals and layout operands are checked independently of the
per-EXE expected evidence. IDA is a research dependency, not a runtime one.
No debugger, game writes or calls into the game are involved.
"""

from __future__ import annotations

import argparse
import json
import re
import struct
import subprocess
import sys
import time
from pathlib import Path

from ida_minimap_versions import Analysis, HOOK_PATTERNS, compile_window, scan_window

ROOT = Path("build/ida/versions")
WORK = ROOT / "scanner"

# Semantic functions, shared across builds. These are reference identifiers
# for authoring signatures, never addresses supplied to the scanned EXE.
FUNCTIONS = {
    "submit": "0x1426eeb60", "flush": "0x1426eed40", "enqueue": "0x1426f42e0", "cancel": "0x1426ee2c0",
    "allocate": "0x1401a2720", "request_init": "0x1426f9820", "complete": "0x1426f94c0",
    "pending_insert": "0x1426ee320",
    "cleanup": "0x1426f9340", "retire": "0x1426f4fd0", "allocator_anchor": "0x1401f4f00",
    "common_param": "0x140d2b040", "piece_param": "0x140d58b40", "event_storage": "0x1405fa250",
    "view_ctor": "0x1408865a0", "player": "0x140887c30", "death": "0x140888860",
    "piece_masks": "0x1408892c0", "grace_ctor": "0x14088c7a0",
    "marker_save_ctor": "0x14081a550", "marker_bind": "0x14087a220", "marker_number": "0x140879330",
    "marker_save_bind": "0x140819ad0", "marker_add": "0x140819d50", "marker_place": "0x140887440",
    "marker_list_add": "0x14087a3a0",
    "convert": "0x140877130", "legacy_lookup": "0x1408785d0",
}


def load(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def write(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + chr(10), encoding="utf-8")


def window(pattern):
    tokens = pattern.split()
    raw = bytes(0 if token == "??" else int(token, 16) for token in tokens)
    mask = bytes(0 if token == "??" else 255 for token in tokens)
    anchor = max(re.finditer(rb"\xff+", mask), key=lambda run: run.end() - run.start())
    return {"bytes": raw.hex(), "mask": mask.hex(), "anchor": raw[anchor.start():anchor.end()].hex(),
            "anchor_offset": anchor.start(), "offset": 0}


def rules():
    references = load(ROOT / "reference-multi.json")["functions"]
    result = {}
    for name, identity in FUNCTIONS.items():
        variants = [f for f in references if f.get("canonical_va", f["va"]) == identity]
        if not variants:
            raise ValueError(f"Missing IDA reference {name}")
        result[name] = []
        seen = set()
        for variant in variants:
            choices = variant["windows"]
            # Spread both anchors through the function, away from shared
            # epilogues; require agreement on their .pdata root.
            chosen = [choices[0], choices[min(5, len(choices) - 1)]]
            key = tuple((item["bytes"], item["mask"]) for item in chosen)
            if key not in seen:
                seen.add(key)
                result[name].append({"authored_from": variant.get("source_version", "1.17.1"),
                                     "anchors": [{**item, "byte_offset": int(item["ea"], 0) - int(variant["va"], 0)} for item in chosen]})
                if name in ("submit", "flush", "enqueue", "cancel", "allocate", "pending_insert", "request_init", "complete", "cleanup", "retire",
                            "event_storage", "convert", "legacy_lookup", "piece_masks", "marker_add"):
                    result[name][-1]["body_contract"] = [
                        {"offset": instruction["offset"], "size": instruction["size"],
                         "bytes": instruction["bytes"], "mask": instruction["mask"]}
                        for instruction in variant["instructions"]]
    return result


def matches(analysis, pattern, start=None, end=None):
    query = window(pattern) if isinstance(pattern, str) else pattern
    hits = scan_window(analysis, query)
    if start is not None:
        hits = [hit for hit in hits if start <= hit[2] < end]
    return hits


def scanner_unique_value(values, name):
    values = sorted(set(values))
    if len(values) != 1:
        raise ValueError(f"{name}: expected one, got {list(map(hex, values))}")
    return values[0]


def relative(analysis, address, operand, length):
    raw = analysis.bytes.get_bytes(address + operand, 4)
    if not raw:
        raise ValueError(f"Unreadable rel32 at {address:#x}")
    return address + length + struct.unpack("<i", raw)[0]


def find_functions(analysis, shared):
    result, support = {}, {}
    for name, variants in shared.items():
        candidates = set()
        proofs = {}
        for variant in variants:
            first, second = (scan_window(analysis, anchor) for anchor in variant["anchors"])
            roots = {hit[0] for hit in first} & {hit[0] for hit in second}
            # PE unwind metadata omits leaf routines. For an entry anchor,
            # require the second at its authored byte displacement; neither
            # an IDA-created function nor a per-build VA is needed.
            if name == "event_storage":
                entry, later = variant["anchors"]
                delta = later["byte_offset"] - entry["byte_offset"]
                second_hits = {hit[2] for hit in matches(analysis, {**later, "offset": 0})}
                for hit in first:
                    if hit[2] + delta in second_hits and entry["byte_offset"] == 0:
                        roots.add(hit[2])
                        proofs[hit[2]] = [hit[2], hit[2] + delta]
            for root in roots:
                candidates.add(root)
                if root not in proofs:
                    proofs[root] = [next(hit[2] for hit in group if hit[0] == root) for group in (first, second)]
        if name in ("piece_param", "common_param"):
            semantic = {"piece_param": "48 8B 0D ?? ?? ?? ?? 45 33 C0 41 8D 50 58 E8 ?? ?? ?? ??",
                        "common_param": "48 8B 0D ?? ?? ?? ?? 48 85 C9 0F 84 ?? ?? ?? ?? 45 33 C0 BA 8D 00 00 00 E8 ?? ?? ?? ??"}[name]
            candidates &= {hit[0] for hit in matches(analysis, semantic)}
        result[name] = scanner_unique_value(candidates, name)
        support[name] = [hex(address) for address in proofs[result[name]]]
    return result, support


def field_unsigned(analysis, address, offset, size=4):
    raw = analysis.bytes.get_bytes(address + offset, size)
    if not raw:
        raise ValueError(f"Unreadable field at {address:#x}")
    return int.from_bytes(raw, "little")


def local(analysis, function, pattern):
    ranges = analysis.function_ranges.get(function) or [analysis.containing(function)]
    expression, _, _ = compile_window(window(pattern))
    addresses = []
    for start, end in ranges:
        data = analysis.bytes.get_bytes(start, end - start)
        if data:
            addresses.extend(start + match.start() for match in expression.finditer(data) if analysis.decode(start + match.start()))
    return addresses


def check_call(analysis, caller, callee, label):
    ranges = analysis.function_ranges.get(caller) or [analysis.containing(caller)]
    calls = [entry for start, end in ranges for entry in analysis.decode_range(start, end)
             if entry["bytes"].startswith("e8") and entry["size"] == 5]
    if not any(relative(analysis, int(entry["ea"], 0), 1, 5) == callee for entry in calls):
        raise ValueError(f"Call relation missing: {label}")


def guard_bodies(analysis, functions, shared):
    guards = {}
    for name, variants in shared.items():
        contracts = [variant["body_contract"] for variant in variants if "body_contract" in variant]
        if not contracts:
            continue
        root = functions[name]
        for contract in contracts:
            valid = True
            for instruction in contract:
                address = root + instruction["offset"]
                raw = analysis.bytes.get_bytes(address, instruction["size"])
                reference, mask = bytes.fromhex(instruction["bytes"]), bytes.fromhex(instruction["mask"])
                decoded = analysis.decode(address)
                if not raw or not decoded or decoded["size"] != instruction["size"] or any((a & m) != (b & m) for a, b, m in zip(raw, reference, mask)):
                    valid = False
                    break
                # The matcher masks rel32 and short branches. Preserve local
                # control flow as part of the ABI guard, not just opcodes.
                if instruction["mask"].endswith("00") and decoded["mnemonic"] != "call":
                    for op in decoded["operands"]:
                        if op["type"] != analysis.ua.o_near or not op["offb"]:
                            continue
                        offb = op["offb"]
                        displacement = int.from_bytes(reference[offb:], "little", signed=True)
                        target_offset = instruction["offset"] + instruction["size"] + displacement
                        if 0 <= target_offset < contract[-1]["offset"] + contract[-1]["size"] and int(op["addr"], 0) != root + target_offset:
                            valid = False
                            break
                if not valid:
                    break
            if valid:
                guards[name] = {"instruction_count": len(contract), "masked_body_and_local_branches": True}
                break
        if name not in guards:
            raise ValueError(f"ABI contract differs {name}")
    return guards


def guard_fields(analysis, functions, layout):
    support = {}

    def required(name, function, patterns):
        addresses = []
        for pattern in patterns:
            addresses.extend(local(analysis, functions[function], pattern))
        if not addresses:
            raise ValueError(f"ABI field guard missing {name}")
        support[name] = sorted({hex(address) for address in addresses})
        return addresses

    required("player/raw-and-map", "player", ["89 47 14 C7 47 24 FF FF FF FF"])
    required("player/xy-and-map", "player", ["48 89 47 28 89 4F 24", "48 89 47 28 89 5F 24"])
    required("player/underground-u8", "player", ["80 7F 17 0C 0F 94 C0 88 47 30"])
    required("player/angle-f32", "player", ["F3 0F 11 4F 34", "F3 0F 11 47 34"])
    death_conversion = required("death/xy-conversion", "death", ["48 8D 97 AC 00 00 00 E8 ?? ?? ?? ??"])
    if not any(relative(analysis, address + 7, 1, 5) == functions["convert"] for address in death_conversion):
        raise ValueError("Death coordinates must use the resolved converter")
    required("death/valid-u8-and-map-i32", "death", ["88 97 A9 00 00 00 44 89 BF B4 00 00 00",
                                                    "88 8F A9 00 00 00 44 89 BF B4 00 00 00"])
    required("death/home-map-param", "death", ["8B 91 78 02 00 00"])
    required("marker/slots-and-capacity", "marker_save_bind", ["48 89 51 08 48 89 59 10"])
    required("marker/active-count", "marker_save_bind", ["49 83 7E 40 00"])
    required("marker/16-byte-slots", "marker_add", ["48 C1 E0 04", "48 C1 FB 04"])
    required("marker/icon-u8", "marker_number", ["48 63 4F 18 48 03 C9 48 8B 47 10 0F B6 54 C8 0D"])
    required("marker/slot-plus-one", "marker_number", ["44 8B 47 18 41 FF C0"])
    required("marker/xy-map-icon-record", "marker_list_add", ["88 54 24 44 49 8B 00 48 89 44 24 3C 44 88 4C 24 45"])
    check_call(analysis, functions["marker_list_add"], functions["marker_add"], "marker_list_add->marker_add")
    required("marker/save-pointer-in-list", "marker_bind", ["48 89 51 38 48 83 C1 08 E8 ?? ?? ?? ??"])
    hits = required("marker/view-list-bind", "view_ctor", ["49 8D 8F 00 03 00 00 E8 ?? ?? ?? ??",
                                                          "49 8D 8E 00 03 00 00 E8 ?? ?? ?? ??"])
    if not any(relative(analysis, address + 7, 1, 5) == functions["marker_bind"] for address in hits):
        raise ValueError("Marker view binding must call the resolved marker binder")
    limit_hits = required("marker/five-point-limit", "marker_place", ["FF C0 39 05 ?? ?? ?? ??"])
    limits = [relative(analysis, address + 2, 2, 6) for address in limit_hits]
    limit = scanner_unique_value(limits, "marker/limit-global")
    if field_unsigned(analysis, limit, 0) != 5:
        raise ValueError("Ordinary beacon placement limit changed")
    required("marker/numbered-icon-one", "marker_place", ["41 B9 01 00 00 00"])
    save_hits = required("marker/ten-reserved-slots", "marker_save_ctor", ["41 B8 0A 00 00 00 48 8D 8F ?? ?? ?? ?? E8 ?? ?? ?? ??"])
    save_callers = [relative(analysis, address + 13, 1, 5) for address in save_hits]
    if scanner_unique_value(save_callers, "marker/save-bind-call") != functions["marker_save_bind"]:
        raise ValueError("Save constructor must bind ten slots using the resolved save binder")
    required("converters/count", "death", ["48 3B 9F 80 02 00 00", "48 39 9F 80 02 00 00"])
    required("converters/array", "death", ["4C 8D A7 F8 00 00 00"])
    required("converters/stride", "death", ["48 83 C6 30"])
    required("grace/id-and-base-icons", "grace_ctor", ["41 0F 10 06 0F 11 86 38 02 00 00",
                                                      "41 0F 10 07 41 0F 11 86 38 02 00 00"])
    required("grace/normal-base-icon", "grace_ctor", ["89 86 48 02 00 00", "41 89 86 48 02 00 00"])
    required("grace/forbidden-base-icon", "grace_ctor", ["89 86 88 02 00 00", "41 89 86 88 02 00 00"])
    alternate_stores = local(analysis, functions["grace_ctor"], "89 86 C8 02 00 00")
    alternate_stores += local(analysis, functions["grace_ctor"], "89 86 08 03 00 00")
    layout["alternate_icons"] = bool(alternate_stores)
    if layout["alternate_icons"]:
        required("grace/normal-alternate-icon", "grace_ctor", ["89 86 C8 02 00 00"])
        required("grace/forbidden-alternate-icon", "grace_ctor", ["89 86 08 03 00 00"])
    check_call(analysis, functions["convert"], functions["legacy_lookup"], "convert->legacy_lookup")
    # The format/storage guards above deliberately require current common
    # fields. Only the constructor-derived members in layout may shift.
    return {"field_patterns": support, "marker_limit_global": hex(limit),
            "guarded_player_death_marker_converter_fields": True, "dynamic_members": sorted(layout)}


def guard_addresses(analysis, functions, globals_):
    def in_segment(address, names):
        return any(name in names and start <= address < start + len(data) for start, name, data in analysis.segments)

    for name, address in functions.items():
        if not in_segment(address, {".text"}):
            raise ValueError(f"Function outside executable section {name}")
    for name, address in globals_.items():
        if not in_segment(address, {".data"}):
            raise ValueError(f"Global outside writable data section {name}")


def extract(analysis, functions):
    found = {}
    for name, pattern in HOOK_PATTERNS.items():
        hit = scanner_unique_value([hit[2] for hit in matches(analysis, pattern)], f"global/{name}")
        found[name] = relative(analysis, hit, 3, 7)
    cancel = functions["cancel"]
    hit = scanner_unique_value(local(analysis, cancel, "48 8B 15 ?? ?? ?? ?? 48 85 D2 74 ?? 48 8B 82 40 01 00 00"), "file-manager")
    found["manager"] = relative(analysis, hit, 3, 7)
    hit = scanner_unique_value(local(analysis, functions["complete"], "48 8B 05 ?? ?? ?? ?? 0F B7 D1 4C 8B 00 49 8B 40 50 48 C1 E9 10"), "pool-owner/complete")
    found["retire_pool_owner"] = relative(analysis, hit, 3, 7)
    hit = scanner_unique_value(local(analysis, functions["cleanup"], "48 8B 1D ?? ?? ?? ?? 48 8B D7 48 8B CB E8 ?? ?? ?? ?? 48 8B 57 28 48 8B 0B E8 ?? ?? ?? ??"), "pool-owner/cleanup")
    if relative(analysis, hit, 3, 7) != found["retire_pool_owner"] or relative(analysis, hit + 25, 1, 5) != functions["retire"]:
        raise ValueError("Pool owner or retirement call disagrees")
    # Textlist routine is a static allocator anchor only, never called.
    anchor = functions["allocator_anchor"]
    candidates = local(analysis, anchor, "4C 8B 05 ?? ?? ?? ?? 4C 89 45 B7 41 8D 54 24 08 B9 B0 00 00 00")
    hit = scanner_unique_value(candidates, "allocator/first")
    found["allocator"] = relative(analysis, hit, 3, 7)
    hit = scanner_unique_value(local(analysis, anchor, "48 8B 0D ?? ?? ?? ?? 48 89 4D 97 4C 89 6D 8F"), "allocator/second")
    if relative(analysis, hit, 3, 7) != found["allocator"]:
        raise ValueError("Allocator anchors disagree")
    hit = scanner_unique_value(local(analysis, functions["common_param"], "48 8B 0D ?? ?? ?? ?? 48 85 C9 0F 84 ?? ?? ?? ?? 45 33 C0 BA 8D 00 00 00 E8 ?? ?? ?? ??"), "repository/common")
    found["repository"] = relative(analysis, hit, 3, 7)
    getter = relative(analysis, hit + 24, 1, 5)
    hit = scanner_unique_value(local(analysis, functions["piece_param"], "48 8B 0D ?? ?? ?? ?? 45 33 C0 41 8D 50 58 E8 ?? ?? ?? ??"), "repository/piece")
    if relative(analysis, hit, 3, 7) != found["repository"] or relative(analysis, hit + 14, 1, 5) != getter:
        raise ValueError("Repository globals/getters disagree")
    raw = analysis.bytes.get_bytes(getter, 47)
    contract = window("81 FA ?? ?? ?? ?? 7D ?? 48 63 D2 48 8D 04 D2 44 3B 84 C1 80 00 00 00 73 ?? 41 8B C0 48 8D 14 D2 48 03 D0 48 8B 84 D1 88 00 00 00 C3 33 C0 C3")
    if not raw or len(raw) < 47 or any((a & m) != (b & m) for a, b, m in zip(raw, bytes.fromhex(contract["bytes"]), bytes.fromhex(contract["mask"]))):
        raise ValueError("Repository stride/cap getter contract changed")
    layout = {"repository_groups": field_unsigned(analysis, getter, 2)}
    for name in ("common_param", "piece_param"):
        if not local(analysis, functions[name], "48 8B 80 80 00 00 00 48 8B 90 80 00 00 00"):
            raise ValueError(f"Repository cap/table ABI changed {name}")
    # RTTI verifies the constructor, independent of AOB register allocation.
    for name, role in [(".?AVWorldMapViewModel@CS@@", "view_ctor"), (".?AVWorldMapWarpPinData@CS@@", "grace_ctor")]:
        tables = [table for descriptor in analysis.rtti(name) for table in descriptor["vtables"] if table["object_offset"] == 0]
        if not any(functions[role] == int(hit["function"], 0) for table in tables for hit in table["references"]):
            raise ValueError(f"RTTI constructor mismatch {role}")
    # Creation sequence: compare view field, allocate, call ctor, store same
    # field. Both field offsets and allocation size are captured, not keyed
    # off an executable version.
    pattern = "48 83 BB ?? ?? ?? ?? 00 75 ?? 4C 8B 05 ?? ?? ?? ?? 4C 89 44 24 40 BA 08 00 00 00 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 89 44 24 48 48 85 C0 74 ?? 48 8B 93 ?? ?? ?? ?? 48 8B C8 E8 ?? ?? ?? ?? 90 48 89 83 ?? ?? ?? ??"
    candidates = [hit for hit in matches(analysis, pattern) if relative(analysis, hit[2] + 57, 1, 5) == functions["view_ctor"]]
    create = scanner_unique_value([hit[0] for hit in candidates], "view-create")
    hit = scanner_unique_value([hit[2] for hit in candidates], "view-create/operands")
    layout["owner_view"] = field_unsigned(analysis, hit, 3)
    layout["view_bytes"] = field_unsigned(analysis, hit, 28)
    if field_unsigned(analysis, hit, 66) != layout["owner_view"]:
        raise ValueError("View comparison and store offsets disagree")
    owner_candidates = []
    for pattern, disp_size, call_offset in [("48 8B 4B ?? 48 85 C9 74 05 E8 ?? ?? ?? ?? 48 8B 4B 08 E8 ?? ?? ?? ??", 1, 9),
                                            ("48 8B 8B ?? ?? ?? ?? 48 85 C9 74 05 E8 ?? ?? ?? ?? 48 8B 4B 08 E8 ?? ?? ?? ??", 4, 12)]:
        for candidate in matches(analysis, pattern):
            ea = candidate[2]
            if relative(analysis, ea + call_offset, 1, 5) == create:
                owner_candidates.append(field_unsigned(analysis, ea, 3, disp_size))
    layout["menu_owner"] = scanner_unique_value(owner_candidates, "menu-owner")
    menu_tables = [table for descriptor in analysis.rtti(".?AVCSMenuManImp@CS@@") for table in descriptor["vtables"] if table["object_offset"] == 0]
    menu_roots = {int(hit["function"], 0) for table in menu_tables for hit in table["references"]}
    pattern = "40 88 BB ?? ?? ?? ?? 48 8D 8B ?? ?? ?? ?? E8 ?? ?? ?? ?? 90 48 89 BB ?? ?? ?? ??"
    hit = scanner_unique_value([candidate[2] for candidate in matches(analysis, pattern) if candidate[0] in menu_roots], "menu-info")
    layout["menu_info"] = field_unsigned(analysis, hit, 10)
    menu_info_ctor = relative(analysis, hit + 14, 1, 5)
    ctor = analysis.decode_range(menu_info_ctor, menu_info_ctor + 49)
    words = [int(op["addr"], 0) for instruction in ctor for op in instruction["operands"]
             if op["type"] == 4 and op["reg"] == 1 and op["dtype"] == 1 and instruction["bytes"].startswith("66")]
    layout["screen_state"] = layout["menu_info"] + scanner_unique_value(words, "menu-info/u16")
    ctor = functions["view_ctor"]
    hit = scanner_unique_value(local(analysis, ctor, "48 69 D7 ?? ?? ?? ?? 48 03 53 08") + local(analysis, ctor, "48 69 D6 ?? ?? ?? ?? 48 03 57 08"), "grace-stride/imul")
    layout["grace_stride"] = field_unsigned(analysis, hit, 3)
    increment = local(analysis, ctor, "48 81 43 10 ?? ?? ?? ??") + local(analysis, ctor, "48 81 47 10 ?? ?? ?? ??")
    if scanner_unique_value([field_unsigned(analysis, ea, 4) for ea in increment], "grace-stride/increment") != layout["grace_stride"]:
        raise ValueError("Grace copy/increment stride disagrees")
    normal = []
    for pattern, disp_offset in [("C6 86 ?? ?? ?? ?? 00 C7 86 ?? ?? ?? ?? FF FF FF FF C6 86 ?? ?? ?? ?? 01", 19),
                                 ("41 C6 86 ?? ?? ?? ?? 00 41 C7 86 ?? ?? ?? ?? FF FF FF FF 41 C6 86 ?? ?? ?? ?? 01", 22)]:
        normal += [field_unsigned(analysis, ea, disp_offset) for ea in local(analysis, functions["grace_ctor"], pattern)]
    layout["grace_normal"] = scanner_unique_value(normal, "grace-normal")
    if layout["grace_normal"] + 8 != layout["grace_stride"]:
        raise ValueError("Grace layout terminal normal field disagrees")
    area = []
    for code, pattern in [(60, "BA 3C 00 00 00 44 8D 4A 04 44 8D 42 E0"), (61, "BA 3D 00 00 00 44 8D 4A 03 44 8D 42 DF")]:
        if local(analysis, ctor, pattern):
            area.append(code)
    if 60 not in area:
        raise ValueError("Missing m60 converter initialization")
    layout["native_area_codes"] = area
    hit = scanner_unique_value(local(analysis, ctor, "0F B6 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 89 07") +
                               local(analysis, ctor, "0F B6 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 89 03"), "reveal")
    found["reveal"] = relative(analysis, hit, 3, 7)
    if relative(analysis, hit + 7, 1, 5) != functions["piece_masks"]:
        raise ValueError("Reveal must feed the resolved piece-mask helper")
    for caller, callee in [("submit", "allocate"), ("allocate", "pending_insert"), ("flush", "enqueue"), ("complete", "cleanup"), ("cleanup", "retire")]:
        check_call(analysis, functions[caller], functions[callee], f"{caller}->{callee}")
    return found, layout


def expected(evidence, label):
    if label == "1.17.1":
        # The latest baseline shares the documented map layout with 1.17;
        # its file functions are the original reference VAs.
        version = next(v for v in evidence["versions"] if v["label"] == "1.17")
        version = {**version, "sha256": evidence["reference"]["sha256"]}
    else:
        version = next(v for v in evidence["versions"] if v["label"] == label)
        if "same_as" in version:
            version = next(v for v in evidence["versions"] if v["label"] == version["same_as"])
    return version


def verify_expected(evidence, label, functions, globals_, layout):
    # Ground truth is used only after the signature scanner has resolved all
    # dependencies, never for selecting candidates or building pointer chains.
    version = expected(evidence, label)
    for name, identity in FUNCTIONS.items():
        if label == "1.17.1":
            target = int(identity, 0)
        else:
            aliases = {"allocate": "request_pool_allocate", "request_init": "request_initialize",
                       "view_ctor": "view_constructor", "player": "player_state", "death": "death_state", "grace_ctor": "grace_constructor",
                       "marker_save_ctor": "save_constructor", "marker_bind": "view_bind", "marker_number": "number_and_icon",
                       "marker_save_bind": "save_bind", "marker_place": "place"}
            source = aliases.get(name, name)
            if name == "allocator_anchor":
                broad = load(ROOT / version["label"] / "discovery.json")
                target = int(next(f for f in broad["functions"] if f["reference"] == identity)["va"], 0)
            else:
                record = next((group[source] for group in [version["file_functions"], version["map_functions"], version["marker_functions"]] if source in group), None)
                if record is None:
                    core = load(ROOT / version["label"] / "core/discovery.json")
                    record = next((f for f in core["functions"] if f["reference"] == identity), None)
                    if record is None:
                        broad = load(ROOT / version["label"] / "discovery.json")
                        record = next(f for f in broad["functions"] if f["reference"] == identity)
                target = int(record["va"], 0)
        if functions[name] != target:
            raise ValueError(f"Ground truth differs {label}/{name}: {functions[name]:#x} != {target:#x}")
    for name in ("menu", "game_data", "event_flags", "field"):
        if globals_[name] != int(version["hooks"][name]["target"], 0):
            raise ValueError(f"Global ground truth differs {name}")
    for name in ("manager", "allocator", "retire_pool_owner"):
        if globals_[name] != int(version["file_globals"][name]["va"], 0):
            raise ValueError(f"File global ground truth differs {name}")
    for name in ("repository", "reveal"):
        if globals_[name] != int(version["map_globals"][name], 0):
            raise ValueError(f"Map global ground truth differs {name}")
    for name in ("menu_owner", "owner_view", "menu_info", "screen_state", "grace_stride", "grace_normal", "native_area_codes"):
        if layout[name] != version["layout"][name]:
            raise ValueError(f"Layout ground truth differs {name}")
    if layout["repository_groups"] != version["repository"]["count"]:
        raise ValueError("Repository count differs")
    if layout["alternate_icons"] != version["layout"]["alternate_icons"]:
        raise ValueError("Alternate icon capability differs")


def worker(args):
    started = time.monotonic()
    shared = load(args.rules or WORK / "rules.json")
    analysis = Analysis(args.exe, args.database)
    try:
        analysis.index_references()
        functions, support = find_functions(analysis, shared)
        globals_, layout = extract(analysis, functions)
        guard_addresses(analysis, functions, globals_)
        body_guards = guard_bodies(analysis, functions, shared)
        field_guards = guard_fields(analysis, functions, layout)
        if not args.resolve_only:
            evidence = load("docs/minimap-version-compatibility.evidence.json")
            verify_expected(evidence, args.label, functions, globals_, layout)
        result = {"label": args.label, "sha256": analysis.sha256, "scan_without_hash_lookup": True,
                  "functions": {key: hex(value) for key, value in functions.items()},
                  "globals": {key: hex(value) for key, value in globals_.items()}, "layout": layout,
                  "two_anchor_support": support, "abi_body_guards": body_guards, "abi_field_guards": field_guards,
                  "ground_truth_matches": None if args.resolve_only else True, "seconds": round(time.monotonic() - started, 2)}
        destination = args.output if args.resolve_only else WORK / f"result-{args.label}.json"
        write(destination, result)
        print(f"PASS {args.label or args.exe.name}: {len(functions)} functions, {len(globals_)} globals, dynamic layout and ABI guards; {result['seconds']} s", flush=True)
    finally:
        analysis.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker", action="store_true")
    parser.add_argument("--resolve-only", action="store_true", help="Resolve one EXE without loading any per-build expected evidence")
    parser.add_argument("--rules", type=Path, help="Self-contained shared signature rules")
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--database", type=Path)
    parser.add_argument("--label")
    parser.add_argument("--labels", nargs="*")
    parser.add_argument("--output", type=Path, default=Path("docs/minimap-scanner.evidence.json"))
    args = parser.parse_args()
    if args.worker or args.resolve_only:
        if not args.exe or not args.database:
            parser.error("--exe and --database are required when resolving one executable")
        worker(args)
        return
    write(WORK / "rules.json", rules())
    evidence = load("docs/minimap-version-compatibility.evidence.json")
    cases = [{"label": v["label"], "path": v["path"], "database": str(ROOT / v.get("same_as", v["label"]) / "fresh.i64"), "sha256": v["sha256"]}
             for v in evidence["versions"]]
    cases.append({"label": "1.17.1", "path": r"D:\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe",
                  "database": "build/ida/minimap.i64", "sha256": evidence["reference"]["sha256"]})
    results, known = [], {}
    for case in cases:
        if args.labels and case["label"] not in args.labels:
            continue
        if case["sha256"] in known:
            results.append({**known[case["sha256"]], "label": case["label"], "same_as": known[case["sha256"]]["label"]})
            continue
        subprocess.run([sys.executable, str(Path(__file__).resolve()), "--worker", "--exe", case["path"], "--database", case["database"], "--label", case["label"]], check=True)
        result = load(WORK / f"result-{case['label']}.json")
        if result["sha256"] != case["sha256"]:
            raise ValueError("IDA input evidence mismatch")
        known[case["sha256"]] = result
        results.append(result)
    write(args.output, {"date": "2026-10-02", "method": "Python idapro/IDA 9.4: .text masked anchors, .pdata chains, RTTI, signed rel32 and instruction operands",
                        "runtime_mod_changed": False, "hash_used_to_resolve": False, "debugger": False, "game_calls": False,
                        "shared_rules": load(WORK / "rules.json"), "coverage": {"directories": len(results), "unique_executables": len(known)}, "results": results})
    print(f"Wrote {args.output}: {len(results)} directories / {len(known)} independent EXEs", flush=True)


if __name__ == "__main__":
    main()
