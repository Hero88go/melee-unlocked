#!/usr/bin/env python3
"""M9: compare a native/translated pair from tools/run_lockstep_pair.py over the first VS match.

lockstep_compare.py --align match refuses traces whose match frame repeats (a match that ends and
returns to character select keeps its frame counter). This takes each engine's rows from the
first retrace in scene 2:2 (a VS match) until the scene leaves it, keys them by match frame and
reports the exact prefix, the first differing field, and each engine's scene order.

    python tools/m9_pair_compare.py run-source/<pair-dir> [--json out.json]
"""
import argparse
import csv
import json
from pathlib import Path


def load(path):
    rows = list(csv.DictReader(open(path, newline='')))
    scenes, prev = [], None
    for r in rows:
        key = (int(r['scene_major'], 16), int(r['scene'], 16))
        if key != prev:
            scenes.append('%d:%d@%s' % (key[0], key[1], r['frame']))
            prev = key
    match, started = {}, False
    for r in rows:
        in_match = int(r['scene_major'], 16) == 2 and int(r['scene'], 16) == 2
        if in_match:
            started = True
            mf = int(r['match_frame'], 16)
            if mf and mf not in match:
                match[mf] = r
        elif started:
            break
    return scenes, match


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('pair', type=Path)
    ap.add_argument('--json', type=Path)
    a = ap.parse_args()
    ns, nm = load(a.pair / 'native' / 'state.csv')
    ts, tm = load(a.pair / 'translated' / 'state.csv')
    fields = [f for f in next(iter(nm.values())).keys() if f not in ('frame',)] if nm else []
    exact, first = 0, None
    for mf in sorted(set(nm) & set(tm)):
        diff = [f for f in fields if nm[mf][f] != tm[mf][f]]
        if diff:
            first = dict(match_frame=mf, field=diff[0], native=nm[mf][diff[0]], translated=tm[mf][diff[0]],
                         native_retrace=int(nm[mf]['frame']), translated_retrace=int(tm[mf]['frame']),
                         fields=diff[:8])
            break
        exact += 1
    report = dict(native_match_frames=len(nm), translated_match_frames=len(tm), exact_prefix=exact,
                  first_divergence=first, scenes_equal=[s.split('@')[0] for s in ns] == [s.split('@')[0] for s in ts],
                  native_scenes=ns, translated_scenes=ts)
    print(json.dumps(report, indent=1))
    if a.json:
        a.json.write_text(json.dumps(report, indent=1))


if __name__ == '__main__':
    main()
