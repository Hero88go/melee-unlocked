#!/usr/bin/env python3
"""Run isolated, muted native/translated traces for exact M7 comparison."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time


def identity(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return {"path": str(path.resolve()), "bytes": path.stat().st_size,
            "sha256": digest.hexdigest()}


def run_one(label, exe, iso, script, out, frames, seed, sys_dir, timeout,
            audio_dump, trace_funcs, real_time, capture_frame, native_game="vanilla"):
    run_dir = out / label
    run_dir.mkdir()
    card_dir = run_dir / "card"
    user_dir = run_dir / "User"
    replay_dir = run_dir / "Replays"
    for path in (card_dir, user_dir, replay_dir):
        path.mkdir()
    (user_dir / "user.json").write_text("{}\n", encoding="utf-8")
    trace = run_dir / "state.csv"
    command = [str(exe), "--iso", str(iso), "--hidden", "--volume", "0",
               "--time-base", "1"]
    if not real_time:
        command.append("--fast")
    command.extend(["--frames", str(frames),
               "--script", str(script), "--sys-dir", str(sys_dir),
               "--replay-dir", str(replay_dir), "--card-dir", str(card_dir),
               "--user-dir", str(user_dir), "--settings-path",
               str(run_dir / "settings.ini"), "--rng-seed", str(seed),
               "--state-digest", str(trace), "--log-file", str(run_dir / "game.log")])
    if audio_dump:
        command.extend(["--audio-dump", str(run_dir / "audio.wav")])
    if capture_frame is not None:
        command.extend(["--capture", str(run_dir / "capture.ppm"),
                        "--capture-frame", str(capture_frame)])
    if label == "translated":
        for trace_func in trace_funcs:
            command.extend(["--trace-func", trace_func])
    elif native_game == "vanilla":
        # The native game carries Legacy's always-on code set; the code-free recompilation
        # (build-vanilla) is compared against the vanilla game.
        command.append("--vanilla-game")
    record = {"command": command, "executable": identity(exe),
              "status": "running", "timeout_seconds": timeout}
    (run_dir / "result.json").write_text(json.dumps(record, indent=2) + "\n",
                                         encoding="utf-8")
    env = dict(os.environ, MELEE_NO_GC_ADAPTER="1")
    start = time.monotonic()
    with (run_dir / "stdout.txt").open("wb") as stdout, \
            (run_dir / "stderr.txt").open("wb") as stderr:
        try:
            result = subprocess.run(
                command, cwd=str(run_dir), env=env, stdout=stdout, stderr=stderr,
                timeout=timeout, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            record["exit_code"] = result.returncode
            record["status"] = "exited"
        except subprocess.TimeoutExpired:
            record["status"] = "timeout"
        except OSError as exc:
            record["status"] = "launch_error"
            record["error"] = str(exc)
    record["elapsed_seconds"] = round(time.monotonic() - start, 3)
    record["trace"] = identity(trace) if trace.exists() else None
    log = run_dir / "game.log"
    log_text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    record["reached_limit"] = f"exit requested after {frames} retraces" in log_text
    record["fatal_log"] = any(word in log_text.lower() for word in
                               ("crash:", "game crash", "panic", "game panic"))
    record["passed"] = (record.get("exit_code") == 0 and record["reached_limit"]
                        and not record["fatal_log"])
    (run_dir / "result.json").write_text(json.dumps(record, indent=2) + "\n",
                                         encoding="utf-8")
    print(json.dumps({"engine": label, "passed": record["passed"],
                      "elapsed_seconds": record["elapsed_seconds"],
                      "trace": record["trace"]}), flush=True)
    return record


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native-exe", type=Path, required=True)
    parser.add_argument("--translated-exe", type=Path, required=True)
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--script", type=Path, required=True)
    parser.add_argument("--sys-dir", type=Path, default=root / "port" / "slippi_sys")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=6100)
    parser.add_argument("--rng-seed", type=int, default=305419896)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--audio-dump", action="store_true",
                        help="also capture mixed PCM for event-aligned comparison")
    parser.add_argument("--real-time", action="store_true",
                        help="pace retraces normally instead of using --fast")
    parser.add_argument("--capture-frame", type=int,
                        help="save a screenshot at this retrace in each isolated run")
    parser.add_argument("--trace-func", action="append", default=[],
                        help="translated guest PC[:entry-limit] to trace; may be repeated")
    parser.add_argument("--native-game", choices=("vanilla", "general"), default="vanilla",
                        help="vanilla: native runs --vanilla-game (against build-vanilla); general: "
                             "native keeps its General Codes (against the general-codes recompilation)")
    args = parser.parse_args()
    if args.frames <= 0 or not 0 < args.timeout <= 600:
        parser.error("positive retrace count and timeout <= 600 required")
    if args.capture_frame is not None and not 0 < args.capture_frame <= args.frames:
        parser.error("capture frame must be within the requested run")
    native, translated, iso, script, sys_dir, out = [p.resolve() for p in
        (args.native_exe, args.translated_exe, args.iso, args.script,
         args.sys_dir, args.out)]
    for path in (native, translated, iso, script):
        if not path.is_file():
            parser.error("missing input: " + str(path))
    if not sys_dir.is_dir():
        parser.error("missing system directory: " + str(sys_dir))
    if out.exists():
        parser.error("output directory already exists: " + str(out))
    if shutil.disk_usage(out.parent).free < 15 * 1024**3:
        parser.error("less than 15 GiB free")
    out.mkdir(parents=True)
    shutil.copyfile(script, out / "input-script.txt")
    inputs = [identity(p) for p in (native, translated, iso, script)]
    dlls = sorted(native.parent.glob("*.dll"))
    inputs.extend(identity(p) for p in dlls)
    summary = {"scope": "paired exact simulation trace; not a complete M7 acceptance",
               "frames_requested": args.frames, "rng_seed": args.rng_seed,
               "native_game": args.native_game,
               "real_time_pacing": args.real_time,
               "inputs": inputs, "runs": []}
    (out / "runs.json").write_text(json.dumps(summary, indent=2) + "\n",
                                   encoding="utf-8")
    for label, exe in (("native", native), ("translated", translated)):
        record = run_one(label, exe, iso, script, out, args.frames,
                         args.rng_seed, sys_dir, args.timeout, args.audio_dump,
                         args.trace_func, args.real_time, args.capture_frame, args.native_game)
        summary["runs"].append({"engine": label, **record})
        (out / "runs.json").write_text(json.dumps(summary, indent=2) + "\n",
                                       encoding="utf-8")
        if not record["passed"]:
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
