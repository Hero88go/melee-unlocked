"""Run one isolated card-load diagnostic; successful boot alone is not M10 parity."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time


def identity(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return {'path': str(path), 'bytes': path.stat().st_size,
            'sha256': digest.hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--iso', type=Path, required=True)
    parser.add_argument('--seed', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--script', type=Path, required=True)
    parser.add_argument('--sys-dir', type=Path,
                        default=Path(__file__).resolve().parents[1] / 'port' / 'slippi_sys')
    parser.add_argument('--frames', type=int, default=1700)
    parser.add_argument('--timeout', type=float, default=90)
    parser.add_argument('--capture-frame', type=int,
                        help='also capture a hidden PPM at this host retrace')
    args = parser.parse_args()
    if args.frames <= 0 or not 0 < args.timeout <= 600:
        parser.error('positive frame count and timeout <= 600 required')
    exe, iso, seed, out, script, sys_dir = [p.resolve() for p in
        (args.exe, args.iso, args.seed, args.out, args.script, args.sys_dir)]
    for path in (exe, iso, script, sys_dir):
        if not path.is_file():
            if path == sys_dir and path.is_dir():
                continue
            parser.error('missing input: ' + str(path))
    cards = sorted(seed.glob('*.gci'))
    if not cards or any(not p.is_file() for p in cards):
        parser.error('seed must contain GCI files')
    if shutil.disk_usage(out.parent).free < 15 * 1024**3:
        parser.error('less than 15 GiB free')
    out.mkdir(exist_ok=False)
    card_dir = out / 'card'
    card_dir.mkdir()
    for card in cards:
        shutil.copy2(card, card_dir / card.name)
    user_dir = out / 'User'
    user_dir.mkdir()
    # The launcher falls back to the user's installed Slippi profile when this
    # file is absent, which can start a rank/profile HTTP request. Keep these
    # acceptance runs offline and isolated with a credential-free empty profile.
    user_config = user_dir / 'user.json'
    user_config.write_text('{}\n', encoding='utf-8')
    inputs = [exe, iso, script, user_config, *cards]
    inputs.extend(sorted(exe.parent.glob('*.dll')))
    report = {'inputs': [identity(p) for p in inputs],
              'scope': 'bounded card-load diagnostic; not save-content parity',
              'status': 'prepared'}
    command = [str(exe), '--iso', str(iso), '--hidden', '--volume', '0',
               '--frames', str(args.frames), '--fast', '--time-base', '1',
               '--rng-seed', '305419896', '--script', str(script),
               '--sys-dir', str(sys_dir), '--replay-dir', str(out / 'Replays'),
               '--card-dir', str(card_dir), '--user-dir', str(user_dir),
               '--log-file', str(out / 'game.log')]
    if args.capture_frame is not None:
        command.extend(['--capture', str(out / 'capture.ppm'),
                        '--capture-frame', str(args.capture_frame)])
    report['command'] = command
    report['timeout_seconds'] = args.timeout
    report_path = out / 'result.json'
    report_path.write_text(json.dumps(report, indent=2), encoding='utf-8')
    env = dict(os.environ, MELEE_NO_GC_ADAPTER='1')
    start = time.monotonic()
    with (out / 'stdout.txt').open('wb') as stdout, (out / 'stderr.txt').open('wb') as stderr:
        try:
            result = subprocess.run(command, cwd=out, env=env, stdout=stdout,
                                    stderr=stderr, timeout=args.timeout)
            report['exit_code'] = result.returncode
            report['status'] = 'exited'
        except subprocess.TimeoutExpired:
            report['status'] = 'timeout'
        except OSError as exc:
            report['status'] = 'launch_error'
            report['error'] = str(exc)
    report['elapsed_seconds'] = time.monotonic() - start
    log = out / 'game.log'
    text = log.read_text(encoding='utf-8', errors='replace') if log.exists() else ''
    report['reached_limit'] = f'exit requested after {args.frames} retraces' in text
    report['fatal_log'] = bool(re.search(r'CRASH:|game crash|PANIC|game panic', text, re.I))
    report['passed'] = (report.get('exit_code') == 0 and report['reached_limit']
                        and not report['fatal_log'])
    report['output_cards'] = [identity(p) for p in sorted(card_dir.glob('*.gci'))]
    capture = out / 'capture.ppm'
    report['capture'] = identity(capture) if capture.is_file() else None
    report_path.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k not in ('inputs', 'command', 'output_cards')}, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
