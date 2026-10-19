#!/usr/bin/env python3
"""Load a native game and verify constructors kept every game address low."""
import argparse
import ctypes
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('library', type=Path)
    args = parser.parse_args()
    library = ctypes.CDLL(str(args.library.resolve()))
    for name in ('mu_game_entry', 'mu_user_gecko_read', 'mu_user_gecko_write'):
        address = ctypes.cast(getattr(library, name), ctypes.c_void_p).value
        if not 0x82800000 <= address < 0x84000000:
            parser.exit(1, f'{name} loaded outside the game window: 0x{address:x}\n')
        print(f'{name}: 0x{address:x}')
    if hasattr(library, 'mu_ak_native_build') and library.mu_ak_native_build() != 1:
        parser.exit(1, 'the native fighter registry is missing\n')


if __name__ == '__main__':
    main()
