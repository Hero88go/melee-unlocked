#!/usr/bin/env python3
"""Checks MeleeUnlocked-<version>-macos.zip before it is uploaded (tools/ci/build-macos.sh bundle).

- One top-level entry, MeleeUnlocked.app, with an x86_64 executable and Info.plist naming <version>.
- Nothing from the player's disc: no disc images or DOL files, and none of the Slippi Sys files the game
  rebuilds from the player's disc (port/sys_deltas/manifest.json), the same exclusion the Windows and
  Linux packagers apply.
- No crash dumps, logs, memory card saves, replays, debug files, Finder metadata or symlinks.
- No build-machine paths: every file is searched for each --forbid string (the checkout, the home folder).
- The game library loads on the Mac: no Linux stack protector, and every strong import is one the Mac ELF
  loader lists (port/mac/elf_loader.cpp).

    python3 tools/ci/verify_macos_package.py <zip> --version <version> [--forbid <path> ...]
"""
import argparse
import json
import plistlib
import re
import stat
import struct
import sys
import zipfile
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[2]
APP = 'MeleeUnlocked.app'
DISC_SUFFIXES = {'.iso', '.gcm', '.ciso', '.gcz', '.rvz', '.wia', '.wbfs', '.nkit', '.dol', '.tgc'}
PRIVATE_SUFFIXES = {'.dmp', '.mdmp', '.crash', '.ips', '.log', '.dbg', '.dsym', '.gci', '.sav', '.slp', '.slpz'}
PRIVATE_NAMES = {'.DS_Store', 'settings.ini', 'launcher.ini'}


def elf_strong_imports(data):
    """Undefined GLOBAL symbols of an x86-64 ELF's .dynsym (weak ones may stay unresolved)."""
    if data[:4] != b'\x7fELF' or data[4] != 2 or data[5] != 1:
        raise ValueError('not a 64-bit little-endian ELF')
    if struct.unpack_from('<H', data, 18)[0] != 62:
        raise ValueError('not an x86-64 ELF')
    shoff, = struct.unpack_from('<Q', data, 0x28)
    shentsize, shnum = struct.unpack_from('<HH', data, 0x3A)
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + i * shentsize) for i in range(shnum)]
    imports = set()
    for _name, kind, _flags, _addr, offset, size, link, _info, _align, entsize in sections:
        if kind != 11:  # SHT_DYNSYM
            continue
        strtab = sections[link]
        for at in range(offset + entsize, offset + size, entsize):  # entry 0 is the null symbol
            st_name, st_info, _other, st_shndx = struct.unpack_from('<IBBH', data, at)
            if st_shndx == 0 and st_info >> 4 == 1:  # SHN_UNDEF, STB_GLOBAL
                start = strtab[4] + st_name
                imports.add(data[start:data.index(b'\0', start)].decode())
    return imports


def loader_table():
    source = (ROOT / 'port/mac/elf_loader.cpp').read_text(encoding='utf-8')
    table = source[source.index('static const Import table[]'):]
    table = table[:table.index('};')]
    return set(re.findall(r'IMPORT\((\w+)\)', table)) | set(re.findall(r'\{"(\w+)"', table))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('zip', type=Path)
    parser.add_argument('--version', required=True)
    parser.add_argument('--forbid', action='append', default=[], help='a string no file may contain (repeatable)')
    args = parser.parse_args()
    expected = f'MeleeUnlocked-{args.version}-macos.zip'
    problems = []
    if args.zip.name != expected:
        problems.append(f'zip is named {args.zip.name}, expected {expected}')
    disc_derived = {f['target'] for f in json.loads((ROOT / 'port/sys_deltas/manifest.json').read_text())['files']}
    forbidden = [s.encode() for s in args.forbid if len(s) > 1]
    contents = f'{APP}/Contents/'
    with zipfile.ZipFile(args.zip) as archive:
        infos = archive.infolist()
        files = {i.filename: i for i in infos if not i.is_dir()}
        tops = {PurePosixPath(i.filename).parts[0] for i in infos}
        if tops != {APP}:
            problems.append(f'top-level entries {sorted(tops)}, expected only {APP}')
        for info in infos:
            path = PurePosixPath(info.filename)
            if stat.S_ISLNK(info.external_attr >> 16):
                problems.append(f'symlink: {info.filename}')
            if '__MACOSX' in path.parts or path.name.startswith('._'):
                problems.append(f'Finder metadata: {info.filename}')
            if info.is_dir():
                continue
            suffix = path.suffix.lower()
            if suffix in DISC_SUFFIXES:
                problems.append(f'disc image or DOL: {info.filename}')
            if suffix in PRIVATE_SUFFIXES or path.name in PRIVATE_NAMES:
                problems.append(f'dump, log, save, replay or debug file: {info.filename}')
            if path.name in disc_derived:
                problems.append(f'disc-derived Sys file: {info.filename}')
            data = archive.read(info)
            for needle in forbidden:
                if needle in data:
                    problems.append(f'build-machine path {needle.decode()!r} in {info.filename}')
        exe = files.get(contents + 'MacOS/MeleeUnlocked')
        if exe is None:
            problems.append('missing Contents/MacOS/MeleeUnlocked')
        else:
            header = archive.read(exe)[:8]
            if header[:4] != b'\xcf\xfa\xed\xfe' or struct.unpack('<i', header[4:8])[0] != 0x01000007:
                problems.append('Contents/MacOS/MeleeUnlocked is not a thin x86_64 Mach-O executable')
            elif not exe.external_attr >> 16 & 0o111:
                problems.append('Contents/MacOS/MeleeUnlocked is not executable')
        plist = files.get(contents + 'Info.plist')
        if plist is None:
            problems.append('missing Contents/Info.plist')
        else:
            info = plistlib.loads(archive.read(plist))
            if info.get('CFBundleShortVersionString') != args.version or info.get('CFBundleExecutable') != 'MeleeUnlocked':
                problems.append(f'Info.plist says {info.get("CFBundleExecutable")} {info.get("CFBundleShortVersionString")}')
        if contents + '_CodeSignature/CodeResources' not in files:
            problems.append('the bundle is not signed (no _CodeSignature/CodeResources)')
        game = files.get(contents + 'Resources/melee_game.so')
        if game is None:
            problems.append('missing Contents/Resources/melee_game.so')
        else:
            imports = elf_strong_imports(archive.read(game))
            if '__stack_chk_fail' in imports:
                problems.append('melee_game.so uses the Linux stack protector; build it with -fno-stack-protector')
            unlisted = sorted(imports - loader_table() - {'__stack_chk_fail'})
            if unlisted:
                problems.append('melee_game.so imports what the Mac ELF loader does not provide: ' + ', '.join(unlisted))
    if problems:
        print(f'{args.zip}: {len(problems)} problem(s)')
        for problem in problems:
            print('  ' + problem)
        sys.exit(1)
    size = args.zip.stat().st_size
    print(f'{args.zip}: OK ({len(files)} files, {size / 1e6:.1f} MB)')


if __name__ == '__main__':
    main()
