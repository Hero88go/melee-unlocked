"""Compares two .slp recordings of the same online game (one from each player's machine) and reports
where they first disagree. Use it on a desync: the port player's replay and the Dolphin player's
replay of that match. Both clients record the frames they finalized, so any difference is the
desync itself, with the action states around it.

    python tools/slp_diff.py <port_replay.slp> <dolphin_replay.slp> [--context 6]
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from replay_compare import parse_slp

FIELDS = ("state", "x", "y", "facing", "percent", "shield", "stocks", "char")
INPUTS = ("jx", "jy", "cx", "cy", "trig", "buttons")


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
    frames = sorted(set(post_a) & set(post_b))
    print(f"A {args.a.name}: frames {min(post_a)}..{max(post_a)} | B {args.b.name}: frames {min(post_b)}..{max(post_b)} | shared {len(frames)}")
    first = None
    for f in frames:
        for key, pa in post_a[f].items():
            pb = post_b[f].get(key)
            if pb is None:
                continue
            bad = [k for k in FIELDS if pa[k] != pb[k]]
            if bad:
                first = (f, key, bad)
                break
        if first:
            break
    # Inputs can differ before any state does (a remote input applied on a different frame).
    first_input = None
    for f in sorted(set(pre_a) & set(pre_b)):
        for key, ia in pre_a[f].items():
            ib = pre_b[f].get(key)
            if ib is not None and any(ia[k] != ib[k] for k in INPUTS):
                first_input = (f, key)
                break
        if first_input:
            break
    if first_input:
        print(f"first input difference: frame {first_input[0]} player {first_input[1]}")
    if not first:
        print("post-frame state identical on every shared frame")
        return 0
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
