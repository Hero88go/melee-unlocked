"""The Mac loader's import table covers what the game library calls, and the build checks it."""
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'port/mac'))
import elf_imports  # noqa: E402


def tiny_elf(symbols):
    """A minimal ELF64 with one .dynsym: (name, bind, defined) entries."""
    strtab = b'\0'
    entries = [b'\0' * 24]
    for name, bind, defined in symbols:
        offset = len(strtab)
        strtab += name.encode() + b'\0'
        entries.append(struct.pack('<IBBHQQ', offset, bind << 4 | 2, 0, 1 if defined else 0, 0, 0))
    dynsym = b''.join(entries)
    header_size = 64
    dynsym_at = header_size
    strtab_at = dynsym_at + len(dynsym)
    shoff = strtab_at + len(strtab)
    data = bytearray(shoff + 3 * 64)
    data[:4] = b'\x7fELF'
    data[4], data[5] = 2, 1
    struct.pack_into('<Q', data, 0x28, shoff)
    struct.pack_into('<HH', data, 0x3A, 64, 3)
    data[dynsym_at:strtab_at] = dynsym
    data[strtab_at:shoff] = strtab
    struct.pack_into('<IIQQQQIIQQ', data, shoff + 64, 0, 11, 0, 0, dynsym_at, len(dynsym), 2, 0, 8, 24)
    struct.pack_into('<IIQQQQIIQQ', data, shoff + 128, 0, 3, 0, 0, strtab_at, len(strtab), 0, 0, 1, 0)
    return bytes(data)


class MacElfImportsTest(unittest.TestCase):
    def test_table_binds_the_heap_and_strcat(self):
        table = elf_imports.loader_table()
        # The 0.9.990 game library imports these; without them it stopped at start on every Mac.
        for name in ['malloc', 'free', 'strcat', 'strchr', 'fwrite', 'memchr']:
            self.assertIn(name, table)

    def test_strong_imports_skip_weak_and_defined(self):
        data = tiny_elf([('free', 1, False), ('__cxa_finalize', 2, False), ('game_entry', 1, True)])
        self.assertEqual(elf_imports.strong_imports(data), {'free'})

    def test_build_refuses_an_unbound_import(self):
        build = (ROOT / 'port/mac/build_full.py').read_text()
        self.assertIn('from elf_imports import missing', build)
        self.assertIn('does not bind', build)


if __name__ == '__main__':
    unittest.main()
