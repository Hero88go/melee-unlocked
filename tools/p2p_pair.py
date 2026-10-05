#!/usr/bin/env python3
# Runs two hidden no-Slippi Source Port instances through one peer-to-peer rollback match and checks they agree.
# SPDX-License-Identifier: GPL-2.0-or-later
"""Starts two instances of the build without the Slippi layer, each with --p2p-* options that
describe the same match, lets them play it on the loopback interface and compares what they report.

    python tools/p2p_pair.py                                   # Fox against Marth, Final Destination
    python tools/p2p_pair.py --lag-ms 40 --jitter-ms 5 --loss 1
    python tools/p2p_pair.py --chars 2/0:9/1 --stage 32 --seed 1234abcd --frames 6000
    python tools/p2p_pair.py --games 3                         # three games over one connection

Slot 0 listens on --base-port and slot 1 on the next port. With any of --lag-ms, --jitter-ms,
--loss, --reorder or --dup, each game dials tools/net_fault_proxy.py instead (two more ports), which
damages the traffic between them in a repeatable way.

PASS needs, on both sides: the handshake done and the match started, more than 0 checksums compared
with 0 mismatched, no DESYNC and no crash line, a result file written, and the two result files
holding the same descriptor digest and the same input transcript digest.

With --games n both instances get --p2p-games n and play n games without a new connection. Game k
after the first writes result.g<k>.json. PASS then needs all of the above for every game (each
game's own checksum totals, its own pair of result files with equal digests), and every game on a
descriptor digest of its own.
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
DEFAULT_EXE = "build-release-080-ns/port/Release/melee_source.exe"
DEFAULT_SCRIPT = "port/scripts/p2p_bot.txt"


def parse_log(path):
    # "games": one entry per game end line, in order (the session's counters start over with each game).
    r = {"connected": False, "handshake": False, "started": False, "compared": 0, "mismatched": 0, "agree_frame": 0,
         "desync": [], "errors": [], "waits": 0, "stalls": 0, "ended": False, "result_written": False, "crash": "",
         "starts": 0, "results_written": 0, "games": []}
    try:
        lines = open(path, errors="replace").read().splitlines()
    except OSError:
        return r
    for line in lines:
        if "p2p: connected to" in line:
            r["connected"] = True
        elif re.search(r"p2p: (mu_net: )?handshake done", line):
            r["handshake"] = True
        elif re.search(r"p2p: (mu_net: )?match starts", line):
            r["started"] = True
            r["starts"] += 1
        elif "p2p: result file written" in line:
            r["result_written"] = True
            r["results_written"] += 1
        elif "p2p: failed" in line or re.search(r"p2p: (mu_net: )?session failed", line) or "p2p: the session did not start" in line \
                or "p2p: no match was made" in line or "p2p: identity file" in line:
            r["errors"].append(line.strip())
        if "DESYNC" in line:
            r["desync"].append(line.strip())
        if "game crash" in line or line.startswith("CRASH:"):
            r["crash"] = line.strip()
        m = re.search(r"checksums agree through frame (\d+) \((\d+) compared, (\d+) mismatched\)", line)
        if m:
            r["agree_frame"] = int(m.group(1))
            r["compared"] = max(r["compared"], int(m.group(2)))
            r["mismatched"] = max(r["mismatched"], int(m.group(3)))
        # The totals at the game's end: the agreement line above is only printed every 20 comparisons.
        m = re.search(r"p2p: game end: (\d+) input frames confirmed, (\d+) waits, (\d+) frames stalled, "
                      r"(\d+) checksums compared, (\d+) mismatched", line)
        if m:
            r["ended"] = True
            r["waits"], r["stalls"] = int(m.group(2)), int(m.group(3))
            r["compared"] = max(r["compared"], int(m.group(4)))
            r["mismatched"] = max(r["mismatched"], int(m.group(5)))
            r["games"].append({"frames": int(m.group(1)), "waits": int(m.group(2)), "stalls": int(m.group(3)),
                               "compared": int(m.group(4)), "mismatched": int(m.group(5))})
    return r


def result_file(first, game):
    """The result file of game `game` beside the first game's file: result.json, result.g2.json, ..."""
    first = Path(first)
    return first if game <= 1 else first.with_name(f"{first.stem}.g{game}{first.suffix}")


def check_games(logs, firsts, games, labels=("P1", "P2")):
    """Every game of a session: both sides ended it with checksums compared and none mismatched, both
    wrote its result file, the two files agree, and no two games share a descriptor digest.
    Prints one line per game and returns True when all of it holds."""
    ok = True
    seen = {}
    print("\ngame  side  frames compared mismatched result  peer        descriptor        transcript")
    for game in range(1, games + 1):
        pair = []
        for i in range(2):
            ends = logs[i]["games"]
            end = ends[game - 1] if len(ends) >= game else None
            res = parse_result(result_file(firsts[i], game))
            pair.append(res)
            print(f"{game:4}  {labels[i]:5} {end['frames'] if end else 0:6} {end['compared'] if end else 0:8} "
                  f"{end['mismatched'] if end else 0:10} {'yes' if res else 'no':6}  {(res['peer_agreement'] if res else '-'):10}  "
                  f"{(res['descriptor_digest'][:16] if res else '-'):16}  {(res['transcript_digest'][:16] if res else '-'):16}")
            if not end or end["compared"] == 0 or end["mismatched"] or not res:
                ok = False
        if all(pair):
            a, b = pair
            for key, label in (("descriptor_digest", "descriptor digest"), ("transcript_digest", "input transcript digest")):
                if not a[key] or a[key] != b[key]:
                    print(f"      game {game}: {label} DIFFERENT")
                    ok = False
            if any(p["peer_agreement"] == "mismatch" for p in pair):
                print(f"      game {game}: a side reports the other's inputs as different")
                ok = False
            if a["descriptor_digest"] in seen:
                print(f"      game {game}: the same descriptor digest as game {seen[a['descriptor_digest']]}")
                ok = False
            seen[a["descriptor_digest"]] = game
    for i in range(2):
        if len(logs[i]["games"]) > games:
            print(f"      {labels[i]} ended {len(logs[i]['games'])} games, {games} were asked for")
            ok = False
    return ok


def parse_result(path):
    """The fields of a result file this check reads, or None when the file is missing."""
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return None
    out = {}
    for key in ("descriptor_digest", "transcript_digest", "peer_agreement"):
        m = re.search(r'"%s"\s*:\s*"([^"]*)"' % key, text)
        out[key] = m.group(1) if m else ""
    for key in ("transcript_frames", "winner"):
        m = re.search(r'"%s"\s*:\s*(-?\d+)' % key, text)
        out[key] = int(m.group(1)) if m else -1
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", default=DEFAULT_EXE, help="the no-Slippi melee_source.exe (default %(default)s)")
    ap.add_argument("--iso")
    ap.add_argument("--frames", type=int, default=0, help="retraces each instance runs before it exits (default 6000 per game)")
    ap.add_argument("--games", type=int, default=1, help="games played over the one connection (default %(default)s)")
    ap.add_argument("--chars", default="2/0:9/0", help="<id>[/<color>]:<id>[/<color>], external character ids (default Fox, Marth)")
    ap.add_argument("--stage", default="32", help="external stage id (default 32, Final Destination)")
    ap.add_argument("--seed", default="5eed1234", help="hex")
    ap.add_argument("--delay", type=int, default=2)
    ap.add_argument("--base-port", type=int, default=41300, help="slot 0 listens here, slot 1 on the next port; the proxy takes the two after")
    ap.add_argument("--lag-ms", type=float, default=0.0, help="one-way delay")
    ap.add_argument("--jitter-ms", type=float, default=0.0)
    ap.add_argument("--loss", type=float, default=0.0, help="percent")
    ap.add_argument("--reorder", type=float, default=0.0, help="percent")
    ap.add_argument("--dup", type=float, default=0.0, help="percent")
    ap.add_argument("--fault-seed", type=int, default=1, help="seed of the proxy's faults")
    ap.add_argument("--script", default=DEFAULT_SCRIPT, help="input script passed to both (default %(default)s)")
    ap.add_argument("--no-script", action="store_true")
    ap.add_argument("--stagger", type=float, default=0.5, help="seconds between the two launches")
    ap.add_argument("--timeout", type=float, default=0, help="kill the instances after this many seconds (0: frames/40 + 60)")
    ap.add_argument("--out", default="reports/p2p-pair")
    ap.add_argument("--extra", nargs=argparse.REMAINDER, default=[], help="more options for both instances")
    args = ap.parse_args()
    if args.games < 1 or args.games > 127:
        sys.exit("--games takes 1 to 127")
    if args.frames <= 0:
        args.frames = 6000 * args.games

    iso = require_iso(args.iso)
    exe = Path(args.exe)
    if not exe.is_absolute():
        exe = ROOT / exe
    if not exe.is_file():
        sys.exit(f"missing {exe}")
    script = None
    if not args.no_script:
        script = Path(args.script)
        if not script.is_absolute():
            script = ROOT / script
        if not script.is_file():
            sys.exit(f"missing {script}")
    out = (ROOT / args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    game_ports = [args.base_port, args.base_port + 1]
    faults = any(v > 0 for v in (args.lag_ms, args.jitter_ms, args.loss, args.reorder, args.dup))
    proxy_ports = [args.base_port + 2, args.base_port + 3]
    # What each game dials: the other game, or the proxy socket that faces this game.
    dial = proxy_ports if faults else [game_ports[1], game_ports[0]]
    timeout = args.timeout or args.frames / 40 + 60

    proxy = proxy_log = None
    if faults:
        proxy_log = open(out / "proxy.txt", "w")
        proxy = subprocess.Popen(
            [sys.executable, str(ROOT / "tools/net_fault_proxy.py"),
             "--a-port", str(proxy_ports[0]), "--b-port", str(proxy_ports[1]),
             "--a-game", f"127.0.0.1:{game_ports[0]}", "--b-game", f"127.0.0.1:{game_ports[1]}",
             "--delay", str(args.lag_ms), "--jitter", str(args.jitter_ms), "--loss", str(args.loss),
             "--reorder", str(args.reorder), "--dup", str(args.dup), "--seed", str(args.fault_seed),
             "--duration", str(timeout + 10)],
            cwd=ROOT, stdout=proxy_log, stderr=subprocess.STDOUT)
        print(f"proxy pid {proxy.pid}: ports {proxy_ports[0]} and {proxy_ports[1]}, lag {args.lag_ms} ms, jitter {args.jitter_ms} ms, "
              f"loss {args.loss}%, reorder {args.reorder}%, dup {args.dup}%")
        time.sleep(0.3)   # its sockets are bound before the first game dials

    env = dict(os.environ, MELEE_NO_GC_ADAPTER="1")
    procs = []
    for i in range(2):
        d = out / f"p{i + 1}"
        (d / "card").mkdir(parents=True, exist_ok=True)
        # A run only counts what it wrote itself.
        for stale in [d / "port.log", d / "log.txt", d / "result.json"] + list(d.glob("result.g*.json")):
            try:
                stale.unlink()
            except OSError:
                pass
        cmd = [str(exe), "--iso", str(iso), "--hidden", "--volume", "0", "--no-music", "--frames", str(args.frames),
               "--card-dir", str(d / "card"), "--settings-path", str(d / "settings.ini"), "--log-file", str(d / "port.log"),
               "--p2p-port", str(game_ports[i]), "--p2p-peer", f"127.0.0.1:{dial[i]}", "--p2p-slot", str(i),
               "--p2p-chars", args.chars, "--p2p-stage", args.stage, "--p2p-seed", args.seed, "--p2p-delay", str(args.delay),
               "--p2p-identity", str(d / "identity.json"), "--p2p-result", str(d / "result.json")]
        if args.games > 1:   # one game needs no option, and a build from before the option still runs
            cmd += ["--p2p-games", str(args.games)]
        if script:
            cmd += ["--script", str(script)]
        cmd += args.extra
        log = open(d / "log.txt", "w")
        p = subprocess.Popen(cmd, cwd=exe.parent, stdout=log, stderr=subprocess.STDOUT, env=env)
        procs.append((p, log))
        print(f"P{i + 1} pid {p.pid} slot {i} UDP {game_ports[i]} -> 127.0.0.1:{dial[i]}")
        if i == 0:
            time.sleep(args.stagger)

    deadline = time.time() + timeout
    for i, (p, log) in enumerate(procs):
        try:
            p.wait(timeout=max(1, deadline - time.time()))
        except subprocess.TimeoutExpired:
            print(f"P{i + 1} timed out, killing pid {p.pid}")
            p.kill()
            p.wait()
        log.close()
    if proxy:
        proxy.terminate()
        proxy.wait()
        proxy_log.close()

    ok = True
    results = []
    logs = []
    print("\nslot exit handshake started compared mismatched agree-frame waits stalled result frames peer")
    for i, (p, _) in enumerate(procs):
        d = out / f"p{i + 1}"
        r = parse_log(d / "port.log")
        if not r["handshake"] and not r["errors"]:
            r = parse_log(d / "log.txt")   # a build that logs to its console only
        logs.append(r)
        res = parse_result(d / "result.json")
        results.append(res)
        print(f"P{i + 1}   {p.returncode:4} {str(r['handshake']):9} {str(r['started']):7} {r['compared']:8} {r['mismatched']:10} "
              f"{r['agree_frame']:11} {r['waits']:5} {r['stalls']:7} {'yes' if res else 'no':6} "
              f"{res['transcript_frames'] if res else 0:6} {res['peer_agreement'] if res else '-'}")
        for line in r["desync"][:5] + r["errors"][:5]:
            print(f"     {line}")
        if r["crash"]:
            print(f"     {r['crash']}")
        if not r["handshake"] or not r["started"] or r["compared"] == 0 or r["mismatched"] or r["desync"] or r["crash"]:
            ok = False
        if not res or not r["result_written"]:
            print("     no result file")
            ok = False
    if all(results):
        a, b = results
        for key, label in (("descriptor_digest", "descriptor digest"), ("transcript_digest", "input transcript digest")):
            same = bool(a[key]) and a[key] == b[key]
            print(f"{label}: {'same' if same else 'DIFFERENT'} ({a[key][:16] or 'none'} / {b[key][:16] or 'none'})")
            if not same:
                ok = False
    if args.games > 1:
        if not check_games(logs, [out / f"p{i + 1}" / "result.json" for i in range(2)], args.games):
            ok = False
        for i, r in enumerate(logs):
            if r["starts"] != args.games:
                print(f"P{i + 1} started {r['starts']} games, {args.games} were asked for")
                ok = False
    print("\nPASS" if ok else "\nFAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
