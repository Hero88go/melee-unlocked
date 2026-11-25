#!/usr/bin/env python3
"""Checks that the Mac builds compute the same game state as upstream's own Linux release.

The Linux release's melee_game.so is built by upstream with Ubuntu's GCC 14 and the stack protector;
the Mac builds use GCC 15.2 without it. This script loads the release library itself in the x86-64
Mac host and runs every port/scripts/parity_*.txt on it and on the Mac apps, then compares their state
digests match frame by match frame. Every app gets a throwaway profile.

    python3 port/mac/release_parity.py --iso path/to/melee.iso
    python3 port/mac/release_parity.py --iso ... --release path/to/MeleeUnlocked-1.0.14-linux-x86_64.tar.gz
    python3 port/mac/release_parity.py --iso ... --scripts parity_vs parity_lcancel

Without --release it downloads the Linux release named by VERSION from upstream's GitHub releases.

The Mac loader refuses stack-protector code, so the script loads a copy of the release library made
loadable for this test only. Each `mov %fs:0x28,reg` (the canary load) becomes `mov $0,reg32`, and
each `sub %fs:0x28,reg` (the check) becomes `test reg,reg`. The canary is then the constant 0, and the
`__stack_chk_fail` import is renamed `free`, since only a smashed stack calls it. The canary does not
take part in the game's arithmetic. The loader still never patches code, and the copy never ships.
"""
import argparse
import concurrent.futures
import os
import shutil
import struct
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'port/mac'))
sys.path.insert(0, str(ROOT / 'tools'))
from compare_digest import compare  # noqa: E402
from melee_iso import require_iso  # noqa: E402

UPSTREAM = 'https://github.com/Hero88go/melee-unlocked/releases/download'
WORK = ROOT / 'build-mac-release-ref'
APPS = {'mac-x86_64': ROOT / 'build-mac-full-squad/MeleeUnlocked.app',
        'mac-arm64': ROOT / 'build-mac-full-arm64-squad/MeleeUnlocked.app'}


def zero_canaries(data):
    """The canary rewrite described above, on a Linux x86-64 ELF image. Returns (loads, checks)."""
    b = data
    if b[:4] != b'\x7fELF' or b[4] != 2 or struct.unpack_from('<H', b, 18)[0] != 62:
        raise SystemExit('not a 64-bit x86-64 ELF')
    shoff, = struct.unpack_from('<Q', b, 40)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', b, 58)
    sections = [struct.unpack_from('<IIQQQQ', b, shoff + i * shentsize) for i in range(shnum)]
    names = sections[shstrndx][4]

    def name(section):
        return bytes(b[names + section[0]:b.index(0, names + section[0])]).decode()
    loads = checks = 0
    for section in sections:
        if not section[2] & 4:   # SHF_EXECINSTR
            continue
        at, end = section[4], section[4] + section[5]
        while (at := b.find(b'\x64', at, end)) >= 0:
            rex, op, modrm = b[at + 1], b[at + 2], b[at + 3]
            if rex in (0x48, 0x4C) and op in (0x8B, 0x2B) and modrm & 0xC7 == 0x04 and b[at + 4:at + 9] == b'\x25\x28\0\0\0':
                reg = (modrm >> 3 & 7) + (8 if rex == 0x4C else 0)
                if op == 0x8B:   # mov $0, reg32 (flags untouched)
                    new = (b'\x41' if reg >= 8 else b'') + bytes([0xB8 + (reg & 7)]) + b'\0\0\0\0'
                    loads += 1
                else:            # test reg, reg: ZF set exactly when the stored canary is still 0
                    new = bytes([0x4D if reg >= 8 else 0x48, 0x85, 0xC0 | (reg & 7) << 3 | reg & 7])
                    checks += 1
                b[at:at + 9] = new + b'\x90' * (9 - len(new))
                at += 9
            else:
                at += 1
    dynstr = next(s for s in sections if name(s) == '.dynstr')
    at = b.find(b'\0__stack_chk_fail\0', dynstr[4], dynstr[4] + dynstr[5])
    if at > 0:
        b[at + 1:at + 17] = b'free'.ljust(16, b'\0')
    if not loads or loads > checks:
        raise SystemExit(f'canary code not recognised ({loads} loads, {checks} checks)')
    return loads, checks


def reference_app(release):
    """The x86-64 Mac app with the release's game library (made loadable) and its exclusions."""
    version = (ROOT / 'VERSION').read_text().strip()
    WORK.mkdir(exist_ok=True)
    if release is None:
        release = WORK / f'MeleeUnlocked-{version}-linux-x86_64.tar.gz'
        if not release.is_file():
            url = f'{UPSTREAM}/v{version}/{release.name}'
            print('downloading', url)
            with urllib.request.urlopen(url) as response, open(str(release) + '.part', 'wb') as out:
                shutil.copyfileobj(response, out)
            os.replace(str(release) + '.part', release)
    files = {}
    with tarfile.open(release) as tar:
        for member in tar.getmembers():
            if member.isfile() and Path(member.name).name in ('melee_game.so', 'melee_game.snapexcl'):
                files[Path(member.name).name] = tar.extractfile(member).read()
    if set(files) != {'melee_game.so', 'melee_game.snapexcl'}:
        raise SystemExit(f'{release}: no melee_game.so and melee_game.snapexcl')
    host = APPS['mac-x86_64']
    if not (host / 'Contents/MacOS/MeleeUnlocked').is_file():
        raise SystemExit(f'the reference runs in the x86-64 host: build it first (port/mac/build_all.sh <iso>)')
    app = WORK / 'MeleeUnlocked.app'
    shutil.rmtree(app, ignore_errors=True)
    shutil.copytree(host, app)
    library = bytearray(files['melee_game.so'])
    loads, checks = zero_canaries(library)
    (app / 'Contents/Resources/melee_game.so').write_bytes(library)
    (app / 'Contents/Resources/melee_game.snapexcl').write_bytes(files['melee_game.snapexcl'])
    print(f'reference: {release.name}, {loads} canary loads and {checks} checks set to 0')
    return app


def run(app, label, script, iso, frames):
    home = WORK / 'runs' / f'{script.stem}-{label}'
    shutil.rmtree(home, ignore_errors=True)
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', MELEE_NO_GC_ADAPTER='1')
    for key, sub in (('HOME', 'home'), ('XDG_DATA_HOME', 'data'), ('XDG_CONFIG_HOME', 'config'),
                     ('XDG_CACHE_HOME', 'cache'), ('XDG_STATE_HOME', 'state'), (None, 'user')):
        (home / sub).mkdir(parents=True)
        if key:
            env[key] = str(home / sub)
    cmd = [str(app / 'Contents/MacOS/MeleeUnlocked'), '--iso', str(iso), '--headless', '--fast', '--vanilla-game',
           '--vanilla-only', '--frames', str(frames), '--volume', '0', '--rng-seed', '0x12345678', '--time-base', '0',
           '--script', str(script), '--user-dir', str(home / 'user'), '--state-digest', str(home / 'digest.csv')]
    with open(home / 'console.log', 'w') as log:
        code = subprocess.run(cmd, cwd=home, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=1200).returncode
    return code, home / 'digest.csv'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--iso')
    ap.add_argument('--release', type=Path, help='the Linux release tarball (default: download VERSION\'s)')
    ap.add_argument('--scripts', nargs='*', help='port/scripts names (default: every parity_*.txt)')
    ap.add_argument('--frames', type=int, default=9000)
    args = ap.parse_args()
    iso = require_iso(args.iso)
    scripts = [ROOT / f'port/scripts/{name}.txt' for name in args.scripts] if args.scripts else \
        sorted((ROOT / 'port/scripts').glob('parity_*.txt'))
    apps = {'linux-release': reference_app(args.release)}
    apps.update({label: app for label, app in APPS.items() if (app / 'Contents/MacOS/MeleeUnlocked').is_file()})
    failed = False
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(apps)) as pool:
        for script in scripts:
            runs = {label: pool.submit(run, app, label, script, iso, args.frames) for label, app in apps.items()}
            results = {label: future.result() for label, future in runs.items()}
            for label, (code, _) in results.items():
                if code:
                    print(f'{script.stem}: {label} exited {code}')
                    failed = True
            reference_code, reference = results['linux-release']
            for label, (code, digest) in results.items():
                if label == 'linux-release' or code or reference_code:
                    continue
                r = compare(reference, digest)
                if not r['reference_match_frames']:   # an old script that no longer reaches a match
                    print(f'{script.stem}: no match reached, skipped')
                    break
                print(f'{script.stem}: linux-release vs {label}: {r["compared_frames"]} match frames x '
                      f'{r["fields_per_frame"]} fields, {r["differing_values"]} differing'
                      + ('' if r['passed'] else f'  FAIL {r["first_differences"][:3]}'))
                failed |= not r['passed']
    print('FAIL' if failed else 'PASS')
    return int(failed)


if __name__ == '__main__':
    sys.exit(main())
