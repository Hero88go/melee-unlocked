#!/usr/bin/env python3
"""Field-level Fighter comparison between a Legacy dump (console layout, big-endian) and a Source dump
(native layout), both written at the post-frame point with MELEE_DUMP_FIGHTERS / _OUT.

Prints, frame by frame, the first fields that differ for each fighter, so a 1-ULP position split can be
traced back to the first value that went different (and from its name, the function that writes it).

    python tools/fieldmap/field_diff.py legacy.bin source.bin [--map tools/fieldmap/fighter_map.json]
           [--limit 12] [--ignore regex]
"""
import argparse
import json
import re
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent


def records(path, bones=False):
    """Fighter records, or with bones=True the bone records (port | 0x80) keyed by the real port."""
    d = Path(path).read_bytes()
    i, out = 0, {}
    while i + 8 <= len(d):
        frame, port, follower, size = struct.unpack('<iBBH', d[i:i + 8])
        if bool(port & 0x80) == bones:
            out[(frame, port & 0x7F, follower)] = d[i + 8:i + 8 + size]
        i += 8 + size
    return out


def rev_bits(b):
    return bytes(int('{:08b}'.format(x)[::-1], 2) for x in b)


def equal(c, n, kind):
    """Console bytes vs native bytes of one field, allowing host byte order per element and the
    opposite bitfield allocation order."""
    if c == n:
        return True
    if kind == 'bits':
        return c == rev_bits(n) or c == rev_bits(n[::-1])
    if kind == 'arr':
        for w in (4, 2):
            if len(c) % w == 0 and all(c[i:i + w] in (n[i:i + w], n[i:i + w][::-1]) for i in range(0, len(c), w)):
                return True
        return False
    return c == n[::-1]


def as_value(b, kind, big):
    if kind == 'flt' and len(b) == 4:
        return struct.unpack('>f' if big else '<f', b)[0]
    if kind in ('int', 'bits') and len(b) in (1, 2, 4, 8):
        return int.from_bytes(b, 'big' if big else 'little')
    return b.hex()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('legacy')
    ap.add_argument('source')
    ap.add_argument('--map', default=str(HERE / 'fighter_map.json'))
    ap.add_argument('--limit', type=int, default=12)
    ap.add_argument('--ignore', default=r'^fp\.(gobj|x2C_|parts|ft_data|x10C|x8AC|dmg\.x1868|dmg\.x1870)')
    a = ap.parse_args()
    fmap = json.load(open(a.map))['fields']
    ptr_bytes = set()
    for f in fmap:
        if f['kind'] in ('ptr', 'discptr'):
            ptr_bytes.update(range(f['coff'], f['coff'] + f['csize']))
    fmap = [f for f in fmap if f['kind'] not in ('ptr', 'discptr')
            and not any(b in ptr_bytes for b in range(f['coff'], f['coff'] + f['csize']))]
    ign = re.compile(a.ignore) if a.ignore else None
    L, S = records(a.legacy), records(a.source)
    keys = sorted(set(L) & set(S))
    shown = 0
    for k in keys:
        cb, nb = L[k], S[k]
        diffs = []
        for f in fmap:
            if ign and ign.search(f['path']):
                continue
            c = cb[f['coff']:f['coff'] + f['csize']]
            if f['csize'] != f['nsize'] or len(c) != f['csize']:
                continue                       # size changed natively (pointers, host ints): skip
            n = nb[f['noff']:f['noff'] + f['nsize']]
            if equal(c, n, f['kind']):
                continue
            diffs.append((f['path'], as_value(c, f['kind'], True), as_value(n, f['kind'], False),
                          as_value(n, f['kind'], True)))
        if diffs:
            print(f'frame {k[0]} port {k[1]} follower {k[2]}: {len(diffs)} fields differ')
            for p, lv, sv, sv_be in diffs[:8]:
                print(f'    {p:55s} legacy {lv!r:>22}  source {sv!r:>22}')
            shown += 1
            if shown >= a.limit:
                break
    print(f'{len(keys)} fighter-frames compared')


if __name__ == '__main__':
    main()
