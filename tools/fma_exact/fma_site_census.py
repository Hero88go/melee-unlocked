#!/usr/bin/env python3
"""Per-function census of fused multiply-adds: retail game (from the Legacy translation, one line per
PowerPC instruction) versus the native game DLL (FMA3 instructions per function, by symbol).

A function where the retail code fuses more (or less) than the native build is a candidate for the
last 1-ULP differences: the console compiler contracted a*b+c across statements or skipped a
contraction that GCC's -ffp-contract=on made.

    python tools/fma_exact/fma_site_census.py [--dll build-sourceport-gcc/melee_game.dll] [--top 40]
"""
import argparse
import collections
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OBJDUMP = r'C:/Users/Chandler/toolchains/winlibs-gcc-15.3/mingw64/bin/objdump.exe'
FUSED = re.compile(r'ppc::(fn?m(?:add|sub))\(')


def retail_counts():
    counts, cur = collections.Counter(), None
    single = collections.Counter()
    for f in sorted((ROOT / 'port/generated_vanilla').glob('guest_*.cpp')):
        for line in f.read_text(errors='replace').splitlines():
            m = re.match(r'void f_([0-9A-F]{8})\(', line)
            if m:
                cur = int(m.group(1), 16)
                continue
            if cur is not None and FUSED.search(line):
                counts[cur] += 1
                if 'ppc::fs(' in line:
                    single[cur] += 1
    return counts, single


def symbol_addresses():
    """decomp symbol name -> retail address, from config/GALE01/symbols.txt."""
    out = {}
    sym = ROOT / 'sourceport/extern/melee/config/GALE01/symbols.txt'
    for line in sym.read_text(errors='replace').splitlines():
        m = re.match(r'(\S+) = \.text:0x([0-9A-Fa-f]{8});', line)
        if m:
            out[m.group(1)] = int(m.group(2), 16)
    return out


def native_counts(dll):
    counts, cur = collections.Counter(), None
    dis = subprocess.run([OBJDUMP, '-d', '--no-show-raw-insn', str(dll)], capture_output=True, text=True).stdout
    for line in dis.splitlines():
        m = re.match(r'^[0-9a-f]+ <([^>+]+)>:', line)
        if m:
            cur = m.group(1)
            continue
        if cur and re.search(r'\svf(n?)m(add|sub)\d{3}s[sd]\s', line):
            counts[cur] += 1
        if cur and re.search(r'vmul[sp]d\s+%[xy]mm5,%[xy]mm4,%[xy]mm4', line):
            counts[cur] += 1   # rewritten site (gcc_fma_wrap.py): scratch regs are reserved
    return counts


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--dll', type=Path, default=ROOT / 'build-sourceport-gcc/melee_game.dll')
    ap.add_argument('--top', type=int, default=40)
    a = ap.parse_args()
    retail, retail_single = retail_counts()
    addr = symbol_addresses()
    native = native_counts(a.dll)
    by_addr = collections.Counter()
    for name, n in native.items():
        base = name.split('.')[0]
        if base in addr:
            by_addr[addr[base]] += n
    rows = []
    for fn in set(retail) | set(by_addr):
        r, n = retail[fn], by_addr[fn]
        if r != n:
            rows.append((r - n, fn, r, retail_single[fn], n))
    rows.sort(reverse=True)
    names = {v: k for k, v in addr.items()}
    print(f'retail fused ops {sum(retail.values())} in {len(retail)} functions; native {sum(by_addr.values())} '
          f'in {len(by_addr)} mapped functions; {len(rows)} functions differ')
    for d, fn, r, rs, n in rows[:a.top]:
        print(f'{fn:08X} {names.get(fn, "?"):40s} retail {r:3d} (single {rs:3d})  native {n:3d}  diff {d:+d}')


if __name__ == '__main__':
    main()
