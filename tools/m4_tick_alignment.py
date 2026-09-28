#!/usr/bin/env python3
"""M4: which AX tick services each sound request, native versus Legacy.

Reads the `[ax-driver-queue-*]` trace lines from a run_lockstep_pair.py run and, for every queued
request, the first tick at which it appears (frame=-1, not yet assigned a voice) and the tick at
which it gets its voice. The two engines boot at different speeds, so their absolute tick numbers
differ by a constant; a constant offset leaves every voice aligned in the PCM stream. Any request
whose offset differs from the common one starts one AX frame (5 ms) early or late relative to the
others, which changes how overlapping voices sum and so the mixed peak, not the RMS.
"""
import argparse, collections, json, re
from pathlib import Path

LINE = re.compile(r'\[ax-driver-queue-(native|legacy)\] retrace=(\d+) tick=(\d+) depth=\d+ id=(\d+) vid=(-?\d+) frame=(-?\d+)')


def requests(log):
    first = {}          # (id, occurrence) -> dict
    seen_frame = collections.Counter()
    active = {}         # id -> key of the open (unassigned) request
    for line in Path(log).read_text(encoding='utf-8', errors='replace').splitlines():
        m = LINE.search(line)
        if not m:
            continue
        _, retrace, tick, sid, vid, frame = m.groups()
        retrace, tick, sid, vid, frame = int(retrace), int(tick), int(sid), int(vid), int(frame)
        if frame == -1 and sid not in active:
            seen_frame[sid] += 1
            key = (sid, seen_frame[sid])
            first[key] = dict(id=sid, n=seen_frame[sid], queued_tick=tick, queued_retrace=retrace)
            active[sid] = key
        elif frame != -1 and sid in active:
            key = active.pop(sid)
            first[key].update(voice_tick=frame, voice_retrace=retrace)
    return first


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('run', type=Path, help='run_lockstep_pair.py output directory (native/ and translated/)')
    ap.add_argument('--out', type=Path)
    a = ap.parse_args()
    nat = requests(a.run / 'native' / 'game.log')
    leg = requests(a.run / 'translated' / 'game.log')
    common = sorted(set(nat) & set(leg), key=lambda k: nat[k]['queued_tick'])
    offsets = collections.Counter(leg[k]['queued_tick'] - nat[k]['queued_tick'] for k in common)
    base = offsets.most_common(1)[0][0] if offsets else 0
    rows = []
    for k in common:
        n, l = nat[k], leg[k]
        rows.append(dict(id=k[0], n=k[1], native_tick=n['queued_tick'], legacy_tick=l['queued_tick'],
                         offset=l['queued_tick'] - n['queued_tick'],
                         native_retrace=n['queued_retrace'], legacy_retrace=l['queued_retrace'],
                         voice_offset=(l.get('voice_tick', 0) - n.get('voice_tick', 0)) if 'voice_tick' in n and 'voice_tick' in l else None))
    shifted = [r for r in rows if r['offset'] != base]
    result = dict(common_requests=len(common), native_only=len(set(nat) - set(leg)), legacy_only=len(set(leg) - set(nat)),
                  offset_histogram={str(k): v for k, v in sorted(offsets.items())}, common_offset=base,
                  shifted=shifted, rows=rows)
    text = json.dumps(result, indent=2)
    if a.out:
        a.out.write_text(text + '\n')
    print(json.dumps({k: result[k] for k in ('common_requests', 'native_only', 'legacy_only', 'offset_histogram', 'common_offset')}, indent=2))
    for r in shifted[:40]:
        print('shifted:', r)


if __name__ == '__main__':
    main()
