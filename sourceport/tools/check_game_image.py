#!/usr/bin/env python3
"""Reject game libraries that cannot occupy the console-compatible image window."""
import argparse
from pathlib import Path
import struct


def image_range(data: bytes) -> tuple[int, int]:
    if data[:4] == b'\x7fELF':
        if data[4:6] != b'\x02\x01':
            raise ValueError('the native game must be a little-endian ELF64 library')
        header = struct.unpack_from('<16sHHIQQQIHHHHHH', data)
        if header[1:3] != (3, 62):
            raise ValueError('the native game must be an x86-64 ELF shared object')
        phoff, phentsize, phnum = header[5], header[9], header[10]
        if phentsize < 56:
            raise ValueError('invalid ELF program header size')
        loads = []
        for index in range(phnum):
            ph = struct.unpack_from('<IIQQQQQQ', data, phoff + index * phentsize)
            if ph[0] == 1:
                loads.append((ph[3], ph[3] + ph[6]))
        if not loads:
            raise ValueError('ELF library has no loadable segments')
        return min(start for start, _ in loads), max(end for _, end in loads)
    if data[:2] == b'MZ':
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        if data[pe:pe + 4] != b'PE\0\0':
            raise ValueError('invalid PE signature')
        optional = pe + 24
        if struct.unpack_from('<H', data, optional)[0] != 0x20b:
            raise ValueError('the native game must be a PE32+ library')
        base = struct.unpack_from('<Q', data, optional + 24)[0]
        size = struct.unpack_from('<I', data, optional + 56)[0]
        return base, base + size
    raise ValueError('unrecognized game library format')


def check(data: bytes, base: int, limit: int = 0x84000000) -> tuple[int, int]:
    start, end = image_range(data)
    if start != base:
        raise ValueError(f'game image base is 0x{start:x}; required 0x{base:x}')
    if end <= start or end > limit:
        raise ValueError(f'game image ends at 0x{end:x}; required below 0x{limit:x}')
    return start, end


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--base', type=lambda value: int(value, 0), required=True)
    args = parser.parse_args()
    try:
        start, end = check(args.image.read_bytes(), args.base)
    except (ValueError, struct.error) as error:
        parser.exit(1, f'check_game_image: {error}\n')
    print(f'check_game_image: 0x{start:x}..0x{end:x} fits the native game window')


if __name__ == '__main__':
    main()
