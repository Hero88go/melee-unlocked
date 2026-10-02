#!/usr/bin/env python3
"""Print the item events of a Slippi replay for a frame range.

One line per item per frame: frame, item kind, state, facing, velocity, position, timer and
spawn id. A recording without item events (the Source Port's playback recordings carry none)
prints a note instead.

    py -3.12 tools/fieldmap/slp_items.py replay.slp --first 350 --last 380
"""
import argparse
import struct
import sys
from pathlib import Path

ITEM_UPDATE = 0x3B


def item_events(path):
    data = Path(path).read_bytes()
    if data[:11] != b"{U\x03raw[$U#l":
        raise ValueError("not a Slippi replay")
    raw_length = struct.unpack_from(">I", data, 11)[0]
    pos = 15
    if data[pos] != 0x35:
        raise ValueError("the replay does not start with the event payloads event")
    count = data[pos + 1]
    sizes = {}
    for index in range((count - 1) // 3):
        command, size = struct.unpack_from(">BH", data, pos + 2 + 3 * index)
        sizes[command] = size
    pos += 1 + count
    end = 15 + raw_length if raw_length else len(data)
    events = []
    while pos < end and data[pos] in sizes:
        command, size = data[pos], sizes[data[pos]]
        if command == ITEM_UPDATE and size >= 0x26:
            frame, kind, state, facing, vx, vy, x, y, damage, timer, spawn = struct.unpack_from(
                ">iHBfffffHfI", data, pos + 1)
            events.append(dict(frame=frame, kind=kind, state=state, facing=facing, vx=vx, vy=vy,
                               x=x, y=y, damage=damage, timer=timer, spawn=spawn))
        pos += 1 + size
    return events, ITEM_UPDATE in sizes


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("replay", type=Path)
    ap.add_argument("--first", type=int, default=-123)
    ap.add_argument("--last", type=int, default=1 << 30)
    args = ap.parse_args(argv)
    try:
        events, declared = item_events(args.replay)
    except (OSError, ValueError, struct.error) as exc:
        print(f"{args.replay}: {exc}")
        return 2
    if not declared:
        print(f"{args.replay.name}: this recording carries no item events")
        return 0
    shown = [e for e in events if args.first <= e["frame"] <= args.last]
    print(f"{args.replay.name}: {len(events)} item events, {len(shown)} in frames "
          f"{args.first}..{args.last}")
    for e in shown:
        print(f"  frame {e['frame']:5} kind {e['kind']:3} state {e['state']} facing {e['facing']:+.0f} "
              f"vel ({e['vx']:.3f}, {e['vy']:.3f}) pos ({e['x']:.2f}, {e['y']:.2f}) "
              f"timer {e['timer']:.0f} spawn {e['spawn']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
