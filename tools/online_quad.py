"""Runs 2-4 hidden port instances through one online match on local peering and checks they agree.

Each instance gets --local-peer idx:port:127.0.0.1:port,... (every other player, in player-index
order), so they meet without Slippi's servers, with the player list, decider and addresses the
matchmaking server would have sent. The default is a four-player Teams match through the menus:
players 1-2 stay red (port/scripts/online_teams.txt), players 3-4 toggle to blue
(online_teams_blue.txt). Each slot can run a different engine:

    python tools/online_quad.py                                  # 4 x native, Teams
    python tools/online_quad.py --engines native,native,static,static --lag-ms 40
    python tools/online_quad.py --players 2 --scripts port/scripts/online_bot.txt

Reports per slot: connection, checksums compared/mismatched, DESYNC lines, rollbacks, online frames,
and each replay's teams flag, characters and teams (all slots must record the same match).
"""
import argparse
import os
import re
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from melee_iso import require_iso

ROOT = Path(__file__).resolve().parents[1]
ENGINES = {
    "native": ROOT / "build-sourceport-slippi/port/Release/melee_source.exe",
    "static": ROOT / "build-legacy-ref/port/Release/melee_port.exe",
}
TEAMS_SCRIPTS = ["port/scripts/online_teams.txt", "port/scripts/online_teams.txt",
                 "port/scripts/online_teams_blue.txt", "port/scripts/online_teams_blue.txt"]
CODE_HISTORY = '[\n  {\n    "connectCode": "PEER#001",\n    "lastPlayed": ""\n  }\n]'


def parse_log(path):
    r = {"connected": False, "local": "", "compared": 0, "mismatched": 0, "desync": [], "rollbacks": 0,
         "last_frame": 0, "agree_frame": 0, "errors": [], "setup": ""}
    try:
        lines = open(path, errors="replace").read().splitlines()
    except OSError:
        return r
    for line in lines:
        if "online connection successful" in line:
            r["connected"] = True
        elif "local peer test:" in line:
            r["local"] = line.split("local peer test:", 1)[1].strip()
        elif "match setup " in line:
            r["setup"] = line.split("match setup ", 1)[1].strip()
        elif "DESYNC" in line:
            r["desync"].append(line.strip())
        elif "game crash" in line or line.startswith("CRASH:"):
            r["crash"] = line.strip()
        elif "Could not connect" in line or "online connection failed" in line or "force-disconnecting" in line:
            r["errors"].append(line.strip())
        m = re.search(r"checksums agree through frame (\d+) \((\d+) compared, (\d+) mismatched\)", line)
        if m:
            r["agree_frame"], r["compared"], r["mismatched"] = int(m.group(1)), int(m.group(2)), int(m.group(3))
        m = re.search(r"online frame (\d+) wall [\d.]+ s retrace \d+ rollbacks (\d+)", line)
        if m:
            r["last_frame"], r["rollbacks"] = int(m.group(1)), int(m.group(2))
    r["mismatched"] = max(r["mismatched"], len(r["desync"]))
    return r


def parse_slp(path):
    """Game Start of a .slp: (is_teams, [(port, char, team) for occupied slots], stage)."""
    data = path.read_bytes()
    raw = data.find(b"raw[$U#l")
    if raw < 0:
        return None
    pos = raw + 12
    if data[pos] != 0x35:
        return None
    sizes = {}
    n = data[pos + 1]
    for i in range(pos + 2, pos + 1 + n, 3):
        sizes[data[i]] = int.from_bytes(data[i + 1:i + 3], "big")
    pos += 1 + n
    if data[pos] != 0x36:
        return None
    block = data[pos + 5:pos + 5 + 312]
    players = []
    for i in range(4):
        base = 0x60 + 0x24 * i
        if block[base + 1] != 3:
            players.append((i + 1, block[base], block[base + 9]))
    return block[0x8] != 0, players, int.from_bytes(block[0xE:0x10], "big")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--players", type=int, default=4, choices=[2, 3, 4])
    ap.add_argument("--engines", default="native", help="native|static|<exe path>, one or comma-separated per slot")
    ap.add_argument("--scripts", help="one or comma-separated per slot (default: the Teams scripts)")
    # Lobby Direct at boot (--lobby-direct CODE --lobby-character N per slot, through QUAD_SLOT<i>_EXTRA):
    # the game's own driver takes the match from the main menu, and a script would fight it.
    ap.add_argument("--no-script", action="store_true", help="run the slots without input scripts")
    ap.add_argument("--frames", type=int, default=4200)
    ap.add_argument("--base-port", type=int, default=41200, help="slot i listens on base+i (keep clear of 41100/41101)")
    ap.add_argument("--lag-ms", type=int, default=0, help="MELEE_NET_LAG_MS for every slot")
    ap.add_argument("--stagger", type=float, default=0.5, help="seconds between launches")
    ap.add_argument("--timeout", type=float, default=0, help="kill the slots after this many seconds (0: frames/40 + 60)")
    ap.add_argument("--out", default="reports/online-quad")
    ap.add_argument("--iso")
    ap.add_argument("--extra", nargs=argparse.REMAINDER, default=[])
    args = ap.parse_args()
    n = args.players
    iso = require_iso(args.iso)

    def per_slot(value, default):
        items = [v.strip() for v in value.split(",")] if value else list(default)
        if len(items) == 1:
            items *= n
        if len(items) < n:
            sys.exit(f"need {n} values, got {len(items)}: {value}")
        return items[:n]

    engines = per_slot(args.engines, ["native"])
    exes = [ENGINES.get(e, Path(e)) for e in engines]
    scripts = per_slot(args.scripts, TEAMS_SCRIPTS)
    for exe in exes:
        if not exe.is_file():
            sys.exit(f"missing {exe}")
    ports = [args.base_port + i for i in range(n)]
    out = (ROOT / args.out).resolve()
    env = dict(os.environ, MELEE_NO_GC_ADAPTER="1")
    if args.lag_ms > 0:
        env["MELEE_NET_LAG_MS"] = str(args.lag_ms)
    else:
        env.pop("MELEE_NET_LAG_MS", None)

    procs = []
    for i in range(n):
        d = out / f"p{i + 1}"
        user = d / "user"
        user.mkdir(parents=True, exist_ok=True)
        for f in d.rglob("*.slp"):
            f.unlink()
        # Connect-code history for the code-entry screen (Z accepts the suggestion). Local peering
        # ignores the code itself; login falls back to the Slippi Launcher's read-only profile.
        for name in ("teams-codes.json", "direct-codes.json"):
            (user / name).write_text(CODE_HISTORY)
        remotes = ",".join(f"127.0.0.1:{p}" for j, p in enumerate(ports) if j != i)
        script = [] if args.no_script else ["--script", str((ROOT / scripts[i]).resolve())]
        cmd = [str(exes[i].resolve()), "--iso", str(iso), "--hidden", "--volume", "0", "--frames", str(args.frames)] + script + [
               "--replay-dir", str(d), "--log-file", str(d / "port.log"),
               "--user-dir", str(user), "--time-base", "1", "--local-peer", f"{i}:{ports[i]}:{remotes}"] + args.extra
        cmd += os.environ.get(f"QUAD_SLOT{i}_EXTRA", "").split()   # this slot only, e.g. a display option
        log = open(d / "log.txt", "w")
        p = subprocess.Popen(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, env=env)
        procs.append((p, log))
        print(f"P{i + 1} pid {p.pid} {engines[i]} {'no script' if args.no_script else Path(scripts[i]).name} --local-peer {i}:{ports[i]}:{remotes}")
        time.sleep(args.stagger)

    deadline = time.time() + (args.timeout or args.frames / 40 + 60)
    for i, (p, log) in enumerate(procs):
        try:
            p.wait(timeout=max(1, deadline - time.time()))
        except subprocess.TimeoutExpired:
            print(f"P{i + 1} timed out, killing pid {p.pid}")
            p.kill()
            p.wait()
        log.close()

    ok = True
    replays, setups = [], []
    print(f"\nslot engine  exit connected compared mismatched rollbacks online-frames")
    for i, (p, _) in enumerate(procs):
        d = out / f"p{i + 1}"
        r = parse_log(d / "log.txt")
        print(f"P{i + 1}   {engines[i]:7} {p.returncode:4} {str(r['connected']):9} {r['compared']:8} {r['mismatched']:10} "
              f"{r['rollbacks']:9} {r['last_frame']:6}")
        if r["local"]:
            print(f"     {r['local']}")
        if r["setup"]:
            print(f"     setup: {r['setup']}")
        setups.append(r["setup"])
        for line in r["desync"][:5] + r["errors"][:5]:
            print(f"     {line}")
        if not r["connected"] or r["mismatched"] or r["compared"] == 0:
            ok = False
        # A slot that crashed fails the run even when every checksum it compared agreed (09-29: a
        # crash at online frame 83 still printed PASS).
        if r.get("crash"):
            print(f"     {r['crash']}")
            ok = False
        slps = sorted(d.rglob("*.slp"), key=lambda f: f.stat().st_mtime)
        rep = parse_slp(slps[-1]) if slps else None
        replays.append(rep)
        if rep:
            teams, players, stage = rep
            desc = ", ".join(f"P{port} char {c} team {t}" for port, c, t in players)
            print(f"     replay {slps[-1].name}: teams={int(teams)} stage {stage}: {desc}")
    if any(s != setups[0] for s in setups) or not setups[0]:
        print("slots disagree on the match setup (or logged none)")
        ok = False
    if replays and all(replays) and any(r != replays[0] for r in replays):
        print("replays disagree on the match setup")
        ok = False
    print("\nPASS" if ok else "\nFAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
