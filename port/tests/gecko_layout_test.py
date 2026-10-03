"""Regression checks for the GCT layout exported to Slippi recordings, and for the Gecko data
writes the Source Port accepts (tools/gecko_targets.py)."""

import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "recomp"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import gecko  # noqa: E402
import gecko_targets  # noqa: E402


class GeckoLayoutTest(unittest.TestCase):
    def test_host_gated_port_codes_are_a_final_suffix(self):
        normal = gecko.GeckoCode("normal")
        normal.enabled = True
        normal.codes = [(0x04000010, 1)]

        optional = gecko.GeckoCode("optional")
        optional.enabled = True
        optional.optional = "widescreen"
        optional.codes = [(0x04000020, 2)]

        port = gecko.GeckoCode("port-only")
        port.enabled = True
        port.port_flag = "no_screen_shake"
        port.codes = [(0xC2000030, 0), (0x60000000, 0)]

        table, optional_offset, port_offset = gecko.generate_gct([normal, optional, port])

        self.assertEqual(optional_offset, 16)
        self.assertEqual(port_offset, 24)
        self.assertEqual(table[8:16], struct.pack(">II", 0x04000010, 1))
        self.assertEqual(table[16:24], struct.pack(">II", 0x04000020, 2))
        self.assertEqual(table[24:40], struct.pack(">IIII", 0xC2000030, 0, 0x60000000, 0))
        self.assertEqual(table[-8:], struct.pack(">II", 0xFF000000, 0))

        # EVENT_GECKO_LIST omits the eight-byte GCT header. Replacing the first word at this
        # adjusted boundary with FF000000 hides every host-gated code without changing event size.
        event = bytearray(table[8:])
        event_offset = 24 - 8
        event[event_offset:event_offset + 8] = struct.pack(">II", 0xFF000000, 0)
        self.assertEqual(event[event_offset:event_offset + 8], struct.pack(">II", 0xFF000000, 0))
        self.assertNotIn(struct.pack(">II", 0xC2000030, 0), event[:event_offset + 8])


def symbol(name, section, addr, size, kind, layout=""):
    s = gecko_targets.Symbol(name, section, addr, size)
    s.kind, s.layout = kind, layout
    return s


def expand(word, value):
    """One 00, 02 or 04 code line as (console address, big-endian bytes), the way the host does."""
    addr = 0x80000000 | (word & 0x01FFFFFF)
    kind = (word >> 24) & 0x0E
    if kind == 0x00:
        return addr, bytes([value & 0xFF]) * ((value >> 16) + 1)
    if kind == 0x02:
        return addr, struct.pack(">H", value & 0xFFFF) * ((value >> 16) + 1)
    return addr, struct.pack(">I", value)


class SourcePortGeckoWriteTest(unittest.TestCase):
    """The rule both the host (user_gecko.cpp) and the game library (shim/mu_gecko.c) implement,
    run on a small made-up image: a function, a float array, a 16-bit array, a struct of two
    16-bit fields and a float, a pointer table and a byte array, back to back."""

    def setUp(self):
        t = gecko_targets
        self.symbols = [
            symbol("ftCo_Attack_Example", ".text", 0x80100000, 0x40, t.CODE),
            symbol("tunable_floats", ".data", 0x80400000, 0x10, t.OK, "4"),
            symbol("tunable_shorts", ".data", 0x80400010, 0x08, t.OK, "2"),
            symbol("mixed_fields", ".data", 0x80400018, 0x10, t.OK, "224"),
            symbol("callback_table", ".data", 0x80400028, 0x08, t.POINTERS),
            symbol("tunable_bytes", ".data", 0x80400030, 0x04, t.OK, "1"),
        ]
        self.memory = {s.name: bytearray(s.size) for s in self.symbols if s.kind == t.OK}

    def write(self, word, value):
        addr, data = expand(word, value)
        reason = gecko_targets.check_write(self.symbols, addr, len(data))
        if not reason:
            gecko_targets.apply_write(self.symbols, self.memory, addr, data)
        return reason

    def test_32_bit_write_on_a_float_tunable(self):
        self.assertEqual(self.write(0x04400004, 0x3FC00000), "")   # 1.5f into the second float
        self.assertEqual(struct.unpack("<4f", self.memory["tunable_floats"]), (0.0, 1.5, 0.0, 0.0))

    def test_16_bit_write_with_repeat(self):
        self.assertEqual(self.write(0x02400012, 0x00021234), "")   # three halves from the second
        self.assertEqual(struct.unpack("<4H", self.memory["tunable_shorts"]), (0, 0x1234, 0x1234, 0x1234))

    def test_8_bit_write_with_repeat(self):
        self.assertEqual(self.write(0x00400030, 0x000300AB), "")
        self.assertEqual(self.memory["tunable_bytes"], b"\xAB" * 4)

    def test_32_bit_write_over_two_16_bit_fields_swaps_each_half(self):
        self.assertEqual(self.write(0x04400018, 0x11223344), "")
        self.assertEqual(struct.unpack("<HHf", self.memory["mixed_fields"][:8]), (0x1122, 0x3344, 0.0))
        # The second period of the same layout, and its float.
        self.assertEqual(self.write(0x04400024, 0x40000000), "")
        self.assertEqual(struct.unpack("<HHf", self.memory["mixed_fields"][8:]), (0, 0, 2.0))

    def test_8_bit_write_into_the_middle_of_a_float(self):
        # The console's most significant byte is the host's last one.
        self.assertEqual(self.write(0x00400000, 0x0000003F), "")
        self.assertEqual(self.write(0x00400001, 0x00000080), "")
        self.assertEqual(struct.unpack("<f", self.memory["tunable_floats"][:4]), (1.0,))

    def test_write_straddling_two_symbols_is_refused(self):
        before = {k: bytes(v) for k, v in self.memory.items()}
        self.assertEqual(self.write(0x0440000E, 0x12345678), "writes past the end of tunable_floats")
        self.assertEqual(self.write(0x0240000E, 0x00010001), "writes past the end of tunable_floats")
        self.assertEqual({k: bytes(v) for k, v in self.memory.items()}, before)

    def test_code_write_is_refused_with_the_function_name(self):
        self.assertEqual(self.write(0x04100010, 0x60000000),
                         "writes game code at ftCo_Attack_Example: needs the Static Recomp engine")

    def test_pointer_bearing_object_is_refused_by_name(self):
        self.assertEqual(self.write(0x04400028, 0x80100000), "writes callback_table, which holds pointers")

    def test_unknown_memory_is_refused(self):
        self.assertIn("not a game variable", self.write(0x04C00000, 1))


class SourcePortGeckoTableTest(unittest.TestCase):
    """The real tables: what the tool proves from this tree."""

    @classmethod
    def setUpClass(cls):
        if not gecko_targets.SYMBOLS.exists():
            raise unittest.SkipTest("the decomp is not checked out")
        cls.symbols = gecko_targets.build()

    def test_generated_tables_are_current(self):
        for path, text in ((gecko_targets.OUT_GAME, gecko_targets.game_table(self.symbols)),
                           (gecko_targets.OUT_HOST, gecko_targets.host_table(self.symbols))):
            self.assertTrue(path.exists(), "%s is missing: run tools/gecko_targets.py" % path.name)
            self.assertEqual(path.read_bytes().decode("utf-8"), text,
                             "%s is stale: run tools/gecko_targets.py" % path.name)

    def test_supported_symbols_are_writable_data_of_proven_size(self):
        supported = [s for s in self.symbols if s.kind == gecko_targets.OK]
        self.assertGreater(len(supported), 100)
        for s in supported:
            self.assertIn(s.section, (".data", ".sdata", ".bss", ".sbss"), s.name)
            self.assertEqual(s.size % sum(int(c) for c in s.layout), 0, s.name)

    def test_real_float_tunable_and_real_function(self):
        floats = gecko_targets.find(self.symbols, 0x803D05C8)   # f32 ftPr_Init_803D05C8[4]
        self.assertEqual((floats.name, floats.kind, floats.layout), ("ftPr_Init_803D05C8", gecko_targets.OK, "4"))
        self.assertEqual(gecko_targets.check_write(self.symbols, 0x803D05CC, 4), "")
        self.assertEqual(gecko_targets.check_write(self.symbols, 0x803D05D6, 4), "writes past the end of ftPr_Init_803D05C8")
        code = gecko_targets.find(self.symbols, 0x80003100)
        self.assertEqual(gecko_targets.check_write(self.symbols, 0x80003100, 4),
                         "writes game code at %s: needs the Static Recomp engine" % code.name)

    def test_no_supported_ranges_overlap(self):
        supported = [s for s in self.symbols if s.kind == gecko_targets.OK]
        for a, b in zip(supported, supported[1:]):
            self.assertLessEqual(a.addr + a.size, b.addr, a.name)


if __name__ == "__main__":
    unittest.main()
