"""Measure landing lag with and without auto L-cancel.

Runs port/scripts/lcancel.txt twice with an identical time base, once with --auto-lcancel and
once without, and reports how many frames the fighter spent in each LandingAir* state. L-cancel
halves that number, so the two runs have to differ for the feature to be doing anything.
"""
import argparse
import csv
import shutil
import subprocess
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

LANDING_AIR = {70: "LandingAirN", 71: "LandingAirF", 72: "LandingAirB", 73: "LandingAirHi", 74: "LandingAirLw"}
ATTACK_AIR = {65: "AttackAirN", 66: "AttackAirF", 67: "AttackAirB", 68: "AttackAirHi", 69: "AttackAirLw"}


def run(exe, iso, out_dir, name, extra, frames, script, seed_card=None, trace=None):
    out_dir.mkdir(parents=True, exist_ok=True)
    log = (out_dir / f"{name}.csv").resolve()
    # An empty user.json keeps the run signed out: signed in, the Slippi build's main menu goes to
    # the online modes and the scripted menu path lands on the Ranked character select instead of
    # an offline match. It also stops the fallback that would otherwise borrow the machine's own
    # Slippi Launcher login.
    user_dir = (out_dir / f"{name}-user").resolve()
    user_dir.mkdir(parents=True, exist_ok=True)
    (user_dir / "user.json").write_text("{}", encoding="utf-8")
    # The scripted menu path only reaches a VS match when the memory card already holds a save, so
    # every run starts from the same seeded card.
    card_dir = (out_dir / f"{name}-card").resolve()
    shutil.rmtree(card_dir, ignore_errors=True)
    if seed_card:
        shutil.copytree(seed_card, card_dir)
    command = [str(exe.resolve()), "--iso", str(iso.resolve()), "--headless", "--volume", "0", "--fast",
               "--frames", str(frames), "--time-base", "1",
               "--card-dir", str(card_dir),
               "--user-dir", str(user_dir),
               "--replay-dir", str((out_dir / f"{name}-replays").resolve()),
               "--log-file", str((out_dir / f"{name}-port.log").resolve()),
               "--script", str(script.resolve()), "--lcancel-log", str(log), *extra]
    if trace:
        command += ["--state-trace", str(trace)]
    proc = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=900)
    if proc.returncode:
        raise SystemExit(f"{name} exited {proc.returncode}\n{proc.stdout[-4000:]}\n{proc.stderr[-4000:]}")
    return log


def seed(exe, iso, out_dir, frames, script):
    """Boot once so the game writes its save file, and keep that card as the seed for both runs."""
    card = (out_dir / "card-seed").resolve()
    if any(card.glob("*.gci")):
        return card
    run(exe, iso, out_dir, "seed", [], frames, script)
    shutil.rmtree(card, ignore_errors=True)
    shutil.copytree(out_dir / "seed-card", card)
    return card


def episodes(path, port):
    """Every run of consecutive frames in a LandingAir* state, as (first retrace, state, frames)."""
    out, current = [], None
    with path.open() as stream:
        for row in csv.DictReader(stream):
            if int(row["port"]) != port:
                continue
            motion = int(row["motion_id"])
            if motion in LANDING_AIR:
                if current and current[1] == motion:
                    current[2] += 1
                else:
                    if current:
                        out.append(tuple(current))
                    current = [int(row["retrace"]), motion, 1]
            elif current:
                out.append(tuple(current))
                current = None
    if current:
        out.append(tuple(current))
    return out


def injections(path, port):
    with path.open() as stream:
        return sum(1 for row in csv.DictReader(stream) if int(row["port"]) == port and row["injected"] == "1")


def since_press_at_landing(path, port):
    """The fighter's 'frames since a trigger press' counter on each landing frame."""
    out, last = [], None
    with path.open() as stream:
        for row in csv.DictReader(stream):
            if int(row["port"]) != port:
                continue
            motion = int(row["motion_id"])
            if motion in LANDING_AIR and motion != last:
                out.append(int(row["frames_since_trigger"]))
            last = motion
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--iso", type=Path, required=True)
    ap.add_argument("--exe", type=Path, default=ROOT / "build-lcancel/port/Release/melee_port.exe")
    ap.add_argument("--script", type=Path, default=ROOT / "port/scripts/lcancel.txt")
    ap.add_argument("--out", type=Path, default=ROOT / "reports/lcancel")
    ap.add_argument("--frames", type=int, default=2500)
    ap.add_argument("--port", type=int, default=1)
    args = ap.parse_args()

    card = seed(args.exe, args.iso, args.out, args.frames, args.script)
    results, traces = {}, {}
    for name, extra in (("off", []), ("indicator", ["--lcancel-indicator"]), ("on", ["--auto-lcancel"])):
        traces[name] = (args.out / f"{name}-trace.csv").resolve()
        path = run(args.exe, args.iso, args.out, name, extra, args.frames, args.script, card, traces[name])
        results[name] = (episodes(path, args.port), injections(path, args.port), since_press_at_landing(path, args.port))

    for name, (eps, inj, since) in results.items():
        print(f"--- auto L-cancel {name}: {inj} injected trigger frames")
        for retrace, motion, frames in eps:
            print(f"    retrace {retrace:5d}  {LANDING_AIR[motion]:<13} {frames:3d} frames")
        print(f"    frames since trigger press at each landing: {since}")

    off = Counter(f for _, _, f in results["off"][0])
    on = Counter(f for _, _, f in results["on"][0])
    print(f"\nlanding-lag histogram  off: {dict(off)}   on: {dict(on)}")
    if not results["off"][0] or not results["on"][0]:
        raise SystemExit("no aerial landings recorded; the fixture did not produce any")
    if off == on:
        raise SystemExit("FAIL: landing lag is identical with the setting on and off")

    # The indicator must not perturb the simulation at all: same CPU/RAM/ARAM hash every retrace.
    base_trace = traces["off"].read_bytes()
    if traces["indicator"].read_bytes() != base_trace:
        raise SystemExit("FAIL: the indicator changed the simulation state trace")
    if traces["on"].read_bytes() == base_trace:
        raise SystemExit("FAIL: auto L-cancel left the simulation unchanged")
    print("OK: landing lag changed with auto L-cancel on")
    print("OK: the indicator left the state trace byte for byte identical")


if __name__ == "__main__":
    main()
