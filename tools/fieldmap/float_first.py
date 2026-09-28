#!/usr/bin/env python3
"""First frames where any Fighter float differs between a Legacy and a Source dump (see field_diff.py).
Skips float slots that hold console addresses on both sides (motion-var unions keep pointers there).

    python tools/fieldmap/float_first.py legacy.bin source.bin [--frames 6] [--from N]
"""
import argparse
import json
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import field_diff as fd  # noqa: E402


def is_addr(b):
    v = int.from_bytes(b, 'big')
    return 0x80000000 <= v < 0x81800000


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('legacy')
    ap.add_argument('source')
    ap.add_argument('--frames', type=int, default=6)
    ap.add_argument('--from', dest='start', type=int, default=-1000)
    a = ap.parse_args()
    fmap = json.load(open(HERE / 'fighter_map.json'))['fields']
    ptr = set()
    for f in fmap:
        if f['kind'] in ('ptr', 'discptr'):
            ptr.update(range(f['coff'], f['coff'] + f['csize']))
    fl = [f for f in fmap if f['kind'] == 'flt' and f['csize'] == 4
          and not any(b in ptr for b in range(f['coff'], f['coff'] + 4))]
    L, S = fd.records(a.legacy), fd.records(a.source)
    shown = 0
    for k in sorted(set(L) & set(S)):
        if k[0] < a.start:
            continue
        d = []
        for f in fl:
            c = L[k][f['coff']:f['coff'] + 4]
            raw = S[k][f['noff']:f['noff'] + 4]
            n = raw if c == raw else raw[::-1]      # native storage is host order, or big-endian for disc views
            if c == n or (is_addr(c) and is_addr(n)):
                continue
            d.append((f['path'], struct.unpack('>f', c)[0], struct.unpack('>f', n)[0], c.hex(), n.hex()))
        if d:
            print(f'frame {k[0]} port {k[1]} follower {k[2]}: {len(d)} floats differ')
            for p, lv, sv, lh, sh in d[:14]:
                print(f'    {p:48s} legacy {lv:>14.9g} ({lh})  source {sv:>14.9g} ({sh})')
            shown += 1
            if shown >= a.frames:
                break


if __name__ == '__main__':
    main()
