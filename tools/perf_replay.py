#!/usr/bin/env python3
"""Frame-time comparison of the two engines on the same Slippi replays, paced at 60 Hz.

Each replay is played (hidden, muted, no GC adapter) by the Source Port (melee_source.exe --replay)
and by the static recomp playback build (melee_port_playback.exe --replay), with the renderer
settings people actually play on (--fps unlocked --frame-mode authored --scale auto
--threaded-renderer). Every simulation frame's work time comes from MELEE_SIM_TIMES (host.cpp),
every presentation from --frame-times. Reported over the match (match frame > 0):

  sim     work per 60 Hz tick (what "sim frame N took X ms" logs above 20 ms): p50 p99 max, >16.7
  arrive  interval between simulation frames reaching the renderer: p99 max, count > 20 ms
  present interval between presented frames: p99 max, count > 16.7 ms

A spike only counts when the machine is quiet: run it when no build is going. Engines alternate
per replay so drifting background load hits both.

    python tools/perf_replay.py --out run-source/perf-g0/before --source-bin <dir> --legacy-bin <dir> \
        [--replays a.slp b.slp] [--legal-stages 1] [--frames 5400] [--repeats 1]
"""
import argparse
import csv
import json
import os
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from melee_iso import require_iso            # noqa: E402
from replay_batch import is_vanilla          # noqa: E402

LEGAL = {2: 'FoD', 3: 'Stadium', 8: 'Yoshis', 28: 'DreamLand', 31: 'Battlefield', 32: 'FD'}


def stage_of(slp):
    d = slp.read_bytes()[:4096]
    i = d.find(b'raw')
    raw = d[i + 12:]
    if i < 0 or len(raw) < 2 or raw[:1] != b'\x35':
        return -1
    gs = raw[1 + raw[1]:]
    return struct.unpack('>H', gs[0x13:0x15])[0] if gs[:1] == b'\x36' and len(gs) >= 0x15 else -1


def pct(values, p):
    if not values:
        return 0.0
    v = sorted(values)
    return v[min(len(v) - 1, round((len(v) - 1) * p))]


def summarize(sim_csv, present_csv):
    with open(sim_csv) as f:
        rows = [r for r in csv.DictReader(f)]
    match = [r for r in rows if int(r['match_frame']) > 0]
    first = int(match[0]['retrace']) if match else 0
    last = int(match[-1]['retrace']) if match else 0
    sim = [float(r['sim_ms']) for r in match]
    cpu = [float(r['cpu_ms']) for r in match if 'cpu_ms' in r]
    worst = sorted(match, key=lambda r: -float(r.get('cpu_ms') or r['sim_ms']))[:8]
    out = dict(match_frames=len(sim), sim_p50=pct(sim, .5), sim_p99=pct(sim, .99), sim_max=max(sim or [0]),
               sim_over_16_7=sum(v > 16.7 for v in sim), sim_over_20=sum(v > 20 for v in sim),
               cpu_p50=pct(cpu, .5), cpu_p99=pct(cpu, .99), cpu_max=max(cpu or [0]), cpu_over_16_7=sum(v > 16.7 for v in cpu), cpu_over_8=sum(v > 8 for v in cpu),
               worst=[{k: r[k] for k in r if k in ('retrace', 'sim_ms', 'cpu_ms') or (k not in ('scene_major', 'scene_minor', 'match_frame') and float(r[k]) >= 0.5)} for r in worst])
    arrive, present = [], []
    try:
        with open(present_csv) as f:
            prow = list(csv.DictReader(f))
    except OSError:
        prow = []
    t, prev_seq, prev_time = 0.0, None, None
    for r in prow:
        t += float(r['interval_ms'])
        seq = int(r['simulation'])
        if not (first <= seq <= last):
            continue
        present.append(float(r['interval_ms']))
        if seq != prev_seq:
            made = t - float(r['solver_ms']) - float(r['submit_ms']) - float(r['present_wait_ms']) - float(r['source_age_ms'])
            if prev_time is not None and seq == prev_seq + 1:
                arrive.append(made - prev_time)
            prev_seq, prev_time = seq, made
    out.update(arrive_p50=pct(arrive, .5), arrive_p99=pct(arrive, .99), arrive_max=max(arrive or [0]),
               arrive_over_20=sum(v > 20 for v in arrive), present_p50=pct(present, .5), present_p99=pct(present, .99),
               present_max=max(present or [0]), present_over_16_7=sum(v > 16.7 for v in present), presentations=len(present))
    return out


def run_one(engine, bindir, slp, out, iso, frames, cache, timeout, priority=0):
    exe = Path(bindir) / ('melee_source.exe' if engine == 'source' else 'melee_port_playback.exe')
    tag = f'{engine}-{slp.stem}'
    d = out / tag
    d.mkdir(parents=True, exist_ok=True)
    cmd = [str(exe), '--iso', str(iso), '--replay', str(slp.resolve()), '--volume', '0', '--hidden',
           '--threaded-renderer', '--fps', 'unlocked', '--frame-mode', 'authored', '--scale', 'auto',
           '--frames', str(frames), '--frame-times', str(d / 'present.csv'), '--log-file', str(d / 'port.log'),
           '--replay-dir', str(d / 'rec'), '--card-dir', str(d / 'card'), '--settings-path', str(d / 'settings.ini'),
           '--shader-cache', str(cache)]
    if engine == 'legacy':
        cmd[1:1] = ['--sys-dir', str(ROOT / 'port/slippi_sys_playback')]
    else:
        cmd += ['--slippi-menus', 'off']
    env = dict(os.environ, MELEE_NO_GC_ADAPTER='1', MELEE_SIM_TIMES=str(d / 'sim.csv'))
    start = time.monotonic()
    try:
        p = subprocess.run(cmd, cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           timeout=timeout, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0) | priority)
        code = p.returncode
    except subprocess.TimeoutExpired:
        code = 'timeout'
    for f in (d / 'rec').glob('*.slp'):
        f.unlink()
    r = dict(engine=engine, replay=slp.name, stage=LEGAL.get(stage_of(slp), stage_of(slp)), exit=code,
             seconds=round(time.monotonic() - start, 1))
    try:
        r.update(summarize(d / 'sim.csv', d / 'present.csv'))
    except Exception as e:   # noqa: BLE001 - report and continue with the other runs
        r['error'] = repr(e)
    return r


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--source-bin', type=Path)
    ap.add_argument('--legacy-bin', type=Path)
    ap.add_argument('--replays', type=Path, nargs='*', default=[])
    ap.add_argument('--legal-stages', type=int, default=0, help='also take this many newest vanilla replays per legal stage')
    ap.add_argument('--src', type=Path, default=Path(os.path.expandvars(r'%USERPROFILE%/Documents/Slippi')))
    ap.add_argument('--frames', type=int, default=5400)
    ap.add_argument('--repeats', type=int, default=1)
    ap.add_argument('--iso', type=Path)
    ap.add_argument('--timeout', type=float, default=600)
    ap.add_argument('--priority', choices=('normal', 'abovenormal', 'high'), default='normal', help='process priority class (diagnostic)')
    a = ap.parse_args()
    iso = require_iso(a.iso)
    out = a.out if a.out.is_absolute() else ROOT / a.out
    out.mkdir(parents=True, exist_ok=True)
    replays = list(a.replays)
    if a.legal_stages:
        files = sorted(a.src.rglob('*.slp'), key=lambda p: p.stat().st_mtime, reverse=True)
        for stage in LEGAL:
            picked = [p for p in files if stage_of(p) == stage and is_vanilla(p)][:a.legal_stages]
            replays += picked
    engines = [e for e, b in (('source', a.source_bin), ('legacy', a.legacy_bin)) if b]
    results = []
    for rep in range(a.repeats):
        for slp in replays:
            for engine in (engines if rep % 2 == 0 else engines[::-1]):
                bindir = a.source_bin if engine == 'source' else a.legacy_bin
                r = run_one(engine, bindir, slp, out, iso, a.frames, out / f'cache-{engine}', a.timeout,
                            {'normal': 0, 'abovenormal': 0x8000, 'high': 0x80}[a.priority])
                r['repeat'] = rep
                results.append(r)
                (out / 'summary.json').write_text(json.dumps(results, indent=1))
                print(json.dumps({k: (round(v, 2) if isinstance(v, float) else v) for k, v in r.items() if k != 'worst'}), flush=True)
    lines = ['engine  replay                         stage        frames  sim p50/p99/max  >16.7  >20 | cpu p50/p99/max >8 | arrive p99/max >20 | present p99/max >16.7']
    for r in results:
        if 'sim_p50' not in r:
            lines.append(f"{r['engine']:7} {r['replay']:30} {r['stage']!s:12} ERROR {r.get('error')} exit {r['exit']}")
            continue
        lines.append(f"{r['engine']:7} {r['replay']:30} {r['stage']!s:12} {r['match_frames']:6}  "
                     f"{r['sim_p50']:.2f}/{r['sim_p99']:.2f}/{r['sim_max']:.1f}  {r['sim_over_16_7']:5} {r['sim_over_20']:4} | "
                     f"{r['cpu_p50']:.2f}/{r['cpu_p99']:.2f}/{r['cpu_max']:.1f} {r['cpu_over_8']:3} | "
                     f"{r['arrive_p99']:.1f}/{r['arrive_max']:.1f} {r['arrive_over_20']:3} | {r['present_p99']:.1f}/{r['present_max']:.1f} {r['present_over_16_7']:4}")
    for engine in engines:
        rs = [r for r in results if r['engine'] == engine and 'sim_p50' in r]
        if rs:
            lines.append(f"TOTAL {engine}: frames {sum(r['match_frames'] for r in rs)}, sim >16.7 {sum(r['sim_over_16_7'] for r in rs)}, "
                         f">20 {sum(r['sim_over_20'] for r in rs)}, worst {max(r['sim_max'] for r in rs):.1f} ms, cpu worst {max(r['cpu_max'] for r in rs):.1f} ms, cpu >8 {sum(r['cpu_over_8'] for r in rs)}, "
                         f"arrive >20 {sum(r['arrive_over_20'] for r in rs)}, present >16.7 {sum(r['present_over_16_7'] for r in rs)}")
    (out / 'summary.txt').write_text('\n'.join(lines) + '\n')
    print('\n'.join(lines))


if __name__ == '__main__':
    main()
