"""Opt-in two-process handoff tests. Uses loopback peering, never Slippi matchmaking servers."""
import json
import os
from pathlib import Path
import subprocess
import shutil
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]


class HandoffTests(unittest.TestCase):
    def run_pair(self, engine):
        exe = Path(os.environ['MELEE_LOBBY_' + engine.upper() + '_EXE']).resolve()
        iso = os.environ['MELEE_ISO']
        output = ROOT / 'reports' / 'lobby-smoke' / engine
        processes, files, seen = [], [], [False, False]
        try:
            for index in range(2):
                folder = output / str(index)
                folder.mkdir(parents=True, exist_ok=True)
                if os.environ.get('MELEE_LOBBY_TEST_CARD'):
                    (folder / 'card').mkdir(exist_ok=True)
                    shutil.copy2(os.environ['MELEE_LOBBY_TEST_CARD'], folder / 'card')
                status = folder / 'status.json'
                status.write_text('{}')
                log = open(folder / 'console.log', 'w')
                files.append(log)
                command = [str(exe), '--iso', iso, '--headless', '--volume', '0', '--frames', '1200',
                           '--time-base', '1', '--local-peer', f'{index}:{42400+index}:127.0.0.1:{42401-index}',
                           '--lobby-direct', f'TEST#{index+1}', '--lobby-character', '2',
                           '--lobby-status-file', str(status), '--user-dir', str(folder / 'user'),
                           '--card-dir', str(folder / 'card'), '--replay-dir', str(folder / 'replays'),
                           '--log-file', str(folder / 'game.log')]
                processes.append(subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                                                   creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0)))
            deadline = time.monotonic() + 70
            while time.monotonic() < deadline and any(p.poll() is None for p in processes):
                for i in range(2):
                    try:
                        state = json.loads((output / str(i) / 'status.json').read_text())
                        if state.get('status') == 'In match' and state.get('stocks') == [4]:
                            seen[i] = True
                    except (OSError, ValueError):
                        pass
                if all(seen):
                    break
                time.sleep(0.2)
            self.assertEqual(seen, [True, True], f'{engine} did not reach a live four-stock match; see {output}')
        finally:
            for process in processes:
                if process.poll() is None:
                    process.terminate()
                process.wait(timeout=10)
            for log in files:
                log.close()

    @unittest.skipUnless(os.environ.get('MELEE_LOBBY_RECOMP_EXE') and os.environ.get('MELEE_ISO'), 'Game smoke disabled')
    def test_recomp(self):
        self.run_pair('recomp')

    @unittest.skipUnless(os.environ.get('MELEE_LOBBY_SOURCE_EXE') and os.environ.get('MELEE_ISO'), 'Game smoke disabled')
    def test_source(self):
        self.run_pair('source')
