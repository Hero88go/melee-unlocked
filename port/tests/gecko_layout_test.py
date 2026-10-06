"""Regression checks for the GCT layout exported to Slippi recordings, and for the Gecko codes the
Source Port accepts: which variables (tools/gecko_targets.py) and which code types
(tools/gecko_coverage.py, the rules port/runtime/host/user_gecko.cpp implements)."""

import struct
import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "recomp"))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import gecko  # noqa: E402
import gecko_coverage  # noqa: E402
import gecko_targets  # noqa: E402


class GeckoLayoutTest(unittest.TestCase):
    def test_results_upgrade_matches_new_generated_caves(self):
        header = Path(__file__).resolve().parents[1] / "runtime/hle/offline_results_code_policy.h"
        text = header.read_text()
        save = int(re.search(r"kSaveNext = 0x([0-9A-Fa-f]+)u", text).group(1), 16)
        restore = int(re.search(r"kRestoreNext = 0x([0-9A-Fa-f]+)u", text).group(1), 16)
        codes = {lines[0][0]: lines for _, flag, lines in gecko.PORT_CODES if flag == "offline_results"}
        self.assertEqual(codes[0xC21A5B00][1][1], save)
        self.assertEqual(codes[0xC21A5B18][2][0], restore)

    def test_mod_unlock_sites_match_generated_switches(self):
        header = Path(__file__).resolve().parents[1] / "runtime/hle/unlock_code_policy.h"
        addresses = {int(a, 16) for a in re.findall(r"case 0x([0-9A-Fa-f]+)u:", header.read_text())}
        self.assertEqual(addresses, set(gecko.TWO_WAY_TEXT))

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


class SourcePortGeckoCodeTypeTest(unittest.TestCase):
    """Every Gecko code type the Source Port runs, and every one it refuses, on the made-up image
    of SourcePortGeckoWriteTest plus a 32-bit counter (tools/gecko_coverage.py holds the rules that
    user_gecko.cpp implements)."""

    def setUp(self):
        t = gecko_targets
        self.symbols = [
            symbol("ftCo_Attack_Example", ".text", 0x80100000, 0x40, t.CODE),
            symbol("tunable_floats", ".data", 0x80400000, 0x10, t.OK, "4"),
            symbol("tunable_shorts", ".data", 0x80400010, 0x08, t.OK, "2"),
            symbol("mixed_fields", ".data", 0x80400018, 0x10, t.OK, "224"),
            symbol("callback_table", ".data", 0x80400028, 0x08, t.POINTERS),
            symbol("tunable_bytes", ".data", 0x80400030, 0x04, t.OK, "1"),
            symbol("counter", ".bss", 0x80400034, 0x04, t.OK, "4"),
            symbol("far_bytes", ".bss", 0x81400000, 0x10, t.OK, "1"),
        ]
        self.memory = {s.name: bytearray(s.size) for s in self.symbols if s.kind == t.OK}
        self.registers = [0] * 16

    def run_code(self, lines, frames=1):
        plan, reads = gecko_coverage.compile_code(lines, self.symbols)
        for _ in range(frames):
            gecko_coverage.run_plan(
                plan,
                lambda addr, length: gecko_targets.read_bytes(self.symbols, self.memory, addr, length),
                lambda addr, data: gecko_targets.apply_write(self.symbols, self.memory, addr, data),
                self.registers)
        return reads

    def refusal(self, lines):
        with self.assertRaises(gecko_coverage.Refused) as caught:
            gecko_coverage.compile_code(lines, self.symbols)
        return caught.exception.cls, caught.exception.reason

    def shorts(self):
        return struct.unpack("<4H", self.memory["tunable_shorts"])

    # -- writes --

    def test_string_write(self):
        self.run_code([(0x06400030, 3), (0x11223344, 0x55667788)])
        self.assertEqual(self.memory["tunable_bytes"], b"\x11\x22\x33\x00")

    def test_string_write_cut_short_is_refused(self):
        self.assertEqual(self.refusal([(0x06400030, 9), (0x11223344, 0x55667788)]),
                         ("malformed", "has a string write cut short"))

    def test_serial_write_steps_address_and_value(self):
        # 16-bit, three more writes, 2 bytes apart, the value up by 0x10 each time.
        self.run_code([(0x08400010, 0x00000100), (0x10030002, 0x00000010)])
        self.assertEqual(self.shorts(), (0x100, 0x110, 0x120, 0x130))

    def test_serial_write_that_leaves_the_variable_is_refused(self):
        # 32-bit, one more write 2 bytes on: the second starts inside the variable and runs past it.
        self.assertEqual(self.refusal([(0x08400030, 0x00000001), (0x20010002, 0x00000000)]),
                         ("past-end", "writes past the end of tunable_bytes"))
        # Each write is judged on its own: a serial write may step from one variable into the next.
        self.run_code([(0x08400010, 0x00000100), (0x10040002, 0x00000010)])
        self.assertEqual(struct.unpack("<H", self.memory["mixed_fields"][:2]), (0x140,))

    def test_serial_write_cut_short_is_refused(self):
        self.assertEqual(self.refusal([(0x08400010, 1)])[0], "malformed")

    def test_address_bit_24_reaches_the_second_16_megabytes(self):
        self.run_code([(0x01400002, 0x000000AB)])   # 81400002
        self.assertEqual(self.memory["far_bytes"][2], 0xAB)

    def test_32_bit_write_is_rounded_down_to_a_word(self):
        # The handler: rlwinm r12,r12,0,0,29 before the stw (80002050).
        self.run_code([(0x04400006, 0x3FC00000)])
        self.assertEqual(struct.unpack("<4f", self.memory["tunable_floats"]), (0.0, 1.5, 0.0, 0.0))
        # The 16-bit and 8-bit writes are not rounded.
        self.run_code([(0x02400031, 0x00001234)])
        self.assertEqual(self.memory["tunable_bytes"], b"\x00\x12\x34\x00")

    # -- the base address and the pointer --

    def test_pointer_set_and_pointer_relative_writes(self):
        self.run_code([(0x4A000000, 0x80400000),    # po = 80400000
                       (0x14000004, 0x3FC00000),    # 32-bit at po + 4
                       (0x12000010, 0x00001234),    # 16-bit at po + 0x10
                       (0x10000030, 0x000000CD)])   # 8-bit at po + 0x30
        self.assertEqual(struct.unpack("<4f", self.memory["tunable_floats"])[1], 1.5)
        self.assertEqual(self.shorts()[0], 0x1234)
        self.assertEqual(self.memory["tunable_bytes"][0], 0xCD)

    def test_pointer_set_added_to_and_reset_by_the_terminator(self):
        self.run_code([(0x4A000000, 0x80400000),    # po = 80400000
                       (0x4A100000, 0x00000010),    # po += 0x10
                       (0x12000000, 0x00000001),    # 16-bit at po
                       (0xE0000000, 0x80008000),    # ba = po = 80000000
                       (0x12400012, 0x00000002)])
        self.assertEqual(self.shorts()[:2], (1, 2))

    def test_base_address_counts_by_its_top_seven_bits_only(self):
        # The handler: rlwinm r12,r6,0,0,6 (80001FCC). A base address of 80400010 counts as
        # 80000000, so the line's own address decides where the write lands.
        self.run_code([(0x42000000, 0x80400010), (0x02400012, 0x00000002)])
        self.assertEqual(self.shorts(), (0, 2, 0, 0))
        self.assertEqual(self.refusal([(0x42000000, 0x80400010), (0x02000000, 0x00000001)]),
                         ("outside", "writes outside game memory (80000000)"))
        # The same for an if, for a register load and for a number made relative.
        struct.pack_into("<I", self.memory["counter"], 0, 5)
        self.run_code([(0x42000000, 0x80400010), (0x20400034, 5), (0x00400030, 0x000000EE), (0xE2000001, 0),
                       (0x82210001, 0x00400034),    # gr1 = [ba + 400034]
                       (0x84010001, 0x00400031),    # [ba + 400031] = gr1, 8-bit
                       (0x4A010000, 0x00400032),    # po = ba + 400032
                       (0x10000000, 0x000000CD)])
        self.assertEqual(self.memory["tunable_bytes"], b"\xEE\x05\xCD\x00")
        # A base address outside 80000000 to 81FFFFFF does move the line.
        self.assertEqual(self.refusal([(0x42000000, 0x90000000), (0x02400012, 0x00000002)]),
                         ("outside", "writes outside game memory (90400012)"))

    def test_base_address_flags_are_one_bit_each(self):
        # 4202: the relative digit's bit 0 is clear, so nothing is added (rlwinm r14,r3,16,31,31).
        self.run_code([(0x4A020000, 0x80400030), (0x10000000, 0x00000011)])
        self.assertEqual(self.memory["tunable_bytes"][0], 0x11)
        # 4A20: the add digit's bit 0 is clear, so the pointer is set, not added to.
        self.run_code([(0x4A000000, 0x80400000), (0x4A200000, 0x80400031), (0x10000000, 0x00000022)])
        self.assertEqual(self.memory["tunable_bytes"][1], 0x22)

    def test_base_address_stored_is_a_number(self):
        # The value stored is the base address in full, the add included. The address is always
        # counted from the base (stwx r6,r12,r4 at 8000221C).
        self.run_code([(0x42000000, 0x80400000), (0x42100000, 0x00000010), (0x44000000, 0x00400034)])
        self.assertEqual(struct.unpack("<I", self.memory["counter"]), (0x80400010,))
        self.assertEqual(self.refusal([(0x42000000, 0x80400010), (0x44000000, 0x80400034)]),
                         ("outside", "writes outside game memory (00400034)"))
        # The pointer, stored through the pointer: 5C counts the address from the pointer.
        self.run_code([(0x4A000000, 0x80400000), (0x5C000000, 0x00000034)])
        self.assertEqual(struct.unpack("<I", self.memory["counter"]), (0x80400000,))

    def test_pointer_loaded_from_memory_is_refused(self):
        self.assertEqual(self.refusal([(0x48000000, 0x80400034), (0x14000000, 1)])[0], "pointer")
        self.assertEqual(self.refusal([(0x40000000, 0x80400034)])[0], "pointer")
        self.assertEqual(self.refusal([(0x4E000000, 0)])[0], "pointer")
        self.assertEqual(self.refusal([(0x42001003, 0x80400000)])[0], "pointer")   # plus a register

    def test_base_set_inside_an_if_is_unknown_after_it(self):
        lines = [(0x20400034, 0), (0x4A000000, 0x80400000), (0x14000000, 0x3F800000), (0xE2000001, 0)]
        self.run_code(lines)   # known inside the block
        self.assertEqual(struct.unpack("<4f", self.memory["tunable_floats"])[0], 1.0)
        cls, reason = self.refusal(lines + [(0x14000004, 0)])
        self.assertEqual(cls, "base")
        self.assertIn("pointer", reason)
        # The endif that resets the pointer makes it known again.
        self.run_code(lines[:3] + [(0xE2000001, 0x00008000), (0x14400004, 0x40000000)])
        self.assertEqual(struct.unpack("<4f", self.memory["tunable_floats"])[1], 2.0)
        # The base address the same way.
        lines = [(0x20400034, 0), (0x42000000, 0x80400000), (0x04400000, 0x40400000), (0xE2000001, 0)]
        self.run_code(lines)
        self.assertEqual(struct.unpack("<4f", self.memory["tunable_floats"])[0], 3.0)
        cls, reason = self.refusal(lines + [(0x04400004, 0)])
        self.assertEqual(cls, "base")
        self.assertIn("base address", reason)
        self.run_code(lines[:3] + [(0xE2000001, 0x80000000), (0x04400004, 0x40800000)])
        self.assertEqual(struct.unpack("<4f", self.memory["tunable_floats"])[1], 4.0)

    def test_base_set_inside_an_if_is_unknown_in_its_else(self):
        cls, _ = self.refusal([(0x20400034, 0), (0x42000000, 0x80400000), (0xE2100000, 0), (0x04000004, 0)])
        self.assertEqual(cls, "base")

    # -- ifs --

    def test_32_bit_ifs(self):
        struct.pack_into("<I", self.memory["counter"], 0, 5)
        for word, value, passes in ((0x20400034, 5, True), (0x20400034, 6, False), (0x22400034, 6, True),
                                    (0x24400034, 4, True), (0x24400034, 5, False), (0x26400034, 6, True),
                                    (0x26400034, 5, False)):
            self.memory["tunable_bytes"][0] = 0
            reads = self.run_code([(word, value), (0x00400030, 0x000000EE), (0xE2000001, 0)])
            self.assertTrue(reads)
            self.assertEqual(self.memory["tunable_bytes"][0] == 0xEE, passes, hex(word))

    def test_16_bit_if_masks_before_comparing(self):
        struct.pack_into("<H", self.memory["tunable_shorts"], 0, 0x1234)
        for value, passes in ((0x00001234, True), (0xFF000034, True), (0xFF000035, False), (0x00001235, False)):
            self.memory["tunable_bytes"][0] = 0
            self.run_code([(0x28400010, value), (0x00400030, 0x000000EE), (0xE2000001, 0)])
            self.assertEqual(self.memory["tunable_bytes"][0] == 0xEE, passes, hex(value))

    def test_if_address_is_rounded_down_to_what_it_reads(self):
        # The handler: a word for the 32-bit ifs (80002104), a half for the 16-bit ones (80002110).
        struct.pack_into("<I", self.memory["counter"], 0, 5)
        struct.pack_into("<H", self.memory["tunable_shorts"], 2, 0x1234)
        self.run_code([(0x20400036, 5), (0x00400030, 0x000000EE), (0xE2000001, 0),    # reads 80400034
                       (0x20400034, 6),                                               # false
                       (0x20400037, 5), (0x00400031, 0x000000DD), (0xE2000001, 0),    # endif, reads 80400034
                       (0x28400034, 9),                                               # false
                       (0x28400013, 0x00001234), (0x00400032, 0x000000CC), (0xE2000001, 0)])   # endif, reads 80400012
        self.assertEqual(self.memory["tunable_bytes"], b"\xEE\xDD\xCC\x00")

    def test_nested_ifs_and_endif_counts(self):
        struct.pack_into("<I", self.memory["counter"], 0, 5)
        self.run_code([(0x20400034, 6),            # false
                       (0x20400034, 5),            # skipped: one deeper
                       (0x00400030, 0x000000AA),   # skipped
                       (0xE2000001, 0),            # ends the inner if: still skipping
                       (0x00400031, 0x000000BB),   # skipped
                       (0xE2000001, 0),            # ends the outer if
                       (0x00400032, 0x000000CC)])  # runs
        self.assertEqual(self.memory["tunable_bytes"], b"\x00\x00\xCC\x00")

    def test_endif_count_is_five_bits(self):
        # The handler: clrlwi. r9,r3,27 (80002740). E2000021 ends one if, not 33.
        self.run_code([(0x20400034, 1),            # false
                       (0x20400034, 1),            # skipped: one deeper
                       (0xE2000021, 0),            # ends the inner if only
                       (0x00400030, 0x000000AA),   # still skipped
                       (0xE2000001, 0),
                       (0x00400031, 0x000000BB)])  # runs
        self.assertEqual(self.memory["tunable_bytes"][:2], b"\x00\xBB")

    def test_else_inside_a_skipped_block_changes_nothing(self):
        # The handler flips the innermost if only when the one around it is running (8000274C).
        self.run_code([(0x20400034, 1),            # false
                       (0x20400034, 0),            # skipped (it would be true)
                       (0x00400030, 0x000000AA),   # skipped
                       (0xE2100000, 0),            # else of the inner if: the outer one is still false
                       (0x00400031, 0x000000BB),   # skipped
                       (0xE2000001, 0),
                       (0x00400032, 0x000000CC),   # skipped: the outer if
                       (0xE2100000, 0),            # else of the outer if
                       (0x00400033, 0x000000DD),   # runs
                       (0xE2000001, 0)])
        self.assertEqual(self.memory["tunable_bytes"], b"\x00\x00\x00\xDD")
        # An endif and an else in one line: one if ends, then the one around it turns into its else.
        self.memory["tunable_bytes"][:] = bytes(4)
        self.run_code([(0x20400034, 0),            # true
                       (0x20400034, 1),            # false
                       (0x00400030, 0x000000AA),   # skipped
                       (0xE2100001, 0),            # ends the inner if; the outer one, true, turns false
                       (0x00400031, 0x000000BB),   # skipped
                       (0xE2000001, 0),
                       (0x00400032, 0x000000CC)])  # runs
        self.assertEqual(self.memory["tunable_bytes"], b"\x00\x00\xCC\x00")

    def test_else(self):
        struct.pack_into("<I", self.memory["counter"], 0, 5)
        self.run_code([(0x20400034, 6), (0x00400030, 0x000000AA), (0xE2100000, 0), (0x00400031, 0x000000BB),
                       (0xE2000001, 0)])
        self.assertEqual(self.memory["tunable_bytes"][:2], b"\x00\xBB")

    def test_address_ending_in_one_ends_the_previous_if(self):
        struct.pack_into("<I", self.memory["counter"], 0, 5)
        self.run_code([(0x20400034, 6), (0x00400030, 0x000000AA),
                       (0x20400035, 5), (0x00400031, 0x000000BB), (0xE0000000, 0x80008000)])
        self.assertEqual(self.memory["tunable_bytes"][:2], b"\x00\xBB")

    def test_terminator_ends_every_if(self):
        self.run_code([(0x20400034, 1), (0x20400034, 1), (0xE0000000, 0), (0x00400030, 0x00000077)])
        self.assertEqual(self.memory["tunable_bytes"][0], 0x77)

    def test_if_on_memory_that_is_no_variable_is_refused(self):
        cls, reason = self.refusal([(0x28C00000, 0x00000100), (0x00400030, 1)])
        self.assertEqual(cls, "no-symbol")
        self.assertTrue(reason.startswith("reads memory at 80C00000"))
        self.assertEqual(self.refusal([(0x20400028, 0)]), ("symbol", "reads callback_table, which holds pointers"))

    # -- registers --

    def test_register_set_store_and_repeat(self):
        self.run_code([(0x80000003, 0x00001234), (0x84100023, 0x80400010)])   # gr3, 16-bit, three in a row
        self.assertEqual(self.shorts(), (0x1234, 0x1234, 0x1234, 0))

    def test_register_load_operate_store_counts_frames(self):
        lines = [(0x82200001, 0x80400034),    # gr1 = counter
                 (0x86000001, 0x00000001),    # gr1 += 1
                 (0x84200001, 0x80400034)]    # counter = gr1
        self.assertTrue(self.run_code(lines, frames=3))
        self.assertEqual(struct.unpack("<I", self.memory["counter"]), (3,))

    def test_register_operations(self):
        op = gecko_coverage.operate
        self.assertEqual(op(0xFFFFFFFF, 2, 0), 1)
        self.assertEqual(op(0x10000, 0x10000, 1), 0)
        self.assertEqual(op(0xF0, 0x0F, 2), 0xFF)
        self.assertEqual(op(0xF0, 0x3C, 3), 0x30)
        self.assertEqual(op(0xF0, 0x3C, 4), 0xCC)
        # The shifts and the rotate: the handler shifts the operand by the register (slw r4,r9,r4
        # at 800023F4, with the register in r4 and the operand in r9).
        self.assertEqual((op(31, 1, 5), op(32, 1, 5)), (0x80000000, 0))
        self.assertEqual((op(31, 0x80000000, 6), op(32, 0x80000000, 6)), (1, 0))
        self.assertEqual((op(1, 0x80000001, 7), op(0, 5, 7)), (3, 5))
        self.assertEqual((op(4, 0x80000000, 8), op(40, 0x80000000, 8), op(4, 0x40000000, 8)),
                         (0xF8000000, 0xFFFFFFFF, 0x04000000))
        self.assertEqual(op(0x3F800000, 0x40000000, 9), 0x40400000)    # 1.0 + 2.0
        self.assertEqual(op(0x40000000, 0x40400000, 10), 0x40C00000)   # 2.0 * 3.0

    def test_register_and_memory_and_two_registers(self):
        # The memory operand's address is always counted from the base (add r19,r12,r19 at 800023B8).
        struct.pack_into("<I", self.memory["counter"], 0, 0x30)
        self.run_code([(0x80000002, 0x0000000C), (0x86020002, 0x00400034),   # gr2 += [counter]
                       (0x80000005, 0x00000003), (0x88100002, 0x00000005),   # gr2 *= gr5
                       (0x84000002, 0x80400030)])
        self.assertEqual(self.memory["tunable_bytes"][0], 0xB4)
        self.assertEqual(self.refusal([(0x86020002, 0x80400034)]), ("outside", "reads outside game memory (00400034)"))
        # Through the pointer (96): counted from the pointer.
        self.run_code([(0x4A000000, 0x80400000), (0x80000002, 0x00000001), (0x96020002, 0x00000034),
                       (0x84000002, 0x80400031)])
        self.assertEqual(self.memory["tunable_bytes"][1], 0x31)

    def test_register_shift_is_the_operand_shifted_by_the_register(self):
        self.run_code([(0x80000004, 0x00000004), (0x86500004, 0x00000003),   # gr4 = 3 << gr4
                       (0x84000004, 0x80400030)])
        self.assertEqual(self.memory["tunable_bytes"][0], 0x30)

    def test_register_set_flags_are_one_bit_each(self):
        # 8020: the add digit's bit 0 is clear, so the register is set (andi. r5,r14,1 at 80002370).
        self.run_code([(0x80000003, 0x00000007), (0x80200003, 0x00000005), (0x84000003, 0x80400030),
                       (0x80100003, 0x00000001), (0x84000003, 0x80400031)])
        self.assertEqual(self.memory["tunable_bytes"][:2], b"\x05\x06")

    def test_register_used_as_an_address_is_refused(self):
        self.assertEqual(self.refusal([(0x86010002, 1)])[0], "pointer")
        self.assertEqual(self.refusal([(0x88020002, 3)])[0], "pointer")
        self.assertEqual(self.refusal([(0x8A000412, 0)])[0], "pointer")

    def test_register_store_past_the_end_is_refused(self):
        self.assertEqual(self.refusal([(0x84200011, 0x80400030)]), ("past-end", "writes past the end of tunable_bytes"))

    # -- what stays refused --

    def test_powerpc_is_refused_with_the_function_name(self):
        self.assertEqual(self.refusal([(0xC2100010, 1), (0x60000000, 0)]),
                         ("code-inject", "injects PowerPC code into ftCo_Attack_Example (C2), which this build cannot run"))
        self.assertEqual(self.refusal([(0xC0000000, 1), (0x4E800020, 0)])[0], "code-run")
        self.assertEqual(self.refusal([(0xC6100010, 0x80100020)])[0], "code-run")
        self.assertEqual(self.refusal([(0x04100010, 0x60000000)])[0], "code-write")

    def test_loops_jumps_and_register_ifs_are_refused(self):
        for word in (0x60000003, 0x62000000, 0x64000000, 0x66000001, 0x68000001, 0xA0400034, 0xA8000000,
                     0xCC000000, 0xCE000000, 0xF6000001, 0x0A400030):
            self.assertEqual(self.refusal([(word, 0)])[0], "type", hex(word))

    def test_outside_memory_and_empty(self):
        self.assertEqual(self.refusal([(0x04000000, 1)]), ("outside", "writes outside game memory (80000000)"))
        self.assertEqual(self.refusal([]), ("malformed", "has no code lines"))

    def test_end_of_list_stops_the_code(self):
        self.run_code([(0x00400030, 0x00000011), (0xF0000000, 0), (0x00400031, 0x00000022)])
        self.assertEqual(self.memory["tunable_bytes"][:2], b"\x11\x00")

    def test_legacy_rules_refuse_what_the_new_ones_run(self):
        lines = [(0x4A000000, 0x80400000), (0x14000004, 0x3FC00000)]
        self.assertEqual(gecko_coverage.legacy_refusal(lines, self.symbols)[0], "type")
        self.assertIsNone(gecko_coverage.legacy_refusal([(0x04400004, 1)], self.symbols))


HEADER = """
#ifndef TYPES_H
#define TYPES_H
typedef struct Pad Pad;
typedef enum Level { Level_A, Level_B } Level;
struct Pad { u32 button; s8 x; s8 y; f32 scale; u8 err; };
struct Rules {
    u32 kind : 3;
    u32 timer : 1;
    u32 rest : 12;
    u8 x2;
    u8 teams;
    u16 stage;
    void (*on_pause)(int);
    HSD_GObj* gobj;
    u8 x10;
    u8 stocks;
    Level level;
    Pad pads[2];
    union { u32 raw; f32 value; } either;
    f32 x3C;
};
struct Wrong { u32 a; u8 x8; };
struct Disc { u32 a; } DISC_STRUCT;
struct HasDisc { struct Disc d; u32 x4; };
#ifdef MU_NATIVE
struct Changed { u64 a; };
#else
struct Changed { u32 a; };
#endif
#endif
"""


class SourcePortGeckoStructTest(unittest.TestCase):
    """Struct and enum typed objects: the console layout worked out from a header, and what each
    member becomes."""

    def setUp(self):
        self.types = gecko_targets.StructTypes(texts={"types.h": HEADER})

    def rows(self, ctype, size, dims=()):
        s = gecko_targets.Symbol("object", ".bss", 0x80500000, size)
        gecko_targets.classify_typed(s, self.types, ctype, list(dims), "object.c")
        return s

    def test_struct_of_numbers_is_one_row_with_its_padding(self):
        s = self.rows("Pad", 0x20, ["2"])
        self.assertEqual(s.kind, gecko_targets.OK, s.note)
        self.assertEqual([(r.name, r.size, r.layout) for r in s.rows], [("object", 0x20, "4111141111")])
        self.assertEqual(s.rows[0].decl, "Pad object[2]")

    def test_struct_with_pointers_is_walked(self):
        s = self.rows("struct Rules", 0x40)
        self.assertEqual(s.kind, gecko_targets.OK, s.note)
        t = gecko_targets
        got = [(r.name, r.addr - 0x80500000, r.size, r.kind, r.layout) for r in s.rows]
        self.assertEqual(got, [
            ("object.kind", 0x00, 2, t.LAYOUT, ""),                 # the bitfields: refused
            ("object.x2 to stage", 0x02, 4, t.OK, "112"),           # neighbours: one row
            ("object.on_pause", 0x08, 4, t.POINTERS, ""),
            ("object.gobj", 0x0C, 4, t.POINTERS, ""),
            ("object.x10 to stocks", 0x10, 2, t.OK, "11"),
            ("object.level", 0x14, 4, t.LAYOUT, ""),                # an enum member: refused
            ("object.pads", 0x18, 0x20, t.OK, "4111141111"),
            ("object.either", 0x38, 4, t.LAYOUT, ""),               # a union: refused
            ("object.x3C", 0x3C, 4, t.OK, "4"),
        ])
        run = s.rows[1]
        self.assertEqual((run.lvalue, [p.name for p in run.parts]),
                         ("object.x2", ["object.x2", "object.teams", "object.stage"]))

    def test_writes_follow_the_rows(self):
        rows = self.rows("struct Rules", 0x40).rows
        self.assertEqual(gecko_targets.check_write(rows, 0x80500002, 4), "")
        self.assertEqual(gecko_targets.check_write(rows, 0x80500004, 4), "writes past the end of object.x2 to stage")
        self.assertEqual(gecko_targets.check_write(rows, 0x80500008, 4), "writes object.on_pause, which holds pointers")
        self.assertEqual(gecko_targets.check_write(rows, 0x80500000, 1),
                         "writes object.kind, whose layout is not proven to match the console's")
        self.assertIn("not a game variable", gecko_targets.check_write(rows, 0x80500012, 1))   # padding
        memory = {"object.x2 to stage": bytearray(4)}
        gecko_targets.apply_write(rows, memory, 0x80500002, b"\x01\x02\x03\x04")
        self.assertEqual(memory["object.x2 to stage"], b"\x01\x02\x04\x03")   # two bytes, then a 16-bit field

    def test_size_that_differs_from_the_console_is_refused(self):
        s = self.rows("struct Rules", 0x44)
        self.assertEqual(s.kind, gecko_targets.LAYOUT)
        self.assertIn("SIZE MISMATCH", s.note)

    def test_member_named_after_another_offset_is_refused(self):
        s = self.rows("struct Wrong", 0x8)
        self.assertEqual(s.kind, gecko_targets.OK)   # all numbers: one row, the names are not walked
        wrong = gecko_targets.StructTypes(texts={"t.h": "struct W { void* p; u32 a; u8 x10; };"})
        s = gecko_targets.Symbol("object", ".bss", 0x80500000, 0xC)
        gecko_targets.classify_typed(s, wrong, "struct W", [], "object.c")
        self.assertEqual(s.kind, gecko_targets.LAYOUT)
        self.assertIn("member offsets not proven", s.note)

    def test_enum_object_is_a_32_bit_number(self):
        s = self.rows("Level", 4)
        self.assertEqual((s.kind, s.rows[0].layout), (gecko_targets.OK, "4"))
        self.assertEqual(self.rows("Level", 1).kind, gecko_targets.LAYOUT)

    def test_disc_data_and_conditional_types_are_not_offered(self):
        s = self.rows("struct HasDisc", 8)
        self.assertEqual([(r.name, r.kind) for r in s.rows],
                         [("object.d", gecko_targets.LAYOUT), ("object.x4", gecko_targets.OK)])
        self.assertEqual(self.rows("struct Disc", 4).kind, gecko_targets.LAYOUT)
        s = self.rows("struct Changed", 4)
        self.assertEqual(s.kind, gecko_targets.LAYOUT)
        self.assertIn("not worked out", s.note)

    def test_generated_shim_asserts_each_member_and_the_run(self):
        s = self.rows("struct Rules", 0x40)
        for r in s.rows:
            r.includes = ["types.h"]
        text = gecko_targets.game_table(s.rows)
        self.assertIn("#include <types.h>", text)
        self.assertIn("MU_GECKO_OBJECT(struct Rules object, object.teams, 0x1)", text)
        self.assertIn("_Static_assert(__builtin_offsetof(struct Rules, stage) + sizeof(object.stage) - "
                      "__builtin_offsetof(struct Rules, x2) == 0x4,", text)
        self.assertIn('MU_GECKO_TARGET(0x80500002u, 0x4u, object.x2, "112")', text)
        self.assertNotIn("on_pause", text)

    def test_string_size_is_counted_in_the_games_character_set(self):
        self.assertEqual(gecko_targets.string_size("PlMrNr.dat"), 11)
        self.assertEqual(gecko_targets.string_size("a\\n\\x41\\0"), 5)
        self.assertEqual(gecko_targets.string_size("\u3042"), 3)   # one two-byte character
        self.assertIsNone(gecko_targets.string_size("\\q"))


class SourcePortGeckoBuiltInTest(unittest.TestCase):
    """A code made only of patches the Source Port carries as C is built in."""

    def setUp(self):
        hook = [(0xC2100010, 2), (0x38600001, 0x38800002), (0x60000000, 0)]
        self.labels, self.units = ["Switch"], {}
        for unit in gecko_targets.code_units([(0x04100020, 0x38600001)] + hook):
            self.units[gecko_targets.unit_key(unit)] = 0
        self.hook = hook

    def label(self, lines):
        return gecko_targets.built_in_label(lines, self.labels, self.units)

    def test_exact_words_match(self):
        self.assertEqual(self.label(self.hook), "Switch")
        self.assertEqual(self.label(self.hook + [(0x04100020, 0x38600001), (0xE0000000, 0x80008000)]), "Switch")

    def test_any_other_word_does_not(self):
        self.assertIsNone(self.label([(0x04100020, 0x38600002)]))
        self.assertIsNone(self.label([self.hook[0], (0x38600001, 0x38800003), self.hook[2]]))
        self.assertIsNone(self.label(self.hook + [(0x04100024, 0x60000000)]))   # one patch more
        self.assertIsNone(self.label(self.hook[:2]))                            # cut short

    def test_single_instruction_injection_is_the_write(self):
        self.assertEqual(self.label([(0xC2100020, 1), (0x38600001, 0)]), "Switch")
        # A relative branch in a cave goes somewhere else than the same word written in place.
        units = {gecko_targets.unit_key([(0x04100020, 0x48000010)]): 0}
        self.assertIsNone(gecko_targets.built_in_label([(0xC2100020, 1), (0x48000010, 0)], self.labels, units))

    def test_real_table_knows_the_shipped_codes(self):
        if not gecko_targets.BUILT_IN_INI.exists():
            self.skipTest("the code list is not in this tree")
        labels, units = gecko_targets.built_in_units()
        codes = dict(gecko_targets.read_code_list(gecko_targets.BUILT_IN_INI.read_text(encoding="utf-8", errors="replace")))
        self.assertEqual(gecko_targets.built_in_label(codes["Optional: Disable Screen Shake"], labels, units),
                         "Disable Screen Shake")
        self.assertEqual(gecko_targets.built_in_label(codes["Required: General Codes"], labels, units), "General Codes")
        self.assertIsNone(gecko_targets.built_in_label(codes["Optional: Center Align 2P HUD"], labels, units))


if __name__ == "__main__":
    unittest.main()
