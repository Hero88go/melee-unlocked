"""Opt-in launcher worker vs. native peer, with no HTTP lobby service."""

import os
import socket
import subprocess
import tempfile
import unittest
from pathlib import Path


class PeerWorkerTest(unittest.TestCase):
    @unittest.skipUnless(os.environ.get('MELEE_LOBBY_PEER_WORKER_TEST_EXE') and
                         os.environ.get('MELEE_LOBBY_PEER_TEST_EXE'),
                         'Peer worker and peer test executables not specified')
    def test_worker_handoff(self):
        reservations = []
        try:
            for _ in range(2):
                sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                sock.bind(('127.0.0.1', 0))
                reservations.append(sock)
            a, b = [sock.getsockname()[1] for sock in reservations]
        finally:
            for sock in reservations:
                sock.close()
        with tempfile.TemporaryDirectory(prefix='melee-peer-worker-') as folder:
            worker = subprocess.Popen([
                os.environ['MELEE_LOBBY_PEER_WORKER_TEST_EXE'], str(Path(folder) / 'alpha'),
                f'127.0.0.1:{b}', str(a)], stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, text=True)
            beta = subprocess.Popen([
                os.environ['MELEE_LOBBY_PEER_TEST_EXE'], 'beta', str(Path(folder) / 'beta'),
                f'127.0.0.1:{a}', str(b)], stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, text=True)
            try:
                worker_output, _ = worker.communicate(timeout=40)
                beta_output, _ = beta.communicate(timeout=40)
            finally:
                for process in (worker, beta):
                    if process.poll() is None:
                        process.kill()
                        process.wait()
            self.assertEqual(worker.returncode, 0, worker_output + beta_output)
            self.assertEqual(beta.returncode, 0, worker_output + beta_output)
            self.assertIn('PASS launcher peer worker and one-shot Slippi Direct handoff', worker_output)


if __name__ == '__main__':
    unittest.main()
