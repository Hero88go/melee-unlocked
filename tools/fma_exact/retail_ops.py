#!/usr/bin/env python3
"""Print the console's float operations for one function (from port/generated_vanilla), skipping the
inline sqrtf refinement, with the address of each instruction.

    python tools/fma_exact/retail_ops.py lbVector_CosAngle [--all]
"""
import argparse
import glob
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('name')
    ap.add_argument('--all', action='store_true', help='also loads, stores, compares and branches')
    a = ap.parse_args()
    addr = None
    for line in open(ROOT / 'sourceport/extern/melee/config/GALE01/symbols.txt'):
        m = re.match(re.escape(a.name) + r' = \.text:0x([0-9A-F]+)', line)
        if m:
            addr = m.group(1)
            break
    if not addr:
        raise SystemExit('no such function')
    for f in glob.glob(str(ROOT / 'port/generated_vanilla/guest_*.cpp')):
        text = open(f, errors='replace').read()
        i = text.find('void f_%s(' % addr)
        if i < 0:
            continue
        body = text[i:text.find('\n}\n', i)]
        in_sqrt = 0
        for line in body.splitlines():
            s = line.strip()
            if 'frsqrte' in s:
                in_sqrt = 12
                print('    [inline sqrtf]')
                continue
            if in_sqrt:
                in_sqrt -= 1
                if 'ppc::fs(' in s:
                    in_sqrt = 0
                continue
            op = ('ps0 = ' in s and 'float_bits_to_double(ppc::ld32' not in s)
            if op or (a.all and re.search(r'fcmp|goto|st32|ld32|^L_|c\.lr = ', s)):
                print(s[:140])
        return
    raise SystemExit('function body not found')


if __name__ == '__main__':
    main()
