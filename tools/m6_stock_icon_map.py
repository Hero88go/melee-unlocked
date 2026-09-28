#!/usr/bin/env python3
"""M6: compare the stock icon's texture frame for every character, fighter variant and costume.

gm_80168B34 picks the HUD stock icon's frame from (character kind, fighter variant, costume); it is a
pure function, so the native and translated games can be compared over every input. The native table
comes from a Source run with MELEE_DUMP_STOCK_ICON_MAP set (lines "[stock-icon-map] c v k BITS" in its
log); the translated one from `port_native_parity --stock-icon-map`.

    python tools/m6_stock_icon_map.py native.log legacy.txt [--json out.json]
"""
import argparse, json, re, sys
from pathlib import Path

LINE = re.compile(r'\[stock-icon-map\] (\d+) (\d+) (\d+) ([0-9A-F]{8})')


def table(path):
    out = {}
    for line in Path(path).read_text(encoding='utf-8', errors='replace').splitlines():
        m = LINE.search(line)
        if m:
            out[(int(m.group(1)), int(m.group(2)), int(m.group(3)))] = m.group(4)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('native')
    ap.add_argument('legacy')
    ap.add_argument('--json', type=Path)
    a = ap.parse_args()
    nat, leg = table(a.native), table(a.legacy)
    keys = sorted(set(nat) | set(leg))
    diffs = [dict(ckind=k[0], variant=k[1], costume=k[2], native=nat.get(k), legacy=leg.get(k))
             for k in keys if nat.get(k) != leg.get(k)]
    result = dict(native_entries=len(nat), legacy_entries=len(leg), compared=len(keys), mismatches=len(diffs),
                  first_mismatches=diffs[:20])
    print(json.dumps({k: v for k, v in result.items() if k != 'first_mismatches'}))
    for d in diffs[:20]:
        print('mismatch', d)
    if a.json:
        a.json.write_text(json.dumps(result, indent=1) + '\n')
    return 1 if diffs or not keys or len(nat) != len(leg) else 0


if __name__ == '__main__':
    sys.exit(main())
