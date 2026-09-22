"""Regression checks for the GCT layout exported to Slippi recordings."""

import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "recomp"))
import gecko  # noqa: E402


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

        table, optional_offset, port_offset = gecko.generate_gct([port, optional, normal])

        self.assertEqual(optional_offset, 16)
        self.assertEqual(port_offset, 24)
        self.assertEqual(table[8:16], struct.pack(">II", 0x04000010, 1))
        self.assertEqual(table[16:24], struct.pack(">II", 0x04000020, 2))
        self.assertEqual(table[24:40], struct.pack(">IIII", 0xC2000030, 0, 0x60000000, 0))
        self.assertEqual(table[-8:], struct.pack(">II", 0xFF000000, 0))

        # EVENT_GECKO_LIST omits the eight-byte GCT header. Replacing the first word at this
        # adjusted boundary with FF000000 hides every host-gated code without changing event size.
        event = bytearray(table[8:])
        event_offset = port_offset - 8
        event[event_offset:event_offset + 8] = struct.pack(">II", 0xFF000000, 0)
        self.assertEqual(event[event_offset:event_offset + 8], struct.pack(">II", 0xFF000000, 0))
        self.assertNotIn(struct.pack(">II", 0xC2000030, 0), event[:event_offset + 8])


if __name__ == "__main__":
    unittest.main()
