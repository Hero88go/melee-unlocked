"""YouGame Ranked from the online menu, end to end on this PC, hidden: two launcher bridges (the real
bridge code, port_launcher_yougame_test --serve) signed in to the local stand-in site
(fake_site.py), two Source Port games whose menus pick the first YouGame row (online_bot.txt), the
stand-in room pairing them, and the match played over the games' own UDP.

    py -3.12 tools/yougame_fake/e2e.py --out run-source/yg-e2e [--frames 4200]

Pass: both games signed in through their bridge, the stand-in issued two tickets carrying the build's
hashes, both games exchanged addresses in the room and played online frames with zero checksum
mismatches. Nothing reaches YouGame or Slippi.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REL = ROOT / 'build-avx2' / 'port' / 'Release'


def find_iso():
    sys.path.insert(0, str(ROOT / 'tools'))
    from melee_iso import find_iso as f   # noqa: E402
    return f()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', default='run-source/yg-e2e')
    ap.add_argument('--frames', type=int, default=4200)
    ap.add_argument('--script', default='port/scripts/online_bot.txt')
    ap.add_argument('--timeout', type=int, default=0)
    a = ap.parse_args()
    out = (ROOT / a.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    iso = os.environ.get('MELEE_ISO') or find_iso()
    if not iso:
        sys.exit('no ISO (MELEE_ISO)')
    no_window = 0x08000000
    portfile = out / 'fake.port'
    portfile.unlink(missing_ok=True)
    fake_log = out / 'fake.jsonl'
    fake_log.unlink(missing_ok=True)
    fake = subprocess.Popen([sys.executable, '-I', str(ROOT / 'tools/yougame_fake/fake_site.py'), '--log', str(fake_log),
                             '--portfile', str(portfile)], creationflags=no_window)
    for _ in range(100):
        if portfile.exists() and portfile.read_text():
            break
        time.sleep(0.05)
    site = f'http://127.0.0.1:{portfile.read_text()}'
    version = (ROOT / 'VERSION').read_text().strip()
    exe, dll = REL / 'melee_source.exe', REL / 'melee_game.dll'
    bridges, games, results = [], [], {'site': site}
    try:
        for i, email in enumerate(('alice@example.test', 'bob@example.test')):
            b = subprocess.Popen([str(REL / 'port_launcher_yougame_test.exe'), '--serve', site, email, 'pw', version, str(exe), str(dll)],
                                 stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, creationflags=no_window)
            line = b.stdout.readline().split()
            if not line or line[0] != 'BRIDGE':
                sys.exit(f'bridge {i} failed: {" ".join(line)}')
            bridges.append(b)
            results[f'bridge{i}'] = {'url': line[1], 'name': line[3], 'code': line[4]}
            d = out / f'p{i + 1}'
            (d / 'user').mkdir(parents=True, exist_ok=True)
            (d / 'cards').mkdir(exist_ok=True)
            settings = d / 'settings.ini'
            settings.write_text('startup 0\nbackend d3d11\nvolume 0\nscale 1\n')
            env = dict(os.environ, MELEE_NO_GC_ADAPTER='1', APPDATA=str(d / 'appdata'), LOCALAPPDATA=str(d / 'localappdata'),
                       YOUGAME_BRIDGE=line[1], YOUGAME_TOKEN=line[2], MELEE_YOUGAME_LAUNCHER='1')
            env.pop('MELEE_NET_LAG_MS', None)
            cmd = [str(exe), '--iso', str(iso), '--hidden', '--volume', '0', '--frames', str(a.frames),
                   '--script', str((ROOT / a.script).resolve()), '--sys-dir', str(ROOT / 'port' / 'slippi_sys'),
                   '--user-dir', str(d / 'user'), '--card-dir', str(d / 'cards'), '--settings-path', str(settings),
                   '--load-settings', '--log-file', str(d / 'port.log'), '--replay-dir', str(d), '--time-base', '1',
                   '--allow-matchmaking']
            log = open(d / 'log.txt', 'w')
            games.append((subprocess.Popen(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, env=env, creationflags=no_window), log))
            time.sleep(1.0)
        deadline = time.time() + (a.timeout or a.frames / 40 + 90)
        for p, log in games:
            try:
                p.wait(timeout=max(1, deadline - time.time()))
            except subprocess.TimeoutExpired:
                p.kill()
            log.close()
    finally:
        for b in bridges:
            try:
                b.stdin.close()
                b.wait(timeout=5)
            except Exception:
                b.kill()
        fake.kill()

    entries = [json.loads(l) for l in fake_log.read_text().splitlines() if l.strip()] if fake_log.exists() else []
    tickets = [e for e in entries if e.get('route') == 'ticket']
    cands = [e for e in entries if e.get('route') == 'room-cand']
    ok = True
    for i in range(2):
        text = (out / f'p{i + 1}' / 'port.log').read_text(errors='replace') if (out / f'p{i + 1}' / 'port.log').exists() else ''
        signed = re.search(r'yougame: signed in as (.+)', text)
        found = 'YouGame opponent found' in text
        frames = [int(m) for m in re.findall(r'online frame (\d+) wall', text)]
        cs = re.findall(r'checksums agree through frame (\d+) \((\d+) compared, (\d+) mismatched\)', text)
        # The log's counts are running totals: the last line holds the match's own.
        mism = int(cs[-1][2]) if cs else 0
        compared = int(cs[-1][1]) if cs else 0
        row = {'signed_in': signed.group(1) if signed else None, 'opponent_found': found,
               'online_frames': max(frames) if frames else 0, 'checksums_compared': compared, 'mismatched': mism,
               'slippi_servers': 'mm.slippi.gg' in text, 'stdio_or_ring': ('YouGame mode (stdio bridge' in text or 'directAttach' in text)}
        results[f'p{i + 1}'] = row
        ok &= bool(signed) and found and row['online_frames'] > 600 and mism == 0 and not row['slippi_servers'] and not row['stdio_or_ring']
    results['tickets'] = [{'user': t['user'], 'queue': t['body'].get('queue'), 'build': t['body'].get('build'),
                           'compat': (t['body'].get('lobby') or {}).get('compatibility'), 'cookie': t['cookie']} for t in tickets]
    results['candidates_exchanged'] = len(cands)
    ok &= len(tickets) >= 2 and all(t['body'].get('build', {}).get('exe_sha256') for t in tickets) and len(cands) >= 2
    results['pass'] = bool(ok)
    (out / 'result.json').write_text(json.dumps(results, indent=2))
    print(('PASS' if ok else 'FAIL') + ' yougame e2e: ' + json.dumps({k: results[k] for k in ('p1', 'p2')}) + f' | evidence {out}')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
