"""Recover bitmap/vector recipes from CPU bytes of the game's world-map GFX.

The executable is analyzed with idapro separately. This helper only reads files
already returned by the game's file layer; it never attaches to the game.
The observed GFX tag layouts are checked against the native loader exports.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import struct
from pathlib import Path

from minimap_asset_inspect import checked_slice


IDENTITY = [1.0, 0.0, 0.0, 1.0, 0.0, 0.0]


class Reader:
    def __init__(self, data: bytes, position: int = 0):
        self.data = data
        self.position = position

    def take(self, size: int) -> bytes:
        value = checked_slice(self.data, self.position, size)
        self.position += size
        return value

    def unpack(self, format: str):
        return struct.unpack(format, self.take(struct.calcsize(format)))

    def u8(self) -> int:
        return self.take(1)[0]

    def u16(self) -> int:
        return self.unpack("<H")[0]

    def u32(self) -> int:
        return self.unpack("<I")[0]

    def variable(self) -> int:
        result = 0
        for index in range(5):
            value = self.u8()
            result |= (value & 127) << (7 * index)
            if not value & 128:
                return result
        raise ValueError("Invalid five-byte variable integer")

    def textz(self) -> str:
        end = self.data.find(b"\0", self.position)
        if end < 0:
            raise ValueError("Unterminated GFX string")
        return self.take(end - self.position + 1)[:-1].decode("utf-8")

    def text8(self) -> str:
        return self.take(self.u8()).rstrip(b"\0").decode("utf-8")

    def finish(self) -> None:
        if self.position != len(self.data):
            raise ValueError(f"Unconsumed record bytes: {self.position}/{len(self.data)}")


class Bits:
    def __init__(self, data: bytes, position: int = 0):
        self.data = data
        self.bit = position * 8

    def get(self, count: int, signed: bool = False) -> int:
        if count < 0 or self.bit < 0 or self.bit + count > len(self.data) * 8:
            raise ValueError("Truncated GFX bit record")
        value = 0
        for _ in range(count):
            value = value * 2 + ((self.data[self.bit // 8] >> (7 - self.bit % 8)) & 1)
            self.bit += 1
        return value - (1 << count) if signed and count and value & (1 << (count - 1)) else value

    def align(self) -> int:
        self.bit = (self.bit + 7) // 8 * 8
        return self.bit // 8


def rectangle(data: bytes, position: int) -> tuple[list[float], int]:
    bits = Bits(data, position)
    count = bits.get(5)
    # SWF rectangle order is Xmin, Xmax, Ymin, Ymax.
    x0, x1, y0, y1 = [bits.get(count, True) / 20 for _ in range(4)]
    return [x0, y0, x1, y1], bits.align()


def matrix(data: bytes, position: int) -> tuple[list[float], int]:
    bits = Bits(data, position)
    a = d = 1.0
    b = c = 0.0
    if bits.get(1):
        count = bits.get(5)
        a, d = [bits.get(count, True) / 65536 for _ in range(2)]
    if bits.get(1):
        count = bits.get(5)
        # Native RVA 0x11BDBC0 stores the first skew as m10, the second as m01.
        b, c = [bits.get(count, True) / 65536 for _ in range(2)]
    count = bits.get(5)
    x, y = [bits.get(count, True) / 20 for _ in range(2)]
    return [a, b, c, d, x, y], bits.align()


def multiply(outer: list[float], inner: list[float]) -> list[float]:
    a, b, c, d, x, y = outer
    e, f, g, h, u, v = inner
    return [a * e + c * f, b * e + d * f, a * g + c * h, b * g + d * h,
            a * u + c * v + x, b * u + d * v + y]


def transform(matrix: list[float], x: float, y: float) -> list[float]:
    a, b, c, d, u, v = matrix
    return [a * x + c * y + u, b * x + d * y + v]


def color_transform(data: bytes, position: int) -> tuple[dict, int]:
    bits = Bits(data, position)
    add, multiply = bits.get(1), bits.get(1)
    count = bits.get(4)
    mult = [bits.get(count, True) / 256 for _ in range(4)] if multiply else [1.0] * 4
    offsets = [bits.get(count, True) for _ in range(4)] if add else [0] * 4
    return {"multiply": mult, "add": offsets}, bits.align()


def filters(reader: Reader) -> list[dict]:
    result = []
    for _ in range(reader.u8()):
        kind = reader.u8()
        start = reader.position
        if kind in (0, 1, 2, 3, 6):
            reader.take({0: 23, 1: 9, 2: 15, 3: 27, 6: 80}[kind])
        elif kind in (4, 7):
            count = reader.u8()
            reader.take(count * 5 + 19)
        elif kind == 5:
            columns, rows = reader.u8(), reader.u8()
            reader.take(13 + columns * rows * 4)
        else:
            raise ValueError(f"Unsupported GFX filter {kind}")
        result.append({"kind": kind, "bytes": reader.data[start:reader.position].hex()})
    return result


def placement(data: bytes, code: int) -> dict:
    reader = Reader(data)
    flags = reader.u8()
    extra = reader.u8() if code == 70 else 0
    if extra & 0xC0:
        raise ValueError("Unobserved PlaceObject3 flags")
    result = {"depth": reader.u16(), "move": bool(flags & 1)}
    # Current native RVA 0x11BF220 only consumes a class name for extra bit 0x08.
    # Bit 0x10 in this version's bitmap placements has no additional string.
    if extra & 8:
        result["class_name"] = reader.textz()
    if flags & 2:
        result["character_id"] = reader.u16()
    if flags & 4:
        result["matrix"], reader.position = matrix(data, reader.position)
    if flags & 8:
        result["color"], reader.position = color_transform(data, reader.position)
    if flags & 16:
        result["ratio"] = reader.u16()
    if flags & 32:
        result["name"] = reader.textz()
    if flags & 64:
        result["clip_depth"] = reader.u16()
    if extra & 1:
        result["filters"] = filters(reader)
    if extra & 2:
        result["blend_mode"] = reader.u8()
    if extra & 4:
        result["cache_as_bitmap"] = reader.u8()
    if extra & 32:
        result["visible"] = reader.u8()
    if flags & 128:
        # Clip actions are not executed by this resource-inspection helper.
        result["clip_action_bytes"] = reader.take(len(data) - reader.position).hex()
    reader.finish()
    return result


def tags(data: bytes, start: int = 0, end: int | None = None):
    limit = len(data) if end is None else end
    if not 0 <= start <= limit <= len(data):
        raise ValueError("Invalid GFX container bounds")
    reader = Reader(data[:limit], start)
    while reader.position < limit:
        offset = reader.position
        header = reader.u16()
        code, size = header >> 6, header & 63
        if size == 63:
            size = reader.u32()
        if size > limit - reader.position:
            raise ValueError(f"GFX tag {code} crosses its container boundary")
        body_offset = reader.position
        yield code, offset, body_offset, reader.take(size)
        if code == 0:
            if size or reader.position != limit:
                raise ValueError("GFX End tag is not the end of its declared container")
            return
    raise ValueError("GFX container has no End tag")


def timeline(records: list, expected_frames: int, base_offset: int = 0) -> list[dict]:
    display: dict[int, dict] = {}
    frames, events, labels = [], [], []
    for code, offset, _, body in records:
        if code in (26, 70):
            item = placement(body, code)
            item["tag_offset"] = hex(base_offset + offset)
            depth = item["depth"]
            if item["move"] and depth not in display:
                raise ValueError("Placement modifies a nonexistent depth")
            if not item["move"] and "character_id" not in item:
                raise ValueError("New placement has no character")
            old = display[depth] if item["move"] else {}
            display[depth] = {**old, **item}
            events.append(item)
        elif code == 28:
            reader = Reader(body)
            depth = reader.u16()
            reader.finish()
            display.pop(depth, None)
            events.append({"remove_depth": depth})
        elif code == 43:
            reader = Reader(body)
            labels.append(reader.textz())
            if reader.position < len(body):
                reader.u8()  # Optional named-anchor flag.
            reader.finish()
        elif code == 1:
            if body:
                raise ValueError("ShowFrame payload is not empty")
            frames.append({"frame": len(frames) + 1, "labels": labels, "events": events,
                           "display": [copy.deepcopy(display[depth]) for depth in sorted(display)]})
            events, labels = [], []
        elif code in (4, 5):
            raise ValueError("Unobserved legacy placement/removal tag")
    if len(frames) != expected_frames:
        raise ValueError(f"GFX frame count differs: {len(frames)}/{expected_frames}")
    return frames


def abc_frame_metadata(data: bytes) -> dict:
    """Inspect AS3 class/frame-script records without executing any bytecode."""
    reader = Reader(data)
    minor, major = reader.u16(), reader.u16()
    for _ in range(max(0, reader.variable() - 1)):
        reader.variable()  # Signed constant pool; no value is used here.
    for _ in range(max(0, reader.variable() - 1)):
        reader.variable()
    for _ in range(max(0, reader.variable() - 1)):
        reader.take(8)
    strings = [""] + [reader.take(reader.variable()).decode("utf-8") for _ in range(max(0, reader.variable() - 1))]
    namespaces = [None] + [{"kind": reader.u8(), "name": reader.variable()} for _ in range(max(0, reader.variable() - 1))]
    namespace_sets = [None] + [[reader.variable() for _ in range(reader.variable())] for _ in range(max(0, reader.variable() - 1))]
    names = [None]
    for _ in range(max(0, reader.variable() - 1)):
        kind = reader.u8()
        name = {"kind": kind}
        if kind in (7, 13):
            name.update(namespace=reader.variable(), name=reader.variable())
        elif kind in (15, 16):
            name["name"] = reader.variable()
        elif kind in (9, 14):
            name.update(name=reader.variable(), namespace_set=reader.variable())
        elif kind in (27, 28):
            name["namespace_set"] = reader.variable()
        elif kind == 29:
            name.update(type=reader.variable(), parameters=[reader.variable() for _ in range(reader.variable())])
        elif kind not in (17, 18):
            raise ValueError(f"Unobserved ABC multiname kind {kind}")
        names.append(name)

    def named(index: int) -> str:
        if not index:
            return "*"
        value = names[index]
        text = strings[value["name"]] if "name" in value else str(value)
        if value.get("namespace"):
            return strings[namespaces[value["namespace"]]["name"]] + ":" + text
        return text

    method_count = reader.variable()
    for _ in range(method_count):
        count = reader.variable()
        reader.variable()  # Return type.
        for _ in range(count):
            reader.variable()
        reader.variable()  # Name.
        flags = reader.u8()
        if flags & 8:
            for _ in range(reader.variable()):
                reader.variable()
                reader.u8()
        if flags & 128:
            for _ in range(count):
                reader.variable()
    for _ in range(reader.variable()):
        reader.variable()  # Metadata name.
        count = reader.variable()
        for _ in range(count * 2):
            reader.variable()

    def traits() -> list[dict]:
        values = []
        for _ in range(reader.variable()):
            name, flags = reader.variable(), reader.u8()
            kind = flags & 15
            value = {"name": named(name), "kind": kind}
            if kind in (0, 6):
                reader.variable()
                reader.variable()
                if reader.variable():
                    reader.u8()
            elif kind in (1, 2, 3, 5):
                reader.variable()
                value["method"] = reader.variable()
            elif kind == 4:
                reader.variable()
                reader.variable()
            else:
                raise ValueError("Unobserved ABC trait kind")
            if flags & 64:
                for _ in range(reader.variable()):
                    reader.variable()
            values.append(value)
        return values

    count = reader.variable()
    classes = []
    for _ in range(count):
        name, parent, flags = reader.variable(), reader.variable(), reader.u8()
        if flags & 8:
            reader.variable()
        for _ in range(reader.variable()):
            reader.variable()
        initializer = reader.variable()
        classes.append({"name": named(name), "parent": named(parent), "initializer": initializer, "traits": traits()})
    for value in classes:
        value["class_initializer"] = reader.variable()
        value["class_traits"] = traits()
    for _ in range(reader.variable()):
        reader.variable()
        traits()
    bodies = {}
    for _ in range(reader.variable()):
        identity = reader.variable()
        for _ in range(4):
            reader.variable()  # Stack/local/scope bounds.
        code = reader.take(reader.variable())
        for _ in range(reader.variable()):
            for _ in range(5):
                reader.variable()
        traits()
        bodies[identity] = code
    reader.finish()
    relevant = []
    for value in classes:
        if value["name"] not in ("_02_120_WorldMap_fla:Timeline_23", "_02_120_WorldMap_fla:PC__48", "_02_120_WorldMap_fla:PC__49"):
            continue
        frame_scripts = []
        for item in value["traits"]:
            if ":frame" not in item["name"] or "method" not in item:
                continue
            code = bodies[item["method"]]
            # getlocal_0; pushscope; findpropstrict <stop>; callpropvoid <stop>,0; returnvoid.
            current = Reader(code)
            prefix = current.take(2)
            find = current.u8()
            find_name = named(current.variable())
            call = current.u8()
            call_name = named(current.variable())
            arguments, end = current.variable(), current.u8()
            current.finish()
            stop_only = prefix == b"\xD0\x30" and find == 0x5D and call == 0x4F and arguments == 0 and end == 0x47 and find_name == call_name == ":stop"
            frame_scripts.append({"name": item["name"], "method": item["method"], "code_hex": code.hex(), "stop_only": stop_only})
        relevant.append({"class": value["name"], "initializer_code_hex": bodies[value["initializer"]].hex(), "frame_scripts": frame_scripts})
    return {"version": [minor, major], "method_count": method_count, "class_count": len(classes),
            "namespace_set_count": len(namespace_sets) - 1, "relevant_frame_scripts": relevant}


def read_gfx(path: Path) -> dict:
    data = path.read_bytes()
    reader = Reader(data)
    signature, version = reader.take(3), reader.u8()
    declared = reader.u32()
    if signature != b"GFX" or version != 11 or not 8 <= declared <= len(data):
        raise ValueError("Expected the observed uncompressed GFX version 11")
    if any(data[declared:]):
        raise ValueError("Nonzero data after the declared GFX length")
    reader = Reader(data[:declared], reader.position)
    bounds, reader.position = rectangle(reader.data, reader.position)
    rate, frame_count = reader.u16() / 256, reader.u16()
    records = list(tags(reader.data, reader.position, declared))
    images, sprites, shapes, symbols, abc_blocks = {}, {}, {}, {}, []
    for code, offset, body_offset, body in records:
        current = Reader(body)
        if code == 1009:
            identity, format = current.u32(), current.u16()
            width, height = current.u16(), current.u16()
            export_name, file_name = current.text8(), current.text8()
            current.finish()
            images[identity] = {"id": identity, "offset": hex(offset), "format": format,
                                "width": width, "height": height, "export_name": export_name,
                                "file_name": file_name}
        elif code == 39:
            identity, count = current.u16(), current.u16()
            nested = list(tags(body, current.position))
            sprites[identity] = {"id": identity, "offset": hex(offset), "frame_count": count,
                                 "frames": timeline(nested, count, body_offset)}
        elif code in (2, 22, 32, 83):
            identity = current.u16()
            shapes[identity] = {"id": identity, "offset": hex(offset), "code": code,
                                "body_hex": body.hex()}
        elif code == 76:
            for _ in range(current.u16()):
                identity, name = current.u16(), current.textz()
                if identity in symbols:
                    raise ValueError("Duplicate GFX symbol")
                symbols[identity] = name
            current.finish()
        elif code == 82:
            current.u32()
            current.textz()
            abc_blocks.append({"offset": hex(offset), "bytes": len(body),
                               "sha256": hashlib.sha256(body).hexdigest(),
                               "metadata": abc_frame_metadata(body[current.position:])})
    for identity, sprite in sprites.items():
        sprite["symbol"] = symbols.get(identity)
    return {"file": str(path), "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data),
            "declared_bytes": declared, "trailing_zero_bytes": len(data) - declared,
            "version": version, "bounds": bounds, "frame_rate": rate, "frame_count": frame_count,
            "images": images, "sprites": sprites, "shapes": shapes, "symbols": symbols,
            "root_frames": timeline(records, frame_count), "abc_blocks": abc_blocks}


def solid_shape(record: dict) -> dict:
    """Decode the observed solid fills, solid strokes, and straight/curve edges."""
    data = bytes.fromhex(record["body_hex"])
    reader = Reader(data, 2)
    bounds, reader.position = rectangle(data, reader.position)
    if record["code"] == 83:
        _, reader.position = rectangle(data, reader.position)
        reader.u8()
    if record["code"] not in (32, 83):
        raise ValueError("Only observed Shape3/Shape4 vectors are supported")
    count = reader.u8()
    if count == 255:
        count = reader.u16()
    fills = []
    for _ in range(count):
        if reader.u8() != 0:
            raise ValueError("Recipe contains an unsupported non-solid fill")
        fills.append(list(reader.take(4)))
    count = reader.u8()
    if count == 255:
        count = reader.u16()
    strokes = []
    for _ in range(count):
        width = reader.u16() / 20
        flags = 0
        if record["code"] == 83:
            bits = Bits(data, reader.position)
            flags = bits.get(16)
            reader.position = bits.align()
            if flags >> 12 & 3 == 2:
                reader.u16()  # Miter-limit fixed8.
            if flags & 0x800:
                raise ValueError("Recipe contains a filled stroke")
        strokes.append({"width": width, "rgba": list(reader.take(4)), "flags": flags})
    bits = Bits(data, reader.position)
    fill_bits, line_bits = bits.get(4), bits.get(4)
    x = y = fill0 = fill1 = stroke = 0
    commands = []
    while True:
        if bits.get(1):
            straight, count = bits.get(1), bits.get(4) + 2
            if straight:
                dx = dy = 0
                if bits.get(1):
                    dx, dy = bits.get(count, True), bits.get(count, True)
                elif bits.get(1):
                    dy = bits.get(count, True)
                else:
                    dx = bits.get(count, True)
                x, y = x + dx, y + dy
                command = {"op": "line", "to": [x / 20, y / 20]}
            else:
                cx, cy = x + bits.get(count, True), y + bits.get(count, True)
                x, y = cx + bits.get(count, True), cy + bits.get(count, True)
                command = {"op": "quadratic", "control": [cx / 20, cy / 20], "to": [x / 20, y / 20]}
            commands.append({**command, "fill0": fill0, "fill1": fill1, "stroke": stroke})
        else:
            flags = bits.get(5)
            if not flags:
                break
            if flags & 1:
                count = bits.get(5)
                x, y = bits.get(count, True), bits.get(count, True)
                commands.append({"op": "move", "to": [x / 20, y / 20]})
            if flags & 2:
                fill0 = bits.get(fill_bits)
            if flags & 4:
                fill1 = bits.get(fill_bits)
            if flags & 8:
                stroke = bits.get(line_bits)
            if flags & 16:
                raise ValueError("Recipe contains unobserved appended vector styles")
            if fill0 > len(fills) or fill1 > len(fills) or stroke > len(strokes):
                raise ValueError("Vector style index is out of range")
    if bits.align() != len(data):
        raise ValueError("Unconsumed vector bytes")
    return {"bounds": bounds, "fills": fills, "strokes": strokes, "commands": commands,
            "fully_transparent": all(value[3] == 0 for value in fills) and not strokes}


def named_path(movie: dict, path: str) -> int:
    display = movie["root_frames"][0]["display"]
    identity = None
    for segment in path.split("/"):
        matches = [entry for entry in display if entry.get("name") == segment]
        if len(matches) != 1:
            raise ValueError(f"GFX path segment is missing or ambiguous: {path}/{segment}")
        identity = matches[0]["character_id"]
        if identity in movie["sprites"]:
            display = movie["sprites"][identity]["frames"][0]["display"]
        else:
            display = []
    if identity is None:
        raise ValueError("Empty GFX path")
    return identity


def recipe(movie: dict, identity: int, frame: int = 1, parent: list[float] | None = None,
           depth_path: tuple[int, ...] = (), ancestors: tuple[int, ...] = ()) -> list[dict]:
    matrix_value = IDENTITY if parent is None else parent
    if identity in ancestors or len(ancestors) > 32:
        raise ValueError("Cyclic or excessively deep GFX display tree")
    if identity in movie["images"]:
        return [{"kind": "bitmap", "image": movie["images"][identity], "matrix": matrix_value,
                 "depth_path": list(depth_path)}]
    if identity in movie["shapes"]:
        return [{"kind": "vector", "shape": {**movie["shapes"][identity],
                                                 **solid_shape(movie["shapes"][identity])},
                 "matrix": matrix_value, "depth_path": list(depth_path)}]
    if identity not in movie["sprites"]:
        raise ValueError(f"Unresolved GFX character {identity}")
    frames = movie["sprites"][identity]["frames"]
    if not 1 <= frame <= len(frames):
        raise ValueError(f"GFX frame out of range: {identity}/{frame}")
    layers = []
    for entry in frames[frame - 1]["display"]:
        if entry.get("visible", 1) == 0:
            continue
        if "clip_depth" in entry or "clip_action_bytes" in entry:
            raise ValueError("Recipe requires runtime clipping or actions")
        current = multiply(matrix_value, entry.get("matrix", IDENTITY))
        nested = recipe(movie, entry["character_id"], 1, current,
                        depth_path + (entry["depth"],), ancestors + (identity,))
        for layer in nested:
            layer.setdefault("source_tags", []).insert(0, entry["tag_offset"])
            if "color" in entry:
                layer.setdefault("color_transforms", []).insert(0, entry["color"])
            if entry.get("filters"):
                layer.setdefault("filters", []).extend(entry["filters"])
            if entry.get("blend_mode", 1) != 1:
                layer["blend_mode"] = entry["blend_mode"]
        layers.extend(nested)
    return layers


def layer_bounds(layer: dict) -> list[float]:
    if layer["kind"] == "bitmap":
        bounds = [0, 0, layer["image"]["width"], layer["image"]["height"]]
    else:
        bounds = layer["shape"]["bounds"]
    x0, y0, x1, y1 = bounds
    points = [transform(layer["matrix"], x, y) for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1))]
    return [min(point[0] for point in points), min(point[1] for point in points),
            max(point[0] for point in points), max(point[1] for point in points)]


def worldmap_metadata(movie: dict) -> dict:
    matches = [identity for identity, value in movie["symbols"].items() if value == "WorldMapItem"]
    if len(matches) != 1:
        raise ValueError("WorldMapItem symbol is missing or ambiguous")
    item_id = matches[0]
    icons = [entry for entry in movie["sprites"][item_id]["frames"][0]["display"] if entry.get("name") == "Icon_0"]
    if len(icons) != 1:
        raise ValueError("WorldMapItem/Icon_0 is missing")
    icon_id = icons[0]["character_id"]
    icon_frames = movie["sprites"][icon_id]["frames"]
    recipes = {frame["frame"]: recipe(movie, icon_id, frame["frame"]) for frame in icon_frames}
    paths = {"home": "Body/_/Base/Home", "player_rotate": "Body/_/Base/Player/Rotate",
             "player_fix": "Body/_/Base/Player/Fix"}
    special = {name: {"path": path, "character_id": named_path(movie, path),
                      "layers": recipe(movie, named_path(movie, path))} for name, path in paths.items()}
    host_ids = [identity for identity, value in movie["images"].items() if value["export_name"] == "MENU_MAP_Host"]
    if len(host_ids) != 1:
        raise ValueError("Host bitmap is missing")
    special["overlay_player_compatibility"] = {"layers": recipe(movie, host_ids[0]),
                                               "reason": "The existing external Player bitmap is the native Host bitmap"}
    return {"file": movie["file"], "sha256": movie["sha256"], "bytes": movie["bytes"],
            "declared_bytes": movie["declared_bytes"], "trailing_zero_bytes": movie["trailing_zero_bytes"],
            "version": movie["version"], "frame_count": movie["frame_count"],
            "image_count": len(movie["images"]), "sprite_count": len(movie["sprites"]),
            "worldmap_item_id": item_id, "icon_timeline_id": icon_id,
            "icon_timeline_offset": movie["sprites"][icon_id]["offset"],
            "icon_frame_count": len(icon_frames), "icon_recipes": recipes,
            "special_recipes": special, "abc_blocks": movie["abc_blocks"]}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("gfx", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    movie = read_gfx(args.gfx)
    output = worldmap_metadata(movie)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Recovered {output['icon_frame_count']} icon frames, {output['image_count']} bitmap definitions and Home/Player recipes")


if __name__ == "__main__":
    main()
