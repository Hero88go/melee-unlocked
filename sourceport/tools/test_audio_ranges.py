import importlib.util
from pathlib import Path
import unittest


path = Path(__file__).resolve().parents[2] / 'tools/snapshot_audio_ranges.py'
spec = importlib.util.spec_from_file_location('audio_ranges', path)
audio_ranges = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audio_ranges)


class AudioMapTest(unittest.TestCase):
    def test_pe_and_elf_objects_with_spaces(self):
        lines = [' .bss 0x82801000 0x20 C:/Users/Some Name/synth.c.obj\n',
                 ' .data 0x82802000 0x10 CMakeFiles/AXVPB.c.o\n']
        self.assertEqual(list(audio_ranges.audio_ranges(lines, 0x82800000)),
                         [(0x1000, 0x20, 'bss', 'synth.c.obj'),
                          (0x2000, 0x10, 'data', 'AXVPB.c.obj')])

    def test_wrapped_elf_pointer_data(self):
        lines = [' .data.rel.local\n', '                0x82802000 0x18 object/axdriver.c.o\n']
        self.assertEqual(list(audio_ranges.audio_ranges(lines, 0x82800000)),
                         [(0x2000, 0x18, 'data', 'axdriver.c.obj')])

    def test_ignores_discarded_input_sections(self):
        self.assertEqual(list(audio_ranges.audio_ranges([' .bss 0x0 0x20 synth.c.o\n'], 0x82800000)), [])


if __name__ == '__main__':
    unittest.main()
