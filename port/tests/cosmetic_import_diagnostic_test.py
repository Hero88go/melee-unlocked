#!/usr/bin/env python3
import importlib.util
import struct
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "cosmetic_import_diagnostic", ROOT / "tools/cosmetic_import_diagnostic.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(MODULE)


def costume_dat(root: str, *, material_animation: bool = True, relocations=()) -> bytes:
    names = [f"{root}_Share_joint\0".encode()]
    if material_animation:
        names.append(f"{root}_Share_matanim_joint\0".encode())
    data_size = 0x20
    root_table = b""
    name_offset = 0
    for index, name in enumerate(names):
        root_table += struct.pack(">2I", index * 4, name_offset)
        name_offset += len(name)
    relocation_table = b"".join(struct.pack(">I", value) for value in relocations)
    body = bytes(data_size) + relocation_table + root_table + b"".join(names)
    return struct.pack(">5I3I", 0x20 + len(body), data_size, len(relocations), len(names), 0, 0, 0, 0) + body


def fox_dat() -> bytes:
    return costume_dat("PlyFox5KGr")


class CosmeticDiagnosticTests(unittest.TestCase):
    def test_valid_dat_and_deflated_zip(self):
        data = fox_dat()
        result = MODULE.inspect_dat(data)
        self.assertEqual(result["target_path"], "PlFxGr.dat")
        with tempfile.TemporaryDirectory() as td:
            archive = Path(td) / "Tom Nook.zip"
            with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zf:
                zf.writestr("Finished Tom Nook/Tom Nook.dat", data)
                zf.writestr("Finished Tom Nook/Tom Nook CSP.png", b"png")
            imported = MODULE.read_input(archive)
            self.assertEqual(imported["source_member"], "Finished Tom Nook/Tom Nook.dat")
            self.assertEqual(len(imported["companions"]), 1)

    def test_malformed_dat_size_is_rejected(self):
        data = bytearray(fox_dat())
        data[3] ^= 1
        with self.assertRaises(MODULE.InvalidImport):
            MODULE.inspect_dat(bytes(data))

    def test_all_character_identity_and_default_root(self):
        marth = MODULE.inspect_dat(costume_dat("PlyMars5KWh"))
        self.assertEqual((marth["character"], marth["target_path"]), ("Marth", "PlMsWh.dat"))
        falcon = MODULE.inspect_dat(costume_dat("PlyCaptain5K", material_animation=False))
        self.assertEqual((falcon["character"], falcon["target_path"]), ("Captain Falcon", "PlCaNr.dat"))

    def test_nonexistent_slot_and_conflicting_roots_are_rejected(self):
        with self.assertRaisesRegex(MODULE.InvalidImport, "supported existing costume"):
            MODULE.inspect_dat(costume_dat("PlyFox5KRe"))
        first = b"PlyFox5KGr_Share_joint\0"
        second = b"PlyMars5K_Share_joint\0"
        data_size = 0x20
        roots = struct.pack(">4I", 0, 0, 4, len(first))
        body = bytes(data_size) + roots + first + second
        data = struct.pack(">5I3I", 0x20 + len(body), data_size, 0, 2, 0, 0, 0, 0) + body
        with self.assertRaisesRegex(MODULE.InvalidImport, "conflict"):
            MODULE.inspect_dat(data)

    def test_invalid_relocation_is_rejected(self):
        with self.assertRaisesRegex(MODULE.InvalidImport, "unaligned"):
            MODULE.inspect_dat(costume_dat("PlyFox5KGr", relocations=(1,)))

    def test_archive_traversal_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            archive = Path(td) / "unsafe.zip"
            with zipfile.ZipFile(archive, "w") as zf:
                zf.writestr("../escape.dat", fox_dat())
            with self.assertRaisesRegex(MODULE.InvalidImport, "unsafe ZIP path"):
                MODULE.read_input(archive)

    def test_multiple_supported_dats_are_not_silently_collapsed(self):
        with tempfile.TemporaryDirectory() as td:
            archive = Path(td) / "variants.zip"
            with zipfile.ZipFile(archive, "w") as zf:
                zf.writestr("one.dat", fox_dat())
                zf.writestr("two.dat", fox_dat())
            with self.assertRaisesRegex(MODULE.InvalidImport, "exactly one DAT"):
                MODULE.read_input(archive)

    def test_case_insensitive_duplicate_and_symlink_are_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            duplicate = Path(td) / "duplicate.zip"
            with zipfile.ZipFile(duplicate, "w") as zf:
                zf.writestr("Skin.dat", fox_dat())
                zf.writestr("skin.dat", fox_dat())
            with self.assertRaisesRegex(MODULE.InvalidImport, "duplicate ZIP path"):
                MODULE.read_input(duplicate)

            symlink = Path(td) / "symlink.zip"
            link = zipfile.ZipInfo("link.dat")
            link.create_system = 3
            link.external_attr = (0o120777 << 16)
            with zipfile.ZipFile(symlink, "w") as zf:
                zf.writestr(link, b"target")
            with self.assertRaisesRegex(MODULE.InvalidImport, "symbolic-link"):
                MODULE.read_input(symlink)

    def test_oversized_direct_resource_fails_before_read(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "too-big.dat"
            with path.open("wb") as output:
                output.seek(MODULE.MAX_ASSET)
                output.write(b"\0")
            with self.assertRaisesRegex(MODULE.InvalidImport, "64 MB"):
                MODULE.read_input(path)


if __name__ == "__main__":
    unittest.main()
