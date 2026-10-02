#!/usr/bin/env python3
"""Print the declared Fighter fields around a digest's first difference, for both engines.

After tools/fighter_digest.py reports a first difference, this shows every schema field (the
action state is fp.motion_id) of the differing player for the frames before and after it, the
console value next to the native one, with the differing ones marked. With --all-players the
other players are printed too.

    py -3.12 tools/fieldmap/digest_context.py static/fighters.bin source/fighters.bin \
        --schema digest/fighter_core_ak.json --result digest/core-digest.json --span 5
"""
import argparse
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from fighter_digest import read_records  # noqa: E402

FORMATS = {"u8": ">B", "i8": ">b", "u16": ">H", "i16": ">h", "u32": ">I", "i32": ">i",
           "u64": ">Q", "i64": ">q", "f32": ">f", "f64": ">d"}


def show(raw, kind):
    value = struct.unpack(FORMATS[kind], raw)[0]
    if kind.startswith("f"):
        return f"{value:.6g} [{raw.hex()}]"
    return str(value)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("console", type=Path)
    ap.add_argument("native", type=Path)
    ap.add_argument("--schema", type=Path, required=True)
    ap.add_argument("--result", type=Path, help="fighter_digest.py result with first_difference")
    ap.add_argument("--frame", type=int, help="centre frame, when there is no --result")
    ap.add_argument("--port", type=int, default=None)
    ap.add_argument("--span", type=int, default=5)
    ap.add_argument("--all-players", action="store_true")
    args = ap.parse_args(argv)

    schema = json.loads(args.schema.read_text(encoding="utf-8"))
    types = {"core:" + field["path"]: field["type"] for field in schema["fields"]}
    frame, port = args.frame, args.port
    if args.result:
        first = json.loads(args.result.read_text(encoding="utf-8")).get("first_difference")
        if not first:
            print("the result has no first difference")
            return 0
        frame = first["frame"] if frame is None else frame
        port = first["port"] if port is None else port
        print(f"first difference: frame {first['frame']} port {first['port']} "
              f"follower {first['follower']} field {first['field']}: "
              f"console {first.get('console')} native {first.get('native')}")
    if frame is None:
        ap.error("give --result or --frame")
    console = read_records(args.console, "console", schema)
    native = read_records(args.native, "native", schema)
    keys = sorted(key for key in console.keys() | native.keys()
                  if frame - args.span <= key[0] <= frame + args.span
                  and (args.all_players or port is None or key[1] == port))
    for key in keys:
        a, b = console.get(key), native.get(key)
        print(f"\nframe {key[0]} port {key[1]} follower {key[2]}"
              + ("" if a and b else "   (missing on " + ("console" if not a else "native") + ")"))
        for name, kind in types.items():
            left = show(a[name], kind) if a else "-"
            right = show(b[name], kind) if b else "-"
            mark = "  <-- differs" if a and b and a[name] != b[name] else ""
            print(f"  {name[5:]:28} console {left:28} native {right}{mark}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
