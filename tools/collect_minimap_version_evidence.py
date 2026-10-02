"""Collect historical Minimap compatibility evidence exported by Python IDA.

This collector never reads or disassembles executable bytes. It consumes the
hash-bound IDA JSON exports and rejects missing/mismatched evidence.
"""

from __future__ import annotations

import argparse
import json
import re
from collections import Counter
from pathlib import Path

BASE = 0x140000000
FILE_FUNCTIONS = {
    "request_pool_allocate": "0x1401a2720",
    "pending_insert": "0x1426ee320",
    "submit": "0x1426eeb60",
    "flush": "0x1426eed40",
    "enqueue": "0x1426f42e0",
    "cancel": "0x1426ee2c0",
    "request_initialize": "0x1426f9820",
    "dcx": "0x1426f8870",
    "complete": "0x1426f94c0",
    "cleanup": "0x1426f9340",
    "retire": "0x1426f4fd0",
}
RESOURCE_FUNCTIONS = {
    "atlas_path": "0x140d78f80", "mount_map_tiles": "0x140d79300",
    "gfx_path": "0x140d7d5b0", "atlas_parse": "0x140d665a0",
    "alias_key": "0x140120da0", "alias_region": "0x140d67f20",
    "gfx_tag_dispatch": "0x14116b400", "gfx_tag_header": "0x1411bc9f0",
    "gfx_external_image": "0x1411e5890", "gfx_place_object3": "0x1411bf220",
    "gfx_matrix": "0x1411bdbc0", "gfx_sprite": "0x1411c0180",
}
MAP_FUNCTIONS = {
    "view_constructor": "0x1408865a0", "view_create": "0x1407ee6c0",
    "player_state": "0x140887c30", "death_state": "0x140888860",
    "piece_masks": "0x1408892c0", "piece_param": "0x140d58b40",
    "common_param": "0x140d2b040", "event_storage": "0x1405fa250",
    "convert": "0x140877130", "tile_name": "0x140885450",
    "legacy_lookup": "0x1408785d0", "legacy_graph": "0x140877cd0",
    "tile_span": "0x1408859d0", "grace_constructor": "0x14088c7a0",
    "grace_select": "0x14088cb50", "point_constructor": "0x14087ca60",
    "point_select": "0x14087cf10",
}
ALTERNATE_FUNCTIONS = {
    "grace_alternate": "0x140d272b0", "grace_text": "0x140d27550",
    "grace_enable": "0x140d27890", "point_alternate": "0x140d59d80",
    "point_text": "0x140d5a220", "point_disable": "0x140d5a300",
    "point_enable": "0x140d5a3f0",
}
MARKER_FUNCTIONS = {
    "save_bind": "0x140819ad0", "save_constructor": "0x14081a550",
    "view_bind": "0x14087a220", "number_and_icon": "0x140879330",
    "visibility": "0x1408794f0", "place": "0x140887440",
}
TPF_FUNCTIONS = {
    "dds_size": "0x141e95840", "type": "0x141e95850",
    "format": "0x141e95860", "name": "0x141e95870",
    "width": "0x141e958b0", "height": "0x141e958c0",
    "mips": "0x141e958d0", "entry": "0x141e959d0",
    "header": "0x141e969f0",
}


def read(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def write(path: Path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def rva(va: str) -> str:
    return hex(int(va, 0) - BASE)


def export(path: Path, sha256: str):
    value = read(path)
    if value.get("sha256") != sha256:
        raise ValueError(f"SHA256 mismatch in {path}")
    return value


def decoded(folder: Path, function: dict, sha256: str):
    return export(folder / f"decoded_{int(function['va'], 0):x}.json", sha256)["instructions"]


def compact(function: dict, require_identical: bool = True):
    if not function.get("va") or (require_identical and not function.get("same_shape_as_reference")):
        raise ValueError(f"Function is not confirmed: {function['reference']}")
    result = {"reference": function["reference"], "va": function["va"], "rva": rva(function["va"]),
              "same_masked_body": function.get("same_shape_as_reference", False),
              "source_version": function.get("source_version", "1.17.1")}
    if function.get("source_reference"):
        result["source_reference"] = function["source_reference"]
    return result


def instruction(item: dict):
    result = {"va": item["ea"], "bytes": item["bytes"]}
    if item.get("disassembly"):
        result["disassembly"] = item["disassembly"]
    return result


def unique_mapping(discovery: dict, source: str):
    mappings = discovery["address_correspondences"].get(source, [])
    if len(mappings) != 1:
        raise ValueError(f"Ambiguous address correspondence {source}: {mappings}")
    return mappings[0]


def direct_target(instructions: list[dict], offset: int, start: str):
    item = next(i for i in instructions if int(i["ea"], 0) == int(start, 0) + offset)
    operand = next(o for o in item["operands"] if o["type"] == 2)
    return operand["addr"], instruction(item)


def validate_body(reference: dict, actual: list[dict], start: str):
    by_offset = {int(i["ea"], 0) - int(start, 0): i for i in actual}
    for expected in reference["instructions"]:
        item = by_offset.get(expected["offset"])
        if not item or item["size"] != expected["size"]:
            raise ValueError(f"Instruction boundaries differ at {start}+{expected['offset']:#x}")
        old, new, mask = bytes.fromhex(expected["bytes"]), bytes.fromhex(item["bytes"]), bytes.fromhex(expected["mask"])
        if len(old) != len(new) or any((a & m) != (b & m) for a, b, m in zip(old, new, mask)):
            raise ValueError(f"Masked body differs at {item['ea']}")
        for source, target in zip(expected.get("operands", []), item["operands"]):
            branch = int(source["addr"], 0)
            if source["type"] == 7 and int(reference["va"], 0) <= branch < int(reference["end"], 0):
                if int(target["addr"], 0) - int(start, 0) != branch - int(reference["va"], 0):
                    raise ValueError(f"Internal branch differs at {item['ea']}")


def collect(root: Path):
    inventory = read(root / "inventory.json")
    multi = read(root / "reference-multi.json")
    references = {(f["va"], f.get("source_version", "1.17.1")): f for f in multi["functions"]}
    latest_refs = {f["va"]: f for f in read(root / "reference.json")["functions"]}
    versions, known = [], {}
    for sample in inventory["files"]:
        if sample["sha256"] in known:
            versions.append({"label": sample["label"], "path": sample["path"],
                             "sha256": sample["sha256"], "file_version": sample["file_version"],
                             "same_as": known[sample["sha256"]]})
            continue
        label, digest = sample["label"], sample["sha256"]
        folder = root / label
        broad = export(folder / "discovery.json", digest)
        core = export(folder / "core/discovery.json", digest)
        tpf = export(folder / "tpf/discovery.json", digest)
        memory = export(folder / "verified/memory-check.json", digest)
        old, current = ({f["reference"]: f for f in d["functions"]} for d in (broad, core))
        for function in core["functions"]:
            if not function.get("same_shape_as_reference"):
                continue
            key = (function.get("source_reference", function["reference"]), function.get("source_version", "1.17.1"))
            validate_body(references[key], decoded(folder / "core", function, digest), function["va"])
        for name, source in {**FILE_FUNCTIONS, **RESOURCE_FUNCTIONS}.items():
            function = old[source]
            compact(function)
            validate_body(latest_refs[source], decoded(folder, function, digest), function["va"])
        for source in ("0x1408785d0", "0x140877cd0"):
            compact(old[source])
            validate_body(latest_refs[source], decoded(folder, old[source], digest), old[source]["va"])
        for source in ["0x140887c30", "0x140888860", "0x1408865a0", "0x1405fa250",
                       "0x140819ad0", "0x140879330", "0x1408794f0", "0x140887440",
                       "0x14087a220", "0x140877130", "0x140d58b40", "0x140d2b040"]:
            compact(current[source])
        # Check inter-function calls, not just relocated byte bodies.
        for caller, callee in [("0x1426eeb60", "0x1401a2720"), ("0x1426eed40", "0x1426f42e0"),
                               ("0x1426f94c0", "0x1426f9340"), ("0x1426f9340", "0x1426f4fd0")]:
            target = old[callee]["va"]
            if not any(o["type"] == 7 and o["addr"] == target for i in decoded(folder, old[caller], digest) for o in i["operands"]):
                raise ValueError(f"File call correspondence differs in {label}")
        hook_globals = {}
        for name, hits in core["hooks"].items():
            if len(hits) != 1 or not hits[0]["target"]:
                raise ValueError(f"Non-unique core hook {label}/{name}")
            hook_globals[name] = {**hits[0], "rva": rva(hits[0]["target"])}
        file_records = {name: compact(old[source]) for name, source in FILE_FUNCTIONS.items()}
        resource_records = {name: compact(old[source]) for name, source in RESOURCE_FUNCTIONS.items()}
        file_globals = {}
        for name, source in [("manager", "0x1448611a0"), ("retire_pool_owner", "0x144860d70")]:
            match = unique_mapping(core, source)
            file_globals[name] = {"va": match["target"], "rva": rva(match["target"]), "evidence": match["evidence"]}
        # This textlist accessor is only a static allocator anchor; the overlay
        # still uses independent CPU-byte requests at runtime.
        allocator_fn = old["0x1401f4f00"]
        compact(allocator_fn)
        allocator_insns = decoded(folder, allocator_fn, digest)
        allocator, allocator_evidence = direct_target(allocator_insns, 0x45, allocator_fn["va"])
        allocator2, _ = direct_target(allocator_insns, 0x88, allocator_fn["va"])
        if allocator != allocator2:
            raise ValueError(f"Allocator global mismatch in {label}")
        file_globals["allocator"] = {"va": allocator, "rva": rva(allocator), "evidence": allocator_evidence}
        marker_global = unique_mapping(core, "0x143b3b890")["target"]
        marker_bytes = next(item["bytes"] for item in memory["records"] if item["va"] == marker_global)
        marker_limit = int.from_bytes(bytes.fromhex(marker_bytes)[:4], "little")
        if marker_limit != 5:
            raise ValueError(f"Unexpected numbered marker limit in {label}")
        view_fn = current["0x1407ee6c0"]["va"]
        view_export = export(folder / f"verified/function_{int(view_fn, 0):x}.json", digest)
        view_code = view_export["pseudocode"]
        view_offset = 0x250 if "a1[74] =" in view_code else 0x248 if "a1[73] =" in view_code else None
        if view_offset is None:
            raise ValueError(f"View pointer field not found in {label}")
        owner_link_files = list((folder / "owner-link").glob("function_*.json"))
        if len(owner_link_files) != 1:
            raise ValueError(f"Owner caller is not unique in {label}")
        owner_link = export(owner_link_files[0], digest)
        owner_code = owner_link["pseudocode"]
        owner_offset = 0x78 if "a1 + 120" in owner_code else 0x80 if "a1 + 128" in owner_code else None
        if owner_offset is None:
            raise ValueError(f"Menu owner field not found in {label}")
        verified = [export(p, digest) for p in (folder / "verified").glob("function_*.json")]
        repository_getter = next(f for f in verified if "a2 >=" in f.get("pseudocode", ""))
        group_count = int(re.search(r"a2 >= (\d+)", repository_getter["pseudocode"])[1])
        if not all(text in repository_getter["pseudocode"] for text in ("72LL", "128", "136")):
            raise ValueError(f"Repository layout differs in {label}")
        menu = next(f for f in verified if "*(_QWORD *)(a1 + 128) = 0;" in f.get("pseudocode", ""))
        menu_fields = [int(o["addr"], 0) for i in menu["instructions"] if i["mnemonic"] == "lea"
                       for o in i["operands"] if o["type"] == 4 and int(o["addr"], 0) in (0x708, 0x718, 0x720)]
        menu_info = max(menu_fields)
        # Verify corresponding graces/points groups directly from per-EXE calls.
        param_groups, param_evidence = set(), []
        for p in (folder / "param").glob("function_*.json"):
            function = export(p, digest)
            for group in (43, 87):
                if f"a2: {group}," in function.get("pseudocode", ""):
                    param_groups.add(group)
                    param_evidence.append({"group": group, "function": function["start"]})
        if param_groups != {43, 87}:
            raise ValueError(f"Missing param accessors in {label}")
        constructor = current["0x1408865a0"]
        ctor_insns = decoded(folder / "core", constructor, digest)
        area_codes = sorted({int(o["value"], 0) for i in ctor_insns for o in i["operands"]
                             if o["type"] == 5 and int(o["value"], 0) in (60, 61)})
        reveal = None
        for index, item in enumerate(ctor_insns):
            if any(o["type"] == 7 and o["addr"] == current["0x1408892c0"]["va"] for o in item["operands"]):
                candidates = [o["addr"] for i in ctor_insns[max(0, index - 5):index] for o in i["operands"] if o["type"] == 2]
                reveal = candidates[-1] if candidates else None
        if not reveal:
            raise ValueError(f"Reveal byte not found in {label}")
        early = constructor["source_version"] == "1.02"
        alternate_records = {}
        if not early:
            alternate_records = {name: compact(current[source]) for name, source in ALTERNATE_FUNCTIONS.items()}
            compact(current["0x14088cb50"])
            compact(current["0x14087cf10"])
        grace_stride = 0x2D0 if early else 0x350
        grace_constructor = current["0x14088c7a0"]
        grace_insns = decoded(folder / "core", grace_constructor, digest)
        normal_field = 0x2C8 if early else 0x348
        if not any(i["operands"] and i["operands"][0]["type"] == 4 and int(i["operands"][0]["addr"], 0) == normal_field
                   for i in grace_insns):
            raise ValueError(f"Grace normal flag not confirmed in {label}")
        # The constructor copy increments the native contiguous grace range.
        if not any(o["type"] == 5 and int(o["value"], 0) == grace_stride for i in ctor_insns for o in i["operands"]):
            raise ValueError(f"Grace stride not confirmed in {label}")
        death_fn = current["0x140888860"]
        death_insns = decoded(folder / "core", death_fn, digest)
        player_fn = current["0x140887c30"]
        player_insns = decoded(folder / "core", player_fn, digest)
        selected = {"player": [], "death": [], "grace": [], "marker": []}
        for i in player_insns:
            if (i["operands"] and i["operands"][0]["type"] == 4 and i["operands"][0]["reg"] != 4
                    and int(i["operands"][0]["addr"], 0) in (0x14, 0x24, 0x28, 0x30, 0x34)):
                selected["player"].append(instruction(i))
        for i in death_insns:
            if any(o["type"] == 4 and int(o["addr"], 0) in (0xA9, 0xAC, 0xB4, 0x278) for o in i["operands"]):
                selected["death"].append(instruction(i))
        for i in grace_insns:
            if any(o["type"] == 4 and int(o["addr"], 0) in (0x238, 0x240, 0x248, 0x288, 0x2C8, 0x308, 0x348) for o in i["operands"]):
                selected["grace"].append(instruction(i))
        marker_place = decoded(folder / "core", current["0x140887440"], digest)
        selected["marker"] = [instruction(i) for i in marker_place if any(o["type"] == 4 and int(o["addr"], 0) == 0x300 for o in i["operands"])]
        tables = memory["native_tables"]
        if {t["name"] for t in tables} != {"01_Common", "71_MapTile"}:
            raise ValueError(f"Native menu groups missing in {label}")
        tpf_records = {}
        for name, source in TPF_FUNCTIONS.items():
            function = next(f for f in tpf["functions"] if f["reference"] == source)
            validate_body(latest_refs[source], decoded(folder / "tpf", function, digest), function["va"])
            tpf_records[name] = compact(function)
        offset_file = list((folder / "tpf-offset").glob("function_*.json"))
        if len(offset_file) != 1:
            raise ValueError(f"TPF data-offset accessor not exported in {label}")
        offset = export(offset_file[0], digest)
        if "".join(i["bytes"] for i in offset["instructions"]) != "488b42088b00c3":
            raise ValueError(f"TPF offset accessor body differs in {label}")
        tpf_records["dds_offset"] = {"va": offset["start"], "rva": rva(offset["start"]), "same_masked_body": True}
        groups = {"count": group_count, "stride": 72, "entry": 0x88, "cap": 0x80, "table": 0x80,
                  "world_map_piece": 88, "world_map_point": 87, "bonfire_warp": 43, "game_system_common": 141,
                  "getter": repository_getter["start"], "accessors": param_evidence}
        record = {**sample, "current_adapter_supported": False,
                  "static_relocatable": True, "file_functions": file_records, "file_globals": file_globals,
                  "resource_functions": resource_records, "tpf_functions": tpf_records, "hooks": hook_globals,
                  "map_functions": {name: compact(current.get(source, old[source]), False) for name, source in MAP_FUNCTIONS.items()
                                    if current.get(source, old[source]).get("va")},
                  "marker_functions": {name: compact(current[source]) for name, source in MARKER_FUNCTIONS.items()},
                  "alternate_functions": alternate_records,
                  "layout": {"menu_owner": owner_offset, "owner_view": view_offset, "menu_info": menu_info, "screen_state": menu_info + 0x10,
                             "view_raw_map": 0x14, "view_location": 0x24, "underground": 0x30, "underground_bytes": 1,
                             "death_valid": 0xA9, "death_xy": 0xAC, "death_map": 0xB4,
                             "converter_array": 0xF8, "converter_stride": 48, "converter_count": 0x280,
                             "grace_begin": 0x2E8, "grace_end": 0x2F0, "grace_stride": grace_stride,
                             "grace_id": 0x238, "grace_param": 0x240, "grace_normal": normal_field,
                             "grace_icon_offsets": [0x248, 0x288] if early else [0x248, 0x288, 0x2C8, 0x308],
                             "alternate_icons": not early, "marker_list": 0x300, "marker_save": 0x338,
                             "marker_slots": 8, "marker_capacity": 16, "marker_count": 0x40, "marker_record_bytes": 16,
                             "marker_limit": marker_limit, "home_row_map": 0x278, "native_area_codes": area_codes},
                  "repository": groups, "map_globals": {"repository": unique_mapping(core, "0x143d85f58")["target"], "reveal": reveal},
                  "native_menu_tables": tables, "selected_instructions": selected,
                  "owner_link": {"function": owner_link["start"], "view_create": view_fn},
                  "resource_payloads_verified": False, "old_game_runtime_verified": False}
        versions.append(record)
        known[digest] = label
    return {"analysis_date": "2026-10-02", "source_directory": inventory["root"],
            "reference": {"version": "1.17.1", "file_version": "2.7.1.0", "sha256": multi["sha256"], "image_base": hex(BASE)},
            "method": "Python idapro 0.0.10 / IDA 9.4; hash checks, RTTI, PE unwind chains, decoded instruction bodies and branch/call verification",
            "constraints": {"debugger_attachment": False, "breakpoints": False, "source_executable_modified": False,
                            "game_started": False, "runtime_offsets_changed": False, "game_gpu_handles_reused": False},
            "coverage": {"directories": len(versions), "unique_executables": len(known), "current_adapter_supported_old_executables": 0},
            "versions": versions}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("build/ida/versions"))
    parser.add_argument("--output", type=Path, default=Path("docs/minimap-version-compatibility.evidence.json"))
    args = parser.parse_args()
    result = collect(args.root)
    write(args.output, result)
    counts = Counter((v["layout"]["menu_owner"], v["layout"]["owner_view"], v["layout"]["grace_stride"]) for v in result["versions"] if "layout" in v)
    print(json.dumps({"coverage": result["coverage"], "layout_groups": {str(k): n for k, n in counts.items()},
                      "output": str(args.output)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
