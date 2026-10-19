import importlib.util
from pathlib import Path
import unittest


spec = importlib.util.spec_from_file_location('overlay', Path(__file__).with_name('console_long_overlay.py'))
overlay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(overlay)


class ConsoleLongOverlayTest(unittest.TestCase):
    def test_console_widths_and_type_spelling(self):
        self.assertEqual(overlay.normalize(b'long a; unsigned long b; long int c; signed int long d;'),
                         b'int a; unsigned int b;  int c; signed int  d;')
        self.assertEqual(overlay.normalize(b'long unsigned int x; long /* width */ int y;'),
                         b' unsigned int x;  /* width */ int y;')

    def test_preserves_64_bit_types_and_floating_point(self):
        value = b'long long x; unsigned long long int y; long double z; 1.0L; 1e3L; 0x1.fp2L; 1LL; 1ULL;'
        self.assertEqual(overlay.normalize(value), value)

    def test_preserves_strings_comments_and_identifiers(self):
        value = b'"long and \\" long"; \'l\'; // long\n/* long */ int long_name;'
        self.assertEqual(overlay.normalize(value), value)

    def test_integer_suffix_semantics(self):
        self.assertEqual(overlay.normalize(b'1L + 0xffffffffUL + 123lu + 0b10L + 010L'),
                         b'1 + 0xffffffffU + 123u + 0b10 + 010')

    def test_idempotent(self):
        value = b'typedef unsigned long u32; long long ticks; long int sample = 123L;'
        first = overlay.normalize(value)
        self.assertEqual(overlay.normalize(first), first)


if __name__ == '__main__':
    unittest.main()
