"""Compares two MELEE_TRACE_SLIPPI_CMDS=1 logs (static recomp vs native, or two runs).

Each log line looks like
  slippi-cmd: seq N retrace R cmd B3 len 0 payload  reply 962 fnv 1234ABCD [(xK)]
Normalization drops what differs by engine or timing: sequence numbers, retrace counts, repeat
counts, the match stream (B0-B2, BD), rank traffic (E3-E5, static recomp only), and B3 reply
hashes (B3 runs every frame; its content is compared as a list of distinct consecutive replies).
The remaining command sequence and payloads must be equal.

usage: slippi_trace_diff.py a.log b.log [--keep-b3-hash] [--show N]
Exit status 0 when equal, 1 when they differ, 2 on usage errors.
"""
import argparse
import difflib
import re
import sys

LINE = re.compile(r"slippi-cmd: seq \d+ retrace \d+ cmd ([0-9A-F]{2}) len (\d+) payload (\S*)\s+reply (\d+) fnv ([0-9A-F]{8})( unhandled)?")
DROP = {"B0", "B1", "B2", "BD", "E3", "E4", "E5"}


def normalize(path, keep_b3_hash):
    out = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for raw in f:
            m = LINE.search(raw)
            if not m:
                continue
            cmd, length, payload, reply, fnv, unhandled = m.groups()
            if cmd in DROP:
                continue
            if cmd == "B3" and not keep_b3_hash:
                fnv = "-"
            item = f"{cmd} len {length} payload {payload or '-'} reply {reply} {fnv}{' unhandled' if unhandled else ''}"
            if out and out[-1] == item:
                continue   # collapse runs (and the (xK) repeat line of a run)
            out.append(item)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--keep-b3-hash", action="store_true", help="also compare B3 reply hashes")
    ap.add_argument("--show", type=int, default=3, help="context lines in the diff")
    args = ap.parse_args()
    try:
        a = normalize(args.a, args.keep_b3_hash)
        b = normalize(args.b, args.keep_b3_hash)
    except OSError as e:
        print(e, file=sys.stderr)
        return 2
    if a == b:
        print(f"equal: {len(a)} normalized commands")
        return 0
    for line in difflib.unified_diff(a, b, args.a, args.b, n=args.show, lineterm=""):
        print(line)
    return 1


if __name__ == "__main__":
    sys.exit(main())
