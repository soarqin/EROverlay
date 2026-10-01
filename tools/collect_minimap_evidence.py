"""Collect compact static and ordinary runtime evidence for the minimap report.

Consumes JSON exported by ida_minimap_research.py and a completed probe run.
It does not attach to the game or analyze the executable outside idapro.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import csv
from collections import Counter
from pathlib import Path

from minimap_asset_inspect import inspect


FUNCTIONS = {
    0x1401A2720: "Independent request pool allocation and submit return codes",
    0x1426EEB60: "Independent CPU byte request ABI",
    0x1426EE320: "Pending request list insertion with lock",
    0x1426EED40: "Batch flush into the game task queue",
    0x1426F42E0: "Game task enqueue and wake",
    0x1426EE2C0: "Generation-checked asynchronous cancellation",
    0x1426F9820: "Request buffer allocator, callback, context and DCX flag",
    0x1426F8870: "DCX header and decompression scheduling",
    0x1426F94C0: "Four-register completion callback and successful buffer ownership transfer",
    0x1426F9340: "Cleanup of buffers not transferred and request storage",
    0x14265B300: "High-level path suffix and DCX flag conversion",
    0x1401F5560: "TPF-specific FileCap creation; unsuitable as raw byte API",
    0x142653FA0: "CSFile path-keyed cap reuse",
    0x14265B8A0: "Generic FileCap constructor",
    0x14265B950: "FileCap processor cleanup",
    0x14265BA40: "Temporary CPU buffer acquire",
    0x14265BAA0: "Temporary CPU buffer size",
    0x14265BAD0: "Non-atomic mapped-buffer release",
    0x1401F4F00: "Textlist-specific request; not raw bytes",
    0x140D79300: "71_MapTile group TPFBHD mount",
    0x1401F52F0: "TpfbhdMultiMountFileCap and menutpfbnd",
    0x140D78F80: "Public TextureAtlas layout bundle path selection",
    0x1401F5A00: "Layout bundle request with DCX",
    0x1402236E0: "Binder layout entries passed to TextureAtlas parser",
    0x140D665A0: "TextureAtlas imagePath and SubTexture region parser",
    0x140E7C180: "Optional native path expansion candidate",
    0x141E969F0: "PC TPF magic, platform and header checks",
    0x141E959D0: "TPF entry cursor and optional metadata records",
    0x141E95830: "TPF DDS data offset",
    0x141E95840: "TPF DDS byte size",
    0x141E95850: "TPF texture type",
    0x141E95860: "TPF container format byte",
    0x141E95870: "TPF name offset",
    0x141E958B0: "DDS width getter",
    0x141E958C0: "DDS height getter",
    0x141E958D0: "DDS mip getter",
    0x141E99D10: "DDS magic check",
    0x140885450: "Tile name and key encoding",
    0x140884030: "Tile variant selection and availability",
    0x140887550: "Per-tile allowed variant mask",
    0x140887630: "Per-tile availability",
    0x1408892C0: "Active variant mask from params and flags",
    0x140889530: "Map piece/event flag condition",
    0x14088A0A0: "Active mask update and reveal handling",
    0x140D58B40: "WorldMapPieceParam repository group 88 and row lookup",
    0x1405D2180: "Zero event IDs are false before reading flag storage",
    0x1405FA250: "Event flag storage: type 1 packed index, type 2 direct pointer",
    0x1408877C0: "Map number 0/1/10 to active mask cache slots",
    0x1409CCC10: "World map binds unsearched-mask layers separately from tile layers",
    0x1409E1F10: "Tiled layer stores two map numbers and their independent active masks",
    0x1409E07B0: "Tile Image_0/Image_1 resources use separate map numbers and masks",
    0x1409E1C50: "Image_1 visibility is controlled independently of Image_0",
    0x1408859D0: "World tile spans 256, 342 and 1288; inverse Y indices",
    0x140885B30: "Zoom state to native L index",
    0x1408770F0: "Map coordinate converter settings",
    0x140877130: "World-to-map conversion",
    0x1408865A0: "m60/m61 origins, zero anchor and +128 display offsets",
    0x1408785D0: "Legacy-map conversion lookup",
    0x140877CD0: "Legacy conversion graph from base-point param rows",
    0x1407ECC80: "Menu owner and backreader creation",
    0x1407EE6C0: "Map view model and backreader pointer offsets",
    0x1407EF6D0: "Backreader polling before view model update",
    0x140B820C0: "Alternative game GPU repository lookup; not used by byte route",
    0x140D66E60: "Alternative GPU texture bridge",
    0x1419F25D0: "Alternative CGTexture getter",
    0x141E928E0: "Alternative ID3D12Resource creation IID",
    0x14087CA60: "Point icon IDs copied from parameter offsets +0x0C/+0xB8/+0x1A",
    0x14087CF10: "Point normal/alternate/distant icon selection",
    0x14087BE10: "WorldMap pin binds selected IconID to Icon_0 and rotates it",
    0x14074CB10: "Icon binding chooses a movie frame for numeric IconID",
    0x14074AB30: "Numeric IconID forwarded unchanged to frame-selection adapter",
    0x14074A7D0: "One-based frame selection through Scaleform object interface +0x148",
    0x140D84320: "Current movie frame converted from zero-based engine value to one-based",
    0x140D7D5B0: "World-map GFX path: Win candidate, then menu fallback",
    0x14116B400: "GFX tag dispatch: normal tag table and tags 1000..1009",
    0x1411BC9F0: "GFX tag header: high ten bits type, low six bits length or U32 extension",
    0x1411E5890: "GFX DefineExternalImage2 tag: U32 ID, U16 format and dimensions, U8-length strings",
    0x1411BCC80: "Length-prefixed GFX external-image names",
    0x1411BF220: "PlaceObject3 flags and bitmap class-name consumption",
    0x1411BDBC0: "GFX transform: 16.16 scale/skew and twip translations",
    0x1411C0180: "DefineSprite frame count and ShowFrame records",
    0x140120DA0: "TextureAtlas SubTexture lookup key removes path and extension",
    0x140D67F20: "SubTexture key selects atlas imagePath and region",
    0x140D6A1C0: "Native region dimensions and half flag",
    0x140D59D80: "Point alternate icon applies when a visible text type is 1",
    0x140D5A220: "Point text existence and enable/disable event predicates",
    0x14088C7A0: "Grace normal/forbidden/alternate icon IDs",
    0x14088CB50: "Grace selects normal or forbidden frame and alternate state",
    0x140D272B0: "Grace alternate icon conditional selection",
    0x1409BF830: "WorldMapDialog binds Player, Dead at +0xCA8 and Home; Home row from +0x278",
    0x1409C4B20: "WorldMapDialog applies death-location +0xA9 visibility to the bound Dead object",
    0x140D2B040: "GameSystemCommonParam row 0 through repository group 141",
    0x1409C96D0: "Home marker placed relative to Home layout bounds and viewport",
    0x1409CD710: "Map rectangle to view coordinates",
    0x1409CE2F0: "Home view position back to map coordinates",
    0x140887C30: "Player raw map ID and yaw+180 native presentation",
    0x140256340: "GameDataMan +0x48 supplies the saved death record",
    0x140256B90: "GameDataMan +0x40 is the saved death record validity flag",
    0x140888860: "Death/Home visibility; mask refresh gated by view binding; base and second-map tile requests",
    0x140D85D20: "Native rotation normalized modulo 360; float input is XMM1",
    0x1426F4FD0: "Request pool retirement advances generation after completion callback returns",
    0x140661B70: "Native raw-map bytes encode area/grid X/grid Z",
    0x140886EC0: "Grace record copy preserves native icon state and +0x348 selection flag",
}

IMPORTANT = re.compile(r"call|jmp|mov [re][89]|mov ecx|mov rdx|mov qword|divss|mulss|addss|subss|\+68h|\+90h|0D70h|156h|508h|100h|\+39Ch|\+248h|\+250h|0FFFFFFFF|0FFFFFF0")

SPRITE_FUNCTIONS = {
    0x14087CA60, 0x14087CF10, 0x14087BE10, 0x14074CB10, 0x14074AB30, 0x14074A7D0, 0x140D84320,
    0x140D7D5B0, 0x14116B400, 0x1411BC9F0, 0x1411E5890, 0x1411BCC80, 0x1411BF220, 0x1411BDBC0,
    0x1411C0180, 0x140120DA0, 0x140D67F20, 0x140D6A1C0, 0x140D59D80, 0x140D5A220, 0x14088C7A0,
    0x14088CB50, 0x140D272B0, 0x1409BF830, 0x1409C4B20, 0x140D2B040, 0x1409C96D0, 0x1409CD710, 0x1409CE2F0,
    0x140887C30, 0x140888860, 0x140D85D20,
}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--static", type=Path, default=Path("build/ida/minimap"))
    parser.add_argument("--runtime", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=Path("docs/minimap-game-textures.evidence.json"))
    parser.add_argument("--diagnostics", type=Path, help="Optional directory with ordinary read-only state probe logs")
    parser.add_argument("--initial-runtime", type=Path, help="Initial ordinary probe directory, including failed path and flush observations")
    parser.add_argument("--sprite-evidence", type=Path, help="Checked external-pixel and native GFX recipe evidence")
    parser.add_argument("--sprite-runtime", type=Path, help="Completed ordinary GFX/parameter read run")
    args = parser.parse_args()
    metadata = json.loads((args.static / "metadata.json").read_text(encoding="utf-8"))
    all_functions = [json.loads(path.read_text(encoding="utf-8")) for path in args.static.glob("function_*.json")]
    records = []
    for address, purpose in FUNCTIONS.items():
        record = next((record for record in all_functions if int(record["start"], 16) <= address < int(record["end"], 16)), None)
        if record is None:
            raise ValueError(f"Missing IDA function export at {address:#x}")
        instructions = record["instructions"]
        # Entry evidence is not a verified wildcard runtime signature.
        entry = b""
        cursor = int(record["start"], 16)
        for item in instructions:
            if int(item["ea"], 16) != cursor:
                break
            raw = bytes.fromhex(item["bytes"])
            entry += raw
            cursor += len(raw)
            if len(entry) >= 32:
                break
        if address == 0x1426F94C0:
            selected = [item for item in instructions if 0x1426F9588 <= int(item["ea"], 16) <= 0x1426F95B6]
        elif address == 0x1408859D0:
            selected = [item for item in instructions if 0x1408859F0 <= int(item["ea"], 16) <= 0x140885AF9]
        elif address == 0x1408865A0:
            selected = [item for item in instructions if 0x140886817 <= int(item["ea"], 16) <= 0x1408868D5]
        elif address == 0x1409BF830:
            selected = [item for item in instructions if 0x1409BF9E6 <= int(item["ea"], 16) <= 0x1409BFA45
                        or 0x1409C0FD0 <= int(item["ea"], 16) <= 0x1409C1054]
        elif address == 0x1409C4B20:
            selected = [item for item in instructions if 0x1409C4D25 <= int(item["ea"], 16) <= 0x1409C4DB4
                        or 0x1409C4E67 <= int(item["ea"], 16) <= 0x1409C4E84]
        elif address in (0x14074CB10, 0x14087CA60, 0x14087CF10, 0x140887C30, 0x140888860):
            expressions = {
                0x14074CB10: r"call sub_14074AB30|\+3Ch|\+60h|test|cmp",
                0x14087CA60: r"\+0Ch|\+0B8h|\+1Ah|\+250h|\+290h|\+2D0h",
                0x14087CF10: r"\+238h|\+2D0h|call sub_140D59D80|250h|290h|cmov",
                0x140887C30: r"\+34h|\+14h|call sub_140877130|addss|mulss",
                0x140888860: r"call sub_140D2B040|\+278h|call sub_140D27220|cmp|\+0A9h",
            }
            selected = [item for item in instructions if re.search(expressions[address], item["disassembly"])][:16]
        elif address in SPRITE_FUNCTIONS:
            selected = instructions if len(instructions) <= 20 else [item for item in instructions if IMPORTANT.search(item["disassembly"])][:18]
        else:
            selected = instructions if len(instructions) <= 28 else [item for item in instructions if IMPORTANT.search(item["disassembly"])][:55]
        records.append({"va": record["start"], "rva": record["rva"], "ida_name": record["name"], "purpose": purpose,
                        "entry_bytes": entry[:32].hex(), "incoming": record["callers"], "selected_instructions": selected})

    runtime_log = (args.runtime / "events.log").read_text(encoding="utf-8")
    if "finished" not in runtime_log:
        raise ValueError("Runtime probe has not completed")
    completed = []
    pattern = re.compile(r"complete path=(.*?) id=([0-9a-f]+) status=(\d+) size=(\d+) magic=([0-9a-f]+) output-error=(\d+)")
    for match in pattern.finditer(runtime_log):
        completed.append({"path": match[1], "request_id": match[2], "status": int(match[3]),
                          "bytes": int(match[4]), "magic_hex": match[5], "output_error": int(match[6])})
    if len(completed) != 12 or any(item["output_error"] for item in completed):
        raise ValueError("Expected 12 completed requests with no output errors")
    assets = []
    for path in sorted(args.runtime.iterdir()):
        if path.suffix == ".tpf" or path.name in ("map-index.bin", "map-masks.bin", "common-layouts.bin"):
            assets.append(inspect(path))
    index = next(asset for asset in assets if asset["kind"] == "BHF4")
    map_counts = Counter()
    for entry in index["entries"]:
        match = re.search(r"MENU_MapTile_M(\d\d)_L(\d)_", entry["name"])
        if match:
            map_counts[f"M{match[1]}_L{match[2]}"] += 1
    summaries = []
    for asset in assets:
        summary = {key: asset[key] for key in ("file", "bytes", "sha256", "kind")}
        if asset["kind"] == "TPF":
            summary["texture_count"] = asset["texture_count"]
            summary["textures"] = asset["textures"] if asset["texture_count"] == 1 else [item for item in asset["textures"] if item["name"].startswith("SB_MapCursor")]
        else:
            summary["entry_count"] = asset["entry_count"]
            if asset["kind"] == "BHF4":
                summary["counts_by_map_and_level"] = dict(sorted(map_counts.items()))
                summary["sample_entries"] = asset["entries"][:1] + asset["entries"][-1:]
            elif any("tile_masks" in entry for entry in asset["entries"]):
                summary["maps"] = [{"name": entry["name"], "keys": len(entry["tile_masks"]),
                                    "example_2020": next((item for item in entry["tile_masks"] if item["key"] == 2020), None)} for entry in asset["entries"]]
            else:
                summary["atlases"] = [entry["atlas"] for entry in asset["entries"] if "atlas" in entry and entry["atlas"]["image_path"].startswith("SB_MapCursor")]
        summaries.append(summary)

    constants = {}
    for name, address in (("converter_display_offset", 0x142ADB410), ("tile_display_rectangle", 0x143B3BD10)):
        raw = bytes.fromhex(json.loads((args.static / f"address_{address:x}.json").read_text(encoding="utf-8"))["bytes"])
        constants[name] = {"va": hex(address), "floats": list(struct.unpack_from("<4f", raw))}
    constants["world_tile_spans"] = [256, 342, 1288]
    constants["axis_counts"] = [41, 31, 9]
    constants["view_underground_flag"] = {"offset": "0x30", "bytes": 1, "writer_rva": "0x887f37"}
    constants["zero_anchor"] = {"status": "Runtime confirmed; initialized .data is unavailable in static source", "value": [0, 0, 0]}

    diagnostics = []
    icon_coverage = None
    if args.diagnostics:
        for name in ("state.log", "legacy-rows.bin", "point-icons.csv", "grace-icons.csv"):
            path = args.diagnostics / name
            if path.exists():
                data = path.read_bytes()
                record = {"file": str(path), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
                if name == "state.log":
                    # Keep the final corrected snapshot, omitting an earlier wrong table index trial.
                    text = data.decode("utf-8")
                    record["log"] = "menu=" + text.rsplit("menu=", 1)[-1]
                diagnostics.append(record)
        point_path = args.diagnostics / "point-icons.csv"
        if point_path.exists():
            rows = list(csv.DictReader(point_path.open(encoding="utf-8")))
            visible = [row for row in rows if int(row["id"]) >= 78500 and int(row["iconId"]) not in (0, 80)
                       and any(int(row[key]) for key in ("dispM00", "dispM01", "dispM10"))]
            icons = {int(row["iconId"]) for row in visible}
            layouts = next(asset for asset in assets if "common-layouts" in asset["file"])
            names = {sprite["name"] for entry in layouts["entries"] for sprite in entry.get("atlas", {}).get("sprites", [])}
            missing = sorted(icon for icon in icons if f"MENU_MAP_{icon:02d}.png" not in names)
            icon_coverage = {"point_rows": len(rows), "visible_markers": len(visible), "unique_icon_ids": len(icons),
                             "direct_name_matches": len(icons) - len(missing), "unresolved_alias_ids": missing}
    initial_runtime = None
    if args.initial_runtime:
        path = args.initial_runtime / "events.log"
        initial_runtime = {"directory": str(args.initial_runtime), "log": path.read_text(encoding="utf-8"),
                           "context": "Title-menu resources were initialized; no in-game world map was opened by the research tools"}
    gpu_outputs = []
    for name in ("final-surface-v8000.bmp", "final-dlc-v3.bmp", "final-map-atlas.bmp"):
        path = args.runtime.parent / name
        if path.exists():
            data = path.read_bytes()
            gpu_outputs.append({"file": str(path), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})

    sprites = None
    if args.sprite_evidence:
        raw = args.sprite_evidence.read_bytes()
        sprite_evidence = json.loads(raw.decode("utf-8"))
        sources = sprite_evidence["sources"]
        for source in sources:
            if hashlib.sha256(Path(source["file"]).read_bytes()).hexdigest() != source["sha256"]:
                raise ValueError(f"Sprite source hash differs: {source['file']}")
        sprites = {"file": str(args.sprite_evidence), "sha256": hashlib.sha256(raw).hexdigest(),
                   "comparison": {key: sprite_evidence["comparison"][key] for key in ("sprites", "exact_matches", "nonidentical")},
                   "gfx": sprite_evidence["gfx"], "coverage": {key: sprite_evidence["coverage"][key] for key in
                       ("base_icon_ids", "candidate_count", "missing_recipe_ids", "alias_marker_counts")},
                   "roundtable": sprite_evidence["roundtable"], "sources": sources}
        if args.sprite_runtime:
            log = (args.sprite_runtime / "events.log").read_text(encoding="utf-8")
            if "finished" not in log:
                raise ValueError("Sprite probe has not completed")
            sprites["runtime"] = {"directory": str(args.sprite_runtime), "log": log, "debugger": False, "breakpoints": False}
        if icon_coverage:
            icon_coverage["ids_without_direct_numbered_name"] = icon_coverage["unresolved_alias_ids"]
            icon_coverage["unresolved_alias_ids"] = sprite_evidence["coverage"]["missing_recipe_ids"]
            icon_coverage["native_recipe_matches"] = icon_coverage["unique_icon_ids"]
            icon_coverage["candidate_state_ids_checked"] = sprite_evidence["coverage"]["candidate_count"]

    checked_sources = []
    for path in [Path("tools/minimap_file_probe.cpp"), Path("tools/minimap_probe_loader.cpp"), Path("tools/minimap_dds_probe.cpp"),
                 Path("tools/minimap_asset_inspect.py"), Path("tools/ida_minimap_research.py"),
                 Path("tools/minimap_gfx_inspect.py"), Path("tools/compare_minimap_sprites.py")]:
        data = path.read_bytes()
        checked_sources.append({"file": str(path), "sha256": hashlib.sha256(data).hexdigest()})
    evidence = {
        "analysis_date": "2026-10-01", "sprite_analysis_completed": "2026-10-02",
        "kind": "idapro static analysis plus ordinary mod file reads and standalone D3D12 sampling",
        "sample": {key: metadata[key] for key in ("input_file", "sha256", "image_base", "ida_version", "analysis_mode")},
        "file_version": "2.7.1.0", "steam_build_id": "25080141", "title_app_version": "1.17.1", "title_regulation_version": "1.17.1",
        "constraints": {"static_tool": "Python idapro/idalib", "debugger_attachment": False, "breakpoints": False,
                        "source_executable_modified": False, "game_gpu_handles_reused": False},
        "functions": records, "constants": constants, "checked_sources": checked_sources, "sprites": sprites,
        "runtime": {"directory": str(args.runtime), "pid": 26836,
                    "context": "Final rerun in the same research process; subsequently observed in gameplay. First successful read was after title-menu initialization.",
                    "log": runtime_log, "completed_requests": completed, "assets": summaries, "diagnostics": diagnostics,
                    "initial_probe": initial_runtime, "icon_coverage": icon_coverage,
                    "gpu_sampling": {"program": "tools/minimap_dds_probe.cpp", "adapter": "Intel(R) Arc(TM) B390 GPU",
                                     "own_resource": True, "own_srv": True, "own_direct_queue": True, "own_fence": True,
                                     "game_device_used": False, "verified_assets": ["Surface variant 00008000", "DLC variant 00000003", "SB_MapCursor"],
                                     "outputs": gpu_outputs,
                                     "method": "Compute shader Load -> owned RGBA UAV -> readback BMP"}},
        "caveats": ["RVA and layouts are specific to this executable SHA256.",
                    "Pseudocode prototypes can be wrong; ABI conclusions use assembly and ordinary calls.",
                    "Entry bytes are evidence, not uniqueness-checked cross-version signatures.",
                    "The verification DLL remains loaded until the research process exits.",
                    "Full minimap visuals, frame synchronization, teleport/reload/exit races and performance are development acceptance work.",
                    "All existing sprite assets have native replacements; complete in-game rendering remains development acceptance.",
                    "Roundtable is a composite: its local comparison crop is not a native single bitmap or native world coordinate."],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(evidence, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {args.output}: {len(records)} static records, {len(completed)} completed requests, {len(assets)} assets")


if __name__ == "__main__":
    main()
