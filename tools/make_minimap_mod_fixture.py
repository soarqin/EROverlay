"""Build local multi-atlas resource fixtures from captured native layouts.

No external sprites are introduced. This redistributes original aliases over
12 synthetic BC1 atlases and adds one unused layout without a DDS. A larger
TPF also includes unreferenced UI images to exercise the native copy budget.
"""

from __future__ import annotations

import json
import struct
import xml.etree.ElementTree as ET
from pathlib import Path

from minimap_asset_inspect import binder_metadata, checked_slice


def binder(entries):
    data = bytearray(64 + 36 * len(entries))
    data[:4] = b"BND4"
    data[10] = 1
    struct.pack_into("<IQQ", data, 12, len(entries), 64, 0)
    struct.pack_into("<Q", data, 32, 36)
    for index, (name, payload) in enumerate(entries):
        if len(data) % 2:
            data.append(0)
        name_offset = len(data)
        data += name.encode("utf-16-le") + bytes(2)
        offset = len(data)
        data += payload
        struct.pack_into("<QQIII", data, 64 + index * 36 + 8, len(payload), len(payload), offset, index, name_offset)
    return data


def dds(width, height, color):
    header = bytearray(128)
    header[:4] = b"DDS "
    struct.pack_into("<IIII", header, 4, 124, 0xA1007, height, width)
    struct.pack_into("<I", header, 28, 1)
    struct.pack_into("<II4s", header, 76, 32, 4, b"DXT1")
    struct.pack_into("<I", header, 108, 0x1000)
    block = struct.pack("<HHI", color, color, 0)
    return bytes(header) + block * ((width + 3) // 4) * ((height + 3) // 4)


def tpf(entries):
    data = bytearray(16 + 20 * len(entries))
    data[:4] = b"TPF\0"
    data[13] = 3
    struct.pack_into("<I", data, 8, len(entries))
    for index, (name, payload) in enumerate(entries):
        if len(data) % 2:
            data.append(0)
        name_offset = len(data)
        data += name.encode("utf-16-le") + bytes(2)
        offset = len(data)
        data += payload
        struct.pack_into("<II4BII", data, 16 + index * 20, offset, len(payload), 0, 0, 0, 1, name_offset, 0)
    struct.pack_into("<I", data, 4, len(data) - 16)
    return data


def write_tpf(path, entries):
    # Stream the large fixture without holding another whole-container copy.
    header = bytearray(16 + 20 * len(entries))
    header[:4] = b"TPF\0"
    header[13] = 3
    struct.pack_into("<I", header, 8, len(entries))
    with path.open("wb") as stream:
        stream.write(header)
        for index, (name, payload) in enumerate(entries):
            if stream.tell() % 2:
                stream.write(bytes(1))
            name_offset = stream.tell()
            stream.write(name.encode("utf-16-le") + bytes(2))
            offset = stream.tell()
            stream.write(payload)
            struct.pack_into("<II4BII", header, 16 + index * 20, offset, len(payload), 0, 0, 0, 1, name_offset, 0)
        struct.pack_into("<I", header, 4, stream.tell() - 16)
        stream.seek(0)
        stream.write(header)


def main():
    output = Path("build/native-checks/mod-fixture")
    output.mkdir(parents=True, exist_ok=True)
    source = Path("build/ida/probes/run-26836-32694765/common-layouts.bin").read_bytes()
    parsed = binder_metadata(source)
    originals = [entry for entry in parsed["entries"] if entry.get("atlas", {}).get("image_path") in
                 ("SB_MapCursor.png", "SB_MapCursor_02.png", "SB_MapCursor_03_dlc.png", "SB_Chara.png")]
    sprites = [sprite for entry in originals for sprite in entry["atlas"]["sprites"]]
    roots = [ET.Element("TextureAtlas", imagePath=f"SB_ModMap_{index:02}.png") for index in range(12)]
    for index, sprite in enumerate(sprites):
        ET.SubElement(roots[index % len(roots)], "SubTexture", sprite)
    entries = [(f"SB_ModMap_{i:02}.layout", ET.tostring(root)) for i, root in enumerate(roots)]
    entries.append(("unused.layout", b'<TextureAtlas imagePath="SB_ModUnused.png"><SubTexture name="unused.png" x="0" y="0" width="8" height="8" half="0"/></TextureAtlas>'))
    entries.append(("duplicate.layout", b'<TextureAtlas imagePath="SB_ModDuplicate.png"><SubTexture name="MENU_MAP_Church.png" x="999" y="999" width="2" height="2" half="0"/></TextureAtlas>'))
    (output / "layouts.bin").write_bytes(binder(entries))
    images = [(f"SB_ModMap_{i:02}", dds(2048, 2048, (i + 1) * 1024)) for i in range(12)]
    (output / "atlases.tpf").write_bytes(tpf(images))
    (output / "partial-atlases.tpf").write_bytes(tpf([entry for i, entry in enumerate(images) if i != 11]))
    (output / "malformed-atlases.tpf").write_bytes(tpf([(name, payload[:-1] if i == 11 else payload) for i, (name, payload) in enumerate(images)]))
    # 42 valid images total >256 MiB, while the 12 requested DDS total <25 MiB.
    # Unreferenced UI atlases must not consume the named-copy budget.
    unused = dds(4096, 4096, 2048)
    write_tpf(output / "large-atlases.tpf", images + [(f"SB_ModUnreferenced_{i:02}", unused) for i in range(30)])
    # Use modern GFX with only pre-DLC aliases/masks to exercise absent DLC
    # frames without claiming to have tested historical asset bundles.
    old_entries = [(entry["name"], checked_slice(source, entry["data_offset"], entry["data_size"]))
                   for entry in originals if entry["atlas"]["image_path"] != "SB_MapCursor_03_dlc.png"]
    (output / "pre-dlc-layouts.bin").write_bytes(binder(old_entries))
    masks = Path("build/ida/probes/run-26836-32694765/map-masks.bin").read_bytes()
    masks_entries = [(entry["name"], checked_slice(masks, entry["data_offset"], entry["data_size"]))
                     for entry in binder_metadata(masks)["entries"] if "MENU_MapTile_M10" not in entry["name"]]
    (output / "pre-dlc-masks.bin").write_bytes(binder(masks_entries))
    print(json.dumps({"atlases": 12, "aliases": len(sprites), "unused_missing_atlases": 1, "duplicate_missing_atlases": 1,
                      "large_tpf_bytes": (output / "large-atlases.tpf").stat().st_size, "output": str(output)}))


if __name__ == "__main__":
    main()
