"""Export only reviewed revival writes from an explicit C++ history commit.

Normal builds consume configs/boss-revival.json and do not need C++ history.
Empty arrays intentionally authorize reviewed primary-only resets; absent keys
do not authorize revival of newly discovered or Mod encounters.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

BASELINE = "afab7e14db2b62ae0bede980d5d0c96634321350"
SOURCE = "src/boss/data/gen/revival.json"


def convert(data: bytes) -> dict[str, list]:
    source = json.loads(data)
    result = {}
    primaries = {entry["flag_id"] for entry in source.values()}
    for entry in source.values():
        primary = entry["flag_id"]
        if type(primary) is not int or not 0 < primary <= 0xffffffff:
            raise ValueError("invalid primary flag")
        if str(primary) in result:
            raise ValueError("duplicate primary flag")
        seen = set()
        writes = []
        for item in entry["revive_flags"]:
            flag = item["flag_id"] if isinstance(item, dict) else item
            value = item["value"] if isinstance(item, dict) else False
            if type(flag) is not int or not 0 < flag <= 0xffffffff or type(value) is not bool:
                raise ValueError("invalid auxiliary flag")
            if flag in primaries or flag in seen:
                raise ValueError("auxiliary flag resets another primary or duplicates a write")
            seen.add(flag)
            writes.append({"flag_id": flag, "value": True} if value else flag)
        result[str(primary)] = writes
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=Path, required=True)
    parser.add_argument("--commit", default=BASELINE)
    parser.add_argument("--output", type=Path, default=Path("configs/boss-revival.json"))
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    data = subprocess.check_output(["git", "-C", str(args.repository), "show", f"{args.commit}:{SOURCE}"])
    result = convert(data)
    output = (json.dumps(result, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    if args.check:
        if args.output.read_bytes() != output:
            raise SystemExit("revival configuration differs from the explicitly selected source")
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(output)
    print(json.dumps({"commit": args.commit, "source": SOURCE,
                      "source_sha256": hashlib.sha256(data).hexdigest(),
                      "encounters": len(result), "writes": sum(map(len, result.values())),
                      "primary_only": sum(not writes for writes in result.values()),
                      "output_sha256": hashlib.sha256(output).hexdigest()}))


if __name__ == "__main__":
    main()
