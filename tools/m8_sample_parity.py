#!/usr/bin/env python3
"""M8 sample parity from two paired frame-time CSVs (benchmark_native.py --frame-times output).

The raw `authored: sampled` totals depend on how many sub-frames each run happened to present,
so they drift between identical runs. This compares, per simulation frame, the mean number of
draws re-posed from their authored channels per presentation, and only presentations with
phase > 0 (sub-frames that actually sample). The two engines boot at different speeds, so the
Legacy sequence numbers are offset from Source; the offset is found as the one where per-frame total draw counts agree best.
"""
import argparse, csv, json
from collections import defaultdict
from pathlib import Path


COLUMN = 'posed'   # route-1 re-poses; `authored_draws` also counts the matrix-blend fallback


def per_frame(path, column):
    frames = defaultdict(lambda: [0, 0, 0])   # sequence -> [presentations, authored_sum, draws]
    with open(path, newline='') as f:
        for row in csv.DictReader(f):
            if float(row['phase']) <= 0.0:
                continue
            seq = int(row['simulation'])
            entry = frames[seq]
            entry[0] += 1
            entry[1] += int(row[column])
            entry[2] = int(row['draws'])
    return {s: (n, a / n, d) for s, (n, a, d) in frames.items() if n}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source', type=Path)
    ap.add_argument('legacy', type=Path)
    ap.add_argument('--min-draws', type=int, default=200, help='only frames with this many draws (in-match)')
    ap.add_argument('--max-offset', type=int, default=200)
    ap.add_argument('--out', type=Path)
    ap.add_argument('--column', default=COLUMN, help='per-presentation count to compare (posed, posed_skinned)')
    a = ap.parse_args()
    src, leg = per_frame(a.source, a.column), per_frame(a.legacy, a.column)

    best = None
    for off in range(-a.max_offset, a.max_offset + 1):
        common = [s for s in src if s - off in leg and src[s][2] >= a.min_draws]
        if len(common) < 100:
            continue
        agree = sum(src[s][2] == leg[s - off][2] for s in common) / len(common)
        if best is None or agree > best[1]:
            best = (off, agree, len(common))
    if best is None:
        raise SystemExit('no alignment with at least 100 common in-match frames')
    off = best[0]
    common = sorted(s for s in src if s - off in leg and src[s][2] >= a.min_draws
                    and src[s][2] == leg[s - off][2])
    src_total = sum(src[s][1] for s in common)
    leg_total = sum(leg[s - off][1] for s in common)
    diff = abs(src_total - leg_total) / max(leg_total, 1e-9) * 100.0
    worst = max(common, key=lambda s: abs(src[s][1] - leg[s - off][1]))
    result = dict(column=a.column, offset=off, draw_count_agreement=best[1], frames=len(common),
                  source_mean_authored_per_frame=src_total / len(common),
                  legacy_mean_authored_per_frame=leg_total / len(common),
                  difference_percent=diff,
                  frames_exactly_equal=sum(abs(src[s][1] - leg[s - off][1]) < 1e-9 for s in common),
                  worst_frame=dict(source_sequence=worst, source=src[worst][1], legacy=leg[worst - off][1]))
    text = json.dumps(result, indent=2)
    print(text)
    if a.out:
        a.out.write_text(text + '\n')


if __name__ == '__main__':
    main()
