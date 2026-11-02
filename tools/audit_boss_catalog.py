"""Run the optional production catalogue audit and fingerprint its actual inputs."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument("--params", required=True, type=Path)
    parser.add_argument("--events", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    env = os.environ.copy()
    for key, value in (
        ("EROVERLAY_CPP_REFERENCE", args.reference),
        ("EROVERLAY_BOSS_PARAM_DIR", args.params),
        ("EROVERLAY_BOSS_EVENTS_DIR", args.events),
        ("EROVERLAY_BOSS_AUDIT_OUT", args.output),
    ):
        env[key] = str(value.resolve())
    subprocess.run(
        ["cargo", "test", "-p", "eroverlay-boss", "--locked",
         "archived_old_207_to_rust_member_audit", "--", "--ignored", "--nocapture"],
        cwd=root, env=env, check=True,
    )
    artifact = json.loads((args.output / "audit.json").read_text(encoding="utf-8"))
    inputs = []

    def fingerprint(kind, directory, name):
        body = (directory / name).read_bytes()
        inputs.append({"kind": kind, "name": name, "bytes": len(body),
                       "sha256": hashlib.sha256(body).hexdigest()})

    fingerprint("cpp", args.reference, "reference.json")
    fingerprint("cpp", args.reference, "src/boss/data/engUS/bosses.json")
    for name in ("GameAreaParam", "BonfireWarpParam", "WorldMapPointParam",
                 "BonfireWarpSubCategoryParam", "BonfireWarpTabParam", "MapDefaultInfoParam"):
        fingerprint("param", args.params, name + ".param")
    for name in ("item_dlc02.bnd", "menu_dlc02.bnd"):
        fingerprint("text", args.params, name)
    for name in sorted(set(artifact["scripts"]) | set(artifact["inventory_scripts"])):
        fingerprint("event", args.events, name)
    for name in ("crates/game/src/boss_events.rs", "crates/game/src/boss_params.rs",
                 "crates/game/src/text.rs", "crates/boss/src/data.rs",
                 "crates/boss/src/data_audit.rs", "tools/audit_boss_catalog.py"):
        fingerprint("producer", root, name)
    manifest = {"schema": 1, "cpp_commit": artifact["cpp_commit"], "inputs": inputs}
    (args.output / "inputs.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Fingerprinted {len(inputs)} actual inputs in {args.output / 'inputs.json'}")


if __name__ == "__main__":
    main()
