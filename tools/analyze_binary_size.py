"""Inspect DLL sizes without loading them; optional LLVM PDB attribution and font matches.

Example: python tools/analyze_binary_size.py core=target/release/EROverlay.dll
    --pdb core=target/release/EROverlay.pdb --output target/binary-size
Only Python's standard library is required. Raw LLVM output stays in --output.
"""

import argparse
from collections import Counter, defaultdict
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import uuid


class PE:
    def __init__(self, data: bytes):
        self.data = data
        if self.take(0, 2) != b"MZ":
            raise ValueError("missing DOS signature")
        header = self.number(0x3C, "I")
        if self.take(header, 4) != b"PE\0\0":
            raise ValueError("missing PE signature")
        self.machine, count = self.unpack(header + 4, "HH")
        optional_size = self.number(header + 20, "H")
        optional = header + 24
        self.take(optional, optional_size)
        magic = self.number(optional, "H")
        if magic not in (0x10B, 0x20B):
            raise ValueError("unsupported PE optional header")
        self.width = 8 if magic == 0x20B else 4
        directory_offset = 112 if self.width == 8 else 96
        if optional_size < directory_offset or count == 0:
            raise ValueError("invalid PE header size or section count")
        self.image_base = self.number(optional + (24 if self.width == 8 else 28), "Q" if self.width == 8 else "I")
        self.image_size = self.number(optional + 56, "I")
        self.header_size = self.number(optional + 60, "I")
        directories = self.number(optional + directory_offset - 4, "I")
        if directories > (optional_size - directory_offset) // 8:
            raise ValueError("truncated PE data directories")
        self.directories = [self.unpack(optional + directory_offset + i * 8, "II") for i in range(directories)]
        self.sections = []
        for i in range(count):
            offset = optional + optional_size + i * 40
            name, virtual_size, rva, raw_size, raw_offset = self.unpack(offset, "8sIIII")
            self.take(offset, 40)
            if raw_size:
                self.take(raw_offset, raw_size)
            self.sections.append(dict(index=i + 1, name=name.rstrip(b"\0").decode("ascii", "replace"),
                                      virtual_size=virtual_size, rva=rva, raw_size=raw_size, raw_offset=raw_offset))

    def take(self, offset, size):
        if offset < 0 or size < 0 or offset + size > len(self.data):
            raise ValueError(f"truncated PE at file offset {offset:#x}, length {size:#x}")
        return self.data[offset:offset + size]

    def unpack(self, offset, format):
        return struct.unpack("<" + format, self.take(offset, struct.calcsize("<" + format)))

    def number(self, offset, format):
        return self.unpack(offset, format)[0]

    def file_offset(self, rva, size=1):
        if 0 <= rva < self.header_size and rva + size <= self.header_size:
            self.take(rva, size)
            return rva
        for section in self.sections:
            delta = rva - section["rva"]
            if 0 <= delta and delta + size <= section["raw_size"]:
                return section["raw_offset"] + delta
        raise ValueError(f"PE RVA {rva:#x} is not backed by {size} file bytes")

    def string(self, rva):
        # Read each byte through the RVA mapping; an unterminated name cannot
        # spill into an unrelated section or allocate an unbounded string.
        value = bytearray()
        for i in range(65536):
            byte = self.data[self.file_offset(rva + i)]
            if byte == 0:
                return value.decode("ascii", "replace")
            value.append(byte)
        raise ValueError("unterminated PE import name")

    def directory(self, index):
        return self.directories[index] if index < len(self.directories) else (0, 0)

    def imports(self, delay=False):
        rva, size = self.directory(13 if delay else 1)
        if not rva and not size:
            return []
        stride = 32 if delay else 20
        if not rva or size < stride:
            raise ValueError("invalid PE import directory")
        result = []
        for i in range(size // stride):
            fields = self.unpack(self.file_offset(rva + i * stride, stride), "8I" if delay else "5I")
            if not any(fields):
                return result
            if delay:
                attrs, name, _, iat, lookup, *_ = fields
                adjust = 0 if attrs & 1 else self.image_base
                name, lookup = name - adjust, (lookup or iat) - adjust
            else:
                lookup, _, _, name, iat = fields
                lookup = lookup or iat
                adjust = 0
            if not name or not lookup:
                raise ValueError("invalid PE import descriptor")
            symbols = []
            for j in range(65536):
                entry = self.number(self.file_offset(lookup + j * self.width, self.width), "Q" if self.width == 8 else "I")
                if not entry:
                    break
                symbols.append({"ordinal": entry & 0xFFFF} if entry >> (self.width * 8 - 1)
                               else {"name": self.string(entry - adjust + 2)})
            else:
                raise ValueError("unterminated PE import thunk table")
            result.append({"dll": self.string(name), "symbols": symbols})
        raise ValueError("unterminated PE import directory")

    def codeview(self):
        rva, size = self.directory(6)
        if size % 28:
            raise ValueError("invalid PE debug directory")
        for i in range(size // 28):
            entry = self.unpack(self.file_offset(rva + i * 28, 28), "IIHHIIII")
            if entry[4] == 2:
                data = self.take(entry[7], entry[5])
                if data.startswith(b"RSDS"):
                    if len(data) < 24:
                        raise ValueError("truncated CodeView record")
                    return {"guid": str(uuid.UUID(bytes_le=data[4:20])).upper(),
                            "age": struct.unpack_from("<I", data, 20)[0]}
        return None


def pdb_contributions(text):
    modules = {int(i): name for i, name in re.findall(r"Mod (\d+) \| `([^`]+)`", text)}
    events = defaultdict(list)
    for name, module, section, start, size in re.findall(
            r"SC\[([^]]+)\]\s*\| mod = (\d+), (\d+):(\d+), size = (\d+)", text):
        start, size, module = int(start), int(size), int(module)
        if size:
            events[(int(section), name)].extend(((start, module, 1), (start + size, module, -1)))
    if not events:
        raise ValueError("PDB has no section contributions")
    attribution = {i: {"id": i, "name": name, "exclusive_bytes": {}, "shared_referenced_bytes": {}}
                   for i, name in modules.items()}
    sections, shared = [], defaultdict(Counter)
    for (index, name), entries in sorted(events.items()):
        active, previous, unique, shared_bytes = Counter(), 0, 0, 0
        for position, module, change in sorted(entries):
            length = position - previous
            owners = tuple(sorted(i for i, count in active.items() if count > 0))
            if owners and length:
                unique += length
                field = "exclusive_bytes" if len(owners) == 1 else "shared_referenced_bytes"
                if len(owners) > 1:
                    shared_bytes += length
                    shared[owners][name] += length
                for owner in owners:
                    row = attribution.setdefault(owner, {"id": owner, "name": "<unknown>",
                                                         "exclusive_bytes": {}, "shared_referenced_bytes": {}})
                    row[field][name] = row[field].get(name, 0) + length
            active[module] += change
            previous = position
        sections.append({"index": index, "name": name, "unique_bytes": unique, "shared_bytes": shared_bytes})
    return {"notes": ["Section-offset intervals are unioned, including overlaps within one compiland. Shared/ICF ranges count once in section totals.",
                       "exclusive_bytes plus shared_groups are additive; shared_referenced_bytes are non-additive references, not extra storage.",
                       "Compiland ownership is linker attribution, not semantic ownership: LTO imports/inlining move code across crate boundaries; padding and linker-generated bytes may be unattributed."],
            "sections": sections,
            "modules": [row for row in attribution.values() if row["exclusive_bytes"] or row["shared_referenced_bytes"]],
            "shared_groups": [{"module_ids": list(owners), "bytes": dict(sizes)} for owners, sizes in sorted(shared.items())]}


def font_matches(data, directory):
    result = []
    for path in sorted(directory.rglob("*")):
        if path.is_file() and path.suffix.lower() in (".ttf", ".otf", ".ttc"):
            font, offsets = path.read_bytes(), []
            if not font:
                raise ValueError(f"empty font: {path}")
            position = data.find(font)
            while position >= 0:
                offsets.append(position)
                position = data.find(font, position + 1)
            result.append({"path": str(path.resolve()), "bytes": len(font),
                           "sha256": hashlib.sha256(font).hexdigest(), "file_offsets": offsets})
    return result


def labeled_paths(values):
    result = {}
    folded = set()
    for value in values:
        label, separator, path = value.partition("=")
        if not separator or not re.fullmatch(r"[A-Za-z0-9_-]+", label) or not path or label.casefold() in folded:
            raise ValueError(f"expected unique label=path with a safe filename label: {value}")
        folded.add(label.casefold())
        result[label] = Path(path)
    return result


def llvm_tool(name, directory):
    if directory:
        path = directory / (name + (".exe" if os.name == "nt" else ""))
        if not path.is_file():
            raise ValueError(f"LLVM tool missing: {path}")
        return str(path)
    found = shutil.which(name)
    if not found and os.name == "nt":
        found = next(iter(sorted(Path(os.environ.get("ProgramFiles", "C:/Program Files")).glob(
            f"Microsoft Visual Studio/*/*/VC/Tools/Llvm/x64/bin/{name}.exe"), reverse=True)), None)
    if not found:
        raise ValueError(f"{name} not found; pass --llvm-bin")
    return str(found)


def run_llvm(tool, arguments, output):
    process = subprocess.run([tool, *arguments], capture_output=True, text=True, encoding="utf-8", errors="replace")
    output.write_text(process.stdout + process.stderr, encoding="utf-8")
    if process.returncode:
        raise ValueError(f"LLVM failed ({process.returncode}); see {output}")
    return process.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", help="label=DLL (repeatable)")
    parser.add_argument("--llvm-bin", type=Path)
    parser.add_argument("--pdb", action="append", default=[], help="label=PDB matching an input")
    parser.add_argument("--font-dir", type=Path)
    parser.add_argument("--output", type=Path, default=Path("target/binary-size"))
    args = parser.parse_args()
    try:
        inputs, pdbs = labeled_paths(args.inputs), labeled_paths(args.pdb)
        if pdbs.keys() - inputs.keys():
            raise ValueError("each --pdb label must match an input label")
        if args.font_dir and not args.font_dir.is_dir():
            raise ValueError(f"font directory missing: {args.font_dir}")
        sources = {path.resolve() for path in [*inputs.values(), *pdbs.values()]}
        outputs = {args.output / "report.json"} | {args.output / f"{label}.{kind}.txt"
                                                  for label in inputs for kind in ("readobj", "pdb")}
        if sources & {path.resolve() for path in outputs}:
            raise ValueError("output files must not overwrite a DLL or PDB input")
        readobj = llvm_tool("llvm-readobj", args.llvm_bin)
        pdbutil = llvm_tool("llvm-pdbutil", args.llvm_bin) if pdbs else None
        report = {"schema_version": 1, "tools": {"readobj": readobj, "pdbutil": pdbutil}, "files": {}}
        args.output.mkdir(parents=True, exist_ok=True)
        for label, path in inputs.items():
            data = path.read_bytes()
            pe = PE(data)
            row = {"path": str(path.resolve()), "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data),
                   "machine": pe.machine, "size_of_image": pe.image_size, "sections": pe.sections,
                   "imports": pe.imports(), "delay_imports": pe.imports(True), "codeview": pe.codeview()}
            raw = args.output / f"{label}.readobj.txt"
            run_llvm(readobj, ["--file-header", "--sections", "--coff-imports", str(path)], raw)
            row["readobj_raw"] = raw.name
            if label in pdbs:
                raw = args.output / f"{label}.pdb.txt"
                text = run_llvm(pdbutil, ["dump", "--summary", "--modules", "--section-contribs", str(pdbs[label])], raw)
                guid, age = re.search(r"GUID: \{([^}]+)\}", text), re.search(r"\bAge: (\d+)", text)
                if not guid or not age or row["codeview"] != {"guid": guid[1].upper(), "age": int(age[1])}:
                    raise ValueError(f"PDB identity does not match {label}'s CodeView GUID/age")
                row["pdb"] = {"path": str(pdbs[label].resolve()), "raw": raw.name, **pdb_contributions(text)}
            if args.font_dir:
                row["fonts"] = font_matches(data, args.font_dir)
            report["files"][label] = row
        output = args.output / "report.json"
        output.write_text(json.dumps(report, separators=(",", ":")) + "\n", encoding="utf-8")
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f"二进制尺寸分析失败：{error}\n")
    print(output.resolve())


if __name__ == "__main__":
    main()
