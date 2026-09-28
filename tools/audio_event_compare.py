"""Compare event-aligned PCM windows from native and translated Melee runs.

The paired runs must use the same 60 Hz input script and emit both --state-digest
and --audio-dump outputs.  Boot timing may differ; scene and match boundaries in
the digest align the windows without shifting audio to improve the result.
"""
import argparse
import csv
import json
import math
import struct
import wave
from pathlib import Path


def boundaries(path):
    with path.open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    menu = next(int(row["frame"]) for row in rows
                if int(row["scene_major"], 16) == 1)
    match = next(int(row["frame"]) for row in rows
                 if int(row["match_frame"], 16) == 1)
    return menu, match


def pcm(path):
    with wave.open(str(path), "rb") as stream:
        if stream.getnchannels() != 2 or stream.getsampwidth() != 2:
            raise ValueError(f"{path}: expected 16-bit stereo PCM")
        rate = stream.getframerate()
        raw = stream.readframes(stream.getnframes())
    return rate, struct.unpack(f"<{len(raw) // 2}h", raw)


def measure(samples, rate, start_retrace, end_retrace):
    first = max(0, round(start_retrace * rate / 60)) * 2
    last = min(len(samples) // 2, round(end_retrace * rate / 60)) * 2
    window = samples[first:last]
    return {
        "duration": (last - first) / 2 / rate,
        "rms": math.sqrt(sum(value * value for value in window) /
                         max(1, len(window))),
        "peak": max((abs(value) for value in window), default=0),
    }


def difference(a, b):
    if b == 0:
        return 0.0 if a == 0 else 100.0
    return abs(a - b) * 100.0 / abs(b)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native-wav", type=Path, required=True)
    parser.add_argument("--native-state", type=Path, required=True)
    parser.add_argument("--reference-wav", type=Path, required=True)
    parser.add_argument("--reference-state", type=Path, required=True)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--gate", type=float, default=5.0)
    parser.add_argument("--scripted-hits-window", nargs=2, type=float,
                        metavar=("START", "END"), default=(1000.0, 1400.0),
                        help="match-relative retrace offsets for the hit window (default: 1000 1400)")
    args = parser.parse_args()
    hit_start, hit_end = args.scripted_hits_window
    if hit_start < 0 or hit_end <= hit_start:
        parser.error("scripted-hits window must have a non-negative start and a later end")

    runs = {}
    for label, wav, state in (
        ("native", args.native_wav, args.native_state),
        ("reference", args.reference_wav, args.reference_state),
    ):
        menu, match = boundaries(state)
        rate, samples = pcm(wav)
        runs[label] = {
            "sample_rate": rate,
            "menu_retrace": menu,
            "match_retrace": match,
            "events": {
                "menu_music": measure(samples, rate, menu, menu + 300),
            "announcer": measure(samples, rate, match, match + 300),
            "scripted_hits": measure(samples, rate, match + hit_start,
                                       match + hit_end),
        },
        }

    comparisons = {}
    passed = True
    for event in runs["native"]["events"]:
        fields = {}
        for field in ("duration", "rms", "peak"):
            delta = difference(runs["native"]["events"][event][field],
                               runs["reference"]["events"][event][field])
            fields[field] = {"difference_percent": delta,
                             "passed": delta <= args.gate}
            passed &= fields[field]["passed"]
        comparisons[event] = fields

    result = {"gate_percent": args.gate,
              "scripted_hits_window_match_retrace_offsets": [hit_start, hit_end],
              "passed": passed,
              "runs": runs, "comparisons": comparisons}
    rendered = json.dumps(result, indent=2)
    print(rendered)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(rendered + "\n")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
