import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "recomp"))
from dol import Dol

class DolValidation(unittest.TestCase):
    def load(self, data):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.dol"
            path.write_bytes(data)
            return Dol(path)

    def test_truncated_header(self):
        with self.assertRaises(ValueError): self.load(bytes(12))

    def test_section_spans(self):
        data = bytearray(0x120)
        struct.pack_into(">I", data, 0, 0x100)
        struct.pack_into(">I", data, 0x48, 0x80001000)
        struct.pack_into(">I", data, 0x90, 0x20)
        self.assertEqual(len(self.load(data).ram), 0x1800000)
        with self.assertRaises(ValueError): self.load(data[:-1])
        struct.pack_into(">I", data, 0, 0xffffffff)
        with self.assertRaises(ValueError): self.load(data)

if __name__ == "__main__": unittest.main()
