#!/usr/bin/env python3
"""Per-function single-precision fused-op counts: console (retail translation) vs native site lists.

    python tools/fma_exact/count_compare.py <sites_a.txt> <sites_b.txt>
Prints how many functions match the console in each, which regressed from a to b, and the worst
remaining mismatches in b. Site lists come from objdump + addr2line (see tag_no_contract.py).
"""
import collections
import glob
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def console():
    sym = {}
    for l in open(ROOT / 'sourceport/extern/melee/config/GALE01/symbols.txt'):
        m = re.match(r'(\S+) = \.text:0x([0-9A-F]+)', l)
        if m:
            sym[m.group(1)] = m.group(2)
    single, cur = collections.Counter(), None
    for f in glob.glob(str(ROOT / 'port/generated_vanilla/guest_*.cpp')):
        for line in open(f, errors='replace'):
            m = re.match(r'void f_([0-9A-F]{8})\(', line)
            if m:
                cur = m.group(1)
                continue
            if cur and re.search(r'ppc::fn?m(add|sub)\(', line) and 'ppc::fs(' in line:
                single[cur] += 1
    return sym, single


def load(path):
    c = collections.Counter()
    for l in open(path, encoding='utf-8', errors='replace'):
        parts = l.split(' ', 3)
        if len(parts) < 4 or not parts[2].endswith('ss') or 'math_native' in parts[3] or 'sdk_math' in parts[3]:
            continue
        c[parts[1].strip('<>:').split('.')[0]] += 1
    return c


def main():
    sym, single = console()
    A, B = load(sys.argv[1]), load(sys.argv[2])
    fns = {f for f in (set(A) | set(B) | {f for f in sym if single[sym[f]]}) if f in sym}
    eqA = sum(A[f] == single[sym[f]] for f in fns)
    eqB = sum(B[f] == single[sym[f]] for f in fns)
    worse = [(f, single[sym[f]], A[f], B[f]) for f in fns if A[f] == single[sym[f]] != B[f]]
    still = [(f, single[sym[f]], A[f], B[f]) for f in fns if B[f] != single[sym[f]]]
    print('functions:', len(fns), '| equal to console: a', eqA, 'b', eqB)
    print('regressed a->b (%d):' % len(worse))
    for x in sorted(worse):
        print('  ', x)
    print('off in b (%d), worst first (name, console, a, b):' % len(still))
    for x in sorted(still, key=lambda x: -abs(x[1] - x[3]))[:45]:
        print('  ', x)


if __name__ == '__main__':
    main()
