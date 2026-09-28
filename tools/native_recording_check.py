"""Run native VS with recording off/on, then replay it and check all gameplay fields."""
import argparse
import json
import os
import subprocess
from pathlib import Path

from melee_iso import require_iso
from replay_compare import parse_slp
from slp_diff import first_difference, FIELDS, INPUTS

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--exe", type=Path, required=True)
    ap.add_argument("--iso", type=Path)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--frames", type=int, default=3600)
    ap.add_argument("--backend", choices=["d3d11", "d3d12"], default="d3d11")
    args = ap.parse_args()
    args.iso = require_iso(args.iso)
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=False)
    exe = str(args.exe.resolve())
    env = dict(os.environ)
    env.pop("MELEE_SOURCE_RECORD", None)

    def run(label, extra):
        out = args.out / label
        out.mkdir()
        command = [exe, "--iso", str(args.iso), "--hidden", "--fast", "--volume", "0",
                   "--scale", "1", "--window", "640x480", "--backend", args.backend, "--dlss", "off",
                   "--time-base", "1", "--card-dir", str(out / "card"),
                   "--replay-dir", str(out), "--log-file", str(out / "port.log"),
                   "--settings-path", str(out / "settings.ini"), "--user-dir", str(out / "User"),
                   "--shader-cache", str(out / "shadercache"),
                   # Offline only: the Slippi menus boot into Online Play, where this script would
                   # search for an opponent on the real matchmaking server.
                   "--slippi-menus", "off", *extra]
        result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True,
                                timeout=180, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        if result.returncode:
            raise RuntimeError(f"{label} exited {result.returncode}; see {out / 'port.log'}")
        log = (out / "port.log").read_text(errors="replace")
        if "matchmaking" in log or "online connection" in log:
            raise RuntimeError(f"{label} went online; this check must stay offline")
        return out

    script = args.out / "vs.txt"
    # The legacy sweep's 30-frame full-stick hold overshoots the character grid
    # with the current input path. Release inside the grid before confirming.
    script.write_text((ROOT / "port/scripts/native_vs.txt").read_text().replace("1530", "1518"))
    options = ["--script", str(script), "--match", "31:2:9",
               "--frames", str(args.frames)]
    off = run("off", [*options, "--state-digest", str(args.out / "off/state.csv")])
    on = run("on", [*options, "--record-native", "--state-digest", str(args.out / "on/state.csv")])
    # Whole state trace, including RNG and every retrace, must be unchanged.
    if (off / "state.csv").read_bytes() != (on / "state.csv").read_bytes():
        raise RuntimeError("recording changed the native state digest")
    files = list(on.glob("*.slp"))
    if len(files) != 1:
        raise RuntimeError(f"expected one recorded VS match, got {len(files)}")
    playback = run("playback", ["--replay", str(files[0])])
    generated = list(playback.glob("*.slp"))
    if len(generated) != 1:
        raise RuntimeError("playback did not produce one recording")
    _, post_a, pre_a = parse_slp(files[0])
    _, post_b, pre_b = parse_slp(generated[0])
    state_difference = first_difference(post_a, post_b, FIELDS)
    input_difference = first_difference(pre_a, pre_b, INPUTS)
    in_play_a = {f: p for f, p in post_a.items() if f >= 0}
    in_play_b = {f: p for f, p in post_b.items() if f >= 0}
    report = dict(recording_preserves_state=True, frames=len(post_a),
                  player_frames=sum(len(p) for p in post_a.values()),
                  first_state_difference=state_difference,
                  first_input_difference=input_difference,
                  first_in_play_difference=first_difference(in_play_a, in_play_b, FIELDS))
    (args.out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 1 if state_difference or input_difference else 0


if __name__ == "__main__":
    raise SystemExit(main())
