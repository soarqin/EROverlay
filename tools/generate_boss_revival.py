"""Derive boss revival recipes from original EMEVDs and IDA evidence.

Only flag operands and instantiated event parameters justify recipe entries.
The numerical boss block limits candidates; it never invents adjacent flags.
Reviewed special cases below handle shared arenas, multi-stage battles, quest
entry gates, and one-hot clone selection. All 207 bosses receive an audit row.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import struct
from collections import defaultdict
from pathlib import Path

from boss_event_inspect import instances_for, load_definitions, read_event
from collect_boss_revival_evidence import BASELINE_SHA256
from ida_minimap_versions import write_json

PRIMARY_CORRECTIONS = {1052520800: 1252520800}

# Each exception names original scripts/events; entries are still required to
# have a decoded read, write or event-completion witness below.
SPECIAL = {
    10000800: {"extra": {10000802: False}},
    1043360800: {
        "maps": ["m60_43_36_00"], "events": [1043362340],
        "extra": {1043360340: False, 1043362379: False},
    },
    31170800: {"events": [31172499], "extra": {31172499: False}},
    1051430800: {"events": [1051432200, 1051432209], "extra": {1051430210: False, 1051432200: False}},
    1037530800: {"events": [1037532345, 1037532450]},
    1037540810: {"events": [1037542255]},
    11050850: {"values": {11050854: False}},
    12030800: {"events": [12032810, 12032811, 12032812], "extra": {12032501: False, 12032502: False, 12032503: False, 12032504: False}},
    12030850: {
        "values": {12032859: True},
        "extra": {12032858: False, 12032860: False, 12032870: False},
        "note": "12032859 restarts the dream cutscene/warp on area reload, so Fia's completed quest does not block re-entry.",
    },
    13000830: {"extra": {71301: False}},
    14000800: {
        "events": [140028121, 140028122, 14003922, 14003923, 14003937, 14003938, 14003962, 14003963, 14003972, 14003973],
        "extra": {14000804: False},
    },
    12090800: {"events": [12092260, 12092290]},
    15000800: {"events": [15000700, 15000701], "extra": {15002700: False, 15002701: False}},
    19000800: {"extra": {19000804: False, 19001100: False}},
    22000800: {"extra": {22000802: False}},
    25000800: {"extra": {2051452602: False}},
    28000800: {"events": [28000700, 28000701, 28000702], "extra": {28003811: False, **{28002700 + i: False for i in range(10)}}},
    31000800: {
        "events": [31003703, 31003705, 31003706],
        "extra": {3680: False, 3681: False, 3682: True, 3683: False, 3684: False, 3685: True,
                  **{flag: False for flag in range(3686, 3698)},
                  31008521: True, 31008820: False, 31008821: False, 31009810: False},
        "note": "Patches uses hostile-but-living 3682 (as original 31003703 initializes) and Murkwater location 3685. This deliberately rewinds his quest location and surrender state. Other global quest dependencies in common 3699 remain a game-test boundary.",
    },
    41020800: {"values": {41022820: True}},
    1038520800: {"events": [1038522346], "extra": {1038522340: False}},
    1041510800: {"events": [1041512320, 1041512321], "extra": {1041512815: False}},
    1041520800: {
        "maps": ["m60_37_51_00"], "events": [1041522300, 1037512350, 1037512301],
        "extra": {1037510800: False, 1037510810: False, 1041520820: False, 1041522810: False},
        "note": "Lansseax's earlier encounter has its own death/retreat flags; both encounters are reset.",
    },
    31210800: {"events": [312112811], "extra": {31212852: False}},
    1049380800: {"events": [1049382300, 1049382301]},
    1051360800: {
        "extra": {9413: True},
        "note": "The Redmane duo is allowed after the festival (9413); this changes a shared festival gate.",
    },
    1252380800: {
        "events": [1252382200, 1252382280, 1252382360, 1252382440, 1252382520, 1252382600, 1252382680, 1252382695, 1252382696],
        "witness_maps": ["common"],
        "extra": {9410: True, 9411: True, 9412: False, 9413: False, 1051369360: True},
        "note": "Reopens Radahn's festival and summon state. Jerren's local dialogue gate 1051369360 prevents common event 3040 from immediately setting 9413 again while global defeat 9130 is preserved. The Redmane duo arena switches to festival mode.",
    },
    1252520800: {
        "events": [1052520800, 1052522810, 1052522811, 1052522812, 1052522815, 1052522816, 1052522817, 1052522849],
        "extra": {1052520800: False, **{1052522820 + i: False for i in range(9)}, **{1052522830 + i: False for i in range(9)}},
    },
    1248550800: {"events": [1248552320, 1248552321, 1248552820, 1248552830, 1248552840]},
    1254560800: {"events": [1054562820], "extra": {1054562820: False}},
    2049440800: {
        "extra": {2049440801: False, 2049442702: True},
        "note": "2049442702 makes the original entry event warp into Dane's duel on area reload, without rewinding his global quest.",
    },
    2049430800: {"events": [2049430830, 2049430831]},
    2050470800: {"events": [2050472800, 2050472810, 2050472820], "extra": {2050470400: False}},
    2050480860: {"events": [2050472800, 2050472811, 2050472821], "extra": {2050470400: False}},
    2051450800: {
        "events": [2051450705, 2051450706, 2051452310, 2051452311],
        "extra": {2051452709: False, 2051452424: False},
        "note": "Ymir's entry still requires the already completed Metyr quest gate; the cathedral interaction and invasion are reset.",
    },
    2052480800: {"events": [2052482200], "extra": {2052482202: False}},
    2054390800: {
        "events": [2054392480, 2054392481, 2054392482, 2054392483, 2054392484, 2054390700, 2054390701, 2054390702, 2054390703],
        "note": "Reset Igon's local summon and combat-dialogue state; his existing global summon-permission flag is preserved.",
    },
}


def flag_refs(instance: dict, instruction: dict) -> list[tuple[int, str, bool | None]]:
    """Return actual IDA-verified flag operands, including bounded ranges."""
    raw = bytes.fromhex(instruction["raw"])
    bank, op = instruction["bank"], instruction["opcode"]
    result = []

    def target(flag_type: int, number: int) -> int:
        if flag_type == 0:
            return number
        if flag_type == 2:
            return number + (instance.get("completion_flag") or 0)
        return 0

    if bank == 2003 and op in (66, 69):
        result.append((target(raw[0], struct.unpack_from("<I", raw, 4)[0]), "write", bool(raw[8])))
    elif bank == 3 and op == 0:
        result.append((target(raw[2], struct.unpack_from("<I", raw, 4)[0]), "read", None))
    elif bank == 1003 and op in (0, 1, 2, 101):
        result.append((target(raw[1 if op == 0 else 2], struct.unpack_from("<I", raw, 4)[0]), "read", None))
    elif (bank == 3 and op in (1, 10)) or (bank == 1003 and op in (3, 4, 103)):
        start, end = struct.unpack_from("<II", raw, 4)
        if end >= start and end - start <= 128:
            result.extend((target(raw[1 if op == 10 else 2], flag), "range_read", None) for flag in range(start, end + 1))
    elif bank == 2003 and op in (17, 22, 63):
        start, end = struct.unpack_from("<II", raw)
        if end >= start and end - start <= 128:
            result.extend((flag, "range_write", bool(raw[8])) for flag in range(start, end + 1))
    elif bank == 2003 and op in (9, 31, 32):
        flag = struct.unpack_from("<I", raw)[0]
        count = struct.unpack_from("<I", raw, 4)[0] if op != 9 else 1
        if count <= 32:
            result.extend((flag + i, "value_write", None) for i in range(count))
    elif bank == 3 and op == 12:
        flag = struct.unpack_from("<I", raw, 4)[0]
        count = raw[8]
        if count <= 32:
            result.extend((flag + i, "value_read", None) for i in range(count))
    elif bank == 2003 and op == 41:
        flag, count, operand, source, source_count = struct.unpack_from("<IIiII", raw)
        if count <= 32:
            result.extend((flag + i, "value_write", None) for i in range(count))
        if source_count <= 32:
            result.extend((source + i, "value_read", None) for i in range(source_count))
    elif bank == 2007 and op == 10:
        result.extend((flag, "dialog_write", None) for flag in struct.unpack_from("<III", raw, 16))
    return [(flag, kind, value) for flag, kind, value in result if 0 < flag <= 0xffffffff]


def allocated_categories(events: Path, ida: Path) -> set[int]:
    # Native CSEventFlagMan::allocation (0x1405D3670/0x1405CE3F0)
    # allocates 1000-bit categories with these banks. Bank 6 is absent.
    banks = (0, 1, 2, 3, 4, 5, 7, 8, 9)
    categories = set(range(10)) | set(range(60, 100)) | {i * 10 for i in range(10, 100)}
    for path in events.glob("*.eventflagalloclist"):
        for row in csv.reader(path.read_text(encoding="utf-8-sig").splitlines()):
            if len(row) != 3 or not row[0].isdigit():
                continue
            area, x, z, _ = [int(n) for n in row[1][1:].split("_")]
            variant = int(row[2])
            prefix = area * 1000 + x * 10 if area < 50 else ((10 if area == 60 else 20) + variant) * 100000 + x * 1000 + z * 10
            categories.update(prefix + bank for bank in banks)
    # 0x1405CB000 builds lookup vectors from 372 native alias records.
    # 0x1405CE550 maps old field tiles to DLC tiles, and allocates their
    # native (direct-pointer) storage too. CSVs alone omit most DLC tiles.
    table = json.loads((ida / "range-143b35ff0.json").read_text(encoding="utf-8"))
    alias_bytes = bytes.fromhex(table["bytes"])
    if len(alias_bytes) != 372 * 16:
        raise ValueError("Unexpected IDA field flag alias table size")
    aliases = {}
    for area, variant, x, z, key, ta, tv, tx, tz, target in struct.iter_unpack("<4BI4BI", alias_bytes):
        if key != z + 100 * (x + 100 * (variant + 10 * area)):
            raise ValueError("Invalid IDA field flag alias key")
        aliases[area, variant, x, z] = (ta, tv, tx, tz) if target else None
    for x in range(24, 63):
        for z in range(25, 65):
            source = (1, 0, x, z)
            target = aliases.get(source, source)
            if target is None:
                continue
            for tile in (source, target):
                area, variant, tx, tz = tile
                categories.update((area * 10 + variant) * 100000 + tx * 1000 + tz * 10 + bank for bank in (0, 2, 4, 5, 7, 8, 9))
    return categories


def witness(instance: dict, instruction: dict | None, kind: str, value: bool | None = None) -> dict:
    result = {"map": instance["map"], "source": instance["source_file"], "event": instance["event"],
              "slot": instance["slot"], "completion_flag": instance["completion_flag"], "kind": kind}
    if instance["map"].startswith(("m60_", "m61_")):
        result["runtime_variant"] = instance["runtime_variant"]
    if instruction is not None:
        result.update({"instruction": instruction["index"], "opcode": f"{instruction['bank']}[{instruction['opcode']}]",
                       "file_offset": hex(instruction["file_offset"]), "raw_arguments": instruction["raw"]})
        if instruction.get("common_ordinal") is not None:
            result["common_ordinal"] = instruction["common_ordinal"]
    if instance["callers"]:
        result["callers"] = instance["callers"]
    if value is not None:
        result["written_value"] = value
    if instance["parameters"]:
        result["parameters"] = instance["parameters"]
    return result


def scoped(flag: int, prefix: int, low: int, upper: int) -> bool:
    if flag // 10000 != prefix:
        return False
    suffix = flag % 10000
    # Distinct bosses in one map own distinct 50-flag blocks. The data is not
    # filled by arithmetic: only operands observed in selected events enter.
    return low <= suffix < upper or low + 2000 <= suffix < upper + 2000 or low + 3000 <= suffix < upper + 3000


def recipe(boss: dict, instances: list[dict], all_refs: dict, primary_flags: set[int], categories: set[int]) -> dict:
    configured_id = boss["flag_id"]
    main = PRIMARY_CORRECTIONS.get(configured_id, configured_id)
    old_id = next((old for old, current in PRIMARY_CORRECTIONS.items() if current == main), configured_id)
    prefix, low = main // 10000, main % 10000
    upper = min([low + 50, 900] + [f % 10000 for f in primary_flags if f // 10000 == prefix and f > main])
    special = SPECIAL.get(main, {})
    local = [(i, [(instruction, flag_refs(i, instruction)) for instruction in i["instructions"]]) for i in instances]
    kills = [i for i, rows in local if any(flag == main and kind == "write" and state for _, refs in rows for flag, kind, state in refs)
             and any(x["bank"] == 2003 and x["opcode"] == 12 for x in i["instructions"])]
    if not kills:
        kills = [i for i, rows in local if any(flag == main and kind == "write" and state for _, refs in rows for flag, kind, state in refs)]
    if not kills:
        raise ValueError(f"No original defeat writer for boss {main} ({boss['boss']})")
    maps = {i["map"] for i in kills}
    # Prefer the map that owns this boss's defeat event over indirect writes
    # by another boss (Morgott also writes Margit's defeat flag).
    owned = {i["map"] for i in kills if i["event"] // 10000 == prefix or i["source_file"] == "common_func.emevd"}
    if owned:
        maps = owned
    kills = [i for i in kills if i["map"] in maps]
    maps.update(special.get("maps", []))
    selected = []
    for instance, rows in local:
        if instance["map"] not in maps or instance["source_file"] != instance["map"] + ".emevd":
            continue
        ident = instance["event"]
        in_block = ident // 10000 == prefix and low + 2000 <= ident % 10000 < upper + 2000
        main_reference = any(flag == main for _, refs in rows for flag, _, _ in refs)
        has_healthbar = any(i["bank"] == 2003 and i["opcode"] == 11 for i in instance["instructions"])
        if ident in special.get("events", []) or in_block or (main_reference and has_healthbar):
            selected.append(instance)
    selected.extend(i for i in kills if i["source_file"] != "common_func.emevd" and i not in selected)
    common = [i for i, rows in local if i["map"] in maps and i["source_file"] == "common_func.emevd"
              and any(flag == main for _, refs in rows for flag, _, _ in refs)
              and i["event"] in (9005800, 9005801, 9005811, 9005813, 9005822, 90005880, 90005881, 90005882, 90005883, 90005884, 90005885)]
    # Shared AI/standby helpers may key their one-time setup on ThisEvent
    # rather than the defeat flag. Bind them to actual boss entities from
    # death/health-bar operands, not to neighboring flag numbers.
    entities = {struct.unpack_from("<I", bytes.fromhex(ins["raw"]), 0 if ins["opcode"] == 12 else 4)[0]
                for i in selected + kills for ins in i["instructions"]
                if ins["bank"] == 2003 and ins["opcode"] in (11, 12)}
    activation = [i for i, rows in local if i["map"] in maps and i["source_file"] == "common_func.emevd"
                  and i["event"] in (90005200, 90005201, 90005210, 90005211, 90005213, 90005220, 90005221,
                                      90005250, 90005251, 90005260, 90005261, 90005263, 90005271, 90005400)
                  and struct.unpack_from("<I", bytes.fromhex(i["parameters"]), 0)[0] in entities]
    common.extend(activation)
    # Close the graph through locally owned battle flags: phase, body parts,
    # generators, and spell/summon state may live in helper events outside the
    # defeat block, especially Rennala, Radahn and the spiritcaller snails.
    known = {main}
    while True:
        before = len(known), len(selected)
        selected_ids = {id(instance) for instance in selected}
        for instance in selected + common:
            for instruction in instance["instructions"]:
                for flag, _, _ in flag_refs(instance, instruction):
                    if scoped(flag, prefix, low, upper):
                        known.add(flag)
        for instance in selected:
            for instruction in instance["instructions"]:
                for flag, kind, state in flag_refs(instance, instruction):
                    if flag // 10000 == prefix and 2000 <= flag % 10000 < 4000 and kind in ("write", "range_write", "value_write"):
                        known.add(flag)
        for instance, rows in local:
            if id(instance) in selected_ids or instance["map"] not in maps or instance["source_file"] != instance["map"] + ".emevd":
                continue
            refs = {flag for _, rr in rows for flag, _, _ in rr}
            if not (refs & (known - {main})):
                continue
            ident = instance["event"]
            # Quest/NPC scripts are explicitly reviewed in SPECIAL; keep
            # implicit expansion within the map's combat/helper ID bands.
            if ident // 10000 == prefix and (2000 <= ident % 10000 < 4000) and not 2700 <= ident % 10000 < 2800:
                selected.append(instance)
        if before == (len(known), len(selected)):
            break
    flags: dict[int, dict] = {}
    excluded = []
    battle_reads = {flag for instance in selected + common for instruction in instance["instructions"]
                    for flag, kind, _ in flag_refs(instance, instruction) if "read" in kind}

    def completion_flag(instance: dict, role: str) -> None:
        completion = instance.get("completion_flag")
        if not completion or completion <= 299 or completion == main:
            return
        evidence = witness(instance, None, "event_completion")
        if completion not in battle_reads:
            excluded.append({"flag_id": completion, "reason": "completion_not_read_by_battle", "evidence": evidence})
            return
        add(completion, role, evidence)

    def add(flag: int, kind: str, evidence: dict, state: bool = False) -> None:
        if flag == main or flag == 0:
            return
        if flag in primary_flags:
            excluded.append({"flag_id": flag, "reason": "other_boss_primary", "evidence": evidence})
            return
        if flag // 1000 not in categories:
            excluded.append({"flag_id": flag, "reason": "no_native_storage_category", "evidence": evidence})
            return
        row = flags.setdefault(flag, {"flag_id": flag, "value": state, "roles": [], "evidence": []})
        if kind not in row["roles"]:
            row["roles"].append(kind)
        if evidence not in row["evidence"] and len(row["evidence"]) < 3:
            row["evidence"].append(evidence)

    for instance in selected + common:
        completion_flag(instance, "event_completion")
        for instruction in instance["instructions"]:
            for flag, kind, value in flag_refs(instance, instruction):
                if scoped(flag, prefix, low, upper) or flag in known - {main}:
                    add(flag, "battle_state", witness(instance, instruction, kind, value))
    for instance in kills:
        completion_flag(instance, "defeat_event_completion")
    # Common evergaol events also use an entry flag at main+5 and a map return
    # flag at main+2000. Their operands, rather than the convention, prove it.
    for flag, state in special.get("extra", {}).items():
        matches = [w for w in all_refs.get(flag, []) if w["map"] in maps or w["map"] in special.get("witness_maps", [])
                   or flag in (9410, 9411, 9412, 9413) or 3680 <= flag <= 3697]
        if not matches:
            raise ValueError(f"No original flag witness for special {flag}, boss {main}")
        for evidence in matches[:3]:
            add(flag, "reviewed_special", evidence, state)
        if flag not in flags:
            raise ValueError(f"Reviewed required flag has no native storage: {flag}")
        flags[flag]["value"] = state
    for flag, state in special.get("values", {}).items():
        if flag not in flags:
            raise ValueError(f"Special target {flag} was not found in battle graph for {main}")
        flags[flag]["value"] = state
        flags[flag]["roles"].append("reviewed_target_value")
    # Prefer a clear initialization and a later set transition as witnesses.
    # An operand proves membership; target values are stated separately.
    for flag, row in flags.items():
        matches = [w for w in all_refs[flag] if w["map"] in maps or "reviewed_special" in row["roles"]]
        relevant = [w for w in matches if any(w["event"] == i["event"] and w["slot"] == i["slot"] and w["map"] == i["map"]
                                             for i in selected + common)]
        pool = relevant or row["evidence"]
        evidence = []
        for kind in ("initial_clear", "read", "write", "event_completion"):
            match = next((w for w in pool if (kind == "initial_clear" and w.get("written_value") is False)
                          or (kind == "write" and "write" in w["kind"] and w.get("written_value") is not False)
                          or (kind not in ("initial_clear", "write") and kind in w["kind"])), None)
            if match and match not in evidence:
                evidence.append(match)
        row["evidence"] = evidence or row["evidence"]
        row["target_basis"] = ("reviewed_entry_or_one_hot_state" if row["value"] else "clear_battle_state_for_reentry")
        if main == 31000800 and flag == 3682:
            row["target_basis"] = "original_31003703_initializes_hostile_living_state"
        elif main == 1252380800 and flag == 1051369360:
            row["target_basis"] = "common_3040_instruction_38_blocks_refinishing_festival"
        elif main == 41020800 and flag == 41022820:
            row["target_basis"] = "valid_one_hot_clone_selection_for_branch_zero"
        elif main == 12030850 and flag == 12032859:
            row["target_basis"] = "12032859_instruction_2_enters_dream_warp"
        elif main == 2049440800 and flag == 2049442702:
            row["target_basis"] = "2049442801_instruction_6_enters_duel_warp"
        if flag // 1000 % 10 == 5 and any(w["source"] == "common_func.emevd" and 90005000 <= w["event"] < 90005500 for w in pool):
            row["roles"].append("boss_activation_completion")
        row["target_is_reconstruction"] = True
        row["storage_category"] = flag // 1000
    primary_evidence = [witness(i, ins, kind, state) for i in kills for ins in i["instructions"]
                        for flag, kind, state in flag_refs(i, ins) if flag == main and kind == "write" and state]
    entry_evidence = [witness(i, ins, kind) for i in instances if i["map"] in maps for ins in i["instructions"]
                      for flag, kind, _ in flag_refs(i, ins) if flag == main and kind == "read" and ins["bank"] == 1003]
    # Retain storage/other-boss exclusions in detail; collapse unused
    # completion candidates to avoid distributing unnecessary script data.
    not_read = sorted({x["flag_id"] for x in excluded if x["reason"] == "completion_not_read_by_battle"})
    excluded = [x for x in excluded if x["reason"] != "completion_not_read_by_battle"]
    for ident in special.get("events", []):
        for instance in selected:
            if instance["event"] != ident:
                continue
            completion = instance.get("completion_flag")
            if completion and completion // 1000 not in categories:
                row = {"flag_id": completion, "reason": "no_native_storage_category",
                       "evidence": witness(instance, None, "event_completion")}
                if row not in excluded:
                    excluded.append(row)
    return {"boss": boss["boss"], "flag_id": main, "previous_flag_id": old_id if main != old_id else None,
            "status": "static_recipe", "maps": sorted(maps), "primary_evidence": primary_evidence[:3], "entry_evidence": entry_evidence[:3],
            "revive_flags": sorted(flags.values(), key=lambda f: f["flag_id"]),
            "excluded_flags": excluded,
            "completion_flags_without_battle_reads": not_read,
            "battle_events": sorted({(i["map"], i["event"], i["slot"]) for i in selected + common}),
            "note": special.get("note"), "in_game_verified": False}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--events", type=Path, default=Path("build/ida/boss/events"))
    parser.add_argument("--ida-output", type=Path, default=Path("build/ida/boss"))
    parser.add_argument("--emedf", type=Path)
    parser.add_argument("--boss-data", type=Path, default=Path("src/boss/data"))
    parser.add_argument("--output", type=Path, default=Path("build/ida/boss/revival.evidence.json"))
    parser.add_argument("--apply", action="store_true", help="Update all localized boss JSONs after the full audit succeeds")
    args = parser.parse_args()
    definitions = load_definitions(args.emedf)
    scripts = {p.name: read_event(p) for p in args.events.glob("*.emevd")}
    metadata = json.loads((args.ida_output / "metadata.json").read_text(encoding="utf-8"))
    if metadata["sha256"] != BASELINE_SHA256:
        raise ValueError("The reviewed interpreter/storage semantics only apply to the 1.17.1 baseline hash")
    alias = json.loads((args.ida_output / "range-143b35ff0.json").read_text(encoding="utf-8"))
    if alias["sha256"] != metadata["sha256"]:
        raise ValueError("IDA alias table and executable metadata hashes differ")
    manifest = json.loads((args.events / "manifest.json").read_text(encoding="utf-8"))
    if len(manifest["allocation_files"]) != 4:
        raise ValueError("Missing original flag allocation tables")
    for source in manifest["allocation_files"]:
        if hashlib.sha256((args.events / source["file"]).read_bytes()).hexdigest() != source["decompressed_sha256"]:
            raise ValueError(f"Original flag allocation table hash mismatch: {source['file']}")
    categories = allocated_categories(args.events, args.ida_output)
    for source in manifest["event_files"]:
        if source["file"] not in scripts or source["decompressed_sha256"] != scripts[source["file"]]["sha256"]:
            raise ValueError(f"Original script hash mismatch: {source['file']}")
    if len(manifest["event_files"]) != len(scripts):
        raise ValueError("Event corpus does not exactly match its extraction manifest")
    by_map, all_instances, missing = {}, [], []
    defeat_maps = defaultdict(set)
    all_refs = defaultdict(list)
    for name, script in scripts.items():
        if name == "common_func.emevd":
            continue
        instances = instances_for(script, scripts, definitions, missing)
        by_map[name.removesuffix(".emevd")] = instances
        all_instances.extend(instances)
        for instance in instances:
            completion = instance.get("completion_flag")
            if completion and completion > 299:
                evidence = witness(instance, None, "event_completion")
                if len(all_refs[completion]) < 64 and evidence not in all_refs[completion]:
                    all_refs[completion].append(evidence)
            for instruction in instance["instructions"]:
                for flag, kind, state in flag_refs(instance, instruction):
                    if kind == "write" and state:
                        defeat_maps[flag].add(instance["map"])
                    evidence = witness(instance, instruction, kind, state)
                    if len(all_refs[flag]) < 64 and evidence not in all_refs[flag]:
                        all_refs[flag].append(evidence)
    english = json.loads((args.boss_data / "engus/bosses.json").read_text(encoding="utf-8"))
    bosses = [boss for region in english for boss in region["bosses"]]
    records = []
    primary_flags = {PRIMARY_CORRECTIONS.get(b["flag_id"], b["flag_id"]) for b in bosses}
    for boss in bosses:
        flag = PRIMARY_CORRECTIONS.get(boss["flag_id"], boss["flag_id"])
        candidates = [instance for map_name in sorted(defeat_maps[flag] | set(SPECIAL.get(flag, {}).get("maps", []))) for instance in by_map[map_name]]
        if flag // 1000 not in categories:
            raise ValueError(f"Primary flag {flag} has no native storage category")
        records.append(recipe(boss, candidates, all_refs, primary_flags, categories))
    metadata.pop("input_file", None)
    evidence = {"schema_version": 1, "sample": metadata, "archive": {k: v for k, v in manifest.items() if k != "event_files"},
                "event_files": manifest["event_files"], "script_count": len(scripts),
                "event_count": sum(len(s["events"]) for s in scripts.values()), "instance_count": len(all_instances),
                "unresolved_original_calls": missing, "boss_count": len(records), "bosses": records}
    write_json(args.output, evidence)
    print(f"Audited {len(records)} bosses; {sum(bool(r['revive_flags']) for r in records)} with extra flags, {sum(len(r['revive_flags']) for r in records)} total reset entries")
    if args.apply:
        recipes = {b["flag_id"]: b for b in records}
        recipes.update({b["previous_flag_id"]: b for b in records if b["previous_flag_id"]})
        pending = []
        source_recipes = {}
        for row in records:
            source_recipes[str(row["flag_id"])] = {"flag_id": row["flag_id"],
                "revive_flags": [{"flag_id": f["flag_id"], "value": True} if f["value"] else f["flag_id"] for f in row["revive_flags"]]}
        for path in sorted(args.boss_data.glob("*/bosses.json")):
            data = json.loads(path.read_text(encoding="utf-8"))
            for region in data:
                for boss in region["bosses"]:
                    row = recipes[boss["flag_id"]]
                    boss["flag_id"] = row["flag_id"]
                    boss["revive_flags"] = [{"flag_id": f["flag_id"], "value": True} if f["value"] else f["flag_id"] for f in row["revive_flags"]]
            pending.append((path, data))
        for path, data in pending:
            write_json(path, data)
        write_json(args.boss_data / "gen/revival.json", source_recipes)
        print(f"Updated {len(pending)} localized datasets")


if __name__ == "__main__":
    main()
