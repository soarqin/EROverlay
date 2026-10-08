"""Read selected original EMEVDs from Elden Ring's installed Data0 archive.

Public archive keys must come from ida_boss_research.py. The archive, executable
and saves are never modified. Extracted copyrighted files belong in build/ida.
Only cryptography and the game's Oodle decompressor are used for container I/O.
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import re
import struct
import zlib
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

from ida_minimap_research import find_steam_executable
from ida_minimap_versions import write_json


def slice_at(data: bytes, offset: int, size: int) -> bytes:
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise ValueError(f"Invalid container range: {offset=}, {size=}, total={len(data)}")
    return data[offset:offset + size]


def path_hash(path: str) -> int:
    result = 0
    for char in "/" + path.strip().replace("\\", "/").lower().lstrip("/"):
        result = (result * 0x85 + ord(char)) & 0xffffffffffffffff
    return result


def decrypt_header(encrypted: bytes, key_paths: list[Path]) -> tuple[bytes, Path]:
    for key_path in key_paths:
        key = serialization.load_pem_public_key(key_path.read_bytes()).public_numbers()
        width = (key.n.bit_length() + 7) // 8
        if len(encrypted) % width:
            continue
        first = pow(int.from_bytes(encrypted[:width], "big"), key.e, key.n).to_bytes(width, "big")
        if first[:5] != b"\0BHD5":
            continue
        chunks = [first[1:]]
        for offset in range(width, len(encrypted), width):
            value = pow(int.from_bytes(encrypted[offset:offset + width], "big"), key.e, key.n)
            chunks.append(value.to_bytes(width - 1, "big"))
        return b"".join(chunks), key_path
    raise ValueError("No IDA-exported public key matches the archive header")


class Archive:
    def __init__(self, game: Path, name: str, keys: list[Path], output: Path):
        source = game / f"{name}.bhd"
        encrypted = source.read_bytes()
        digest = hashlib.sha256(encrypted).hexdigest()
        cache = output / f"{name}-{digest}.bhd"
        key_cache = cache.with_suffix(".key.json")
        if cache.exists() and key_cache.exists():
            header = cache.read_bytes()
            key_path = Path(json.loads(key_cache.read_text())["key"])
        else:
            print(f"Decrypting original {source.name} with IDA-exported public keys", flush=True)
            header, key_path = decrypt_header(encrypted, keys)
            cache.write_bytes(header)
            write_json(key_cache, {"key": str(key_path.resolve())})
        if header[:4] != b"BHD5" or header[4] != 0xff or struct.unpack_from("<I", header, 8)[0] != 1:
            raise ValueError("Unsupported BHD5 header")
        is64 = struct.unpack_from("<I", header, 20)[0] == 0 and struct.unpack_from("<I", header, 28)[0] == 0
        count, table = struct.unpack_from("<QQ" if is64 else "<II", header, 16)
        if not 0 < count < 100000:
            raise ValueError(f"Invalid BHD bucket count {count}")
        self.entries = {}
        for bucket in range(count):
            cursor = table + bucket * (16 if is64 else 8)
            if is64:
                nfiles, unknown, offset = struct.unpack_from("<IIQ", slice_at(header, cursor, 16))
                if unknown != 1:
                    raise ValueError("Unsupported BHD5 bucket flag")
            else:
                nfiles, offset = struct.unpack_from("<II", slice_at(header, cursor, 8))
            slice_at(header, offset, nfiles * 40)
            for index in range(nfiles):
                filehash, padded, size, data_offset, sha_offset, aes_offset = struct.unpack_from("<QIIQQQ", header, offset + index * 40)
                aes_key, ranges = None, []
                if aes_offset:
                    aes_key = slice_at(header, aes_offset, 16)
                    nranges = struct.unpack_from("<I", header, aes_offset + 16)[0]
                    slice_at(header, aes_offset + 20, nranges * 16)
                    ranges = [struct.unpack_from("<qq", header, aes_offset + 20 + i * 16) for i in range(nranges)]
                if filehash in self.entries:
                    raise ValueError(f"Duplicate BHD path hash {filehash:#x}")
                self.entries[filehash] = {"padded": padded, "size": size or padded, "offset": data_offset, "key": aes_key, "ranges": ranges}
        self.stream = (game / f"{name}.bdt").open("rb")
        self.metadata = {"archive": name, "bhd_sha256": digest, "public_key_file": key_path.name,
                         "public_key_sha256": hashlib.sha256(key_path.read_bytes()).hexdigest(),
                         "files": len(self.entries), "buckets": count, "bucket_fields_64_bit": is64}
        print(f"{name}: {len(self.entries)} files, {count} buckets", flush=True)

    def read(self, path: str) -> tuple[bytes, dict] | None:
        entry = self.entries.get(path_hash(path))
        if entry is None:
            return None
        self.stream.seek(entry["offset"])
        payload = bytearray(self.stream.read(entry["padded"]))
        if len(payload) != entry["padded"]:
            raise ValueError(f"Truncated archive entry {path}")
        if entry["key"]:
            for start, end in entry["ranges"]:
                if start == -1 or end == -1 or start == end:
                    continue
                encrypted = slice_at(payload, start, end - start)
                if len(encrypted) % 16:
                    raise ValueError(f"Unaligned AES range for {path}")
                decryptor = Cipher(algorithms.AES(entry["key"]), modes.ECB()).decryptor()
                payload[start:end] = decryptor.update(encrypted) + decryptor.finalize()
        data = bytes(payload[:entry["size"]])
        return data, {"path": path, "path_hash": hex(path_hash(path)), "archive_offset": entry["offset"],
                      "size": entry["size"], "sha256": hashlib.sha256(data).hexdigest()}

    def close(self) -> None:
        self.stream.close()


def decompress(data: bytes, game: Path, magic: bytes | None = b"EVD\0") -> bytes:
    if data[:4] == b"EVD\0":
        return data
    if data[:4] != b"DCX\0" or data[0x18:0x1c] != b"DCS\0" or data[0x24:0x28] != b"DCP\0":
        raise ValueError(f"Unsupported event compression header {data[:48].hex()}")
    size, compressed_size = struct.unpack_from(">II", data, 0x1c)
    dca = data.index(b"DCA\0", 0x2c, 0x60)
    offset = dca + struct.unpack_from(">I", data, dca + 4)[0]
    compressed = slice_at(data, offset, compressed_size)
    kind = data[0x28:0x2c]
    if kind == b"DFLT":
        result = zlib.decompress(compressed)
    elif kind == b"KRAK":
        library = ctypes.CDLL(str(game / "oo2core_6_win64.dll"))
        decode = library.OodleLZ_Decompress
        decode.restype = ctypes.c_ssize_t
        decode.argtypes = [ctypes.c_void_p, ctypes.c_ssize_t, ctypes.c_void_p, ctypes.c_ssize_t,
                           ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_void_p, ctypes.c_size_t,
                           ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
        source = ctypes.create_string_buffer(compressed)
        target = ctypes.create_string_buffer(size)
        written = decode(source, len(compressed), target, size, 1, 0, 0, None, 0, None, None, None, 0, 3)
        if written != size:
            raise ValueError(f"Oodle returned {written}, expected {size}")
        result = target.raw
    else:
        raise ValueError(f"Unsupported DCX compression {kind}")
    if len(result) != size or (magic is not None and result[:4] != magic):
        raise ValueError("Decompressed event length or magic mismatch")
    return result


def event_paths() -> list[str]:
    # Probe hashes only, without reading unrelated archive payloads. Include
    # field-map parent tiles as their scripts initialize linked child events.
    result = ["/event/common.emevd.dcx", "/event/common_func.emevd.dcx",
              "/event/common_macro.emevd.dcx", "/event/m60.emevd.dcx"]
    for area in range(10, 60):
        for block in range(100):
            result.append(f"/event/m{area:02d}_{block:02d}_00_00.emevd.dcx")
    for area in (60, 61):
        for x in range(100):
            for z in range(100):
                for scale in (0, 10, 20):
                    result.append(f"/event/m{area}_{x:02d}_{z:02d}_{scale:02d}.emevd.dcx")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path)
    parser.add_argument("--ida-output", type=Path, default=Path("build/ida/boss"))
    parser.add_argument("--output", type=Path, default=Path("build/ida/boss/events"))
    parser.add_argument("--event-list", type=Path, help="Optional list containing /event/*.emevd.dcx paths")
    args = parser.parse_args()
    game = args.game or find_steam_executable().parent
    args.output.mkdir(parents=True, exist_ok=True)
    keys = sorted(args.ida_output.glob("archive-key-*.pem"))
    if not keys:
        parser.error("Run ida_boss_research.py first to export original archive public keys")
    archive = Archive(game, "Data0", keys, args.output)
    try:
        paths = event_paths() if args.event_list is None else sorted(set(re.findall(r"/event/[^\s]+\.emevd\.dcx", args.event_list.read_text(encoding="utf-8"))))
        entries = []
        for path in paths:
            extracted = archive.read(path)
            if extracted is None:
                continue
            data, info = extracted
            output = args.output / Path(path).name.removesuffix(".dcx")
            result = decompress(data, game)
            output.write_bytes(result)
            entries.append({**info, "decompressed_size": len(result), "decompressed_sha256": hashlib.sha256(result).hexdigest(), "file": output.name})
        allocations = []
        for name in ("LegacyMap", "OpenMap", "LegacyMap_DLC02", "OpenMap_DLC02"):
            path = f"/event/eventflag/{name}.eventflagalloclist.dcx"
            extracted = archive.read(path)
            if extracted is None:
                raise ValueError(f"Original flag allocation list not found: {path}")
            data, info = extracted
            result = decompress(data, game, magic=None)
            output = args.output / (name + ".eventflagalloclist")
            output.write_bytes(result)
            allocations.append({**info, "decompressed_sha256": hashlib.sha256(result).hexdigest(), "file": output.name})
        write_json(args.output / "manifest.json", {**archive.metadata, "event_files": entries, "allocation_files": allocations})
        print(f"Extracted {len(entries)} original events to {args.output}", flush=True)
    finally:
        archive.close()


if __name__ == "__main__":
    main()
