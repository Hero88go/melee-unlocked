import importlib.util
from pathlib import Path
import struct
import unittest


spec = importlib.util.spec_from_file_location('image_check', Path(__file__).with_name('check_game_image.py'))
image_check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(image_check)


def elf(base=0x82800000, size=0x1000):
    ident = b'\x7fELF\x02\x01' + b'\0' * 10
    header = struct.pack('<16sHHIQQQIHHHHHH', ident, 3, 62, 1, 0, 64, 0, 0, 64, 56, 1, 0, 0, 0)
    return header + struct.pack('<IIQQQQQQ', 1, 6, 0, base, base, 64, size, 4096)


class GameImageCheckTest(unittest.TestCase):
    def test_fixed_elf_image(self):
        self.assertEqual(image_check.check(elf(), 0x82800000), (0x82800000, 0x82801000))

    def test_rejects_normal_high_address_shared_library(self):
        with self.assertRaisesRegex(ValueError, 'base'):
            image_check.check(elf(base=0), 0x82800000)

    def test_rejects_image_overflow(self):
        with self.assertRaisesRegex(ValueError, 'ends'):
            image_check.check(elf(size=0x2000000), 0x82800000)

    def test_rejects_wrong_architecture(self):
        data = bytearray(elf())
        struct.pack_into('<H', data, 18, 183)
        with self.assertRaisesRegex(ValueError, 'x86-64'):
            image_check.check(data, 0x82800000)

    def test_existing_pe_image(self):
        data = bytearray(512)
        data[:2] = b'MZ'
        struct.pack_into('<I', data, 0x3c, 128)
        data[128:132] = b'PE\0\0'
        struct.pack_into('<H', data, 152, 0x20b)
        struct.pack_into('<Q', data, 176, 0x82800000)
        struct.pack_into('<I', data, 208, 0x1000)
        self.assertEqual(image_check.check(data, 0x82800000), (0x82800000, 0x82801000))


if __name__ == '__main__':
    unittest.main()
