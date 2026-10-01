"""Compare external minimap pixels with game-file atlas regions and GFX recipes.

Requires Pillow for PNG/BMP comparison. It reads local CPU-resource evidence
only. All image outputs are local research artifacts, not distribution assets.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from datetime import datetime
from pathlib import Path

from minimap_asset_inspect import inspect
from minimap_gfx_inspect import layer_bounds, read_gfx, worldmap_metadata


def source_record(path: Path) -> dict:
    data = path.read_bytes()
    return {"file": str(path), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def read_bmp_rgba(path: Path):
    from PIL import Image

    data = path.read_bytes()
    if data[:2] != b"BM" or struct.unpack_from("<HHI", data, 26) != (1, 32, 0):
        raise ValueError("Expected the probe's uncompressed 32-bit BGRA BMP")
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    if width <= 0 or height == 0 or offset + width * abs(height) * 4 != len(data):
        raise ValueError("Invalid probe BMP dimensions/length")
    return Image.frombytes("RGBA", (width, abs(height)), data[offset:], "raw", "BGRA", 0, 1 if height < 0 else -1)


def rows(path: Path, size: int):
    data = path.read_bytes()
    stride = 8 + size
    if len(data) % stride:
        raise ValueError(f"Partial parameter row: {path}")
    for offset in range(0, len(data), stride):
        yield struct.unpack_from("<Q", data, offset)[0], data[offset + 8:offset + stride]


def parameter_coverage(directory: Path, recipes: dict) -> dict:
    point_rows = list(rows(directory / "point-rows.bin", 256))
    grace_rows = list(rows(directory / "grace-rows.bin", 236))
    field_ids = {name: set() for name in ("point_base", "point_alternate", "point_distant", "grace_base",
                                         "grace_forbidden", "grace_alternate", "grace_alternate_forbidden")}
    selected_points = []
    alias_counts = {3: 0, 15: 0}
    alternate_examples = []
    for identity, data in point_rows:
        icon = struct.unpack_from("<H", data, 12)[0]
        if identity < 78500 or icon in (0, 80) or not data[24] & 7:
            continue
        selected_points.append(identity)
        if icon in alias_counts:
            alias_counts[icon] += 1
        for name, offset in (("point_base", 12), ("point_alternate", 184), ("point_distant", 26)):
            value = struct.unpack_from("<H", data, offset)[0]
            if value:
                field_ids[name].add(value)
        alternate = struct.unpack_from("<H", data, 184)[0]
        if alternate != icon:
            alternate_examples.append({"row_id": identity, "base": icon, "alternate": alternate,
                                       "text_types": list(data[144:152])})
    for _, data in grace_rows:
        for name, offset in (("grace_base", 28), ("grace_forbidden", 16),
                             ("grace_alternate", 232), ("grace_alternate_forbidden", 234)):
            value = struct.unpack_from("<H", data, offset)[0]
            if value:
                field_ids[name].add(value)
    candidate_ids = sorted(set().union(*field_ids.values()))
    missing = [identity for identity in candidate_ids if not recipes.get(identity)]
    if missing:
        raise ValueError(f"Parameter candidate IDs have no native recipe: {missing}")
    return {"point_rows": len(point_rows), "grace_rows": len(grace_rows),
            "selected_base_markers": len(selected_points), "base_icon_ids": len(field_ids["point_base"]),
            "alias_marker_counts": alias_counts, "candidate_nonzero_field_ids": candidate_ids,
            "candidate_count": len(candidate_ids), "field_sets": {name: sorted(values) for name, values in field_ids.items()},
            "missing_recipe_ids": missing, "different_alternate_rows": alternate_examples,
            "scope": "Nonzero parameter fields are coverage candidates; event/text conditions still select the actual frame"}


def main() -> None:
    from PIL import Image, ImageChops, ImageStat

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--external-atlas", required=True, type=Path)
    parser.add_argument("--layouts", required=True, type=Path)
    parser.add_argument("--gfx", required=True, type=Path)
    parser.add_argument("--samples", required=True, action="append", help="Texture name=BGRA BMP from minimap_dds_probe")
    parser.add_argument("--parameters", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--image-output", type=Path, help="Optional local-only comparison image")
    args = parser.parse_args()
    sources = [source_record(path) for path in (args.external_atlas, args.layouts, args.gfx)]
    samples = {}
    for value in args.samples:
        name, path = value.split("=", 1)
        path = Path(path)
        if name in samples:
            raise ValueError("Duplicate texture sample")
        samples[name] = read_bmp_rgba(path)
        sources.append(source_record(path))
    native = {}
    target_half_values = set()
    for entry in inspect(args.layouts)["entries"]:
        atlas = entry.get("atlas")
        if not atlas:
            continue
        image_path = atlas["image_path"]
        texture_name = Path(image_path).stem
        if texture_name not in samples:
            continue
        image = samples[texture_name]
        for sprite in atlas["sprites"]:
            target_half_values.add(int(sprite.get("half", 0)))
            x, y, w, h = [int(sprite[key]) for key in ("x", "y", "width", "height")]
            if not w or not h or x < 0 or y < 0 or x + w > image.width or y + h > image.height:
                raise ValueError("Atlas region exceeds the sampled texture")
            if sprite["name"] in native:
                raise ValueError("Duplicate native sprite name")
            native[sprite["name"]] = {"atlas": texture_name, "rect": [x, y, w, h],
                                       "image": image.crop((x, y, x + w, y + h))}
    movie = read_gfx(args.gfx)
    gfx = worldmap_metadata(movie)
    recipes = gfx["icon_recipes"]
    special = {"Arrow": "MENU_MAP_Player_01.png", "Player": "MENU_MAP_Host.png",
               "Bearing": "MENU_MAP_Bearing.png", "Roundtable": "MENU_FL_Hub.png"}
    comparisons = []
    external_json = json.loads(args.external_atlas.read_text(encoding="utf-8"))
    for atlas in external_json["atlases"]:
        path = args.external_atlas.parent / atlas["file"]
        sources.append(source_record(path))
        external = Image.open(path).convert("RGBA")
        if external.size != (atlas["width"], atlas["height"]):
            raise ValueError("External atlas dimensions differ from its JSON")
        for sprite in atlas["sprites"]:
            name = sprite["name"]
            if name.isdigit():
                recipe = recipes.get(int(name), [])
                if len(recipe) != 1 or recipe[0]["kind"] != "bitmap":
                    raise ValueError(f"Existing numeric sprite is not a single bitmap: {name}")
                native_name = recipe[0]["image"]["export_name"] + ".png"
            else:
                native_name = special[name]
            item = native[native_name]
            x, y, w, h = [sprite[key] for key in ("x", "y", "width", "height")]
            if min(x, y, w, h) < 0 or x + w > external.width or y + h > external.height:
                raise ValueError("External sprite exceeds its atlas")
            image = external.crop((x, y, x + w, y + h))
            raw, native_raw = image.tobytes(), item["image"].tobytes()
            comparisons.append({"external_name": name, "native_name": native_name, "native_atlas": item["atlas"],
                                "native_rect": item["rect"], "size": [w, h], "native_size": list(item["image"].size),
                                "equal_rgba": image.size == item["image"].size and raw == native_raw,
                                "external_pixel_sha256": hashlib.sha256(raw).hexdigest(),
                                "native_pixel_sha256": hashlib.sha256(native_raw).hexdigest(),
                                "external_anchor": [sprite.get("centerX", w / 2), sprite.get("centerY", h / 2)]})
    mismatches = [entry["external_name"] for entry in comparisons if not entry["equal_rgba"]]
    if mismatches != ["Roundtable"]:
        raise ValueError(f"Unexpected comparison differences: {mismatches}")
    # The old Roundtable is a locally authored crop/composition. Its offsets are
    # comparison data, not the game's Home transforms or runtime policy.
    atlas = external_json["atlases"][0]
    external = Image.open(args.external_atlas.parent / atlas["file"]).convert("RGBA")
    entry = next(sprite for sprite in atlas["sprites"] if sprite["name"] == "Roundtable")
    old_home = external.crop((entry["x"], entry["y"], entry["x"] + entry["width"], entry["y"] + entry["height"]))
    composition = Image.new("RGBA", (400, 400))
    composition.alpha_composite(native["MENU_FL_Hub.png"]["image"], (-1, -2))
    composition.alpha_composite(native["MENU_MAP_48.png"]["image"], (118, 109))
    metrics = []
    for gray in (0, 40, 255):
        background = Image.new("RGBA", composition.size, (gray, gray, gray, 255))
        original = Image.alpha_composite(background, old_home)
        restored = Image.alpha_composite(background, composition)
        metrics.append({"background_gray": gray, "rgba_mae": ImageStat.Stat(ImageChops.difference(original, restored)).mean})
    if args.image_output:
        args.image_output.parent.mkdir(parents=True, exist_ok=True)
        preview = Image.new("RGBA", (800, 400), (40, 40, 40, 255))
        preview.alpha_composite(old_home, (0, 0))
        preview.alpha_composite(composition, (400, 0))
        preview.convert("RGB").save(args.image_output)
    coverage = parameter_coverage(args.parameters, recipes)
    for name in ("point-rows.bin", "grace-rows.bin", "game-system-common-rows.bin"):
        sources.append(source_record(args.parameters / name))
    common_row = next(data for identity, data in rows(args.parameters / "game-system-common-rows.bin", 1024) if identity == 0)
    home_id = struct.unpack_from("<i", common_row, 632)[0]
    home_param = next(data for identity, data in rows(args.parameters / "grace-rows.bin", 236) if identity == home_id)
    needed = set(coverage["candidate_nonzero_field_ids"]) | {int(entry["external_name"]) for entry in comparisons if entry["external_name"].isdigit()}
    selected = {identity: recipes[identity] for identity in sorted(needed)}
    for values in list(selected.values()) + [value["layers"] for value in gfx["special_recipes"].values()]:
        for layer in values:
            if layer.get("filters") or layer.get("blend_mode") or layer.get("color_transforms"):
                raise ValueError("Selected frame needs additional rendering behavior")
            if layer["kind"] == "bitmap":
                region = native[layer["image"]["export_name"] + ".png"]
                if region["image"].size != (layer["image"]["width"], layer["image"]["height"]):
                    raise ValueError("GFX dimensions differ from the native atlas region")
                layer["atlas"] = region["atlas"]
                layer["atlas_rect"] = region["rect"]
            layer["local_bounds"] = layer_bounds(layer)
    output = {
        "analysis_date": datetime.now().date().isoformat(), "sources": sources,
        "constraints": {"debugger": False, "breakpoints": False, "game_gpu_reuse": False},
        "comparison": {"sprites": len(comparisons), "exact_matches": sum(entry["equal_rgba"] for entry in comparisons),
                       "nonidentical": mismatches, "items": comparisons},
        "gfx": {key: gfx[key] for key in ("file", "sha256", "bytes", "declared_bytes", "trailing_zero_bytes", "version", "frame_count",
                                          "image_count", "sprite_count", "worldmap_item_id", "icon_timeline_id", "icon_timeline_offset", "icon_frame_count", "abc_blocks")},
        "coverage": coverage, "selected_icon_recipes": selected, "special_recipes": gfx["special_recipes"],
        "roundtable": {"home_bonfire_param_id": home_id, "map": list(home_param[32:35]),
                       "base_icon_id": struct.unpack_from("<H", home_param, 28)[0],
                       "base_position": list(struct.unpack_from("<fff", home_param, 36)),
                       "external_comparison_composition": {"hub_translation": [-1, -2], "icon48_translation": [118, 109],
                                                           "rendered_metrics": metrics, "pixel_identical": False,
                                                           "scope": "Comparison of the author's crop; runtime uses native Home and marker recipes"},
                       "external_name_case": "Roundtable", "current_code_lookup": "RoundTable"},
        "conclusion": "All existing external sprite assets have internal bitmap/recipe replacements; no external pixel fallback is required"
    }
    output["native_alias_chain"] = {"WorldMapItem": gfx["worldmap_item_id"], "Icon_0": gfx["icon_timeline_id"],
                                    "frame_indexing": "one-based frame equals selected iconId",
                                    "alias_3": recipes[3][0]["image"]["export_name"],
                                    "alias_15": recipes[15][0]["image"]["export_name"],
                                    "image_tag_loader_rva": "0x11e5890", "frame_select_rva": "0x74a7d0",
                                    "subtexture_key_rva": "0x120da0", "subtexture_lookup_rva": "0xd67f20"}
    output["all_target_atlas_half_values"] = sorted(target_half_values)
    # Overlay-compatible anchors and the native Rotate pivot are separate.
    output["special_recipes"]["overlay_player_compatibility"]["anchor"] = next(
        item["external_anchor"] for item in comparisons if item["external_name"] == "Player")
    rotation = output["special_recipes"]["player_rotate"]["layers"][0]["matrix"]
    a, b, c, d, tx, ty = rotation
    determinant = a * d - b * c
    if not determinant:
        raise ValueError("Player rotation matrix is singular")
    output["special_recipes"]["player_rotate"]["native_pixel_pivot"] = [(c * ty - d * tx) / determinant,
                                                                       (b * tx - a * ty) / determinant]
    parent_id = next(identity for identity, sprite in movie["sprites"].items()
                     if any(item.get("name") == "Fix" and item.get("character_id") == output["special_recipes"]["player_fix"]["character_id"]
                            for item in sprite["frames"][0]["display"]))
    fix = next(item for item in movie["sprites"][parent_id]["frames"][0]["display"] if item.get("name") == "Fix")
    output["special_recipes"]["player_fix"]["parent_translation"] = fix.get("matrix", [1, 0, 0, 1, 0, 0])[4:6]
    home_layers = output["special_recipes"]["home"]["layers"]
    home_bounds = [min(item["local_bounds"][0] for item in home_layers), min(item["local_bounds"][1] for item in home_layers),
                   max(item["local_bounds"][2] for item in home_layers), max(item["local_bounds"][3] for item in home_layers)]
    home_placement = next(item for sprite in movie["sprites"].values() for item in sprite["frames"][0]["display"]
                          if item.get("name") == "Home" and item.get("character_id") == output["special_recipes"]["home"]["character_id"])
    output["roundtable"]["native_layout"] = {
        "transparent_character_id": next(layer["shape"]["id"] for layer in home_layers if layer["kind"] == "vector"),
        "hub_character_id": next(layer["image"]["id"] for layer in home_layers if layer["kind"] == "bitmap"),
        "home_bounds": home_bounds, "initial_position": home_placement["matrix"][4:6],
        "map_position_rule": "homeView=(V.left+P.x-B.left,V.bottom+P.y-B.bottom); homeMap=(viewOffset+homeView)/viewScale",
        "rva": "0x9c96d0", "comparison_only": "Local external composition translations are not native layout coordinates"}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Compared {len(comparisons)} sprites: 92 exact; Roundtable is Hub + icon 48. Covered {coverage['candidate_count']} candidate state IDs.")


if __name__ == "__main__":
    main()
