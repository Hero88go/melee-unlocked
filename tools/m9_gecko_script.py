#!/usr/bin/env python3
"""M9: input scripts that exercise the native General Codes against the general-codes recompilation.

The menu part is parity_vs.txt's (VS Mode, two human ports), with the cursor move shortened for
the General Codes' hand start so it picks Fox and Ness. --random-stage presses D-pad down at the
character select screen instead of START, so the stage comes from the default singles list
through random stage select (NeutralSpawn's six stages). The match part is seeded pseudo-random play for both ports: dash flicks and dashbacks
through the UCF pad buffer, near-cardinal sticks, shields with downward flicks on platforms,
attacks and C-stick smashes that make hits (SDI, tumble), and runs of neutral.

    python tools/m9_gecko_script.py --out run-source/x/script.txt --frames 6000 --seed 1 [--random-stage]
"""
import argparse
import random
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


# Stage select paths once every stage is unlocked (the cursor starts bottom centre). 'parity' is
# parity_vs.txt's, which lands on Onett; 'fod' goes right 7 frames, then up, to Fountain of Dreams.
SSS_PATHS = {
    'parity': ['700 sy=100', '740', '760 A', '770'],
    'fod': ['700 sx=100', '707 sy=100', '771', '790 A', '800'],
}


def menu_prefix(random_stage, sss='parity', vanilla=False):
    lines = []
    for line in (ROOT / 'port/scripts/parity_vs.txt').read_text(encoding='utf-8').splitlines():
        if line.startswith('@match'):
            break
        # The General Codes start the character-select hands on the HMN buttons, 19 units higher,
        # so the parity path's 30-frame move overshoots the grid; 12 frames picks Fox and Ness.
        # --vanilla keeps the parity path (Ness and Pikachu) for the code-free game.
        if not vanilla and line.strip() == '430':
            line = '412'
        elif not vanilla and line.strip() == '430 p=2':
            line = '412 p=2'
        lines.append(line)
    if sss != 'parity':
        start = lines.index('700 sy=100')
        lines[start:start + 4] = SSS_PATHS[sss]
    if random_stage:
        # CSS confirm at 500: D-pad down instead of START; the stage select screen is skipped.
        out = []
        in_menu = False
        for line in lines:
            s = line.strip()
            if s.startswith('@scene'):
                in_menu = True
            if in_menu and s == '500 START':
                out.append('500 DD')
            elif in_menu and s.split(' ')[0] in ('700', '740', '760', '770') and not s.startswith('#'):
                continue
            else:
                out.append(line)
        lines = out
    return lines


STICKS = [(0, 0), (127, 0), (-127, 0), (0, 127), (0, -127), (80, 0), (-80, 0), (79, 0), (-79, 0),
          (100, 5), (-100, -6), (100, 7), (0, -80), (6, -100), (-7, 90), (90, 90), (-90, -90),
          (60, -60), (40, 0), (-40, 0), (30, -30), (0, -40), (0, -60), (0, -70), (0, -90)]


def segment(rng, port):
    """One held input: (duration, tokens)."""
    r = rng.random()
    toks = []
    if r < 0.18:                                   # neutral
        return rng.randint(1, 6), toks
    if r < 0.30:                                   # dash / dashback flick pair
        a = rng.choice((-127, -110, -90, -60, -40, 40, 60, 90, 110, 127))
        return 0, [('flick', a, -a if rng.random() < 0.7 else a // 2)]
    if r < 0.42:                                   # shield, often with a downward flick
        toks.append('l=255')
        if rng.random() < 0.7:
            y = rng.choice((-30, -40, -50, -60, -70, -80, -90, -110, -127))
            toks.append('sy=%d' % y)
            toks.append('sx=%d' % rng.choice((0, 0, 5, -5, 20, -20)))
        return rng.randint(2, 10), toks
    sx, sy = rng.choice(STICKS)
    if sx:
        toks.append('sx=%d' % sx)
    if sy:
        toks.append('sy=%d' % sy)
    b = rng.random()
    if b < 0.25:
        toks.append('A')
    elif b < 0.35:
        toks.append('B')
    elif b < 0.45:
        toks.append(rng.choice(('X', 'Y')))
    elif b < 0.50:
        toks.append('Z')
    elif b < 0.60:
        cx, cy = rng.choice(((127, 0), (-127, 0), (0, 127), (0, -127)))
        toks += ['cx=%d' % cx, 'cy=%d' % cy]
    return rng.randint(1, 8), toks


def match_lines(frames, seed):
    rng = random.Random(seed)
    lines = []
    for port in (1, 2):
        f = 90
        prefix = '' if port == 1 else 'p=2 '
        while f < frames:
            dur, toks = segment(rng, port)
            if toks and isinstance(toks[0], tuple):
                _, a, b = toks[0]
                # two polls at a, then b: the pad buffer's two-sample difference crosses 75
                release = f + 2 + rng.randint(3, 9)
                lines.append((f, '%d %ssx=%d' % (f, prefix, a)))
                lines.append((f + 2, '%d %ssx=%d' % (f + 2, prefix, b)))
                lines.append((release, ('%d %s' % (release, prefix)).rstrip()))
                f += 12
                continue
            lines.append((f, ('%d %s%s' % (f, prefix, ' '.join(toks))).rstrip()))
            f += max(1, dur)
    lines.sort(key=lambda x: x[0])
    return [l for _, l in lines]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--frames', type=int, default=6000, help='match frames of input')
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--random-stage', action='store_true')
    ap.add_argument('--sss', choices=sorted(SSS_PATHS), default='parity')
    ap.add_argument('--vanilla', action='store_true',
                    help='menu path for the code-free game (paired with build-vanilla)')
    a = ap.parse_args()
    lines = ['# Generated by tools/m9_gecko_script.py --frames %d --seed %d%s --sss %s' %
             (a.frames, a.seed, ' --random-stage' if a.random_stage else '', a.sss)]
    lines += menu_prefix(a.random_stage, a.sss, a.vanilla)
    lines += ['', '@match'] + match_lines(a.frames, a.seed)
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print('%s: %d lines' % (a.out, len(lines)))


if __name__ == '__main__':
    main()
