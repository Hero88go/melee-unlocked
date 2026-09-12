"""Collect and merge native pipeline recipes from reproducible offline scenarios.

Default scenarios are a smoke set, not complete character/stage coverage. Use a
JSON manifest with a cases array of {name, script, frames} for expanded coverage.
All instances are muted, hidden, isolated, and run sequentially.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import shutil

ROOT = Path(__file__).resolve().parents[1]
MAGIC = 0x3150535247505847
RECIPE_BYTES = 8 + 256 * 4 + 0x58 * 4
LIMIT = 16384


def checksum(data):
    # Match gx::hash_bytes, including its eight-byte mixing step.
    value = 0xcbf29ce484222325
    whole = len(data) // 8 * 8
    for (word,) in struct.iter_unpack('<Q', data[:whole]):
        value = ((value ^ word) * 1099511628211) & 0xffffffffffffffff
        value ^= value >> 29
    for byte in data[whole:]:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return value


def read_recipes(path):
    data = path.read_bytes()
    if len(data) < 24:
        raise ValueError(f'truncated recipe header: {path}')
    magic, count, digest = struct.unpack_from('<QQQ', data)
    payload = data[24:]
    if magic != MAGIC or count > LIMIT or len(payload) != count * RECIPE_BYTES or checksum(payload) != digest:
        raise ValueError(f'invalid recipe file: {path}')
    return [payload[i:i+RECIPE_BYTES] for i in range(0, len(payload), RECIPE_BYTES)]


def merge(paths, output):
    recipes = sorted(set(recipe for path in paths for recipe in read_recipes(path)))
    if len(recipes) > LIMIT:
        raise ValueError('recipe limit exceeded; refusing to silently discard coverage')
    payload = b''.join(recipes)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix('.tmp')
    temporary.write_bytes(struct.pack('<QQQ', MAGIC, len(recipes), checksum(payload)) + payload)
    temporary.replace(output)
    return len(recipes)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--iso', type=Path)
    ap.add_argument('--exe', type=Path, default=ROOT / 'build-review/port/Release/melee_port.exe')
    ap.add_argument('--manifest', type=Path)
    ap.add_argument('--out', type=Path, default=ROOT / 'reports/recipe-collection')
    ap.add_argument('--merge', type=Path, nargs='+', help='merge existing caches without launching the game')
    ap.add_argument('--timeout', type=float, default=240)
    ap.add_argument('--card-fixture', type=Path, help='prepared card required by the scenario manifest')
    args = ap.parse_args()
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if args.merge:
        print(f'{merge(args.merge, output / "recipes.bin")} recipes merged')
        return
    if not args.iso or not args.iso.is_file():
        ap.error('--iso is required for scenario collection')
    cases = json.loads(args.manifest.read_text())['cases'] if args.manifest else [
        {'name': 'menus-css', 'script': 'port/scripts/to_css.txt', 'frames': 1200},
        {'name': 'versus', 'script': 'port/scripts/vs_match.txt', 'frames': 3600},
        {'name': 'classic', 'script': 'port/scripts/classic_fox.txt', 'frames': 3600},
    ]
    report = {'kind': 'observed shader coverage; not exhaustive',
              'exe_sha256': hashlib.sha256(args.exe.read_bytes()).hexdigest(), 'cases': []}
    caches = []
    for index, case in enumerate(cases):
        script = (ROOT / case['script']).resolve()
        frames = int(case['frames'])
        if frames < 1 or not script.is_file():
            raise ValueError(f'invalid scenario: {case["name"]}')
        run_dir = output / f'case-{index:03d}'
        run_dir.mkdir(exist_ok=True)
        isolated = Path(tempfile.mkdtemp(prefix='state-', dir=run_dir))
        if args.card_fixture:
            shutil.copytree(args.card_fixture, isolated / 'cards')
        command = [str(args.exe.resolve()), '--iso', str(args.iso.resolve()),
                   '--volume', '0', '--hidden', '--fast', '--frame-mode', 'off', '--dlss', 'off',
                   '--time-base', '1', '--frames', str(frames), '--script', str(script),
                   '--shader-cache', str(run_dir / 'cache'), '--log-file', str(run_dir / 'port.log'),
                   '--card-dir', str(isolated / 'cards'),
                   '--user-dir', str(isolated / 'User'), '--replay-dir', str(run_dir / 'replays')]
        with (run_dir / 'process.log').open('w') as log:
            subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True,
                           timeout=args.timeout, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        cache = run_dir / 'cache/recipes.bin'
        caches.append(cache)
        report['cases'].append({**case, 'script_sha256': hashlib.sha256(script.read_bytes()).hexdigest(),
                                'recipes': len(read_recipes(cache))})
        report['merged_recipes'] = merge(caches, output / 'recipes.bin')
        (output / 'coverage.json').write_text(json.dumps(report, indent=2))
        print(f'{case["name"]}: {report["merged_recipes"]} recipes accumulated', flush=True)


if __name__ == '__main__':
    main()
