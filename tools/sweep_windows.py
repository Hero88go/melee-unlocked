#!/usr/bin/env python3
"""Run the hidden native character/stage stability sweep on Windows."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time


ROOT = Path(__file__).resolve().parents[1]
ISO = Path(r"C:\Games\Smash\DOLPHIN AND SMASH GAMES\Super Smash Bros. Melee (v1.02).iso")
DEFAULT_EXE = ROOT / "build-sourceport" / "port" / "Release" / "melee_source.exe"
SCRIPT = ROOT / "port" / "scripts" / "native_vs.txt"
MIN_FREE_BYTES = 15 * 1024**3


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, help="Fresh output directory under run-source")
    parser.add_argument("--exe", type=Path, default=DEFAULT_EXE,
                        help="native executable to test (default: current build)")
    parser.add_argument("--frames", type=int, default=2400)
    parser.add_argument("--timeout", type=int, default=400)
    parser.add_argument("--state-digest", action="store_true",
                         help="write a per-retrace source state CSV for each case")
    parser.add_argument("spec", nargs="+", help="Match specs such as 0x09:7:13")
    args = parser.parse_args()
    exe = args.exe.resolve()

    out = (ROOT / args.out).resolve()
    try:
        out.relative_to((ROOT / "run-source").resolve())
    except ValueError:
        parser.error("--out must be inside run-source")
    if out.exists():
        parser.error(f"refusing to reuse existing evidence directory: {out}")
    for path in (exe, SCRIPT, ISO):
        if not path.is_file():
            parser.error(f"required input is missing: {path}")
    if args.frames < 200 or args.timeout < 1:
        parser.error("frames must be >= 200 and timeout must be positive")

    out.mkdir(parents=True)
    hashes = {"executable": sha256(exe), "script": sha256(SCRIPT), "iso": sha256(ISO)}
    env = os.environ.copy()
    env["MELEE_NO_GC_ADAPTER"] = "1"
    records = []
    failed = False

    for index, spec in enumerate(args.spec):
        free = shutil.disk_usage(out).free
        if free < MIN_FREE_BYTES:
            failed = True
            records.append({"spec": spec, "status": "aborted", "reason": "free disk space below 15 GiB"})
            break
        tag = spec.replace(":", "_").replace("/", "_")
        log_path = out / f"{tag}.log"
        capture_path = out / f"{tag}.ppm"
        run_home = out / tag
        command = [
            str(exe), "--iso", str(ISO), "--hidden", "--volume", "0",
            "--time-base", "1", "--fast", "--script", str(SCRIPT),
            "--frames", str(args.frames), "--match", spec,
            "--capture", str(capture_path), "--capture-frame", str(args.frames - 100),
            "--replay-dir", str(run_home / "Replays"),
            "--card-dir", str(run_home / "card"),
            "--user-dir", str(run_home / "User"),
            "--settings-path", str(run_home / "settings.ini"),
            "--shader-cache", str(run_home / "shadercache"),
            "--log-file", str(log_path),
        ]
        if args.state_digest:
            command.extend(["--state-digest", str(out / f"{tag}.state.csv")])
        started = time.monotonic()
        record = {"index": index, "spec": spec, "command": command, "free_bytes_before": free}
        try:
            completed = subprocess.run(
                command, cwd=ROOT, env=env, stdout=subprocess.DEVNULL,
                stderr=subprocess.STDOUT, timeout=args.timeout, check=False,
            )
            elapsed = time.monotonic() - started
            log = log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else ""
            fatal = any(marker in log.lower() for marker in ("game crash", "game stopped", "game panic"))
            reached = f"exit requested after {args.frames} retraces" in log
            passed = completed.returncode == 0 and reached and not fatal
            record.update({
                "status": "passed" if passed else "failed",
                "exit_code": completed.returncode,
                "elapsed_seconds": round(elapsed, 3),
                "reached_limit": reached,
                "fatal_log": fatal,
                "log": str(log_path),
                "state_digest": str(out / f"{tag}.state.csv") if args.state_digest else None,
                "capture": str(capture_path) if capture_path.exists() else None,
            })
            failed |= not passed
        except subprocess.TimeoutExpired:
            record.update({"status": "timeout", "elapsed_seconds": round(time.monotonic() - started, 3),
                           "log": str(log_path), "capture": str(capture_path) if capture_path.exists() else None})
            failed = True
        records.append(record)
        summary = {"updated": datetime.now(timezone.utc).isoformat(timespec="seconds"),
                   "frames": args.frames, "hashes": hashes, "results": records,
                   "passed": sum(item.get("status") == "passed" for item in records),
                   "failed": sum(item.get("status") != "passed" for item in records)}
        (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
        print(f"{record['status']:7} {spec}", flush=True)

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
