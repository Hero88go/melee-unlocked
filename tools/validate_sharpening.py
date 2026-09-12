"""Capture a deterministic native scene at four output sharpening strengths.

Checks continuity near zero and that the control affects pixels. These checks
do not establish visual quality or Dolphin parity; inspect the saved images.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import shutil
from PIL import Image, ImageChops, ImageStat

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--iso', type=Path, required=True)
    ap.add_argument('--exe', type=Path, default=ROOT / 'build-review/port/Release/melee_port.exe')
    ap.add_argument('--out', type=Path, default=ROOT / 'reports/sharpening')
    ap.add_argument('--frame', type=int, default=1800)
    ap.add_argument('--card-fixture', type=Path, help='copy a prepared test card into every run')
    args = ap.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    results = {'exe_sha256': hashlib.sha256(args.exe.read_bytes()).hexdigest(),
               'native': True, 'scale': 3, 'window': '1920x1080', 'gx_frame_sequence': args.frame,
               'measurements': []}
    baseline = None
    for strength in (0, .001, .5, 1):
        run = out / str(strength)
        run.mkdir(exist_ok=True)
        isolated = Path(tempfile.mkdtemp(prefix='state-', dir=run))
        if args.card_fixture:
            shutil.copytree(args.card_fixture, isolated / 'cards')
        capture = run / 'capture.ppm'
        cmd = [str(args.exe.resolve()), '--iso', str(args.iso.resolve()), '--hidden', '--fast',
               '--volume', '0', '--time-base', '1', '--frames', str(args.frame+300),
               '--script', str(ROOT / 'port/scripts/vs_match.txt'), '--scale', '3', '--ssaa', '1',
               '--window', '1920x1080', '--widescreen', '--dlss', 'off', '--sharpness', str(strength),
               '--capture', str(capture), '--capture-sim-frame', str(args.frame),
               '--card-dir', str(isolated / 'cards'), '--user-dir', str(isolated / 'User'),
               '--replay-dir', str(run / 'replays'),
               '--log-file', str(run / 'port.log'),
               '--shader-cache', str(out / 'cache')]
        with (run / 'process.log').open('w') as log:
            subprocess.run(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True,
                           timeout=400, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        with Image.open(capture) as image:
            image = image.convert('RGB')
            image.save(run / 'capture.png')
            if baseline is None:
                baseline = image.copy()
            difference = ImageChops.difference(baseline, image)
            mean = sum(ImageStat.Stat(difference).mean)/3
            maximum = max(v[1] for v in difference.getextrema())
        results['measurements'].append({'strength': strength, 'mean_absolute_difference_255': mean,
                                        'max_channel_difference_255': maximum})
        (out / 'summary.json').write_text(json.dumps(results, indent=2))
        print(results['measurements'][-1], flush=True)
    low, middle, high = results['measurements'][1:]
    assert low['max_channel_difference_255'] <= 2, 'discontinuity near disabled'
    assert middle['mean_absolute_difference_255'] > low['mean_absolute_difference_255'], 'slider has no useful effect'
    assert high['mean_absolute_difference_255'] > middle['mean_absolute_difference_255'], 'non-monotonic strength'


if __name__ == '__main__':
    main()
