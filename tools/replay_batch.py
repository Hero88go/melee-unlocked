#!/usr/bin/env python3
"""Play many Slippi Dolphin recordings through the Source Port and compare each frame by frame.

For every .slp under --src (newest first, up to --count), runs tools/replay_compare.py --source into its
own directory, then writes summary.json and summary.txt: frames compared, mismatches, first
divergence. Hidden and muted; stops when C: drops under 15 GiB.

    python tools/replay_batch.py --src "%USERPROFILE%/Documents/Slippi" --count 200 --out run-source/<dir>
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VANILLA_LAST_CHAR = 0x19   # Ganondorf; higher external ids are bosses, wireframes or mod characters


def is_vanilla(slp):
    """False when a present player uses a non-playable or mod character id (m-ex style content)."""
    import struct
    d = slp.read_bytes()
    i = d.find(b'raw')
    if i < 0:
        return False
    raw = d[i + 12:]
    if raw[:1] != b'\x35':
        return False
    gs = raw[1 + raw[1]:]
    if gs[:1] != b'\x36':
        return False
    info = gs[5:5 + 0x138]
    for k in range(4):
        char, kind = info[0x60 + k * 0x24], info[0x61 + k * 0x24]
        if kind != 3 and char > VANILLA_LAST_CHAR:
            return False
    return True


def run(slp, out, iso, timeout):
    d = out / slp.stem
    if (d / 'result.txt').exists():
        text = (d / 'result.txt').read_text()
    else:
        if shutil.disk_usage('C:/').free < 15 * 1024 ** 3:
            return dict(replay=str(slp), status='skipped-low-disk')
        env = dict(os.environ, MELEE_NO_GC_ADAPTER='1')
        p = subprocess.run([sys.executable, 'tools/replay_compare.py', str(slp), '--source', '--iso', iso,
                            '--out', str(d), '--timeout', str(timeout)] + (['--exe', EXE] if EXE else []) +
                           (['--backend', BACKEND] if BACKEND else []), cwd=ROOT, env=env,
                           capture_output=True, text=True)
        text = p.stdout + p.stderr
        d.mkdir(parents=True, exist_ok=True)
        (d / 'result.txt').write_text(text)
        for f in d.glob('*.slp'):
            f.unlink()                      # keep disk use low; the comparison is in result.txt
        shutil.rmtree(d / 'shadercache', ignore_errors=True)
    r = dict(replay=str(slp))
    m = re.search(r'original frames (-?\d+)\.\.(-?\d+) \((\d+)\), recorded (-?\d+)\.\.(-?\d+)', text)
    if m:
        r['frames'] = int(m.group(3))
        r['recorded_last'] = int(m.group(5))
        r['original_last'] = int(m.group(2))
    m = re.search(r'(\d+) player-frames compared over (\d+) reference frames; (\d+) mismatches', text)
    if m:
        r['player_frames'] = int(m.group(1))
        r['mismatches'] = int(m.group(3))
    m = re.search(r'first divergence: frame (-?\d+) player/follower \((\d+), (\d+)\) (\w+): original (\S+) vs port (\S+)', text)
    if m:
        r['first'] = dict(frame=int(m.group(1)), port=int(m.group(2)), follower=int(m.group(3)),
                          field=m.group(4), original=m.group(5), port_value=m.group(6))
    m = re.search(r'mismatches from frame 0 on: (\d+)', text)
    if m:
        r['in_play_mismatches'] = int(m.group(1))
    m = re.search(r'first in-play divergence: frame (-?\d+) player/follower \((\d+), (\d+)\) (\w+): original (\S+) vs port (\S+)', text)
    if m:
        r['first_in_play'] = dict(frame=int(m.group(1)), port=int(m.group(2)), field=m.group(4),
                                  original=m.group(5), port_value=m.group(6))
    r['status'] = 'ok' if 'mismatches' in r else ('timeout' if 'timed out' in text else 'error')
    return r


EXE = None
BACKEND = None


def main():
    global EXE, BACKEND
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--src', type=Path, required=True)
    ap.add_argument('--count', type=int, default=200)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--iso', default=r'C:\Games\Super Smash Bros. Melee (v1.02 NTSC).iso')
    ap.add_argument('--jobs', type=int, default=3)
    ap.add_argument('--exe', default=None, help='melee_source.exe to use (default: build-sourceport/port/Release)')
    ap.add_argument('--backend', choices=['d3d11', 'd3d12'], default=None, help='graphics backend (default: the game default)')
    ap.add_argument('--timeout', type=int, default=1500)
    a = ap.parse_args()
    EXE = a.exe
    BACKEND = a.backend
    out = a.out if a.out.is_absolute() else ROOT / a.out
    out.mkdir(parents=True, exist_ok=True)
    files = [p for p in sorted(a.src.rglob('*.slp'), key=lambda p: p.stat().st_mtime, reverse=True)
             if is_vanilla(p)][:a.count]
    results = []
    with ThreadPoolExecutor(a.jobs) as ex:
        for r in ex.map(lambda f: run(f, out, a.iso, a.timeout), files):
            results.append(r)
            (out / 'summary.json').write_text(json.dumps(results, indent=1))
            print(len(results), Path(r['replay']).name, r.get('frames'), r.get('mismatches'),
                  r.get('first', {}).get('frame'), r['status'], flush=True)
    exact = [r for r in results if r.get('mismatches') == 0]
    play_exact = [r for r in results if r.get('in_play_mismatches') == 0]
    lines = [f"{len(results)} replays, {sum(r.get('frames', 0) for r in results)} frames; "
             f"{len(exact)} frame-exact vs the Dolphin recording, {len(play_exact)} exact from frame 0 on"]
    for r in results:
        if r.get('in_play_mismatches'):
            f = r.get('first_in_play', {})
            lines.append(f"{Path(r['replay']).name}: {r['mismatches']} mismatches, first frame {f.get('frame')} "
                         f"p{f.get('port')} {f.get('field')} {f.get('original')} vs {f.get('port_value')}")
        elif r['status'] != 'ok':
            lines.append(f"{Path(r['replay']).name}: {r['status']}")
    (out / 'summary.txt').write_text('\n'.join(lines) + '\n')
    print(lines[0])


if __name__ == '__main__':
    main()
