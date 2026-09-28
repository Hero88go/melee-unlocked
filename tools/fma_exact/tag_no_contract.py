#!/usr/bin/env python3
"""Tag every decomp function where the console compiler fused no multiply-add, but GCC does, with
MU_NO_CONTRACT (fp-contract off for that function; defined in sourceport/game/include/mu_native.h).

Retail side: port/generated_vanilla (one line per PowerPC instruction). A function qualifies when it
has no single-precision fused op and no double-precision fused op other than fnmsub (the inline
sqrtf refinement, which the native build emulates with explicit fma calls the flag does not touch).
Native side: FMA3 instructions in the built DLL, mapped to source lines with addr2line; lines in
math_native.h / sdk_math.c are the emulation itself and do not count.

    python tools/fma_exact/tag_no_contract.py [--dry-run]
"""
import argparse
import collections
import glob
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BIN = Path(r'C:/Users/Chandler/toolchains/winlibs-gcc-15.3/mingw64/bin')
DLL = ROOT / 'build-sourceport-gcc/melee_game.dll'
DBG = ROOT / 'build-sourceport-gcc/melee_game.dbg'
SRC = ROOT / 'sourceport/extern/melee'
FUSED = re.compile(r'ppc::(fn?m(?:add|sub))\(')
TAG = 'MU_NO_CONTRACT '


def retail():
    single, other_double, cur = collections.Counter(), collections.Counter(), None
    for f in glob.glob(str(ROOT / 'port/generated_vanilla/guest_*.cpp')):
        for line in open(f, errors='replace'):
            m = re.match(r'void f_([0-9A-F]{8})\(', line)
            if m:
                cur = m.group(1)
                continue
            m = FUSED.search(line) if cur else None
            if not m:
                continue
            if 'ppc::fs(' in line:
                single[cur] += 1
            elif m.group(1) != 'fnmsub':
                other_double[cur] += 1
    return single, other_double


def native():
    """function -> (entry address, [fused-op source files])."""
    dis = subprocess.run([str(BIN / 'objdump.exe'), '-d', '--no-show-raw-insn', str(DLL)],
                         capture_output=True, text=True).stdout
    starts, sites, fn = {}, [], None
    for line in dis.splitlines():
        m = re.match(r'^([0-9a-f]+) <([^>]+)>:$', line)
        if m:
            fn = m.group(2)
            starts.setdefault(fn, m.group(1))
            continue
        m = re.match(r'^\s*([0-9a-f]+):\s+vf(n)?m(add|sub)\d+ss\b', line)
        if m and fn:
            sites.append((fn, m.group(1)))
    locs = subprocess.run([str(BIN / 'addr2line.exe'), '-e', str(DBG)],
                          input='\n'.join('0x' + a for _, a in sites), capture_output=True, text=True).stdout.splitlines()
    out = collections.defaultdict(list)
    for (fn, _), loc in zip(sites, locs):
        if '??' in loc or 'math_native' in loc or 'sdk_math' in loc:
            continue
        out[fn].append(loc)
    return starts, out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()
    sym = {}
    for line in open(ROOT / 'sourceport/extern/melee/config/GALE01/symbols.txt'):
        m = re.match(r'(\S+) = \.text:0x([0-9A-F]+)', line)
        if m:
            sym[m.group(1)] = m.group(2)
    single, other_double = retail()
    starts, sites = native()
    chosen = sorted(fn for fn in sites if fn in sym and single[sym[fn]] == 0 and other_double[sym[fn]] == 0)
    # Definition file and line of each chosen function, from the debug info of its entry address.
    defs = subprocess.run([str(BIN / 'addr2line.exe'), '-e', str(DBG)],
                          input='\n'.join('0x' + starts[fn] for fn in chosen), capture_output=True, text=True).stdout.splitlines()
    tagged = missed = 0
    by_file = collections.defaultdict(list)
    for fn, loc in zip(chosen, defs):
        m = re.match(r'(.*):(\d+)', loc.split(' (discriminator')[0])
        if not m or '??' in loc:
            print('no location for', fn)
            missed += 1
            continue
        path, ln = m.group(1), int(m.group(2))
        if path.endswith('.h'):
            # entry line fell inside an inlined header; find the definition by name instead
            hit = subprocess.run(['git', 'grep', '-n', '-E', r'^[A-Za-z].*[ *]' + fn + r'\(.*[^;]$', '--', 'src', 'libs'],
                                 cwd=SRC, capture_output=True, text=True).stdout.splitlines()
            hit = [h for h in hit if h.split(':')[0].endswith('.c')]
            if len(hit) != 1:
                print('definition not unique for', fn, hit[:3])
                missed += 1
                continue
            f, n = hit[0].split(':')[:2]
            path, ln = str(SRC / f), int(n) + 1
        by_file[path].append((fn, ln))
    for path, fns in sorted(by_file.items()):
        p = Path(path)
        if not p.is_file() or SRC not in p.resolve().parents:
            print('outside the decomp:', path, [f for f, _ in fns])
            missed += len(fns)
            continue
        with open(p, encoding='utf-8', newline='') as fh:   # keep the file's own line endings
            lines = fh.read().split('\n')
        for fn, ln in fns:
            # The definition header is at or a few lines above the entry line.
            for i in range(min(ln, len(lines)) - 1, max(-1, ln - 15), -1):
                if re.search(r'(^|[\s*])' + re.escape(fn) + r'\s*\(', lines[i]) and not lines[i].rstrip().endswith(';'):
                    j = i
                    while j > 0 and lines[j - 1].strip() and not lines[j - 1].rstrip().endswith((';', '}', '{', '*/')) \
                            and not lines[j - 1].lstrip().startswith(('#', '//')):
                        j -= 1   # return type on its own line above
                    if TAG.strip() not in lines[j]:
                        lines[j] = TAG + lines[j]
                        tagged += 1
                    break
            else:
                print('definition not found for', fn, 'near', path, ln)
                missed += 1
        if not a.dry_run:
            with open(p, 'w', encoding='utf-8', newline='') as fh:
                fh.write('\n'.join(lines))
    print('%d functions qualify, %d tagged now, %d not tagged' % (len(chosen), tagged, missed))


if __name__ == '__main__':
    main()
