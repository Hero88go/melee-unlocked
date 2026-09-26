#!/usr/bin/env python3
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import effect_cosmetic_diagnostic as MODULE  # noqa: E402


def effect_dat(root: str, *, color=0xFC00, data_size=0xC00,
               stream_offsets=(0x100, 0x200, 0x300), scalar=0) -> bytes:
    descriptor, image = 0x40, 0x800 if data_size == 0xC00 else 0x900
    relocations = (descriptor,)
    root_start = 0x20 + data_size + len(relocations) * 4
    strings = root_start + 8
    data = bytearray(strings + len(root) + 1)
    struct.pack_into(">5I", data, 0, len(data), data_size, len(relocations), 1, 0)
    struct.pack_into(">I2H2I2f", data, 0x20 + descriptor, image, 8, 8, 6, 0, 0.0, 0.0)
    struct.pack_into(">I", data, 0x20 + data_size, descriptor)
    struct.pack_into(">2I", data, root_start, 0, 0)
    data[strings:strings + len(root) + 1] = root.encode() + b"\0"
    for offset, signature in zip(stream_offsets, MODULE.PARTICLE_SIGNATURES):
        for index, suffix in enumerate(signature):
            struct.pack_into(">2H", data, 0x20 + offset + index * 4, color, suffix)
    data[0x20 + 0x500:0x20 + 0x500 + len(MODULE.FOX_SIDE_COLOR)] = MODULE.FOX_SIDE_COLOR
    data[0x20 + 0x700] = scalar
    return bytes(data)


class EffectCosmeticDiagnosticTests(unittest.TestCase):
    def test_dedicated_effect_archive_accepts_effect_animation_changes(self):
        clean = effect_dat("effFoxDataTable")
        candidate = effect_dat("effFoxDataTable", scalar=7)
        runtime, result = MODULE.materialize_effect_dat("EfFxData.dat", clean, candidate)
        self.assertEqual(runtime, candidate)
        self.assertEqual(result["status"], "dedicated_effect_archive")

    def test_common_effect_archive_rejects_non_texture_changes(self):
        clean = effect_dat("effCommonDataTable")
        candidate = effect_dat("effCommonDataTable", scalar=7)
        with self.assertRaisesRegex(MODULE.InvalidImport, "outside validated texture payloads"):
            MODULE.materialize_effect_dat("EfCoData.dat", clean, candidate)

    def test_fox_fighter_archive_accepts_only_verified_color_fields(self):
        clean = effect_dat("ftDataFox")
        candidate = bytearray(effect_dat("ftDataFox", color=0xA50F))
        candidate[0x20 + 0x500:0x20 + 0x506] = bytes.fromhex("ffffffffa100")
        runtime, result = MODULE.materialize_effect_dat("PlFx.dat", clean, bytes(candidate))
        self.assertEqual(runtime, bytes(candidate))
        self.assertEqual(result["status"], "particle_color_merge")

        candidate[0x20 + 0x700] = 1
        with self.assertRaisesRegex(MODULE.InvalidImport, "outside verified"):
            MODULE.materialize_effect_dat("PlFx.dat", clean, bytes(candidate))

    def test_repacked_falco_archive_merges_only_particle_colors(self):
        clean = effect_dat("ftDataFalco")
        candidate = effect_dat(
            "ftDataFalco", color=0x0F0F, data_size=0xD00,
            stream_offsets=(0x180, 0x280, 0x380), scalar=9,
        )
        runtime, result = MODULE.materialize_effect_dat("PlFc.dat", clean, candidate)
        self.assertEqual(len(runtime), len(clean))
        self.assertEqual(runtime[0x20 + 0x700], 0)
        self.assertEqual(struct.unpack_from(">H", runtime, 0x20 + 0x100)[0], 0x0F0F)
        self.assertTrue(result["candidate_repacked"])

    def test_wrong_root_and_noncanonical_clean_stream_fail_closed(self):
        clean = effect_dat("ftDataFalco")
        wrong_root = effect_dat("ftDataFox", color=0x0F0F)
        with self.assertRaisesRegex(MODULE.InvalidImport, "root"):
            MODULE.materialize_effect_dat("PlFc.dat", clean, wrong_root)
        wrong_clean = effect_dat("ftDataFalco", color=0x1234)
        candidate = effect_dat("ftDataFalco", color=0x0F0F)
        with self.assertRaisesRegex(MODULE.InvalidImport, "exact NTSC 1.02"):
            MODULE.materialize_effect_dat("PlFc.dat", wrong_clean, candidate)


if __name__ == "__main__":
    unittest.main()
