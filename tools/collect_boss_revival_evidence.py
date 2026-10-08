"""Publish compact IDA/EMEVD witnesses for every distributed boss recipe.

Full decompilations, copyrighted EMEVDs and archive keys remain in build/ida.
The checked-in audit contains hashes, short instruction windows, flag operands,
caller parameter bytes and explicit reconstruction/verification boundaries.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path

from ida_minimap_versions import write_json


BASELINE_SHA256 = "1a3547101327f65d0c76da2f9190ac0aa66871ea42bae2aecc61e11a8b597891"


# Windows were selected from IDA disassembly, not EMEDF descriptions.
NATIVE = {
    "bank_dispatch": ("140568b90", [("0x140568b90", 282)], "Banks 3/1003/2003/2000 dispatch to condition/control/write/initialize handlers."),
    "flag_write_operands": ("14056cad0", [("0x14056cc2b", 19), ("0x14056cceb", 20)], "2003[66]/[69]: type at +0, ID at +4, state at +8."),
    "flag_conditions": ("14056eee0", [], "Bank 3 flag conditions and event-value byte width verified in IDA."),
    "flag_control_flow": ("14056fd50", [], "Bank 1003 tests flag operands before skip/end/goto; operand offsets are retained in EMEVD witnesses."),
    "flag_type_resolution": ("1405842b0", [("0x1405842b0", 17)], "Type 0 is absolute; type 2 adds this instance's completion flag; type 1 returns zero in the sample."),
    "event_slot": ("140584250", [("0x140584250", 18)], "Positive signed 16-bit slot adds to event ID; slot <= 0 retains event ID."),
    "event_completion": ("140584030", [("0x140584030", 41)], "Finishing/restarting an event writes its completion flag when event ID > 299."),
    "common_initialization": ("140575700", [("0x140575829", 59)], "Common calls use slot -1 and override completion with map + variant + unique argument ordinal."),
    "common_ordinal_table": ("140ce26f0", [("0x140ce26f0", 64)], "Resource event/instruction order; only opcode 6 offsets enter the vector, irrespective of bank."),
    "common_ordinal_dedup": ("140ce1850", [("0x140ce1850", 17)], "Identical argument offsets are inserted once."),
    "common_ordinal_lookup": ("140ce18a0", [("0x140ce18a0", 17)], "Returns the zero-based index in the resource-wide unique-offset vector."),
    "common_flag_bank": ("1405cbc70", [("0x1405cbc70", 11)], "Bank 5, low 16 bits of ordinal."),
    "common_flag_number": ("1405cc060", [("0x1405cc060", 85)], "Legacy area/block or field tile/variant forms the prefix; ordinal modulo 1000 forms the suffix."),
    "map_variant_packing": ("14029a960", [("0x14029a960", 45)], "Field scale is packed separately from runtime variant; 0/100/200 constructors carry variants 0/1/2."),
    "constructors": ("140588ec0", [("0x140588fbe", 39), ("0x14058909d", 47)], "Selects 0/50, 100/150, 200/250 by runtime field-map variant."),
    "instance_map_context": ("1405844c0", [("0x1405844c0", 47)], "Initialized children inherit runtime map/variant context, independently of resource filename."),
    "event_reinitialization": ("140588320", [("0x140588320", 54)], "Checks active instances, not historical completion bits; unused completion flags need not be cleared."),
    "linked_event_lookup": ("1405755d0", [("0x1405755d0", 78)], "Searches linked resources before local event resource."),
    "parameter_patch": ("140585dd0", [], "Copies argument bytes and applies EMEVD parameter patches before execution."),
    "flag_read_storage": ("1405fa250", [("0x1405fa250", 54)], "Exact category key; type 1 packed index; type 2 direct pointer; MSB-first bits."),
    "flag_write_storage": ("1405faa40", [("0x1405faa40", 59)], "Writes the same category and bit layout used by the reader."),
    "category_allocation": ("1405d3670", [("0x1405d3670", 131)], "Allocates global, field and CSV map categories; applies native aliases."),
    "map_banks": ("1405ce3f0", [("0x1405ce3f0", 100)], "Map banks 0,1,2,3,4,5,7,8,9; bank 6 has no storage."),
    "field_banks": ("1405ce550", [("0x1405ce550", 85)], "Default field banks 0,2,4,5,7,8,9, with source-to-DLC alias resolution."),
    "field_aliases": ("1405cb000", [], "Loads 372 native alias records at VA 0x143B35FF0; most DLC tiles reuse base-map storage."),
    "legacy_category_number": ("1405cbcc0", [("0x1405cbcc0", 5)], "bank + 10*(block + 100*area)."),
    "field_category_number": ("1405cbcd0", [("0x1405cbcd0", 10)], "bank + 10*(z + 100*(x + 100*(variant + 10*world)))."),
    "initial_flag_storage": ("1405fa700", [("0x1405fa700", 35)], "Initial packed flag memory is zero-filled."),
    "direct_storage_registration": ("1405fa9c0", [("0x1405fa9c0", 28)], "Native type-2 node registration stores a direct pointer."),
    "dialog_arguments": ("140571370", [], "2007[10]: response flag operands at +16,+20,+24."),
    "dialog_response_storage": ("14057b690", [("0x14057b690", 7)], "Copies yes/no/other flag IDs to offsets +96/+100/+104."),
    "dialog_yes_write": ("14057bad0", [("0x14057bad0", 31)], "Yes callback writes its absolute response flag true."),
    "dialog_no_write": ("14057b9b0", [("0x14057b9b0", 31)], "No callback writes its absolute response flag true."),
}


def native_witnesses(directory: Path, sample: str) -> list[dict]:
    result = []
    for name, (address, windows, meaning) in NATIVE.items():
        record = json.loads((directory / f"function-{address}.json").read_text(encoding="utf-8"))
        if record["sha256"] != sample or not record.get("function_sha256"):
            raise ValueError(f"Missing or mismatched IDA function hash for {name}")
        rows = record["instructions"]
        selected = []
        # Long dispatcher functions use only referenced case windows.
        if name == "flag_conditions":
            windows = [(row["ea"], 13) for row in rows if "case " in row["text"] and any(f"case {op}" in row["text"] for op in (0, 1, 10, 12))][:4]
        elif name == "flag_control_flow":
            windows = [(row["ea"], 12) for row in rows if "case " in row["text"]][:5]
        elif name == "dialog_arguments":
            windows = [(row["ea"], 26) for row in rows if "case 10" in row["text"]][:1]
        elif name == "bank_dispatch":
            windows = [(row["ea"], 5) for row in rows if any(target in row["text"] for target in ("14056CAD0", "14056EEE0", "14056FD50", "140575700"))]
        elif name == "parameter_patch":
            windows = [(record["ea"], 45)]
        elif name == "field_aliases":
            windows = [(row["ea"], 14) for row in rows if "143B35FF8" in row["text"]][:1]
        for start, count in windows:
            index = next((i for i, row in enumerate(rows) if int(row["ea"], 16) >= int(start, 16)), None)
            if index is None:
                raise ValueError(f"Invalid IDA evidence window {name}: {start}")
            selected.extend(rows[index:index + count])
        unique = {row["ea"]: {key: row[key] for key in ("ea", "bytes", "text")} for row in selected}
        result.append({"id": name, "va": record["ea"], "rva": hex(int(record["ea"], 16) - 0x140000000),
                       "end": record["end"], "function_sha256": record["function_sha256"],
                       "meaning": meaning, "instructions": list(unique.values())})
    return result


def compact(record: dict) -> dict:
    result = copy.deepcopy(record)
    witnesses, index = [], {}
    def intern(witness: dict) -> int:
        key = json.dumps(witness, sort_keys=True)
        if key not in index:
            index[key] = len(witnesses)
            witnesses.append(witness)
        return index[key]
    for name in ("primary_evidence", "entry_evidence"):
        result[name] = [intern(w) for w in result[name]]
    for flag in result["revive_flags"]:
        flag["evidence"] = [intern(w) for w in flag["evidence"]]
    for flag in result["excluded_flags"]:
        flag["evidence"] = intern(flag["evidence"])
    result["witnesses"] = witnesses
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--research", type=Path, default=Path("build/ida/boss"))
    parser.add_argument("--recipes", type=Path, default=Path("build/ida/boss/revival.evidence.json"))
    parser.add_argument("--output", type=Path, default=Path("docs/boss-revival.evidence.json"))
    args = parser.parse_args()
    audit = json.loads(args.recipes.read_text(encoding="utf-8"))
    if audit["sample"]["sha256"] != BASELINE_SHA256:
        raise ValueError("This reviewed native evidence profile only applies to the 1.17.1 baseline hash")
    audit["native_functions"] = native_witnesses(args.research, audit["sample"]["sha256"])
    audit["sample"]["file_version"] = "2.7.1.0"
    audit["sample"]["application_version"] = "1.17.1"
    table = json.loads((args.research / "range-143b35ff0.json").read_text(encoding="utf-8"))
    if table["sha256"] != audit["sample"]["sha256"]:
        raise ValueError("IDA alias table belongs to another executable")
    audit["native_alias_table"] = {"va": table["ea"], "size": table["size"],
                                     "sha256": hashlib.sha256(bytes.fromhex(table["bytes"])).hexdigest(), "records": 372}
    audit["coverage"] = {"bosses": len(audit["bosses"]),
                         "with_extra_flags": sum(bool(b["revive_flags"]) for b in audit["bosses"]),
                         "extra_flag_entries": sum(len(b["revive_flags"]) for b in audit["bosses"]),
                         "set_to_true_entries": sum(f["value"] for b in audit["bosses"] for f in b["revive_flags"]),
                         "languages": 14, "in_game_verified": False}
    audit["method"] = [
        "Python idapro/IDA 9.4 verifies instruction argument layouts, relative flag semantics, completion arithmetic and native storage.",
        "Original installed Data0 EMEVDs are decoded with exact parameter patches and linked event resources.",
        "Battle graph candidates are confined to witnessed local combat flags; explicitly reviewed entries handle shared arenas, summons and entry gates.",
        "Each distributed flag has an EMEVD operand/completion witness and an allocated native storage category.",
        "Flag membership is witnessed; revival target values are a static reconstruction, not a captured original pre-fight save.",
        "Primary defeat flags of other listed bosses, reward/story flags and unrelated one-time completion flags are preserved.",
    ]
    audit["limitations"] = [
        "No game injection or in-game revival test was performed. Static recipes require area reload for character/asset recreation.",
        "Quest-linked bosses can depend on existing global progression and talk-script state outside EMEVD; see the accompanying boss-revival.md notes.",
        "The corpus retains 13 unresolved original InitializeEvent calls and unavailable common_macro.emevd links. All 207 primary writers resolve.",
        "Recipes are based on this executable/Data0 hash pair. Modified event scripts and other game versions need separate verification.",
        "Resolved addresses are preflighted before writes; individual bit writes are atomic but do not pause the game's event interpreter.",
    ]
    audit["unavailable_linked_resources"] = ["common_macro.emevd"]
    audit["bosses"] = [compact(record) for record in audit["bosses"]]
    audit["schema_version"] = 2
    write_json(args.output, audit)
    print(f"Published {len(audit['native_functions'])} IDA functions, {len(audit['bosses'])} bosses, {audit['coverage']['extra_flag_entries']} extra flag entries")


if __name__ == "__main__":
    main()
