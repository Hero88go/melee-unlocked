#!/usr/bin/env python3
import hashlib
import json
import struct
import sys
import tempfile
import unittest
from unittest import mock
import warnings
import zipfile
from contextlib import redirect_stderr
from io import BytesIO, StringIO
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import nucleus_vault_diagnostic as MODULE  # noqa: E402


def costume_dat(root: str) -> bytes:
    names = [f"{root}_Share_joint\0".encode(), f"{root}_Share_matanim_joint\0".encode()]
    data_size = 0x20
    roots = struct.pack(">4I", 0, 0, 4, len(names[0]))
    body = bytes(data_size) + roots + b"".join(names)
    return struct.pack(">5I3I", 0x20 + len(body), data_size, 0, 2, 0, 0, 0, 0) + body


def visual_dat() -> bytes:
    data_size, descriptor, image = 0x200, 0x40, 0x80
    root = b"map_head\0"
    root_start = 0x20 + data_size + 4
    strings = root_start + 8
    data = bytearray(strings + len(root))
    struct.pack_into(">5I", data, 0, len(data), data_size, 1, 1, 0)
    struct.pack_into(">I2H2I2f", data, 0x20 + descriptor, image, 8, 8, 6, 0, 0.0, 0.0)
    struct.pack_into(">I", data, 0x20 + data_size, descriptor)
    struct.pack_into(">2I", data, root_start, 0, 0)
    data[strings:] = root
    return bytes(data)


def nested_zip(filename: str, data: bytes) -> bytes:
    output = BytesIO()
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr(filename, data)
    return output.getvalue()


def png(width=136, height=188) -> bytes:
    return MODULE.PNG_SIGNATURE + struct.pack(">I4s2I", 13, b"IHDR", width, height) + b"\x08\x06\0\0\0"


def skin(identifier: str, filename: str, target: str, data: bytes, companions=True) -> dict:
    return {
        "id": identifier,
        "color": identifier.replace("-", " "),
        "costume_code": target,
        "filename": filename,
        "has_csp": companions,
        "has_stock": companions,
        "dat_hash": hashlib.md5(data).hexdigest(),
    }


def write_vault(path: Path, *, target="PlFxGr", missing_companion=False, duplicate=False, symlink=False):
    first = costume_dat("PlyFox5KGr")
    second = costume_dat("PlyFox5KGr")
    metadata = {
        "characters": {
            "Fox": {
                "skins": [
                    skin("tom-nook", "tom-nook.zip", target, first),
                    skin("alternate", "alternate.zip", "PlFxGr", second),
                ],
                "extras": {"shine": [{
                    "id": "purple-shine", "name": "Purple Shine", "model_file": "models/shine.dat"
                }]},
            }
        },
        "stages": {"battlefield": {"variants": [{
            "id": "night", "name": "Night Battlefield", "filename": "night.zip"
        }]}},
    }
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", UserWarning)
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as vault:
            vault.writestr("metadata.json", json.dumps(metadata))
            vault.writestr("Fox/tom-nook.zip", nested_zip("PlFxGr.dat", first))
            vault.writestr("Fox/alternate.zip", nested_zip("PlFxGr.dat", second))
            for stem in ("tom-nook", "alternate"):
                vault.writestr(f"Fox/{stem}_csp.png", png())
                if not (missing_companion and stem == "alternate"):
                    vault.writestr(f"Fox/{stem}_stc.png", png(32, 32))
            vault.writestr("Fox/models/shine.dat", b"visual candidate")
            vault.writestr("das/battlefield/night.zip", nested_zip("GrNBa.dat", visual_dat()))
            if duplicate:
                vault.writestr("FOX/TOM-NOOK.ZIP", b"duplicate")
            if symlink:
                link = zipfile.ZipInfo("Fox/link")
                link.create_system = 3
                link.external_attr = 0o120777 << 16
                vault.writestr(link, b"target")


class NucleusVaultDiagnosticTests(unittest.TestCase):
    def test_preserves_multiple_variants_and_defers_resources_without_iso(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "vault.zip"
            write_vault(path)
            result = MODULE.validate_vault(path)
            self.assertEqual(result["summary"]["character_variants"], 2)
            self.assertEqual(result["summary"]["character_slots"], 1)
            self.assertEqual({item["id"] for item in result["characters"]}, {"tom-nook", "alternate"})
            self.assertEqual(result["stages"][0]["status"], "full_stage_project")
            self.assertEqual(result["effects"][0]["status"], "cataloged_validation_deferred")

    def test_progress_is_flushed_per_record_and_timeout_is_bounded(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "vault.zip"
            write_vault(path)
            progress = StringIO()
            with redirect_stderr(progress):
                result = MODULE.validate_vault(path, progress=True, timeout_seconds=5)
            self.assertIn("COMPLETE character Fox/tom-nook", progress.getvalue())
            self.assertEqual(result["last_completed_record"], "stage battlefield/night: full_stage_project")
            with mock.patch.object(MODULE.time, "monotonic", side_effect=(0.0, 2.0)):
                with self.assertRaisesRegex(MODULE.InvalidImport, "last completed record: none"):
                    MODULE.validate_vault(path, timeout_seconds=1)

    def test_metadata_target_must_match_dat_identity(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "vault.zip"
            write_vault(path, target="PlFxOr")
            with self.assertRaisesRegex(MODULE.InvalidImport, "conflicts with DAT target"):
                MODULE.validate_vault(path)

    def test_missing_promised_companion_uses_vanilla_fallback(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "vault.zip"
            write_vault(path, missing_companion=True)
            result = MODULE.validate_vault(path)
            self.assertTrue(any("vanilla UI fallback" in warning for warning in result["warnings"]))

    def test_duplicate_and_symlink_entries_are_rejected(self):
        for option, message in (("duplicate", "duplicate ZIP path"), ("symlink", "symbolic-link")):
            with self.subTest(option=option), tempfile.TemporaryDirectory() as folder:
                path = Path(folder) / "vault.zip"
                write_vault(path, **{option: True})
                with self.assertRaisesRegex(MODULE.InvalidImport, message):
                    MODULE.validate_vault(path)

    def test_unsafe_nested_path_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "vault.zip"
            write_vault(path)
            with zipfile.ZipFile(path, "a", zipfile.ZIP_DEFLATED) as vault:
                vault.writestr("unsafe.zip", nested_zip("../escape.dat", b"no"))
            # The unreferenced nested ZIP is data only. Make it referenced to exercise the boundary.
            with zipfile.ZipFile(path) as source:
                members = {item.filename: source.read(item) for item in source.infolist()}
            metadata = json.loads(members["metadata.json"])
            metadata["characters"]["Fox"]["skins"][0]["filename"] = "unsafe.zip"
            rebuilt = Path(folder) / "unsafe-vault.zip"
            with zipfile.ZipFile(rebuilt, "w", zipfile.ZIP_DEFLATED) as target:
                for name, data in members.items():
                    target.writestr(name, json.dumps(metadata) if name == "metadata.json" else data)
            with self.assertRaisesRegex(MODULE.InvalidImport, "unsafe ZIP path"):
                MODULE.validate_vault(rebuilt)

    def test_square_brackets_in_project_stage_dat_name_are_supported(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "vault.zip"
            metadata = {
                "characters": {},
                "stages": {"poke_floats": {"variants": [{
                    "id": "blue-sky", "name": "Blue Sky Poké Floats", "filename": "blue-sky.zip"
                }]}},
            }
            with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as vault:
                vault.writestr("metadata.json", json.dumps(metadata))
                vault.writestr(
                    "das/poke_floats/blue-sky.zip",
                    nested_zip("Shiny PokeFloats PkFlt_2][GrPu.dat", visual_dat()),
                )
            result = MODULE.validate_vault(path)
            self.assertEqual(result["stages"][0]["status"], "full_stage_project")
            self.assertEqual(result["stages"][0]["target_path"], "GrPu.dat")


if __name__ == "__main__":
    unittest.main()
