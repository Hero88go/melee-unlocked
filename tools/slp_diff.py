"""Compares two .slp recordings of the same online game (one from each player's machine) and reports
where they first disagree. Use it on a desync: the port player's replay and the Dolphin player's
replay of that match. Both clients record the frames they finalized, so any difference is the
desync itself, with the action states around it.

    python tools/slp_diff.py <port_replay.slp> <dolphin_replay.slp> [--context 6]
"""
import argparse
import sys
import struct
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from replay_compare import parse_slp

FIELDS = ("state", "x", "y", "facing", "percent", "shield", "stocks", "char")
INPUTS = ("jx", "jy", "cx", "cy", "trig", "buttons")


def first_difference(a, b, fields):
    """Compare full coverage, including players and the sign bit of floating zero."""
    if not a or not b:
        return (None, None, ["empty recording"])
    for frame in sorted(set(a) | set(b)):
        if frame not in a or frame not in b:
            return (frame, None, ["missing frame in " + ("A" if frame not in a else "B")])
        for key in sorted(set(a[frame]) | set(b[frame])):
            if key not in a[frame] or key not in b[frame]:
                return (frame, key, ["missing player in " + ("A" if key not in a[frame] else "B")])
            left, right = a[frame][key], b[frame][key]
            bad = []
            for field in fields:
                if field not in left or field not in right:
                    bad.append(field)
                    continue
                x, y = left[field], right[field]
                equal = (struct.pack(">f", x) == struct.pack(">f", y)
                         if isinstance(x, float) and isinstance(y, float) else x == y)
                if not equal:
                    bad.append(field)
            if bad:
                return (frame, key, bad)
    return None


def fmt_post(p):
    return f"state {p['state']:3d} x {p['x']:9.4f} y {p['y']:9.4f} face {p['facing']:+.0f} {p['percent']:5.1f}% shield {p['shield']:6.3f}"


def fmt_pre(p):
    return f"stick {p['jx']:+.4f},{p['jy']:+.4f} c {p['cx']:+.4f},{p['cy']:+.4f} trig {p['trig']:.4f} buttons {p['buttons']:08X}"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("a", type=Path)
    ap.add_argument("b", type=Path)
    ap.add_argument("--context", type=int, default=6)
    args = ap.parse_args()
    _, post_a, pre_a = parse_slp(args.a)
    _, post_b, pre_b = parse_slp(args.b)
    if not post_a or not post_b:
        print("FAIL: one or both recordings have no post-frame events")
        return 1
    print(f"A {args.a.name}: frames {min(post_a)}..{max(post_a)} | B {args.b.name}: frames {min(post_b)}..{max(post_b)}")
    first = first_difference(post_a, post_b, FIELDS)
    # Inputs can differ before any state does (a remote input applied on a different frame).
    first_input = first_difference(pre_a, pre_b, INPUTS)
    if first_input:
        print(f"first input difference: frame {first_input[0]} player {first_input[1]}: {', '.join(first_input[2])}")
    if not first:
        print("post-frame state identical on every frame, with identical player coverage")
        return 1 if first_input else 0
    f0, key, bad = first
    print(f"first state difference: frame {f0} player/follower {key}: {', '.join(bad)}")
    for f in range(f0 - args.context, f0 + 2):
        for k in sorted(set(post_a.get(f, {})) | set(post_b.get(f, {}))):
            pa, pb = post_a.get(f, {}).get(k), post_b.get(f, {}).get(k)
            ia, ib = pre_a.get(f, {}).get(k), pre_b.get(f, {}).get(k)
            mark = "  " if pa == pb else "!!"
            print(f"{mark} {f:6d} p{k[0]}{'f' if k[1] else ' '}  A {fmt_post(pa) if pa else '-'}")
            print(f"            B {fmt_post(pb) if pb else '-'}")
            if ia != ib:
                print(f"   inputs   A {fmt_pre(ia) if ia else '-'}")
                print(f"            B {fmt_pre(ib) if ib else '-'}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
