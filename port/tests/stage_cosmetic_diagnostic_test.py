#!/usr/bin/env python3
import importlib.util
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location("stage_cosmetic_diagnostic", ROOT / "tools/stage_cosmetic_diagnostic.py")
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(MODULE)


def stage_dat() -> bytes:
    data_size = 0x400
    root = b"map_head\0"
    image_pointer, palette_pointer = 0x50, 0x54
    image_descriptor, palette_descriptor = 0x100, 0x340
    relocations = (image_pointer, palette_pointer, image_descriptor, palette_descriptor)
    root_start = 0x20 + data_size + len(relocations) * 4
    strings = root_start + 8
    data = bytearray(strings + len(root))
    struct.pack_into(">5I", data, 0, len(data), data_size, len(relocations), 1, 0)
    struct.pack_into(">I", data, 0x20 + image_pointer, image_descriptor)
    struct.pack_into(">I", data, 0x20 + palette_pointer, palette_descriptor)
    struct.pack_into(">I2H2I2f", data, 0x20 + image_descriptor, 0x200, 8, 8, 6, 0, 0.0, 0.0)
    struct.pack_into(">3IH", data, 0x20 + palette_descriptor, 0x380, 1, 0, 16)
    for index, relocation in enumerate(relocations):
        struct.pack_into(">I", data, 0x20 + data_size + index * 4, relocation)
    struct.pack_into(">2I", data, root_start, 0, 0)
    data[strings:] = root
    return bytes(data)


class StageCosmeticDiagnosticTests(unittest.TestCase):
    def test_texture_payload_only_passes(self):
        clean = stage_dat()
        candidate = bytearray(clean)
        candidate[0x20 + 0x200] = 0x7F
        result = MODULE.compare_stage_dat(clean, bytes(candidate))
        self.assertEqual(result["status"], "texture_payload_only")

    def test_anchored_palette_payload_only_passes(self):
        clean = stage_dat()
        candidate = bytearray(clean)
        candidate[0x20 + 0x380] = 0x6E
        result = MODULE.compare_stage_dat(clean, bytes(candidate))
        self.assertEqual(result["palette_descriptors"], 1)

    def test_scalar_geometry_or_parameter_change_fails(self):
        clean = stage_dat()
        candidate = bytearray(clean)
        candidate[0x20 + 0x20] = 1
        with self.assertRaisesRegex(MODULE.InvalidImport, "non-image data"):
            MODULE.compare_stage_dat(clean, bytes(candidate))

    def test_descriptor_and_layout_changes_fail(self):
        clean = stage_dat()
        descriptor = bytearray(clean)
        descriptor[0x20 + 0x105] = 9
        with self.assertRaisesRegex(MODULE.InvalidImport, "image descriptors"):
            MODULE.compare_stage_dat(clean, bytes(descriptor))
        with self.assertRaises(MODULE.InvalidImport):
            MODULE.compare_stage_dat(clean, clean + b"\0")

    def test_identical_stage_is_not_a_variant(self):
        clean = stage_dat()
        with self.assertRaisesRegex(MODULE.InvalidImport, "identical"):
            MODULE.compare_stage_dat(clean, clean)


if __name__ == "__main__":
    unittest.main()
