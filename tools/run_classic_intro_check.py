"""Reproduce the Classic intro on the public hotfix binaries in isolated folders."""
import argparse
import json
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--engine', choices=['source', 'static'], required=True)
parser.add_argument('--iso', type=Path, required=True)
parser.add_argument('--backend', default='d3d12')
parser.add_argument('--frames', type=int, default=1600)
parser.add_argument('--capture', type=int, default=1400)
parser.add_argument('--exe', type=Path)
parser.add_argument('--dll', type=Path)
parser.add_argument('--tag', default='baseline')
parser.add_argument('--dump', type=int)
parser.add_argument('--burst', type=int, default=0)
args = parser.parse_args()
run = ROOT / 'run-stage-skins' / f'classic-{args.tag}-{args.engine}-{args.backend}'
run.mkdir(parents=True, exist_ok=True)
binary = run / 'bin'
binary.mkdir(exist_ok=True)
name = 'melee_source.exe' if args.engine == 'source' else 'melee_port.exe'
old = ROOT.parent / 'melee-unlocked-current-fixes/build-current-reports/port/Release'
shutil.copy2(args.exe or old / name, binary / name)
if args.engine == 'source':
    shutil.copy2(args.dll or old / 'melee_game.dll', binary / 'melee_game.dll')
(run / 'settings.ini').write_text('startup 0\nbackend ' + args.backend + '\nvolume 0\nscale 1\n')
command = [str(binary / name), '--iso', str(args.iso.resolve()),
           '--hidden', '--volume', '0', '--no-music', '--frames', str(args.frames),
           '--backend', args.backend, '--dlss', 'off', '--scale', '1', '--time-base', '1',
           '--sys-dir', str(ROOT.parent / ('melee-sourceport/port/slippi_sys' if args.engine=='source' else 'melee-sourceport/port/slippi_sys_general')),
           '--user-dir', str(run / 'user'), '--card-dir', str(run / 'cards'),
           '--replay-dir', str(run / 'replays'), '--settings-path', str(run / 'settings.ini'),
           '--load-settings', '--script', str(run / 'inputs.txt'),
           '--log-file', str(run / 'port.log'), '--capture', str(run / 'capture.ppm'),
           '--capture-sim-frame', str(args.capture)]
if args.engine=='source':
    command += ['--slippi-menus', 'off']
if args.dump:
    command += ['--dump-frame', str(args.dump), '--dump', str(run/'draws.txt')]
if args.burst:
    command += ['--capture-burst', str(args.burst)]
(run / 'inputs.txt').write_text((ROOT / 'port/scripts/classic_fox.txt').read_text() + '\n1800 sx=0 sy=-100\n1806\n1850 A\n1860\n1930 START\n1940\n')
(run / 'command.json').write_text(json.dumps(command, indent=2))
with (run / 'process.log').open('w') as log:
    result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=240)
print(args.engine, args.backend, result.returncode, str(run))
raise SystemExit(result.returncode)
