"""Small malformed-PE and overlapping-PDB regressions; no compiler required."""

from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

from analyze_binary_size import PE, font_matches, labeled_paths, pdb_contributions

TARGET = Path(__file__).resolve().parents[1] / "target"


def image():
    data = bytearray(1024)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HH", data, 0x84, 0x8664, 1)
    struct.pack_into("<H", data, 0x94, 240)
    struct.pack_into("<H", data, 0x98, 0x20B)
    struct.pack_into("<Q", data, 0x98 + 24, 0x180000000)
    struct.pack_into("<II", data, 0x98 + 56, 0x2000, 512)
    struct.pack_into("<I", data, 0x98 + 108, 16)
    struct.pack_into("<8sIIII", data, 0x188, b".rdata", 512, 0x1000, 512, 512)
    struct.pack_into("<II", data, 0x98 + 112 + 8, 0x1000, 40)
    struct.pack_into("<5I", data, 512, 0x1040, 0, 0, 0x1080, 0x1060)
    struct.pack_into("<3Q", data, 576, 0x1090, (1 << 63) | 7, 0)
    data[640:653] = b"KERNEL32.dll\0"
    data[656:665] = b"\0\0FooBar\0"
    return bytes(data)


class BinarySizeTest(unittest.TestCase):
    def setUp(self):
        TARGET.mkdir(exist_ok=True)

    def test_pe_sections_and_named_and_ordinal_imports(self):
        pe = PE(image())
        self.assertEqual(pe.image_size, 8192)
        self.assertEqual(pe.sections[0]["raw_size"], 512)
        self.assertEqual(pe.imports(), [{"dll": "KERNEL32.dll", "symbols": [{"name": "FooBar"}, {"ordinal": 7}]}])
        self.assertEqual(pe.imports(True), [])
        self.assertIsNone(pe.codeview())

    def test_invalid_and_truncated_pe_fail_without_reading_beyond_input(self):
        good = image()
        for data in (b"", b"MZ", b"not a PE", good[:255], good[:700],
                     good[:0x188] + b".rdata\0\0" + b"\xff" * 16 + good[0x1A0:]):
            with self.subTest(length=len(data)), self.assertRaises(ValueError):
                PE(data)
        broken = bytearray(good)
        struct.pack_into("<I", broken, 512 + 12, 0x1500)
        with self.assertRaises(ValueError):
            PE(broken).imports()

    def test_pdb_folded_aliases_and_repeated_same_owner_count_once(self):
        text = """Mod 0001 | `one.o`:
Mod 0002 | `two.o`:
SC[.text] | mod = 1, 0001:0000, size = 10
SC[.text] | mod = 1, 0001:0000, size = 10
SC[.text] | mod = 2, 0001:0005, size = 10
SC[.data] | mod = 2, 0003:0000, size = 2
"""
        report = pdb_contributions(text)
        self.assertEqual(report["sections"][0]["unique_bytes"], 15)
        self.assertEqual(report["sections"][0]["shared_bytes"], 5)
        self.assertEqual([m["exclusive_bytes"][".text"] for m in report["modules"]], [5, 5])
        self.assertEqual(report["shared_groups"], [{"module_ids": [1, 2], "bytes": {".text": 5}}])
        with self.assertRaises(ValueError):
            pdb_contributions("no section contributions")

    def test_complete_font_matches_and_input_labels(self):
        with tempfile.TemporaryDirectory(dir=TARGET) as directory:
            path = Path(directory)
            self.assertTrue(path.resolve().is_relative_to(TARGET.resolve()))
            (path / "sample.ttf").write_bytes(b"complete-font")
            match = font_matches(b"complete-font-partial-complete-font", path)[0]
            self.assertEqual(match["file_offsets"], [0, 22])
            self.assertEqual(match["bytes"], 13)
        self.assertEqual(labeled_paths(["core=C:/one.dll"]), {"core": Path("C:/one.dll")})
        for values in (["missing"], ["../escape=x"], ["core=a", "core=b"], ["core=a", "CORE=b"]):
            with self.assertRaises(ValueError):
                labeled_paths(values)

    def test_cli_rejects_truncated_input_and_output_overwrite(self):
        with tempfile.TemporaryDirectory(dir=TARGET) as directory:
            self.assertTrue(Path(directory).resolve().is_relative_to(TARGET.resolve()))
            path = Path(directory) / "core.readobj.txt"
            path.write_bytes(b"MZ")
            script = Path(__file__).with_name("analyze_binary_size.py")
            for output in (Path(directory), Path(directory) / "output"):
                result = subprocess.run([sys.executable, str(script), f"core={path}", "--output", str(output)],
                                        capture_output=True)
                self.assertEqual(result.returncode, 1)
                self.assertNotIn(b"Traceback", result.stderr)
                self.assertFalse((output / "report.json").exists())
                self.assertEqual(path.read_bytes(), b"MZ")


if __name__ == "__main__":
    unittest.main()
