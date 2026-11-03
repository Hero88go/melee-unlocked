#!/usr/bin/env python3
"""Linux observer's raw decimal columns -> Windows named, uppercase hex CSV."""
import csv
import sys

FIGHTER = ('present', 'stocks', 'action', 'anim_frame', 'pos_x', 'pos_y', 'pos_z',
           'vel_x', 'vel_y', 'vel_z', 'percent', 'facing')
RAW = ['frame', 'scene_major', 'scene', 'match_frame', 'rng'] + [
    f'p{p}_{f}' for p in range(6) for f in FIGHTER]
WINDOWS = ['frame', 'rng', 'scene'] + RAW[5:] + ['scene_major', 'match_frame']

def convert(source, target):
    with open(source, newline='') as src, open(target, 'w', newline='') as dst:
        writer = csv.DictWriter(dst, fieldnames=WINDOWS)
        writer.writeheader()
        for row in csv.reader(src):
            if len(row) != len(RAW):
                raise ValueError(f'expected {len(RAW)} columns, got {len(row)}')
            values = dict(zip(RAW, map(int, row)))
            writer.writerow({k: str(v) if k == 'frame' else f'{v:08X}'
                             for k, v in values.items()})

if __name__ == '__main__':
    convert(sys.argv[1], sys.argv[2])
