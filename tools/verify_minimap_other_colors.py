"""Compare production GFX color transforms with an independent BC7 decoder.

Input sprites are captured by minimap_mod_marker_verify --other-markers.
Only local asset bytes are inspected; this does not connect to the game.
Requires texture2ddecoder, used only by this offline verification.
"""

import argparse
import math
import struct
from pathlib import Path

import texture2ddecoder

from minimap_asset_inspect import binder_metadata, checked_slice, dds_metadata, tpf_metadata
from minimap_gfx_inspect import read_gfx, worldmap_metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("gfx", type=Path)
    parser.add_argument("layouts", type=Path)
    parser.add_argument("atlases", type=Path)
    parser.add_argument("sprites", type=Path)
    args = parser.parse_args()
    layouts = binder_metadata(args.layouts.read_bytes())
    regions = {}
    for entry in layouts["entries"]:
        for sprite in entry.get("atlas", {}).get("sprites", []):
            if int(sprite["half"]) == 0:
                region = {key: int(sprite[key]) for key in ("x", "y", "width", "height")}
                region["atlas"] = entry["atlas"]["image_path"].removesuffix(".png")
                regions.setdefault(sprite["name"].removesuffix(".png"), region)
    tpf = args.atlases.read_bytes()
    atlases = {entry["name"]: entry for entry in tpf_metadata(tpf)["textures"]}
    decoded_atlases = {}
    expected = set()
    for layers in worldmap_metadata(read_gfx(args.gfx))["icon_recipes"].values():
        for layer in layers:
            multiply, add = [1.0] * 4, [0.0] * 4
            for color in layer.get("color_transforms", []):
                add = [a + m * ca for a, m, ca in zip(add, multiply, color["add"])]
                multiply = [m * cm for m, cm in zip(multiply, color["multiply"])]
            if layer["kind"] != "bitmap" or (multiply == [1.0] * 4 and add == [0.0] * 4):
                continue
            region = regions[layer["image"]["export_name"]]
            atlas = region["atlas"]
            if atlas not in decoded_atlases:
                entry = atlases[atlas]
                dds = checked_slice(tpf, entry["data_offset"], entry["data_size"])
                metadata = dds_metadata(dds)
                format_id = metadata["dx10"]["dxgi_format"] if metadata["dx10"] else {"DXT1": 71, "DXT3": 74, "DXT5": 77}[metadata["fourcc"]]
                assert format_id in (71, 77, 98), "Independent reference supports BC1, BC3 and BC7"
                decoder = {71: texture2ddecoder.decode_bc1, 77: texture2ddecoder.decode_bc3, 98: texture2ddecoder.decode_bc7}[format_id]
                decoded = decoder(dds[148 if metadata["dx10"] else 128:], metadata["width"], metadata["height"])
                decoded_atlases[atlas] = (decoded, metadata["width"])
            decoded, width = decoded_atlases[atlas]
            pixels = bytearray()
            for y in range(region["y"], region["y"] + region["height"]):
                for x in range(region["x"], region["x"] + region["width"]):
                    offset = (y * width + x) * 4
                    b, g, r, a = decoded[offset:offset + 4]  # Reference decoder emits BGRA.
                    pixels += bytes(math.floor(max(0, min(255, v * m + ca)) + 0.5) for v, m, ca in zip((r, g, b, a), multiply, add))
            expected.add((region["width"], region["height"], bytes(pixels)))
    actual = set()
    for path in args.sprites.glob("*.dds"):
        data = path.read_bytes()
        info = dds_metadata(data)
        assert info["dx10"]["dxgi_format"] == 28
        assert struct.unpack_from("<I", data, 28)[0] == 1
        actual.add((info["width"], info["height"], data[148:]))
    assert actual == expected, f"Production colors differ from independent decoding: actual={len(actual)}, expected={len(expected)}, unmatched={len(actual ^ expected)}"
    print(f"PASS: all {len(actual)} mod color transforms match every reference pixel, including positive/negative offsets, clamping and alpha")


if __name__ == "__main__":
    main()
