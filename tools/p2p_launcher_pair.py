#!/usr/bin/env python3
# Drives two hidden no-Slippi launchers from their lobbies into one peer-to-peer match and checks both games agree.
# SPDX-License-Identifier: GPL-2.0-or-later
"""Starts two copies of the launcher built without the Slippi layer, each from a private folder,
and lets their lobbies find each other on the loopback interface, agree on a match and start the
game, with no click and no window on the desktop.

    python tools/p2p_launcher_pair.py --iso <disc>                  # both press Find match
    python tools/p2p_launcher_pair.py --iso <disc> --mode direct    # A requests, B accepts
    python tools/p2p_launcher_pair.py --iso <disc> --separate-port  # the game on a port of its own
    python tools/p2p_launcher_pair.py --iso <disc> --games 2        # two games over the one connection
    python tools/p2p_launcher_pair.py --iso <disc> --fighters 9/2,0/5   # A plays Marth color 3, B Falcon color 6

Each folder holds the launcher, the game, its libraries and lang/ (hard links to --build-dir where
the disk allows, copies otherwise), a launcher.ini naming the disc, and its own LOCALAPPDATA and
APPDATA, so nothing of the user's is read or written. The public DHT is skipped: each lobby is
given the other's UDP port as its bootstrap peer.

PASS needs: both launchers start a game with --p2p-* arguments, both games exit by themselves, and
both write a result file holding the same descriptor digest and the same input transcript digest.
Unless --separate-port is given it also needs each game on its lobby's own port and each lobby
open again on that port after the game. Only processes this script started are ever ended: the two
launchers by pid, and their own game children if they outlive the timeout.

The launcher starts its games with --p2p-games 0 (play until a player quits). This script always
adds --p2p-games <n> after it (--games, default 1), so a run plays exactly that many games; with
more than one, every game needs its own pair of result files with equal digests (see p2p_pair.py).
--fighters sets each side's character and color as the Profile page would
(MELEE_LAUNCHER_TEST_LOBBY_FIGHTER) and checks that both games were started with exactly those.
"""
import argparse
import ctypes
import os
import re
import shutil
import subprocess
import sys
import time
from ctypes import wintypes
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from melee_iso import require_iso
from p2p_pair import check_games, parse_log, parse_result

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BUILD = "build-release-080-ns/port/Release"
DEFAULT_SCRIPT = "port/scripts/p2p_bot.txt"
LAUNCHER = "MeleeUnlockedLauncher.exe"
GAME = "melee_source.exe"
NAMES = ("Alpha", "Beta")


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [("dwSize", wintypes.DWORD), ("cntUsage", wintypes.DWORD), ("th32ProcessID", wintypes.DWORD),
                ("th32DefaultHeapID", ctypes.c_size_t), ("th32ModuleID", wintypes.DWORD), ("cntThreads", wintypes.DWORD),
                ("th32ParentProcessID", wintypes.DWORD), ("pcPriClassBase", ctypes.c_long), ("dwFlags", wintypes.DWORD),
                ("szExeFile", ctypes.c_wchar * 260)]


def game_children(parent_pid):
    """Pids of the games this launcher started (its direct children named melee_source.exe)."""
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    k32.Process32FirstW.argtypes = k32.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(PROCESSENTRY32W)]
    k32.CloseHandle.argtypes = [wintypes.HANDLE]
    snapshot = k32.CreateToolhelp32Snapshot(0x2, 0)   # TH32CS_SNAPPROCESS
    if not snapshot or snapshot == wintypes.HANDLE(-1).value:
        return []
    out = []
    entry = PROCESSENTRY32W()
    entry.dwSize = ctypes.sizeof(entry)
    more = k32.Process32FirstW(snapshot, ctypes.byref(entry))
    while more:
        if entry.th32ParentProcessID == parent_pid and entry.szExeFile.lower() == GAME:
            out.append(entry.th32ProcessID)
        more = k32.Process32NextW(snapshot, ctypes.byref(entry))
    k32.CloseHandle(snapshot)
    return out


def end_pid(pid):
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.OpenProcess.restype = wintypes.HANDLE
    k32.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
    k32.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = k32.OpenProcess(0x1, False, pid)   # PROCESS_TERMINATE
    if handle:
        k32.TerminateProcess(handle, 1)
        k32.CloseHandle(handle)


def place(source, target):
    """A hard link when the two are on one volume, else a copy. An existing file is replaced."""
    target.parent.mkdir(parents=True, exist_ok=True)
    try:
        target.unlink()
    except OSError:
        pass
    try:
        os.link(source, target)
    except OSError:
        shutil.copy2(source, target)


def prepare(folder, build, iso):
    folder.mkdir(parents=True, exist_ok=True)
    # A run only counts what it wrote itself. The identity stays, so a folder keeps its player.
    for stale in ("launch.log", "lobby.log", "lobby.log.1", "port.log", "lobby-profile.json", "lobby-history.json", "launcher.txt"):
        try:
            (folder / stale).unlink()
        except OSError:
            pass
    shutil.rmtree(folder / "p2p-results", ignore_errors=True)
    wanted = [build / LAUNCHER, build / GAME]
    # The game library with its two companions, and every runtime library beside the executables.
    wanted += sorted(p for p in build.iterdir() if p.is_file() and (p.suffix.lower() == ".dll" or p.name.lower().startswith("melee_game.")))
    for source in dict.fromkeys(wanted):
        if not source.is_file():
            sys.exit(f"missing {source}")
        place(source, folder / source.name)
    lang = build / "lang"
    if lang.is_dir():
        for source in lang.rglob("*"):
            if source.is_file():
                place(source, folder / "lang" / source.relative_to(lang))
    else:
        print(f"note: no lang folder in {build}; the launchers run in English")
    (folder / "launcher.ini").write_text(f"iso={iso}\nengine=1\n", encoding="utf-8")
    for sub in ("appdata/Local", "appdata/Roaming", "card"):
        (folder / sub).mkdir(parents=True, exist_ok=True)


def read_text(path):
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def p2p_launch(folder):
    """The game command line this launcher logged for the match, or ''."""
    for line in read_text(folder / "launch.log").splitlines():
        if "--p2p-port" in line:
            return line
    return ""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build-dir", default=DEFAULT_BUILD, help="the no-Slippi build's output folder (default %(default)s)")
    ap.add_argument("--iso")
    ap.add_argument("--mode", choices=("auto", "direct"), default="auto",
                    help="auto: both press Find match. direct: A sends a match request, B accepts it")
    ap.add_argument("--frames", type=int, default=0, help="retraces each game runs before it exits (default 6000 per game)")
    ap.add_argument("--games", type=int, default=1, help="games played over the one connection (default %(default)s)")
    ap.add_argument("--fighters", default="", help="<character>/<color>,<character>/<color>: side A's and side B's choice (default: each side's first main)")
    ap.add_argument("--script", default=DEFAULT_SCRIPT, help="input script passed to both games (default %(default)s)")
    ap.add_argument("--base-port", type=int, default=41400, help="lobby A's UDP port; lobby B takes the next one")
    ap.add_argument("--separate-port", action="store_true", help="MELEE_P2P_SEPARATE_PORT=1: the game on a free port of its own")
    ap.add_argument("--pair-timeout", type=float, default=90, help="seconds for both launchers to start a match")
    ap.add_argument("--timeout", type=float, default=0, help="seconds for both games to exit (0: frames/40 + 90)")
    ap.add_argument("--out", default="reports/p2p-launcher-pair")
    args = ap.parse_args()
    if args.games < 1 or args.games > 127:
        sys.exit("--games takes 1 to 127")
    if args.frames <= 0:
        args.frames = 6000 * args.games
    fighters = [f.strip() for f in args.fighters.split(",")] if args.fighters else []
    if fighters and (len(fighters) != 2 or not all(re.fullmatch(r"\d+/\d+", f) for f in fighters)):
        sys.exit("--fighters takes <character>/<color>,<character>/<color>")

    iso = require_iso(args.iso)
    build = Path(args.build_dir)
    if not build.is_absolute():
        build = ROOT / build
    if not (build / LAUNCHER).is_file() or not (build / GAME).is_file():
        sys.exit(f"missing {LAUNCHER} or {GAME} in {build}")
    script = Path(args.script)
    if not script.is_absolute():
        script = ROOT / script
    if not script.is_file():
        sys.exit(f"missing {script}")
    out = (ROOT / args.out).resolve()
    folders = [out / "a", out / "b"]
    ports = [args.base_port, args.base_port + 1]
    roles = ({"MELEE_LAUNCHER_TEST_LOBBY_AUTOSEARCH": "1"},) * 2 if args.mode == "auto" else \
            ({"MELEE_LAUNCHER_TEST_LOBBY_REQUEST": "1"}, {"MELEE_LAUNCHER_TEST_LOBBY_ACCEPT": "1"})

    launchers = []
    for i, folder in enumerate(folders):
        prepare(folder, build, iso)
        # Appended after the launcher's own arguments: the card, settings and log stay in this folder.
        # --p2p-games comes after the launcher's own "--p2p-games 0" and so replaces it.
        game_args = (f'--hidden --volume 0 --no-music --frames {args.frames} --script "{script}" --p2p-games {args.games} '
                     f'--card-dir "{folder / "card"}" --settings-path "{folder / "settings.ini"}" --log-file "{folder / "port.log"}"')
        env = dict(os.environ)
        env.pop("MELEE_P2P_SEPARATE_PORT", None)
        env.pop("MELEE_LAUNCHER_TEST_LOBBY_FIGHTER", None)
        if fighters:
            env["MELEE_LAUNCHER_TEST_LOBBY_FIGHTER"] = fighters[i]
        env.update({
            "LOCALAPPDATA": str(folder / "appdata" / "Local"), "APPDATA": str(folder / "appdata" / "Roaming"),
            "MELEE_LAUNCHER_TEST": "1", "MELEE_LAUNCHER_TEST_GAME_ARGS": game_args, "MELEE_NO_GC_ADAPTER": "1",
            "MELEE_LAUNCHER_TEST_LAUNCH_LOG": str(folder / "launch.log"),
            "MELEE_LAUNCHER_TEST_NO_DHT": "1", "MELEE_LAUNCHER_TEST_LOBBY_PORT": str(ports[i]),
            "MELEE_LAUNCHER_TEST_LOBBY_PEER": f"127.0.0.1:{ports[1 - i]}", "MELEE_LAUNCHER_TEST_LOBBY_NAME": NAMES[i],
        })
        env.update(roles[i])
        if args.separate_port:
            env["MELEE_P2P_SEPARATE_PORT"] = "1"
        log = open(folder / "launcher.txt", "w")
        p = subprocess.Popen([str(folder / LAUNCHER)], cwd=folder, env=env, stdout=log, stderr=subprocess.STDOUT)
        launchers.append((p, log))
        print(f"{NAMES[i]}: launcher pid {p.pid}, lobby UDP {ports[i]}, folder {folder}")

    ok = True
    notes = []
    games = [[], []]
    try:
        # 1. Both launchers start a game for the match.
        deadline = time.time() + args.pair_timeout
        while time.time() < deadline and not all(p2p_launch(f) for f in folders):
            if any(p.poll() is not None for p, _ in launchers):
                break
            time.sleep(0.25)
        paired = [bool(p2p_launch(f)) for f in folders]
        if not all(paired):
            ok = False
            notes.append("no match was started by " + ", ".join(NAMES[i] for i in range(2) if not paired[i]))
        # 2. Both games exit by themselves.
        deadline = time.time() + (args.timeout or args.frames / 40 + 90)
        seen = [False, False]
        quiet = 0
        while all(paired) and time.time() < deadline:
            for i, (p, _) in enumerate(launchers):
                games[i] = game_children(p.pid)
                seen[i] = seen[i] or bool(games[i])
            # A game that came and went between two looks still leaves its log behind.
            done = [not games[i] and (seen[i] or (folders[i] / "port.log").is_file()) for i in range(2)]
            quiet = quiet + 1 if all(done) else 0
            if quiet >= 4:
                break
            time.sleep(0.5)
        else:
            if all(paired):
                ok = False
                notes.append("the games did not exit in time")
        # 3. The lobbies come back on their ports (nothing to wait for when the game had its own).
        if all(paired) and not args.separate_port:
            deadline = time.time() + 8
            while time.time() < deadline and not all("lobby open again" in read_text(f / "lobby.log") for f in folders):
                time.sleep(0.25)
    finally:
        for i, (p, log) in enumerate(launchers):
            # Only while the launcher lives is a child pid certainly its game; a game whose launcher
            # died is left to run out its --frames.
            for pid in game_children(p.pid) if p.poll() is None else []:
                print(f"{NAMES[i]}: ending its game, pid {pid}")
                end_pid(pid)
            if p.poll() is None:
                p.kill()
            p.wait()
            log.close()

    results = []
    logs = []
    firsts = []
    chars_seen = []
    print("\nside  lobby  game-port first-peer            handshake started compared mismatched result frames peer-agreement lobby-reopened")
    for i, folder in enumerate(folders):
        line = p2p_launch(folder)
        port = re.search(r"--p2p-port (\d+)", line)
        peer = re.search(r"--p2p-peer (\S+)", line)
        wait = re.search(r"--p2p-connect-seconds (\d+)", line)
        chars = re.search(r"--p2p-chars (\S+)", line)
        chars_seen.append(chars.group(1) if chars else "")
        game_port = int(port.group(1)) if port else 0
        r = parse_log(folder / "port.log")
        logs.append(r)
        # The first game's file is "<request id>.json"; later games of the session sit beside it as
        # "<request id>.g<n>.json".
        files = sorted(f for f in (folder / "p2p-results").glob("*.json") if not re.search(r"\.g\d+\.json$", f.name)) \
            if (folder / "p2p-results").is_dir() else []
        firsts.append(files[-1] if files else folder / "p2p-results" / "missing.json")
        res = parse_result(files[-1]) if files else None
        results.append(res)
        lobby = read_text(folder / "lobby.log")
        reopened = re.search(r"lobby open again on UDP port (\d+)", lobby)
        print(f"{NAMES[i]:5} {ports[i]:6} {game_port:9} {(peer.group(1) if peer else '-'):21} {str(r['handshake']):9} {str(r['started']):7} "
              f"{r['compared']:8} {r['mismatched']:10} {'yes' if res else 'no':6} {res['transcript_frames'] if res else 0:6} "
              f"{(res['peer_agreement'] if res else '-'):14} {reopened.group(1) if reopened else 'no'}")
        for text in r["desync"][:5] + [e for e in r["errors"] if "port could not be opened" not in e][:5]:
            print(f"      {text}")
        if r["crash"]:
            print(f"      {r['crash']}")
        if not line:
            continue
        if not wait:
            ok = False
            notes.append(f"{NAMES[i]}: the launcher passed no --p2p-connect-seconds")
        if not r["handshake"] or not r["started"] or r["compared"] == 0 or r["mismatched"] or r["desync"] or r["crash"]:
            ok = False
            notes.append(f"{NAMES[i]}: the game did not play a clean match (see {folder / 'port.log'})")
        if not res:
            ok = False
            notes.append(f"{NAMES[i]}: no result file in {folder / 'p2p-results'}")
        if args.separate_port:
            if game_port == ports[i]:
                ok = False
                notes.append(f"{NAMES[i]}: the game took the lobby's port although a separate one was asked for")
        else:
            if game_port != ports[i]:
                ok = False
                notes.append(f"{NAMES[i]}: the game's port {game_port} is not the lobby's port {ports[i]}")
            if peer and peer.group(1) != f"127.0.0.1:{ports[1 - i]}":
                ok = False
                notes.append(f"{NAMES[i]}: the first address dialed is {peer.group(1)}, not the other lobby's 127.0.0.1:{ports[1 - i]}")
            if "lobby closed for a match" not in lobby:
                ok = False
                notes.append(f"{NAMES[i]}: the lobby did not close for the match")
            if not reopened or int(reopened.group(1)) != ports[i]:
                ok = False
                notes.append(f"{NAMES[i]}: the lobby did not open again on port {ports[i]}")
    if all(results):
        a, b = results
        for key, label in (("descriptor_digest", "descriptor digest"), ("transcript_digest", "input transcript digest")):
            same = bool(a[key]) and a[key] == b[key]
            print(f"{label}: {'same' if same else 'DIFFERENT'} ({a[key][:16] or 'none'} / {b[key][:16] or 'none'})")
            if not same:
                ok = False
    if args.games > 1 and all(p2p_launch(f) for f in folders):
        if not check_games(logs, firsts, args.games, NAMES):
            ok = False
        for i, r in enumerate(logs):
            if r["starts"] != args.games:
                ok = False
                notes.append(f"{NAMES[i]}: the game started {r['starts']} games, {args.games} were asked for")
    if fighters and all(chars_seen):
        # Slot 0 is whoever sent the request, which automatic pairing does not fix: either order.
        wanted = {f"{fighters[0]}:{fighters[1]}", f"{fighters[1]}:{fighters[0]}"}
        print(f"characters: {chars_seen[0]} / {chars_seen[1]} (chosen: {fighters[0]} and {fighters[1]})")
        if chars_seen[0] != chars_seen[1]:
            ok = False
            notes.append("the two games were started with different --p2p-chars")
        elif chars_seen[0] not in wanted and fighters[0] != fighters[1]:
            ok = False
            notes.append("the games were not started with the chosen characters and colors")
    for note in notes:
        print(note)
    print("\nPASS" if ok else "\nFAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
