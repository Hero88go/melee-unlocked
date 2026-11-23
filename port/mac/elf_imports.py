"""The Mac loader (elf_loader.cpp) binds the game library's imports from a fixed table. A library that
imports anything else fails at start on every Mac ("Mac ELF: unlisted strong import"), so the build checks
the two against each other before it packages anything."""
from pathlib import Path
import re
import struct

LOADER = Path(__file__).resolve().parent / 'elf_loader.cpp'


def loader_table(text=None):
    """Names the loader binds: IMPORT(name) entries and {"name", ...} entries of its table."""
    text = LOADER.read_text() if text is None else text
    body = text[text.index('static const Import table[]'):]
    body = body[:body.index('};')]
    return set(re.findall(r'IMPORT\((\w+)\)', body)) | set(re.findall(r'\{"(\w+)"', body))


def strong_imports(data):
    """Undefined, non-weak dynamic symbols of a little-endian ELF64 file."""
    if data[:4] != b'\x7fELF' or data[4] != 2 or data[5] != 1:
        raise ValueError('not a little-endian ELF64 file')
    shoff = struct.unpack_from('<Q', data, 0x28)[0]
    shentsize, shnum = struct.unpack_from('<HH', data, 0x3A)
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + i * shentsize) for i in range(shnum)]
    names = set()
    for sec in sections:
        if sec[1] != 11:   # SHT_DYNSYM
            continue
        strtab = sections[sec[6]][4]
        for i in range(sec[5] // 24):
            name, info, _other, shndx, _value, _size = struct.unpack_from('<IBBHQQ', data, sec[4] + i * 24)
            if shndx == 0 and name and info >> 4 == 1:   # undefined, STB_GLOBAL
                end = data.index(b'\0', strtab + name)
                names.add(data[strtab + name:end].decode())
    return names


def missing(library_path):
    """Strong imports of the library the loader cannot bind, sorted."""
    return sorted(strong_imports(Path(library_path).read_bytes()) - loader_table())
