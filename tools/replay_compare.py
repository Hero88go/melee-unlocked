"""Replay a Slippi recording on Source or Static and gate every reference player-frame.

Graphics --backend is independent of --engine. Comparison is bit exact by default;
--allow-preframe-ulp permits only the entry-animation cases documented in
docs/replay-validation.md. Runs are hidden and muted unless --visible is supplied.
"""
import argparse
import json
import math
import os
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from melee_iso import require_iso

ROOT = Path(__file__).resolve().parents[1]
FIELDS = ("state", "x", "y", "facing", "percent", "shield", "stocks", "char")
# Exact original -> playback float32 words from the documented retail baseline.
# No arbitrary one-ULP entry-animation value is accepted.
KNOWN_PREFRAME_ULP = {
    (-118, (0, 0), "y"): {(0x41D51E94, 0x41D51E93)},
    (-113, (1, 0), "y"): {(0x41D51E94, 0x41D51E93), (0x41E056E4, 0x41E056E3),
                            (0x41E07070, 0x41E07071), (0x41E05C00, 0x41E05BFF)},
    (-108, (2, 0), "y"): {(0x41E07070, 0x41E07071)},
}


def parse_slp(path):
    """Return settings, post events and pre events; reject incomplete event payloads.

    Rollback can rewrite a frame, so retain the last event for each player/frame,
    as the previous reader did. For a zero-length raw block, a cut-off final
    known event is allowed, but its unfinished frame is excluded from coverage.
    Finalized raw blocks and unknown/malformed events remain strict.
    """
    d = Path(path).read_bytes()
    i = d.find(b"raw[$U#l")
    if i < 0 or i + 12 > len(d):
        raise ValueError(f"{path}: no valid raw block header")
    n = struct.unpack(">I", d[i + 8:i + 12])[0]
    if n and i + 12 + n > len(d):
        raise ValueError(f"{path}: truncated raw block")
    raw = d[i + 12:i + 12 + n] if n else d[i + 12:]
    if len(raw) < 2 or raw[0] != 0x35:
        raise ValueError(f"{path}: raw block does not start with event sizes")
    table_size = raw[1]
    if table_size < 1 or (table_size - 1) % 3 or 1 + table_size > len(raw):
        raise ValueError(f"{path}: malformed event-size table")
    count = (table_size - 1) // 3
    sizes = {raw[2 + 3*k]: struct.unpack(">H", raw[3 + 3*k:5 + 3*k])[0]
             for k in range(count)}
    if len(sizes) != count:
        raise ValueError(f"{path}: duplicate event-size entry")
    pos = 1 + table_size
    post, pre, start = {}, {}, None
    active_frame, last_complete_frame, cut_frame = None, None, None
    while pos < len(raw):
        c = raw[pos]
        size = sizes.get(c)
        if size is None:
            raise ValueError(f"{path}: unknown event at raw offset {pos}")
        if pos + 1 + size > len(raw):
            if n:
                raise ValueError(f"{path}: truncated event at raw offset {pos}")
            tail = raw[pos:]
            cut_frame = (struct.unpack(">i", tail[1:5])[0]
                         if c in (0x37, 0x38, 0x3A, 0x3B, 0x3C) and len(tail) >= 5
                         else active_frame)
            break
        body = raw[pos:pos + 1 + size]
        minimum = {0x36: 5 + 0x138, 0x37: 0x31, 0x38: 0x22}.get(c, 1)
        if len(body) < minimum:
            raise ValueError(f"{path}: event {c:02x} is too short")
        if c in (0x3A, 0x3C) and len(body) >= 5:
            active_frame = struct.unpack(">i", body[1:5])[0]
            if c == 0x3C:
                last_complete_frame = active_frame
        if c == 0x36:
            if start is not None:
                raise ValueError(f"{path}: multiple game-start events")
            start = bytes(body)
        elif c in (0x37, 0x38):
            frame = struct.unpack(">i", body[1:5])[0]
            player, follower = body[5], body[6]
            if player > 3 or follower > 1:
                raise ValueError(f"{path}: invalid player/follower index")
            if c == 0x37:
                jx, jy, cx, cy, trig = struct.unpack(">fffff", body[0x19:0x2D])
                buttons = struct.unpack(">I", body[0x2D:0x31])[0]
                pre.setdefault(frame, {})[(player, follower)] = dict(
                    jx=jx, jy=jy, cx=cx, cy=cy, trig=trig, buttons=buttons)
            else:
                x, y, facing = struct.unpack(">fff", body[0xA:0x16])
                percent, shield = struct.unpack(">ff", body[0x16:0x1E])
                post.setdefault(frame, {})[(player, follower)] = dict(
                    char=body[7], state=struct.unpack(">H", body[8:10])[0],
                    x=x, y=y, facing=facing, percent=percent, shield=shield,
                    stocks=body[0x21], _float_bits={field: body[offset:offset + 4].hex()
                        for field, offset in (("x", 0xA), ("y", 0xE), ("facing", 0x12),
                                              ("percent", 0x16), ("shield", 0x1A))})
        pos += 1 + size
    if start is None:
        raise ValueError(f"{path}: no game-start event")
    if not n:
        # A frame bookend establishes complete coverage even when the next
        # frame's pre/item/post stream was interrupted at an event boundary.
        if last_complete_frame is not None:
            post = {frame: players for frame, players in post.items()
                    if frame <= last_complete_frame}
        if cut_frame is not None:
            post = {frame: players for frame, players in post.items() if frame < cut_frame}
            pre = {frame: players for frame, players in pre.items() if frame < cut_frame}
    return start, post, pre


def same_value(a, b):
    if isinstance(a, float) and isinstance(b, float):
        return struct.pack(">f", a) == struct.pack(">f", b)
    return a == b


def same_field(a, b, field):
    # Keep even signaling NaN payloads exact: converting them through a Python
    # float can quiet the NaN and erase an original bit difference.
    if field in a.get("_float_bits", {}) and field in b.get("_float_bits", {}):
        return a["_float_bits"][field] == b["_float_bits"][field]
    return same_value(a[field], b[field])


def known_preframe_difference(frame, key, field, a, b):
    if (frame, key, field) not in KNOWN_PREFRAME_ULP:
        return False
    if not isinstance(a, float) or not isinstance(b, float):
        return False
    if not math.isfinite(a) or not math.isfinite(b) or a <= 0 or b <= 0:
        return False
    left, right = [struct.unpack(">I", struct.pack(">f", v))[0] for v in (a, b)]
    return (left, right) in KNOWN_PREFRAME_ULP[(frame, key, field)]


def compare_posts(original, recorded, allow_preframe_ulp=False):
    """Compare the full reference interval, including missing frames and followers.

    Source can emit a terminal post frame after playback stops. Output outside the
    reference interval is reported, while every requested frame must be present.
    """
    result = dict(frames=len(original), player_frames=0, mismatches=0,
                  in_play_mismatches=0, unexpected_mismatches=0,
                  allowed_preframe_mismatches=0, coverage_mismatches=0,
                  first=None, first_in_play=None, first_unexpected=None,
                  original_first=min(original) if original else None,
                  original_last=max(original) if original else None,
                  recorded_first=min(recorded) if recorded else None,
                  recorded_last=max(recorded) if recorded else None,
                  outside_reference_frames=len(set(recorded) - set(original)))
    if not original or not any(f >= 0 for f in original):
        result.update(gate_pass=False, error="reference has no in-play post-frame events")
        return result
    if len(original) != max(original) - min(original) + 1:
        result.update(gate_pass=False, error="reference post-frame interval has gaps")
        return result

    def difference(frame, key, field, a=None, b=None, coverage=False, allowed=False):
        detail = dict(frame=frame, port=key[0] if key else None,
                      follower=key[1] if key else None, field=field,
                      original=repr(a), port_value=repr(b))
        result["mismatches"] += 1
        result["first"] = result["first"] or detail
        if frame >= 0:
            result["in_play_mismatches"] += 1
            result["first_in_play"] = result["first_in_play"] or detail
        if coverage:
            result["coverage_mismatches"] += 1
        if allowed:
            result["allowed_preframe_mismatches"] += 1
        else:
            result["unexpected_mismatches"] += 1
            result["first_unexpected"] = result["first_unexpected"] or detail

    for frame in sorted(original):
        expected, actual = original[frame], recorded.get(frame)
        if not expected:
            difference(frame, None, "empty_reference_frame", coverage=True)
            continue
        if actual is None:
            for key in sorted(expected):
                difference(frame, key, "missing_frame", coverage=True)
            continue
        for key in sorted(expected.keys() | actual.keys()):
            a, b = expected.get(key), actual.get(key)
            if a is None or b is None:
                difference(frame, key, "player_coverage", a is not None, b is not None,
                           coverage=True)
                continue
            result["player_frames"] += 1
            bad = [field for field in FIELDS if field not in a or field not in b
                   or not same_field(a, b, field)]
            if bad:
                allowed = allow_preframe_ulp and all(
                    field in a and field in b and
                    known_preframe_difference(frame, key, field, a[field], b[field])
                    for field in bad)
                field = bad[0]
                difference(frame, key, field, a.get(field), b.get(field), allowed=allowed)
    result["gate_pass"] = result["unexpected_mismatches"] == 0
    return result


def game_command(args):
    out = args.out
    cmd = [str(args.exe), "--iso", str(args.iso), "--replay", str(args.replay),
           "--volume", "0", "--no-music", "--replay-dir", str(out),
           "--card-dir", str(out / "card"), "--log-file", str(out / "port.log"),
           "--settings-path", str(out / "settings.ini"), "--user-dir", str(out / "User"),
           "--shader-cache", str(out / "shadercache")]
    if args.engine == "static":
        cmd += ["--sys-dir", str(ROOT / "port/slippi_sys_playback")]
    if args.mod_base_iso:
        cmd += ["--mod-base-iso", str(args.mod_base_iso)]
    if not args.visible:
        cmd += ["--hidden", "--fast"]
    if args.backend:
        cmd += ["--backend", args.backend]
    # Preserve the existing whitespace-separated batch option convention.
    cmd += os.environ.get("REPLAY_COMPARE_EXTRA", "").split()
    return cmd


def print_comparison(result):
    print(f"original frames {result['original_first']}..{result['original_last']} "
          f"({result['frames']}), recorded {result['recorded_first']}..{result['recorded_last']}")
    print(f"{result['player_frames']} player-frames compared over {result['frames']} "
          f"reference frames; {result['mismatches']} mismatches")
    for label, key in (("first divergence", "first"),
                       ("first in-play divergence", "first_in_play")):
        if key == "first_in_play":
            print(f"mismatches from frame 0 on: {result['in_play_mismatches']}")
        item = result[key]
        if item:
            print(f"{label}: frame {item['frame']} player/follower "
                  f"({item['port']}, {item['follower']}) {item['field']}: "
                  f"original {item['original']} vs port {item['port_value']}")
    print(f"gate: {'PASS' if result['gate_pass'] else 'FAIL'}; "
          f"{result['allowed_preframe_mismatches']} known preframe differences, "
          f"{result['coverage_mismatches']} coverage mismatches")
    if result.get("error"):
        print(result["error"])


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("replay", type=Path)
    ap.add_argument("--exe", type=Path, default=None)
    engine = ap.add_mutually_exclusive_group()
    engine.add_argument("--engine", choices=("source", "static"), default="static")
    engine.add_argument("--source", action="store_true", help="alias for --engine source")
    ap.add_argument("--iso", type=Path, default=None)
    ap.add_argument("--mod-base-iso", type=Path, help="vanilla base disc for a Static mod disc")
    ap.add_argument("--out", type=Path, default=ROOT / "reports/native-validation/playback")
    ap.add_argument("--timeout", type=float, default=900)
    ap.add_argument("--visible", action="store_true", help="show the window instead of running hidden")
    ap.add_argument("--backend", choices=("d3d11", "d3d12"), help="graphics backend")
    ap.add_argument("--allow-preframe-ulp", action="store_true",
                    help="allow only documented entry-animation y differences of one ULP")
    args = ap.parse_args(argv)
    if args.timeout <= 0:
        ap.error("--timeout must be positive")
    args.engine = "source" if args.source else args.engine
    args.iso = require_iso(args.iso).resolve()
    if args.mod_base_iso:
        args.mod_base_iso = require_iso(args.mod_base_iso).resolve()
    args.exe = (args.exe or ROOT / ("build-sourceport/port/Release/melee_source.exe"
                 if args.engine == "source" else "build-review/port/Release/melee_port_playback.exe")).resolve()
    args.replay, args.out = args.replay.resolve(), args.out.resolve()
    if args.out == args.replay.parent:
        ap.error("--out must differ from the reference replay directory")
    args.out.mkdir(parents=True, exist_ok=True)
    result_path = args.out / "result.json"
    result_path.unlink(missing_ok=True)
    result = dict(status="error", gate_pass=False, replay=str(args.replay),
                  engine=args.engine, allow_preframe_ulp=args.allow_preframe_ulp)

    def finish(code):
        result["exit_code"] = code
        result_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        return code

    try:
        _, original, _ = parse_slp(args.replay)
        if not original or not any(frame >= 0 for frame in original):
            raise ValueError("reference has no in-play post-frame events")
        for old in args.out.glob("*.slp"):
            old.unlink()
        cmd = game_command(args)
        result["command"] = cmd
        print("running:", " ".join(cmd[1:]))
        env = dict(os.environ, MELEE_NO_GC_ADAPTER="1")
        run = subprocess.run(cmd, cwd=ROOT, env=env, timeout=args.timeout,
                             capture_output=True,
                             creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        result["game_exit_code"] = run.returncode
        (args.out / "stdout.txt").write_bytes(run.stdout)
        (args.out / "stderr.txt").write_bytes(run.stderr)
        print("exit", run.returncode)
        if run.returncode:
            result.update(status="game-error", error=f"game exited {run.returncode}")
            return finish(2)
        recorded = sorted(args.out.glob("*.slp"))
        if len(recorded) != 1:
            raise ValueError(f"expected one recording, found {len(recorded)}; see {args.out / 'port.log'}")
        _, actual, _ = parse_slp(recorded[0])
        result.update(compare_posts(original, actual, args.allow_preframe_ulp))
        result["status"] = "ok" if result["gate_pass"] else "mismatch"
        print_comparison(result)
        return finish(0 if result["gate_pass"] else 1)
    except subprocess.TimeoutExpired:
        result.update(status="timeout", error="game timed out")
    except (OSError, ValueError, struct.error) as exc:
        result["error"] = str(exc)
    print(result["error"])
    return finish(2)


if __name__ == "__main__":
    sys.exit(main())
