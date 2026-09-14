"""Runs two port instances through an online match and reports their time sync.

Default: direct local peering (no matchmaking server). With --real both instances queue at the
real Slippi matchmaking server using the user folders given by --user-a/--user-b.

    python tools/online_pair.py [--frames 3600] [--script port/scripts/online_unranked.txt]
"""
import argparse
import re
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from melee_iso import require_iso

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "build-review/port/Release/melee_port.exe"


def parse(path):
    frames, sync = {}, []
    for line in open(path, errors="replace"):
        m = re.search(r"online frame (\d+) wall ([\d.]+) s retrace (\d+)", line)
        if m:
            frames[int(m.group(1))] = (float(m.group(2)), int(m.group(3)))
        m = re.search(r"(halting|advancing) on frame (\d+) .*offset (-?\d+) us, (\d+) frames", line)
        if m:
            sync.append((int(m.group(2)), m.group(1)[:3], int(m.group(3)), int(m.group(4))))
    return frames, sync


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", type=int, default=3600)
    ap.add_argument("--script", default="port/scripts/online_unranked.txt")
    ap.add_argument("--out", default="reports/native-validation")
    ap.add_argument("--real", action="store_true", help="queue at mm.slippi.gg instead of local peering")
    ap.add_argument("--user-a")
    ap.add_argument("--user-b")
    ap.add_argument("--only", choices=["A", "B"], help="launch a single instance (the other side is external)")
    ap.add_argument("--iso")
    ap.add_argument("--exe", type=Path, default=EXE)
    ap.add_argument("--extra", nargs=argparse.REMAINDER, default=[])
    args = ap.parse_args()
    iso = require_iso(args.iso)
    out = ROOT / args.out
    procs = {}
    for name, idx, port, other in (("A", 0, 41100, 41101), ("B", 1, 41101, 41100)):
        if args.only and args.only != name:
            continue
        d = out / f"peer{name}"
        d.mkdir(parents=True, exist_ok=True)
        cmd = [str(args.exe.resolve()), "--iso", str(iso), "--hidden", "--volume", "0", "--frames", str(args.frames),
               "--script", str(ROOT / args.script), "--replay-dir", str(d), "--log-file", str(d / "port.log")]
        if args.real:
            user = args.user_a if name == "A" else args.user_b
            if user:
                cmd += ["--user-dir", user]
            cmd += ["--netplay-port", str(port)]
        else:
            cmd += ["--time-base", "1", "--local-peer", f"{idx}:{port}:127.0.0.1:{other}"]
        cmd += args.extra
        log = open(d / "log.txt", "w")
        procs[name] = (subprocess.Popen(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT), log)
        print(name, "started:", " ".join(cmd[1:6]), "...")
        time.sleep(2)
    for name, (p, log) in procs.items():
        p.wait()
        log.close()
        print(name, "exit", p.returncode)
    for name in procs:
        frames, sync = parse(out / f"peer{name}" / "log.txt")
        print(f"== {name}: {len(frames)} online frame stamps, {len(sync)} sync events")
        for s in sync[:40]:
            print("  ", s)
    if len(procs) == 2:
        A, _ = parse(out / "peerA/log.txt")
        B, _ = parse(out / "peerB/log.txt")
        print("frame   A-B wall ms   A retrace-frame   B retrace-frame")
        for f in sorted(set(A) & set(B)):
            if f % 60 == 0:
                print(f"{f:5d}   {1000 * (A[f][0] - B[f][0]):9.1f}   {A[f][1] - f:8d}   {B[f][1] - f:8d}")


if __name__ == "__main__":
    sys.exit(main())
