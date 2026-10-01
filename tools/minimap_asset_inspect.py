"""Inspect CPU assets returned by Elden Ring's file loader.

Reads TPF/DDS and the observed PC Binder4 format; it does not read encrypted
Steam archives, attach to a game process, or use a debugger.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import xml.etree.ElementTree as ET
from pathlib import Path


def checked_slice(data: bytes, offset: int, size: int) -> bytes:
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise ValueError(f"Out-of-bounds asset range: offset={offset}, size={size}, bytes={len(data)}")
    return data[offset:offset + size]


def utf16_string(data: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(data) or offset % 2:
        raise ValueError(f"Invalid UTF-16 name offset: {offset}")
    end = offset
    while end + 2 <= len(data):
        if data[end:end + 2] == b"\0\0":
            return data[offset:end].decode("utf-16-le")
        end += 2
    raise ValueError(f"Unterminated UTF-16 name at {offset}")


def dds_metadata(data: bytes) -> dict:
    checked_slice(data, 0, 128)
    if data[:4] != b"DDS " or struct.unpack_from("<I", data, 4)[0] != 124:
        raise ValueError("Expected a DDS header of 124 bytes")
    height, width, pitch, depth, mip_count = struct.unpack_from("<IIIII", data, 12)
    pf_size, pf_flags, fourcc, rgb_bits, red, green, blue, alpha = struct.unpack_from("<II4sIIIII", data, 76)
    if not width or not height or pf_size != 32:
        raise ValueError("Invalid DDS dimensions or pixel-format header")
    dx10 = None
    header_size = 128
    if fourcc == b"DX10":
        checked_slice(data, 128, 20)
        dx10 = dict(zip(("dxgi_format", "resource_dimension", "misc_flag", "array_size", "misc_flags2"),
                        struct.unpack_from("<IIIII", data, 128)))
        header_size = 148
    return {
        "width": width, "height": height, "mips": mip_count or 1, "depth": depth,
        "pitch_or_linear_size": pitch, "fourcc": fourcc.decode("ascii", errors="replace"),
        "pixel_format_flags": pf_flags, "rgb_bits": rgb_bits,
        "channel_masks": [red, green, blue, alpha], "dx10": dx10,
        "payload_bytes": len(data) - header_size, "payload_sha256": hashlib.sha256(data[header_size:]).hexdigest(),
    }


def tpf_metadata(data: bytes) -> dict:
    checked_slice(data, 0, 16)
    if data[:4] != b"TPF\0" or data[12] != 0 or data[13] != 3:
        raise ValueError("Only the observed PC TPF header is supported")
    count = struct.unpack_from("<I", data, 8)[0]
    if not count or count > 65536:
        raise ValueError(f"Invalid TPF texture count: {count}")
    cursor = 48 if data[15] & 1 else 16
    textures = []
    for _ in range(count):
        checked_slice(data, cursor, 20)
        offset, size = struct.unpack_from("<II", data, cursor)
        name_offset, extra_count = struct.unpack_from("<II", data, cursor + 12)
        dds = checked_slice(data, offset, size)
        textures.append({"name": utf16_string(data, name_offset), "data_offset": offset, "data_size": size,
                         "tpf_format": data[cursor + 8], "texture_type": data[cursor + 9],
                         "tpf_mip_byte": data[cursor + 11], "dds": dds_metadata(dds)})
        cursor += 20
        for _ in range(extra_count):
            checked_slice(data, cursor, 8)
            extra_size = struct.unpack_from("<I", data, cursor + 4)[0]
            checked_slice(data, cursor, extra_size + 8)
            cursor += extra_size + 8
    return {"kind": "TPF", "texture_count": count, "textures": textures}


def binder_metadata(data: bytes) -> dict:
    checked_slice(data, 0, 64)
    if data[:4] not in (b"BND4", b"BHF4") or data[4:12] != bytes.fromhex("0000000000000100"):
        raise ValueError("Only the observed little-endian PC Binder4 header is supported")
    count = struct.unpack_from("<I", data, 12)[0]
    start = struct.unpack_from("<Q", data, 16)[0]
    stride = struct.unpack_from("<Q", data, 32)[0]
    if start != 64 or stride != 36:
        raise ValueError(f"Unsupported Binder4 entry layout: start={start}, stride={stride}")
    checked_slice(data, start, count * stride)
    entries = []
    for index in range(count):
        cursor = start + index * stride
        compressed_size, uncompressed_size = struct.unpack_from("<QQ", data, cursor + 8)
        offset, id, name_offset = struct.unpack_from("<III", data, cursor + 24)
        name = utf16_string(data, name_offset)
        entry = {"id": id, "name": name, "data_offset": offset,
                 "data_size": compressed_size, "uncompressed_size": uncompressed_size}
        if data[:4] == b"BND4":
            payload = checked_slice(data, offset, compressed_size)
            if compressed_size != uncompressed_size:
                raise ValueError("Compressed Binder4 entries require the game's decompression layer")
            if name.endswith(".mtmsk") or name.endswith(".layout"):
                document = ET.fromstring(payload.rstrip(b"\0"))
                if document.tag == "MapTileMaskList":
                    entry["tile_masks"] = [{"key": int(item.attrib["id"]), "mask": int(item.attrib["mask"]),
                                            "exists": bool(int(item.attrib["exists"]))} for item in document]
                elif document.tag == "TextureAtlas":
                    entry["atlas"] = {"image_path": document.attrib["imagePath"],
                                      "sprites": [dict(item.attrib) for item in document]}
        entries.append(entry)
    return {"kind": data[:4].decode("ascii"), "entry_count": count, "entries": entries}


def inspect(path: Path) -> dict:
    data = path.read_bytes()
    if data[:4] == b"TPF\0":
        result = tpf_metadata(data)
    elif data[:4] in (b"BND4", b"BHF4"):
        result = binder_metadata(data)
    elif data[:4] == b"DDS ":
        result = {"kind": "DDS", "dds": dds_metadata(data)}
    else:
        raise ValueError(f"Unsupported asset magic: {data[:4].hex()}")
    return {"file": str(path), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(), **result}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    results = [inspect(path) for path in args.inputs]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    for result in results:
        print(f"{result['file']}: {result['kind']}, {result['bytes']} bytes, SHA256={result['sha256']}")


if __name__ == "__main__":
    main()
