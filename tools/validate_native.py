"""Compare native CPU/RAM/ARAM traces with and without D3D12 rendering.

This checks renderer isolation, not equivalence to Dolphin or complete rollback state.
"""
import argparse
import csv
import json
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--iso", type=Path, required=True)
    ap.add_argument("--exe", type=Path, default=ROOT / "build-review/port/Release/melee_port.exe")
    ap.add_argument("--script", type=Path, default=ROOT / "port/scripts/vs_match.txt")
    ap.add_argument("--out", type=Path, default=ROOT / "reports/native-validation")
    ap.add_argument("--frames", type=int, default=2400)
    ap.add_argument("--timeout", type=float, default=240)
    ap.add_argument('--card-fixture', type=Path, help='prepared card copied separately for each mode')
    args = ap.parse_args()
    if args.frames < 1 or args.timeout <= 0: ap.error("frames and timeout must be positive")
    args.out.mkdir(parents=True, exist_ok=True)
    results = {"frames": args.frames, "kind": "native renderer isolation", "runs": {}}
    rows = []
    modes = ("headless", "hidden", "threaded", "authored")
    for mode in modes:
        trace = (args.out / (mode + ".csv")).resolve()
        log_path = args.out / (mode + "-trace.log")
        mode_flags = ["--hidden", "--threaded-renderer"] if mode == "threaded" else ["--" + mode]
        if mode == "authored": mode_flags = ["--hidden", "--threaded-renderer", "--fps", "240", "--frame-mode", "authored"]
        # Each run gets an empty memory card: the game's boot path differs with and without a save.
        card_dir = (args.out / (mode + "-card")).resolve()
        shutil.rmtree(card_dir, ignore_errors=True)
        if args.card_fixture:
            shutil.copytree(args.card_fixture, card_dir)
        command = [str(args.exe.resolve()), "--iso", str(args.iso.resolve()), *mode_flags,
                   "--volume", "0", "--fast", "--frames", str(args.frames), "--time-base", "1",
                   "--card-dir", str(card_dir),
                   "--user-dir", str(card_dir / 'User'),
                   "--log-file", str((args.out / (mode + "-port.log")).resolve()),
                   "--script", str(args.script.resolve()), "--state-trace", str(trace)]
        start = time.monotonic()
        with log_path.open("w", encoding="utf-8") as log:
            try:
                run = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                                     timeout=args.timeout, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            except subprocess.TimeoutExpired:
                raise SystemExit(f"{mode} timed out; see {log_path}")
        results["runs"][mode] = {"exit": run.returncode, "seconds": time.monotonic() - start}
        if run.returncode: raise SystemExit(f"{mode} exited {run.returncode}; see {log_path}")
        with trace.open() as stream: rows.append(list(csv.DictReader(stream)))
        if len(rows[-1]) != args.frames: raise SystemExit(f"{mode}: missing trace checkpoints")
        text = log_path.read_text(encoding="utf-8")
        if "mmio read " in text or "mmio write " in text or "FATAL" in text:
            raise SystemExit(f"{mode}: invalid-access diagnostics; see {log_path}")
        print(f"{mode}: {len(rows[-1])} checkpoints", flush=True)
    differences = [{"frame": i + 1, **dict(zip(modes, checkpoint))}
                   for i, checkpoint in enumerate(zip(*rows))
                   if any(row != checkpoint[0] for row in checkpoint[1:])]
    results["mismatch_count"] = len(differences)
    results["first_differences"] = differences[:10]
    (args.out / "render-state-comparison.json").write_text(json.dumps(results, indent=2))
    print(f"{args.frames} checkpoints compared; {len(differences)} mismatches")
    return bool(differences)

if __name__ == "__main__": raise SystemExit(main())
