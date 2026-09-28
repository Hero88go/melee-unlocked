#!/usr/bin/env python3
"""M3: run the Source and Legacy engines with each command-line settings option and compare effects.

Every presentation option is consumed by host code both executables share, so what has to be shown
is that the option takes the same effect whichever engine feeds the renderer. Each run is hidden,
muted, scripted into the menus (parity_vs.txt works for both engines) and captured at one presented
frame. Per run it records the renderer's backend line, internal resolution, host audio volume,
the capture's content box (letterboxing) and whether the capture differs from that engine's own
baseline. The engines are then compared option by option.

    python tools/m3_option_runs.py --out run-source/m3-option-runs-YYYYMMDD
"""
import argparse, json, re, subprocess
from pathlib import Path

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
ISO = r'C:/Games/Smash/DOLPHIN AND SMASH GAMES/Super Smash Bros. Melee (v1.02).iso'
ENGINES = {'source': ROOT / 'build-sourceport/port/Release/melee_source.exe',
           'legacy': ROOT / 'build-vanilla/port/Release/melee_port.exe'}
OPTIONS = {
    'baseline': [],
    'backend-d3d11': ['--backend', 'd3d11'],
    'scale-1': ['--scale', '1'],
    'scale-3': ['--scale', '3'],
    'ssaa-2': ['--ssaa', '2'],
    'aspect-4-3': ['--aspect', '4:3'],
    'aspect-16-9': ['--aspect', '16:9'],
    'aspect-stretch': ['--aspect', 'stretch'],
    'true-widescreen': ['--true-widescreen'],
    'sharpness-1': ['--sharpness', '1'],
    'dlss-quality': ['--dlss', 'quality'],
    'volume-50': ['--volume', '50'],
}
# Options only the settings file carries (the panel's saved values), read with --load-settings.
# Each is compared with a settings-loaded baseline that changes nothing.
SETTINGS = {
    'settings-baseline': '',
    'brightness-1.5': 'brightness 1.5',
    'contrast-1.5': 'contrast 1.5',
    'vibrance-2': 'vibrance 2',
    'effects-2': 'effects 2',
    'anisotropy-1': 'anisotropy 1',
    'texpack-off': 'customtextures 0',
    'showfps': 'showfps 1',
    'performance-overlay': 'performance 1',
    'input-overlay': 'inputoverlay 1',
}


def content_box(im):
    """Bounding box of everything that is not the black letterbox."""
    mask = im.convert('L').point(lambda v: 255 if v > 6 else 0)
    return mask.getbbox()


def run(engine, name, args, out, settings=None):
    d = out / engine / name
    d.mkdir(parents=True, exist_ok=True)
    if settings is not None:
        (d / 'settings.ini').write_text('startup 0\n' + (settings + '\n' if settings else ''))
        args = ['--load-settings'] + args
    cap = d / 'frame.ppm'
    cmd = [str(ENGINES[engine]), '--iso', ISO, '--hidden', '--time-base', '1', '--fast', '--frames', '1100',
           '--window', '1280x960', '--script', str(ROOT / 'port/scripts/parity_vs.txt'),
           '--sys-dir', str(ROOT / 'port/slippi_sys'), '--card-dir', str(d / 'card'), '--user-dir', str(d / 'User'),
           '--settings-path', str(d / 'settings.ini'), '--replay-dir', str(d / 'Replays'), '--rng-seed', '305419896',
           '--capture', str(cap), '--capture-frame', '900', '--log-file', str(d / 'game.log')]
    if '--volume' not in args:
        cmd += ['--volume', '0']
    if engine == 'source':
        cmd.append('--vanilla-game')   # the Legacy side is the code-free recompilation
    cmd += args
    rc = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=400,
                        creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0)).returncode
    log = (d / 'game.log').read_text(encoding='utf-8', errors='replace') if (d / 'game.log').exists() else ''
    grab = lambda pat: (re.search(pat, log).group(1) if re.search(pat, log) else None)
    rec = dict(exit=rc, backend=grab(r'(?m)^(d3d1[12]): '),
               internal=grab(r'internal resolution (\d+x\d+ \(EFB x\d+)'),
               audio_volume=grab(r'audio: .*volume (\d+)%'), fatal=bool(re.search(r'game crash|game panic|game stopped', log)))
    if cap.exists():
        im = Image.open(cap).convert('RGB')
        rec['size'] = im.size
        rec['content_box'] = content_box(im)
    return rec, (Image.open(cap).convert('RGB') if cap.exists() else None)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    results = {}
    runs = [(n, a_, None) for n, a_ in OPTIONS.items()] + [(n, [], t) for n, t in SETTINGS.items()]
    for engine in ENGINES:
        base_im = None
        for name, args, settings in runs:
            rec, im = run(engine, name, args, a.out, settings)
            if name in ('baseline', 'settings-baseline'):
                base_im = im
            elif im is not None and base_im is not None and im.size == base_im.size:
                diff = ImageChops.difference(im, base_im).convert('L').point(lambda v: 255 if v > 8 else 0)
                rec['changed_vs_baseline_px'] = diff.histogram()[255]
            results.setdefault(name, {})[engine] = rec
            print(engine, name, json.dumps(rec), flush=True)
    summary = {}
    for name, per in results.items():
        s, l = per.get('source', {}), per.get('legacy', {})
        same = {k: s.get(k) == l.get(k) for k in ('backend', 'internal', 'audio_volume', 'content_box', 'size')}
        summary[name] = dict(both_exit_0=s.get('exit') == 0 and l.get('exit') == 0,
                             no_fatal=not s.get('fatal') and not l.get('fatal'), same=same,
                             source=s, legacy=l)
    (a.out / 'summary.json').write_text(json.dumps(summary, indent=1))
    for name, v in summary.items():
        print(f"{name:16s} exit_ok={v['both_exit_0']} no_fatal={v['no_fatal']} " +
              ' '.join(f"{k}={'=' if ok else 'DIFF'}" for k, ok in v['same'].items()) +
              f"  changed src={v['source'].get('changed_vs_baseline_px')} leg={v['legacy'].get('changed_vs_baseline_px')}")


if __name__ == '__main__':
    main()
