"""Compare production GFX recipes with the checked idapro-backed CPU parser.

Consumes local research GFX and production verifier JSON; never touches a game
process or executes ActionScript. Run the C++ verifier before this helper.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from minimap_gfx_inspect import read_gfx, worldmap_metadata


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gfx", type=Path, default=Path("build/ida/sprite-probe/run-24636-34293203/worldmap.gfx"))
    parser.add_argument("--production", type=Path, default=Path("build/native/production-recipes.json"))
    args = parser.parse_args()
    production = json.loads(args.production.read_text(encoding="utf-8"))["icons"]
    expected = worldmap_metadata(read_gfx(args.gfx))["icon_recipes"]
    layers = vectors = 0
    max_error = 0.0
    if set(production) != set(map(str, expected)):
        raise ValueError("Production frame set differs")
    for frame, recipe in expected.items():
        actual = production[str(frame)]
        if len(actual) != len(recipe):
            raise ValueError(f"Frame {frame} layer count differs")
        for a, b in zip(actual, recipe):
            if a["image"] != b.get("image", {}).get("export_name", "") or a["depth"] != b["depth_path"]:
                raise ValueError(f"Frame {frame} bitmap alias or depth differs")
            max_error = max(max_error, *(abs(x - y) for x, y in zip(a["matrix"], b["matrix"])))
            layers += 1
            if b["kind"] == "bitmap":
                if [a["width"], a["height"]] != [b["image"]["width"], b["image"]["height"]]:
                    raise ValueError(f"Frame {frame} dimensions differ")
            else:
                vectors += 1
                if len(a["commands"]) != len(b["shape"]["commands"]) or len(a["strokes"]) != len(b["shape"]["strokes"]):
                    raise ValueError(f"Frame {frame} vector counts differ")
                for x, y in zip(a["commands"], b["shape"]["commands"]):
                    if x["op"] != {"move": 0, "line": 1, "quadratic": 2}[y["op"]] or any(abs(u - v) > 0.0001 for u, v in zip(x["to"], y["to"])):
                        raise ValueError(f"Frame {frame} vector edge differs")
                for x, y in zip(a["strokes"], b["shape"]["strokes"]):
                    color = sum(component << (8 * i) for i, component in enumerate(y["rgba"]))
                    if x["color"] != color or abs(x["width"] - y["width"]) > 0.0001:
                        raise ValueError(f"Frame {frame} stroke differs")
    if max_error > 0.00001:
        raise ValueError(f"Production matrix error exceeds tolerance: {max_error}")
    print(f"PASS: {len(expected)} frames, {layers} layers, {vectors} vectors; max matrix error {max_error:.8f}.")


if __name__ == "__main__":
    main()
