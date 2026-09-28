#!/usr/bin/env python3
"""Names the raw --profile samples (MELEE_PROFILE_SAMPLES=<csv>) of melee_source.exe.

The in-process report cannot name the Source Port's game code (a GCC DLL without a PDB); this reads
the executable's /MAP file and the DLL's symbols (nm on melee_game.dbg, image base fixed at
0x82800000) and prints the hottest functions, optionally only over chosen simulation frames:

    python tools/profile_symbolize.py samples.csv --map <dir>/melee_source.map --dbg <dir>/melee_game.dbg \
        [--sim sim.csv --min-cpu 8]    # only frames whose CPU time was at least 8 ms
"""
import argparse
import bisect
import collections
import csv
import re
import subprocess
from pathlib import Path

NM = [r'C:\Users\Chandler\toolchains\winlibs-gcc-15.3\mingw64\bin\nm.exe', r'C:\msys64\usr\bin\nm.exe', 'nm']


def load_map(path):
    syms, preferred = [], 0x140000000
    for line in Path(path).read_text(errors='replace').splitlines():
        m = re.match(r'\s*Preferred load address is ([0-9a-fA-F]+)', line)
        if m:
            preferred = int(m.group(1), 16)
        m = re.match(r'\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\s', line)
        if m:
            name = m.group(1)
            if name.startswith('?'):
                name = name[1:].split('@@')[0]
                parts = name.split('@')
                name = '::'.join(reversed(parts)) if len(parts) > 1 else name
            syms.append((int(m.group(2), 16), name))
    syms.sort()
    return preferred, syms


def load_nm(path):
    for nm in NM:
        try:
            out = subprocess.run([nm, '-C', '--defined-only', str(path)], capture_output=True, text=True, check=True).stdout
            break
        except (OSError, subprocess.CalledProcessError):
            continue
    else:
        raise SystemExit('no nm found')
    syms = []
    for line in out.splitlines():
        parts = line.split(' ', 2)
        if len(parts) == 3 and parts[1] in 'tTwW':
            syms.append((int(parts[0], 16), parts[2]))
    syms.sort()
    return syms


def lookup(syms, addr):
    i = bisect.bisect_right(syms, (addr, '\uffff')) - 1
    return syms[i][1] if i >= 0 else '?'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('samples', type=Path)
    ap.add_argument('--map', type=Path, required=True)
    ap.add_argument('--dbg', type=Path, required=True)
    ap.add_argument('--sim', type=Path, help='MELEE_SIM_TIMES csv, to select frames')
    ap.add_argument('--min-cpu', type=float, default=0.0)
    ap.add_argument('--min-sim', type=float, default=0.0)
    ap.add_argument('--match-only', action='store_true')
    ap.add_argument('--top', type=int, default=40)
    ap.add_argument('--include-waits', action='store_true', help='count samples taken while the thread was waiting')
    a = ap.parse_args()
    lines = a.samples.read_text().splitlines()
    header = [l for l in lines if l.startswith('#')]
    exe_base = next((int(l.split()[2], 16) for l in header if l.startswith('# exe ')), 0x140000000)
    modules = sorted((int(l.split()[2], 16), int(l.split()[3], 16), l.split(None, 4)[4]) for l in header if l.startswith('# module '))
    rows = list(csv.DictReader([l for l in lines if not l.startswith('#')]))
    preferred, exe_syms = load_map(a.map)
    dll_syms = load_nm(a.dbg)
    dll_lo, dll_hi = 0x82800000, 0x90000000
    exe_hi = exe_syms[-1][0] + (exe_base - preferred) + 0x1000000 if exe_syms else 0
    frames = None
    if a.sim:
        frames = set()
        with open(a.sim) as f:
            for r in csv.DictReader(f):
                if a.match_only and int(r['match_frame']) <= 0:
                    continue
                if float(r.get('cpu_ms') or 0) >= a.min_cpu and float(r['sim_ms']) >= a.min_sim:
                    frames.add(int(r['retrace']))
    hits, callers, waits = collections.Counter(), collections.Counter(), collections.Counter()
    total = 0
    for r in rows:
        # host::retrace publishes id R and the work that follows is logged in sim.csv as retrace R
        # by the next retrace (its completed_frame).
        fid = int(r['frame'])
        if frames is not None and fid not in frames:
            continue
        # The sampler also stops the thread while it waits (the tick sleep, a lock): a sample whose
        # thread ran under ~20 us of cycles since the previous one was waiting, not working.
        if not a.include_waits and 'cycles' in r and int(r['cycles']) < 60000:
            waits[('wait ' + lookup(exe_syms, int(r['ret'], 16) - exe_base + preferred)) if exe_base <= int(r['ret'], 16) < exe_hi else 'wait'] += 1
            continue
        total += 1
        rip, ret = int(r['rip'], 16), int(r['ret'], 16)
        if dll_lo <= rip < dll_hi:
            label = 'game ' + lookup(dll_syms, rip)
        elif exe_base <= rip < exe_hi:
            label = 'host ' + lookup(exe_syms, rip - exe_base + preferred)
        else:
            mod = next((name for base, size, name in modules if base <= rip < base + size), '?')
            label = 'system ' + mod
            if exe_base <= ret < exe_hi:
                callers['from host ' + lookup(exe_syms, ret - exe_base + preferred)] += 1
            elif dll_lo <= ret < dll_hi:
                callers['from game ' + lookup(dll_syms, ret)] += 1
        hits[label] += 1
    print(f'{total} samples' + (f' in {len(frames)} selected frames' if frames is not None else ''))
    for label, n in hits.most_common(a.top):
        print(f'{100.0 * n / max(1, total):5.1f}%  {n:6}  {label}')
    if waits:
        print(f'{sum(waits.values())} samples while waiting: ' + ', '.join(f'{k} {v}' for k, v in waits.most_common(6)))
    if callers:
        print('system time by caller:')
        for label, n in callers.most_common(15):
            print(f'{100.0 * n / max(1, total):5.1f}%  {n:6}  {label}')


if __name__ == '__main__':
    main()
