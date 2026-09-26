"""Repeat a hidden Customize capture to locate intermittent renderer shutdown stalls."""
import argparse
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--iso', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--runs', type=int, default=6)
    ap.add_argument('--timeout', type=int, default=35)
    args = ap.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env['MELEE_UI_DIAG'] = '1'
    env['MELEE_SETTINGS_TAB'] = 'Customize'
    failures = 0
    for n in range(args.runs):
        ini = out / f'run-{n}.ini'
        ini.write_text('startup 1\noverlaystyle 0\nbackend d3d12\nvsync 0\nwindow 800x600\n')
        log_path = out / f'run-{n}.txt'
        cmd = [str(ROOT/'build-integration/port/Release/melee_port.exe'),
               '--iso', str(args.iso), '--hidden', '--load-settings', '--pc-settings-open',
               '--backend', 'd3d12', '--frames', '360', '--capture-frame', '0',
               '--capture-sim-frame', '200', '--capture', str(out/f'run-{n}.ppm'),
               '--window', '800x600', '--volume', '0', '--no-music', '--settings-path', str(ini)]
        with log_path.open('w') as log:
            try:
                result = subprocess.run(cmd, cwd=ROOT, env=env, stdout=log,
                                        stderr=subprocess.STDOUT, timeout=args.timeout,
                                        creationflags=subprocess.CREATE_NO_WINDOW)
                status = str(result.returncode)
            except subprocess.TimeoutExpired:
                failures += 1
                status = 'timeout'
        lines = log_path.read_text(errors='replace').splitlines()
        phase = [line for line in lines if 'ui diag:' in line]
        print(f'run {n}: {status}; last phase: {phase[-1] if phase else "before backend shutdown"}',
              flush=True)
    raise SystemExit(1 if failures else 0)


if __name__ == '__main__':
    main()
