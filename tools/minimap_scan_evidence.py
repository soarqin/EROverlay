"""Check the runtime scanner against archived profiles and optional independent IDA evidence."""

import argparse
from datetime import date
import hashlib
import json
from pathlib import Path
import subprocess

from generate_minimap_profiles import profiles


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def ida_bindings(evidence):
    base = 0x140000000
    result = {name: int(evidence["functions"][name], 0) - base for name in ("submit", "flush", "enqueue", "cancel")}
    for name, key in (("manager", "manager"), ("allocator", "allocator"), ("retirePoolOwner", "retire_pool_owner"),
                      ("menu", "menu"), ("gameData", "game_data"), ("eventFlags", "event_flags"), ("field", "field"),
                      ("repository", "repository"), ("reveal", "reveal")):
        result[name] = int(evidence["globals"][key], 0) - base
    layout = evidence["layout"]
    result["layout"] = [layout[key] for key in ("menu_owner", "owner_view", "menu_info", "screen_state", "grace_stride", "grace_normal")]
    result["layout"] += [int(layout["alternate_icons"]), 7 if 61 in layout["native_area_codes"] else 3, layout["repository_groups"]]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--extra-exe", type=Path, action="append", default=[])
    parser.add_argument("--ida-evidence", type=Path, action="append", default=[])
    parser.add_argument("--output", type=Path, default=Path("build/ui-scene/minimap-scan-evidence.json"))
    args = parser.parse_args()
    args.archive = args.archive.resolve()
    args.extra_exe = [path.resolve() for path in args.extra_exe]
    args.output = args.output.resolve()
    root = Path(__file__).resolve().parent.parent
    verifier = root / "build/ui-scene/minimap_scan_verify.exe"
    historical = json.loads((root / "docs/minimap-version-compatibility.evidence.json").read_text(encoding="utf-8"))
    expected = {item["sha256"]: {key: value for key, value in item.items() if key not in ("label", "sha256", "path")} for item in profiles(historical)}
    for path in args.ida_evidence:
        evidence = json.loads(path.read_text(encoding="utf-8"))
        expected[evidence["sha256"]] = ida_bindings(evidence)
    files = sorted(args.archive.glob("*/Game/eldenring.exe"), key=lambda p: tuple(int(v) for v in p.parents[1].name.split(".")))
    files += args.extra_exe
    if not files:
        raise RuntimeError("No executable fixtures")
    records = []
    for exe in files:
        run = subprocess.run([str(verifier), str(exe)], capture_output=True, text=True, cwd=root)
        if run.returncode:
            raise RuntimeError(f"{exe}: {run.stderr}")
        record = json.loads(run.stdout)
        checksum = digest(exe)
        if checksum in expected and record["bindings"] != expected[checksum]:
            raise AssertionError(f"Resolved bindings differ from independent evidence: {exe}")
        record.update(path=str(exe), sha256=checksum, independent_evidence_matches=checksum in expected)
        records.append(record)
        print(f"PASS {exe.parents[1].name}: {record['seconds']:.3f}s, independent evidence={checksum in expected}", flush=True)
    negative = subprocess.run([str(verifier), str(files[-1]), "--negative"], capture_output=True, text=True, cwd=root)
    if negative.returncode:
        raise RuntimeError(negative.stderr)
    sources = ["src/gamefiles.cpp", "src/nativeapi.h", "src/util/gamescan.hpp", "src/util/gamebindings.hpp", "src/util/minimap_scan_rules.inc",
               "src/util/peimage.hpp", "src/util/signature.hpp", "docs/minimap-scanner-signatures.json", "docs/minimap-version-compatibility.evidence.json",
               "deps/pattern16/scanners/base.h", "deps/pattern16/scanners/x64/AVX.h", "deps/pattern16/scanners/x64/SSE.h", "deps/pattern16/scanners/x64/x64.h",
               "tools/generate_minimap_scan_rules.py", "tools/generate_minimap_profiles.py", "tools/minimap_scan_verify.cpp", "tools/fixtures/pe_image.hpp",
               "tools/minimap_scan_evidence.py"]
    result = {"date": date.today().isoformat(), "hash_used_to_resolve": False, "game_code_executed": False,
              "files": len(records), "unique_executables": len({item["sha256"] for item in records}),
              "independently_verified_executables": len({item["sha256"] for item in records if item["independent_evidence_matches"]}),
              "source_sha256_encoding": "raw UTF-8 bytes with CRLF normalized to LF",
              "source_sha256": {name: hashlib.sha256((root / name).read_bytes().replace(b"\r\n", b"\n")).hexdigest() for name in sources},
              "ida_evidence_sha256": {str(path.resolve()): digest(path) for path in args.ida_evidence},
              "results": records, "negative_cases": negative.stderr.splitlines()}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {len(records)} EXEs; changed hashes are accepted, altered ABI/calls/RTTI and ambiguous candidates are rejected")


if __name__ == "__main__":
    main()
