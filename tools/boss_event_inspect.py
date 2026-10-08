"""Decode original Elden Ring EMEVDs and expand initialized event parameters.

Container bytes come from boss_event_extract.py; opcode semantics are checked
against the IDA evidence. An optional EMEDF supplies human-readable names only.
All full event dumps stay under build/ida/boss, outside distributed boss data.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from collections import Counter
from pathlib import Path

from boss_event_extract import slice_at
from ida_minimap_versions import write_json

TYPES = {0: ("B", 1), 1: ("H", 2), 2: ("I", 4), 3: ("b", 1), 4: ("h", 2),
         5: ("i", 4), 6: ("f", 4), 8: ("I", 4)}


def read_event(path: Path) -> dict:
    data = path.read_bytes()
    if data[:12] != b"EVD\0\0\xff\x01\xff\xcd\0\0\0" or len(data) < 144 or struct.unpack_from("<I", data, 12)[0] != len(data):
        raise ValueError(f"Unsupported EMEVD header in {path}")
    tables = [struct.unpack_from("<QQ", data, 16 + i * 16) for i in range(8)]
    for (count, offset), stride in zip(tables, (48, 32, 1, 32, 32, 8, 1, 1)):
        if count:
            slice_at(data, offset, count * stride)
    events = {}
    # Native 0x140CE26F0 visits events/instructions in file order and inserts
    # unique argument offsets for opcode 6 (it does not test the bank). The
    # index in this vector, not the instruction index, scopes common flags.
    common_offsets: dict[int, int] = {}
    for event_index in range(tables[0][0]):
        cursor = tables[0][1] + event_index * 48
        event_id, count, offset, pcount, poffset, rest, unknown = struct.unpack_from("<QQQQQII", data, cursor)
        if unknown or rest > 2 or event_id in events:
            raise ValueError(f"Invalid EMEVD event in {path}: {event_id}")
        instructions = []
        for index in range(count):
            instruction_offset = tables[1][1] + offset + index * 32
            bank, opcode, size, argument_offset, layer = struct.unpack_from("<iiQQQ", slice_at(data, instruction_offset, 32))
            arguments = slice_at(data, tables[6][1] + argument_offset, size) if size else b""
            if opcode == 6:
                common_offsets.setdefault(argument_offset, len(common_offsets))
            instructions.append({"index": index, "bank": bank, "opcode": opcode,
                                 "common_ordinal": common_offsets.get(argument_offset) if opcode == 6 else None,
                                 "file_offset": instruction_offset, "argument_file_offset": tables[6][1] + argument_offset if size else None,
                                 "raw": arguments.hex(), "layer": None if layer == 0xffffffffffffffff else layer})
        parameters = []
        for index in range(pcount):
            parameter = struct.unpack_from("<QQQII", slice_at(data, tables[4][1] + poffset + index * 32, 32))
            instruction, target, source, size, unknown = parameter
            if instruction >= count or target + size > len(bytes.fromhex(instructions[instruction]["raw"])):
                raise ValueError(f"Invalid EMEVD parameter in {path}: {event_id}")
            parameters.append({"instruction": instruction, "target": target, "source": source, "size": size})
        events[event_id] = {"id": event_id, "rest": rest, "file_offset": cursor, "instructions": instructions, "parameters": parameters}
    linked = []
    for index in range(tables[5][0]):
        string_offset = struct.unpack_from("<Q", data, tables[5][1] + index * 8)[0]
        cursor = tables[7][1] + string_offset
        end = cursor
        while slice_at(data, end, 2) != b"\0\0":
            end += 2
        linked.append(data[cursor:end].decode("utf-16-le").replace("\\", "/").rsplit("/", 1)[-1].removesuffix(".dcx"))
    return {"file": path.name, "sha256": hashlib.sha256(data).hexdigest(), "linked": linked, "events": events}


def load_definitions(path: Path | None) -> dict:
    if path is None:
        return {}
    emedf = json.loads(path.read_text(encoding="utf-8"))
    return {(c["index"], i["index"]): i for c in emedf["main_classes"] for i in c["instrs"]}


def describe(instruction: dict, definitions: dict) -> dict:
    data = bytes.fromhex(instruction["raw"])
    definition = definitions.get((instruction["bank"], instruction["opcode"]))
    arguments = []
    if definition:
        cursor = 0
        for argument in definition["args"]:
            fmt, width = TYPES[argument["type"]]
            cursor = (cursor + width - 1) // width * width
            if cursor + width > len(data):
                break
            value = struct.unpack_from("<" + fmt, data, cursor)[0]
            arguments.append({"name": argument["name"], "value": value, "offset": cursor, "width": width})
            cursor += width
        if instruction["bank"] == 2000 and instruction["opcode"] in (0, 6):
            arguments = arguments[:2] + [{"name": f"X{index * 4}_4", "value": value[0], "offset": 8 + index * 4, "width": 4}
                                       for index, value in enumerate(struct.iter_unpack("<I", data[8:]))]
    return {**instruction, "name": definition["name"] if definition else f"{instruction['bank']}[{instruction['opcode']}]", "arguments": arguments}


def instantiate(event: dict, params: bytes, definitions: dict) -> list[dict]:
    raw = [bytearray.fromhex(i["raw"]) for i in event["instructions"]]
    for parameter in event["parameters"]:
        start, size = parameter["source"], parameter["size"]
        if start + size > len(params):
            raise ValueError(f"Event {event['id']} needs parameter bytes [{start}, {start + size}), supplied {len(params)}")
        target = parameter["target"]
        raw[parameter["instruction"]][target:target + size] = params[start:start + size]
    return [describe({**i, "raw": bytes(r).hex()}, definitions) for i, r in zip(event["instructions"], raw)]


def instances_for(script: dict, scripts: dict, definitions: dict, missing: list[dict]) -> list[dict]:
    # 2000[6] uses slot -1 and a completion flag scoped to its caller's map and
    # unique opcode-6 argument ordinal (IDA 0x140CE26F0/0x140CE18A0).
    numbers = [int(n) for n in re.findall(r"\d+", script["file"])] if script["file"].startswith("m") else []

    def common_completion(ordinal: int, runtime_variant: int) -> int:
        if not numbers:
            return 5000 + (ordinal & 0xffff) % 1000
        area, block, sub, variant = numbers
        ordinal = (ordinal & 0xffff) % 1000
        if area == 60:
            return (10 + runtime_variant) * 100000000 + block * 1000000 + sub * 10000 + 5000 + ordinal
        if area == 61:
            return (20 + runtime_variant) * 100000000 + block * 1000000 + sub * 10000 + 5000 + ordinal
        return area % 100 * 1000000 + block * 10000 + 5000 + ordinal

    # Native 0x140588EC0 selects 0/50, 100/150, or 200/250 by the
    # field-map variant. Include all present constructors for the audit.
    roots = (0, 50, 100, 150, 200, 250)
    queue = [(script["events"][i], -1, b"", [], i, i // 100 if numbers and numbers[0] in (60, 61) else 0)
             for i in roots if i in script["events"]]
    found = []
    seen = set()
    while queue:
        event, slot, params, callers, completion, runtime_variant = queue.pop(0)
        key = (event["id"], slot, params, completion, runtime_variant)
        if key in seen:
            continue
        seen.add(key)
        instructions = instantiate(event, params, definitions)
        found.append({"map": script["file"].removesuffix(".emevd"), "event": event["id"],
                      "source_file": next(s["file"] for s in scripts.values() if s["events"].get(event["id"]) is event),
                      "slot": slot, "completion_flag": completion if event["id"] not in roots else None,
                      "runtime_variant": runtime_variant,
                      "parameters": params.hex(), "callers": callers, "instructions": instructions})
        for instruction in instructions:
            if instruction["bank"] != 2000 or instruction["opcode"] not in (0, 6):
                continue
            raw = bytes.fromhex(instruction["raw"])
            if len(raw) < 8:
                raise ValueError("Truncated InitializeEvent")
            target_slot = struct.unpack_from("<h", raw)[0]
            event_id = struct.unpack_from("<I", raw, 4)[0]
            completion_flag = event_id + max(target_slot, 0)
            if instruction["opcode"] == 6:
                target_slot = -1
                completion_flag = common_completion(instruction["common_ordinal"], runtime_variant)
            # 0x1405755D0 checks linked resources before the map resource.
            resources = [scripts[name] for name in script["linked"] if name in scripts] + [script]
            target = next((s["events"][event_id] for s in resources if event_id in s["events"]), None)
            if target is None:
                missing.append({"script": script["file"], "event": event["id"], "instruction": instruction["index"],
                                "target": event_id, "raw_arguments": instruction["raw"], "linked_resources": script["linked"]})
                continue
            queue.append((target, target_slot, raw[8:], callers + [{"event": event["id"], "instruction": instruction["index"]}],
                          completion_flag, runtime_variant))
    return found


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--events", type=Path, default=Path("build/ida/boss/events"))
    parser.add_argument("--emedf", type=Path)
    parser.add_argument("--output", type=Path, default=Path("build/ida/boss/decoded"))
    args = parser.parse_args()
    definitions = load_definitions(args.emedf)
    scripts = {p.name: read_event(p) for p in args.events.glob("*.emevd")}
    args.output.mkdir(parents=True, exist_ok=True)
    instances, errors, missing = [], [], []
    for name, script in scripts.items():
        values = {**script, "events": [{**e, "instructions": [describe(i, definitions) for i in e["instructions"]]} for e in script["events"].values()]}
        write_json(args.output / (name + ".json"), values)
        if name == "common_func.emevd":
            continue
        try:
            instances.extend(instances_for(script, scripts, definitions, missing))
        except ValueError as error:
            errors.append({"script": name, "error": str(error)})
    write_json(args.output / "instances.json", instances)
    write_json(args.output / "errors.json", errors)
    write_json(args.output / "missing-events.json", missing)
    print(f"Decoded {len(scripts)} scripts, {sum(len(s['events']) for s in scripts.values())} events, {len(instances)} parameterized instances, {len(errors)} errors")
    if errors:
        print(*errors[:10], sep="\n")
        raise SystemExit(1)


if __name__ == "__main__":
    main()
