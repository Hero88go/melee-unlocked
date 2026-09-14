"""Muted native presentation benchmark. CPU timings do not prove displayed FPS."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import time
import tempfile
import shutil

ROOT = Path(__file__).resolve().parents[1]


def distribution(values):
    values = sorted(values)
    if not values:
        return None
    def percentile(p):
        return values[round((len(values) - 1) * p)]
    return dict(count=len(values), median=percentile(.5), p95=percentile(.95),
                p99=percentile(.99), maximum=values[-1])


def simulation_summary(rows, start):
    rows = [r for r in rows if int(r['retrace']) >= start]
    if len(rows) < 2:
        raise ValueError('not enough simulation timing checkpoints')
    ticks = [int(r['retrace']) for r in rows]
    times = [float(r['wall_seconds']) for r in rows]
    if any(b != a + 1 for a, b in zip(ticks, ticks[1:])) or any(b <= a for a, b in zip(times, times[1:])):
        raise ValueError('incomplete or non-monotonic simulation timing trace')
    return dict(retraces=len(rows), retraces_per_second=(ticks[-1]-ticks[0])/(times[-1]-times[0]),
                sixty_tick_hz=distribution([60/(times[i]-times[i-60]) for i in range(60, len(times))]),
                interval_ms=distribution([float(r['interval_ms']) for r in rows[1:]]),
                requested_speed_min=min(float(r['speed']) for r in rows),
                requested_speed_max=max(float(r['speed']) for r in rows),
                fast_retraces=sum(int(r['fast']) for r in rows),
                clock_resyncs=sum(int(r['resynced']) for r in rows))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--iso', type=Path, required=True)
    ap.add_argument('--exe', type=Path, default=ROOT / 'build-review/port/Release/melee_port.exe')
    ap.add_argument('--out', type=Path, default=ROOT / 'reports/high-refresh')
    ap.add_argument('--caps', nargs='+', default=['120', '144', '165', '200', '240', 'unlocked'])
    ap.add_argument('--frames', type=int, default=3600)
    ap.add_argument('--match-start', type=int, default=1500)
    ap.add_argument('--script', type=Path, default=ROOT / 'port/scripts/vs_match.txt')
    ap.add_argument('--repeats', type=int, default=2)
    ap.add_argument('--timeout', type=float, default=180)
    ap.add_argument('--window', default='1920x1080')
    ap.add_argument('--scale', type=int, default=3)
    ap.add_argument('--frame-mode', choices=['off', 'authored', 'authored-interpolate'], default='authored')
    ap.add_argument('--card-fixture', type=Path, help='prepared card required by the selected scenario')
    ap.add_argument('--recipes', type=Path, help='prewarm these collected shader recipes in each isolated cache')
    ap.add_argument('--profile-draws', action='store_true', help='enable expensive per-draw timing for overhead comparison')
    args = ap.parse_args()
    if args.frames <= args.match_start or args.repeats < 1:
        ap.error('need frames beyond match-start and at least one repeat')
    if any(c not in ('unlocked', 'monitor') and (not c.isdigit() or int(c) < 1) for c in args.caps):
        ap.error('invalid FPS cap')
    args.out.mkdir(parents=True, exist_ok=True)
    result = {'kind': 'CPU presentation submission timing; not physical display latency',
              'exe_sha256': hashlib.sha256(args.exe.read_bytes()).hexdigest(),
              'script_sha256': hashlib.sha256(args.script.read_bytes()).hexdigest(), 'runs': []}
    result['graphics'] = dict(window=args.window, scale=args.scale, dlss='off', ssaa=1, sharpness=0)
    result['profile_draws'] = args.profile_draws
    result['frame_mode'] = args.frame_mode
    if args.recipes:
        result['recipes_sha256'] = hashlib.sha256(args.recipes.read_bytes()).hexdigest()
    # Separate cache per cap. A fresh output directory gives cold then warm trials.
    for cap in args.caps:
        cache = (args.out / ('cache-' + cap)).resolve()
        existed = cache.exists()
        if args.recipes:
            cache.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(args.recipes, cache / 'recipes.bin')
        for repeat in range(args.repeats):
            label = f'{cap}-{repeat}'
            isolated = Path(tempfile.mkdtemp(prefix=label+'-state-', dir=args.out)).resolve()
            if args.card_fixture:
                shutil.copytree(args.card_fixture, isolated / 'cards')
            trace = (args.out / (label + '.csv')).resolve()
            sim_trace = (args.out / (label + '-simulation.csv')).resolve()
            command = [str(args.exe.resolve()), '--iso', str(args.iso.resolve()),
                       '--volume', '0', '--hidden', '--threaded-renderer', '--fps', cap,
                       '--frame-mode', args.frame_mode, '--frames', str(args.frames), '--time-base', '1',
                       '--window', args.window, '--scale', str(args.scale), '--dlss', 'off', '--ssaa', '1', '--sharpness', '0',
                       '--script', str(args.script.resolve()), '--frame-times', str(trace), '--sim-times', str(sim_trace),
                       '--log-file', str((args.out / (label + '-port.log')).resolve()),
                       '--card-dir', str(isolated / 'cards'), '--user-dir', str(isolated / 'User'),
                       '--shader-cache', str(cache), '--replay-dir', str((args.out / 'replays').resolve())]
            start = time.monotonic()
            if args.profile_draws:
                command.append('--profile-draws')
            with (args.out / (label + '.log')).open('w') as log:
                run = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                                     timeout=args.timeout, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            if run.returncode:
                raise SystemExit(f'{label} failed: {run.returncode}; inspect log')
            with trace.open() as f:
                rows = list(csv.DictReader(f))
            match = [r for r in rows if int(r['simulation']) >= args.match_start]
            record = dict(cap=cap, repeat=repeat, cache='existing' if existed or repeat else 'fresh',
                          seconds=time.monotonic()-start, presentations=len(rows), match_presentations=len(match))
            for field in ('interval_ms', 'solver_ms', 'submit_ms', 'source_age_ms'):
                record[field] = distribution([float(r[field]) for r in match])
            record['presentations_with_authored_draws'] = sum(int(r['authored_draws']) > 0 for r in match)
            # GPU queries complete a few submissions later. Filter by their own
            # source frame, exclude drained work, and never count one query twice.
            gpu = {int(r['gpu_submission']): float(r['gpu_ms']) for r in rows
                   if int(r.get('gpu_submission', 0)) > 0 and int(r.get('gpu_presented', 0))
                   and int(r.get('gpu_simulation', 0)) >= args.match_start}
            record['gpu_execution_ms'] = distribution(list(gpu.values()))
            with sim_trace.open() as f:
                record['simulation_clock'] = simulation_summary(list(csv.DictReader(f)), args.match_start)
            result['runs'].append(record)
            (args.out / 'summary.json').write_text(json.dumps(result, indent=2))
            print(json.dumps(record), flush=True)


if __name__ == '__main__':
    main()
