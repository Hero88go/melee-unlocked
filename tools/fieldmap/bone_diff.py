#!/usr/bin/env python3
"""First frames where a fighter bone differs between a Legacy and a Source dump (bone records from
MELEE_DUMP_FIGHTERS, 22 little-endian floats per part: rotate xyzw, scale, translate, world matrix).

    python tools/fieldmap/bone_diff.py legacy.bin source.bin [--frames 4]
"""
import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from field_diff import records  # noqa: E402

NAMES = ['rot.x', 'rot.y', 'rot.z', 'rot.w', 'scl.x', 'scl.y', 'scl.z', 'tr.x', 'tr.y', 'tr.z'] + \
        ['mtx[%d][%d]' % (r, c) for r in range(3) for c in range(4)]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('legacy')
    ap.add_argument('source')
    ap.add_argument('--frames', type=int, default=4)
    ap.add_argument('--no-mtx', action='store_true', help='ignore the world matrix (recomputed lazily, so it differs by timing)')
    a = ap.parse_args()
    leg, src = records(a.legacy, bones=True), records(a.source, bones=True)
    shown = 0
    for key in sorted(set(leg) & set(src)):
        l, s = leg[key], src[key]
        diffs = []
        for i in range(min(len(l), len(s)) // 4):
            if a.no_mtx and i % 22 >= 10:
                continue
            if l[4 * i:4 * i + 4] != s[4 * i:4 * i + 4]:
                lv, sv = struct.unpack('<f', l[4 * i:4 * i + 4])[0], struct.unpack('<f', s[4 * i:4 * i + 4])[0]
                diffs.append('    part %2d %-10s legacy %.9g (%s)  source %.9g (%s)' % (
                    i // 22, NAMES[i % 22], lv, l[4 * i:4 * i + 4][::-1].hex(), sv, s[4 * i:4 * i + 4][::-1].hex()))
        if diffs:
            print('frame %d port %d follower %d: %d bone floats differ' % (key[0], key[1], key[2], len(diffs)))
            print('\n'.join(diffs[:40]))
            shown += 1
            if shown >= a.frames:
                break
    if not shown:
        print('no bone differences in %d common records' % len(set(leg) & set(src)))


if __name__ == '__main__':
    main()
