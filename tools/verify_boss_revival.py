"""Validate distributed revival data and recheck original IDA/EMEVD witnesses.

Runs without game files for schema/language/evidence consistency. When the
local build/ida/boss corpus exists, also expands raw EMEVD parameters again
and checks each persisted flag witness against the original bytes.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

from boss_event_inspect import instances_for, read_event
from generate_boss_revival import allocated_categories, flag_refs


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def flags(entries: list, primary: int) -> dict[int, bool]:
    require(isinstance(entries, list), "revive_flags must be an array")
    result = {}
    for entry in entries:
        if isinstance(entry, dict):
            flag, value = entry.get("flag_id"), entry.get("value")
            require(type(value) is bool, f"Non-boolean target for {primary}: {entry}")
        else:
            flag, value = entry, False
        require(type(flag) is int and 0 < flag <= 0xffffffff, f"Invalid flag ID for {primary}: {entry}")
        require(flag != primary and flag not in result, f"Primary/duplicate extra flag for {primary}: {flag}")
        result[flag] = value
    return result


def instance_key(value: dict, source_field: str) -> tuple:
    return (value[source_field], value["event"], value["slot"], value["completion_flag"],
            value.get("runtime_variant", 0), value.get("parameters", ""),
            tuple((caller["event"], caller["instruction"]) for caller in value.get("callers", [])))


def native_semantics_check() -> None:
    # Handcrafted raw instruction operands, independent of EMEDF or recipes.
    instance = {"completion_flag": 1252385027}
    def instruction(bank: int, op: int, raw: bytes) -> dict:
        return {"bank": bank, "opcode": op, "raw": raw.hex()}
    require(flag_refs(instance, instruction(2003, 66, struct.pack("<B3xIB3x", 0, 10000801, 1))) == [(10000801, "write", True)], "absolute flag write")
    require(flag_refs(instance, instruction(2003, 69, struct.pack("<B3xIB3x", 2, 0, 0))) == [(1252385027, "write", False)], "relative common flag write")
    require(flag_refs(instance, instruction(3, 12, struct.pack("<b3xIBB2xI", 0, 41022835, 3, 4, 7))) ==
            [(41022835 + i, "value_read", None) for i in range(3)], "event value width is a byte")
    require(flag_refs(instance, instruction(2007, 10, struct.pack("<ihhi f III", 1, 0, 2, 0, 3.0, 10000801, 10000802, 10000803))) ==
            [(10000801 + i, "dialog_write", None) for i in range(3)], "dialog response flag operand offsets")
    # Common-offset indices count opcode 6 with unique offsets, not all lines.
    instruction_rows = [
        {"index": 0, "bank": 1014, "opcode": 0, "raw": "", "common_ordinal": None},
        {"index": 1, "bank": 2000, "opcode": 6, "raw": struct.pack("<III", 0, 90005250, 10000800).hex(), "common_ordinal": 4},
    ]
    common_rows = [{"index": 0, "bank": 1003, "opcode": 2, "raw": struct.pack("<BBB x I", 0, 1, 2, 0).hex(), "common_ordinal": None}]
    helper = {"id": 90005250, "rest": 0, "instructions": common_rows, "parameters": []}
    root = {"id": 200, "rest": 0, "instructions": instruction_rows, "parameters": []}
    script = {"file": "m60_52_38_00.emevd", "linked": ["common_func.emevd"], "events": {200: root}}
    scripts = {script["file"]: script, "common_func.emevd": {"file": "common_func.emevd", "events": {90005250: helper}}}
    result = instances_for(script, scripts, {}, [])
    require(result[-1]["completion_flag"] == 1252385004 and result[-1]["slot"] == -1, "common completion uses runtime variant and unique ordinal")


def data_check(audit: dict, data_path: Path) -> int:
    recipes = {record["flag_id"]: record for record in audit["bosses"]}
    require(len(recipes) == audit["boss_count"] == 207, "Boss audit coverage differs from 207")
    require(len(audit["native_functions"]) >= 30, "Missing native interpreter/storage evidence")
    files = {record["file"]: record for record in audit["event_files"]}
    source = json.loads((data_path / "gen/revival.json").read_text(encoding="utf-8"))
    require({int(key) for key in source} == set(recipes), "Generation source does not cover exactly the audit")
    baseline = None
    count = 0
    for path in sorted(data_path.glob("*/bosses.json")):
        regions = json.loads(path.read_text(encoding="utf-8"))
        bosses = [boss for region in regions for boss in region["bosses"]]
        require(len(bosses) == 207, f"Boss count differs in {path}")
        layout = [(region["regions"], region.get("dlc", 0), [(boss["flag_id"], boss["revive_flags"], boss.get("rememberance")) for boss in region["bosses"]]) for region in regions]
        require(baseline is None or layout == baseline, f"Language data differs: {path}")
        baseline = layout
        for boss in bosses:
            primary = boss["flag_id"]
            recipe = recipes[primary]
            actual = flags(boss["revive_flags"], primary)
            expected = {flag["flag_id"]: flag["value"] for flag in recipe["revive_flags"]}
            require(actual == expected, f"Audit/data mismatch for {primary} in {path}")
            require(flags(source[str(primary)]["revive_flags"], primary) == expected, f"Generation source mismatch for {primary}")
            require(not (set(actual) & set(recipes)), f"Other boss's primary would be changed: {primary}")
            require(recipe["primary_evidence"] and recipe["entry_evidence"], f"No original defeat/entry evidence for {primary}")
            for row in recipe["revive_flags"]:
                require(row["evidence"] and row["target_is_reconstruction"] is True and row["storage_category"] == row["flag_id"] // 1000,
                        f"Missing flag provenance/target boundary: {primary}/{row['flag_id']}")
                for index in row["evidence"]:
                    witness = recipe["witnesses"][index]
                    require(witness["source"] in files, f"Witness script is absent from manifest: {witness}")
            for index in recipe["primary_evidence"]:
                require(recipe["witnesses"][index].get("written_value") is True, f"Primary witness does not set defeat: {primary}")
            for flag in recipe["excluded_flags"]:
                require(flag["flag_id"] not in expected, f"Excluded flag is distributed: {primary}/{flag['flag_id']}")
        count += 1
    require(count == audit["coverage"]["languages"] == 14, "Expected 14 localized datasets")
    extra = sum(len(record["revive_flags"]) for record in recipes.values())
    require(extra == audit["coverage"]["extra_flag_entries"], "Extra flag count mismatch")
    true = sum(flag["value"] for record in recipes.values() for flag in record["revive_flags"])
    require(true == audit["coverage"]["set_to_true_entries"], "True target count mismatch")
    # Specific semantic regressions that ordinary JSON consistency cannot catch.
    def targets(primary: int) -> dict:
        return {flag["flag_id"]: flag["value"] for flag in recipes[primary]["revive_flags"]}
    require(1052520800 not in recipes and targets(1252520800)[1052520800] is False, "Fire Giant primary correction and old completion reset")
    require(all(targets(1252520800)[flag] is False for start in (1052522820, 1052522830) for flag in range(start, start + 9)), "Fire Giant body-part flags")
    require(targets(1252380800)[1051369360] is True and targets(1252380800)[9411] is True and targets(1252380800)[9413] is False, "Radahn festival re-entry")
    require(targets(31000800)[3682] is True and targets(31000800)[3680] is False and targets(31000800)[3685] is True, "Patches hostile/living/location state")
    require(targets(12030850)[12032859] is True and targets(2049440800)[2049442702] is True, "Dream/duel re-entry triggers")
    require(sum(targets(41020800)[flag] for flag in range(41022820, 41022828)) == 1, "Lamenter one-hot selection")
    return extra


def original_check(audit: dict, research: Path) -> int:
    event_path = research / "events"
    scripts = {path.name: read_event(path) for path in event_path.glob("*.emevd")}
    files = {record["file"]: record for record in audit["event_files"]}
    require(len(scripts) == len(files) == audit["script_count"], "Original event corpus changed")
    for name, script in scripts.items():
        require(script["sha256"] == files[name]["decompressed_sha256"], f"Original script hash mismatch: {name}")
    allocation_files = audit["archive"]["allocation_files"]
    require(len(allocation_files) == 4, "Missing original flag allocation tables")
    for record in allocation_files:
        require(hashlib.sha256((event_path / record["file"]).read_bytes()).hexdigest() == record["decompressed_sha256"],
                f"Original flag allocation table hash mismatch: {record['file']}")
    aliases = json.loads((research / "range-143b35ff0.json").read_text(encoding="utf-8"))
    require(aliases["sha256"] == audit["sample"]["sha256"], "IDA alias table belongs to another executable")
    require(hashlib.sha256(bytes.fromhex(aliases["bytes"])).hexdigest() == audit["native_alias_table"]["sha256"],
            "IDA native alias table bytes differ from the published evidence")
    categories = allocated_categories(event_path, research)
    expanded = {}

    def original_instance(witness: dict) -> dict:
        # Rebuild the caller parameters, slots and runtime variant from the
        # map constructors. Supplied witness parameters alone cannot prove
        # that the instance or common completion flag exists in the game.
        map_file = witness["map"] + ".emevd"
        require(map_file in scripts, f"Missing witness map resource: {map_file}")
        if map_file not in expanded:
            instances = instances_for(scripts[map_file], scripts, {}, [])
            expanded[map_file] = {instance_key(value, "source_file"): value for value in instances}
        key = instance_key(witness, "source")
        require(key in expanded[map_file], f"No original caller/slot/variant/parameter chain for {witness}")
        return expanded[map_file][key]

    count = 0
    for recipe in audit["bosses"]:
        references = [(recipe["flag_id"], index) for index in recipe["primary_evidence"] + recipe["entry_evidence"]]
        references += [(row["flag_id"], index) for row in recipe["revive_flags"] for index in row["evidence"]]
        for flag, index in references:
            require(flag // 1000 in categories, f"No allocated storage for {flag}")
            witness = recipe["witnesses"][index]
            instance = original_instance(witness)
            if witness["kind"] == "event_completion":
                require(instance["event"] > 299 and flag == instance["completion_flag"], f"Completion witness mismatch: {flag}")
            else:
                instruction = instance["instructions"][witness["instruction"]]
                require(instruction["raw"] == witness["raw_arguments"] and hex(instruction["file_offset"]) == witness["file_offset"],
                        f"Argument patch/offset mismatch for {flag}: {witness}")
                require(f"{instruction['bank']}[{instruction['opcode']}]" == witness["opcode"], f"Opcode witness mismatch: {witness}")
                if "common_ordinal" in witness:
                    require(instruction["common_ordinal"] == witness["common_ordinal"], f"Common ordinal witness mismatch: {witness}")
                require(any(value == flag and kind == witness["kind"] and ("written_value" not in witness or state == witness["written_value"])
                            for value, kind, state in flag_refs(instance, instruction)),
                        f"No original flag operand for {flag}: {witness}")
            count += 1
    for native in audit["native_functions"]:
        original = json.loads((research / f"function-{int(native['va'], 16):x}.json").read_text(encoding="utf-8"))
        require(original["sha256"] == audit["sample"]["sha256"] and original["function_sha256"] == native["function_sha256"], f"IDA function hash differs: {native['id']}")
        instructions = {row["ea"]: row for row in original["instructions"]}
        for row in native["instructions"]:
            require(row["bytes"] == instructions[row["ea"]]["bytes"], f"IDA instruction differs: {row['ea']}")
    return count


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, default=Path("src/boss/data"))
    parser.add_argument("--evidence", type=Path, default=Path("docs/boss-revival.evidence.json"))
    parser.add_argument("--research", type=Path, default=Path("build/ida/boss"))
    args = parser.parse_args()
    audit = json.loads(args.evidence.read_text(encoding="utf-8"))
    native_semantics_check()
    extra = data_check(audit, args.data)
    original = original_check(audit, args.research) if (args.research / "events/manifest.json").exists() else None
    print(f"PASS: 207 bosses, 14 languages, {extra} extra flags, generation source, native semantics and evidence consistency")
    if original is not None:
        print(f"PASS: {original} original EMEVD operand/completion witnesses, caller/slot/variant chains and all IDA instruction windows")
    else:
        print("Original local IDA/EMEVD corpus absent; byte-level witness recheck was not run")


if __name__ == "__main__":
    main()
