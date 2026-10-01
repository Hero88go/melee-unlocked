#!/usr/bin/env python3
"""Gate a requested number of Slippi recordings on Source or Static, hidden and muted.

Default selection excludes added character ids. --include-mods opts into them;
the caller supplies the matching disc, and --mod-base-iso supplies Static's vanilla
base disc. --backend continues to select graphics. See docs/replay-validation.md.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from melee_iso import require_iso
from replay_compare import parse_slp

ROOT = Path(__file__).resolve().parents[1]
VANILLA_LAST_CHAR = 0x19
CACHE_VERSION = 1


def is_vanilla(slp):
    """Compatibility predicate; batch selection reports malformed files explicitly."""
    try:
        return classify_replay(slp)
    except (OSError, ValueError):
        return False


def classify_replay(slp):
    start, post, _ = parse_slp(slp)
    if not post or not any(frame >= 0 for frame in post):
        raise ValueError(f"{slp}: no complete in-play post-frame events")
    info = start[5:5 + 0x138]
    return all(info[0x61 + k * 0x24] == 3 or info[0x60 + k * 0x24] <= VANILLA_LAST_CHAR
               for k in range(4))


def file_identity(path):
    path = Path(path).resolve()
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        before = os.fstat(stream.fileno())
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
        after = os.fstat(stream.fileno())
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise ValueError(f"input changed while hashing: {path}")
    return dict(path=str(path), bytes=after.st_size, sha256=digest.hexdigest())


def run_identity(args):
    """Hash common inputs once per batch, not once per replay or cache lookup."""
    paths = {args.exe, args.iso, Path(__file__), ROOT / "tools/replay_compare.py",
             ROOT / "tools/melee_iso.py"}
    paths.update(args.exe.parent.glob("*.dll"))
    # Native playback can use either the normal Sys directory or its playback
    # fallback. Static explicitly uses the playback directory.
    sys_dirs = ("slippi_sys", "slippi_sys_playback") if args.engine == "source" else ("slippi_sys_playback",)
    for name in sys_dirs:
        paths.update(p for p in (ROOT / "port" / name).rglob("*") if p.is_file())
    if args.engine == "source":
        dll = Path(os.environ.get("MELEE_GAME_DLL", str(args.exe.parent / "melee_game.dll")))
        if not dll.is_absolute():
            dll = ROOT / dll
        paths.add(dll.resolve())
        for suffix in (".dbg", ".snapexcl"):
            sidecar = dll.with_suffix(suffix)
            if sidecar.exists():
                paths.add(sidecar.resolve())
    if args.mod_base_iso:
        paths.add(args.mod_base_iso)
    env_options = {k: v for k, v in os.environ.items()
                   if k.startswith("MELEE_") or k == "REPLAY_COMPARE_EXTRA"}
    return dict(cache_version=CACHE_VERSION,
                files=[file_identity(p) for p in sorted(paths, key=str)],
                options=dict(engine=args.engine, backend=args.backend, timeout=args.timeout,
                             allow_preframe_ulp=args.allow_preframe_ulp,
                             environment_sha256=hashlib.sha256(json.dumps(
                                 env_options, sort_keys=True).encode()).hexdigest()))


def load_cache(path, identity):
    try:
        cached = json.loads(path.read_text(encoding="utf-8"))
        result = cached["result"]
        if cached["identity"] != identity or not result_passes(result):
            return None
        if not cached["artifacts"]:
            return None
        for artifact in cached["artifacts"]:
            if file_identity(artifact["path"]) != artifact:
                return None
        return dict(result, cached=True)
    except (OSError, ValueError, KeyError, TypeError):
        return None


def result_passes(result):
    return (isinstance(result, dict)
            and result.get("status") == "ok" and result.get("gate_pass") is True
            and result.get("exit_code") == 0 and result.get("game_exit_code") == 0
            and result.get("frames", 0) > 0 and result.get("player_frames", 0) > 0
            and result.get("in_play_mismatches") == 0
            and result.get("coverage_mismatches") == 0
            and result.get("unexpected_mismatches") == 0)


def run(slp, out, args, common_identity):
    # Equal stems in different input subdirectories must not share results or files.
    path_tag = hashlib.sha256(str(slp.resolve()).encode()).hexdigest()[:12]
    case_dir = out / f"{slp.stem}-{path_tag}"
    cache_path = case_dir / "cache.json"
    result = dict(replay=str(slp), status="error", gate_pass=False, cached=False)
    try:
        identity = dict(common_identity, replay=file_identity(slp))
        if not args.no_cache:
            cached = load_cache(cache_path, identity)
            if cached is not None:
                return cached
        if shutil.disk_usage(out.anchor or out.parent).free < 15 * 1024 ** 3:
            return dict(result, status="skipped-low-disk")
        # A cache miss gets fresh card/settings/User directories, including after failures.
        run_dir = case_dir / f"attempt-{time.time_ns()}"
        run_dir.mkdir(parents=True)
        command = [sys.executable, str(ROOT / "tools/replay_compare.py"), str(slp.resolve()),
                   "--engine", args.engine, "--iso", str(args.iso), "--exe", str(args.exe),
                   "--out", str(run_dir), "--timeout", str(args.timeout)]
        if args.backend:
            command += ["--backend", args.backend]
        if args.mod_base_iso:
            command += ["--mod-base-iso", str(args.mod_base_iso)]
        if args.allow_preframe_ulp:
            command.append("--allow-preframe-ulp")
        env = dict(os.environ, MELEE_NO_GC_ADAPTER="1")
        process = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True,
                                 timeout=args.timeout + 120,
                                 creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        text_path = run_dir / "result.txt"
        text_path.write_text(process.stdout + process.stderr, encoding="utf-8")
        result_path = run_dir / "result.json"
        if not result_path.is_file():
            raise ValueError("comparison produced no structured result")
        result.update(json.loads(result_path.read_text(encoding="utf-8")))
        result.update(replay=str(slp), output=str(run_dir), cached=False)
        if process.returncode != result.get("exit_code"):
            result.update(status="error", gate_pass=False,
                          error=f"comparison exit {process.returncode} disagrees with result")
        if process.returncode != 0 or not result_passes(result):
            result["gate_pass"] = False
        for replay in run_dir.glob("*.slp"):
            replay.unlink()
        shutil.rmtree(run_dir / "shadercache", ignore_errors=True)
        if result_passes(result):
            cache_path.write_text(json.dumps(dict(identity=identity, result=result,
                artifacts=[file_identity(result_path), file_identity(text_path)]), indent=2) + "\n",
                encoding="utf-8")
        return result
    except subprocess.TimeoutExpired:
        return dict(result, status="timeout", gate_pass=False)
    except (OSError, ValueError, TypeError) as exc:
        return dict(result, status="error", gate_pass=False, error=str(exc))


def batch_passes(results, requested):
    return len(results) == requested and requested > 0 and all(result_passes(r) for r in results)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--src", type=Path, required=True)
    ap.add_argument("--count", type=int, default=200, help="required replay count, not an upper bound")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--iso", type=Path)
    ap.add_argument("--mod-base-iso", type=Path)
    ap.add_argument("--jobs", type=int, default=3)
    ap.add_argument("--exe", type=Path)
    ap.add_argument("--engine", choices=("source", "static"), default="source")
    ap.add_argument("--include-mods", action="store_true", help="also select added-character replays")
    ap.add_argument("--backend", choices=("d3d11", "d3d12"), help="graphics backend")
    ap.add_argument("--timeout", type=float, default=1500)
    ap.add_argument("--allow-preframe-ulp", action="store_true")
    ap.add_argument("--no-cache", action="store_true", help="run again even when input identities match")
    args = ap.parse_args(argv)
    if args.count <= 0 or args.jobs <= 0 or args.timeout <= 0:
        ap.error("--count, --jobs and --timeout must be positive")
    args.src, args.out = args.src.resolve(), args.out.resolve()
    if not args.src.is_dir():
        ap.error("--src must be an existing directory")
    if args.out == args.src or args.src in args.out.parents:
        ap.error("--out must be outside the input corpus")
    args.out.mkdir(parents=True, exist_ok=True)
    files = sorted(args.src.rglob("*.slp"), key=lambda p: (-p.stat().st_mtime_ns, str(p)))
    selected = []
    try:
        for path in files:
            if len(selected) == args.count:
                break
            if args.include_mods or classify_replay(path):
                selected.append(path)
    except (OSError, ValueError) as exc:
        message = f"FAIL: malformed requested corpus input: {exc}"
        (args.out / "summary.json").write_text("[]\n", encoding="utf-8")
        (args.out / "summary.txt").write_text(message + "\n", encoding="utf-8")
        print(message)
        return 2
    if len(selected) != args.count:
        message = f"FAIL: requested {args.count} replays, selected {len(selected)}"
        (args.out / "summary.json").write_text("[]\n", encoding="utf-8")
        (args.out / "summary.txt").write_text(message + "\n", encoding="utf-8")
        print(message)
        return 1
    try:
        args.iso = require_iso(args.iso).resolve()
        if args.mod_base_iso:
            args.mod_base_iso = require_iso(args.mod_base_iso).resolve()
        args.exe = (args.exe or ROOT / ("build-sourceport/port/Release/melee_source.exe"
                    if args.engine == "source" else "build-review/port/Release/melee_port_playback.exe")).resolve()
        identity = run_identity(args)
    except (OSError, ValueError) as exc:
        message = f"FAIL: {exc}"
        (args.out / "summary.json").write_text("[]\n", encoding="utf-8")
        (args.out / "summary.txt").write_text(message + "\n", encoding="utf-8")
        print(message)
        return 2
    results = []
    with ThreadPoolExecutor(args.jobs) as executor:
        for result in executor.map(lambda f: run(f, args.out, args, identity), selected):
            results.append(result)
            (args.out / "summary.json").write_text(json.dumps(results, indent=1) + "\n", encoding="utf-8")
            print(len(results), Path(result["replay"]).name, result.get("frames"),
                  result.get("mismatches"), result["status"], flush=True)
    exact = sum(result_passes(r) and r.get("mismatches") == 0 for r in results)
    play_exact = sum(result_passes(r) for r in results)
    lines = [f"{len(results)} replays, {sum(r.get('frames', 0) for r in results)} frames; "
             f"{exact} frame-exact vs the Dolphin recording, {play_exact} exact from frame 0 on"]
    for result in results:
        if not result_passes(result):
            lines.append(f"{Path(result['replay']).name}: FAIL {result['status']} "
                         f"{result.get('error', result.get('first_unexpected'))}")
    passed = batch_passes(results, args.count)
    lines.append(f"{'PASS' if passed else 'FAIL'}: {play_exact}/{args.count} requested replay gates passed")
    (args.out / "summary.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join((lines[0], lines[-1])))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
