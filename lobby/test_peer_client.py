"""Opt-in native two-process lobby exchange with no shared lobby service."""

import json
import os
import socket
import subprocess
import tempfile
import unittest
from pathlib import Path


class PeerClientTest(unittest.TestCase):
    def run_pair(self, host, prefix='', timeout=30, outside=False):
        exe = os.environ['MELEE_LOBBY_PEER_TEST_EXE']
        sockets = []
        try:
            for _ in range(2):
                sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                sock.bind((host, 0))
                sockets.append(sock)
            a, b = [sock.getsockname()[1] for sock in sockets]
        finally:
            for sock in sockets:
                sock.close()
        with tempfile.TemporaryDirectory(prefix='melee-peer-lobby-') as folder:
            extra = ['outside'] if outside else []
            alpha = subprocess.Popen([exe, 'alpha', str(Path(folder) / 'alpha'), f'{prefix}{host}:{b}', str(a), str(timeout), *extra],
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            beta = subprocess.Popen([exe, 'beta', str(Path(folder) / 'beta'), f'{prefix}{host}:{a}', str(b), str(timeout), *extra],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            try:
                out_a, _ = alpha.communicate(timeout=timeout + 15)
                out_b, _ = beta.communicate(timeout=timeout + 15)
            finally:
                for process in (alpha, beta):
                    if process.poll() is None:
                        process.kill()
                        process.wait()
            self.assertEqual(alpha.returncode, 0, out_a + '\n' + out_b)
            self.assertEqual(beta.returncode, 0, out_a + '\n' + out_b)
            if os.environ.get('MELEE_PEER_DEBUG'):
                print('Alpha:', out_a, 'Beta:', out_b)
            self.assertIn('PASS peer chat, friendship, request, match, stocks, RTT', out_a)
            self.assertIn('PASS peer receiving chat, friend and match acceptance', out_b)
            if outside:
                resumed = [
                    subprocess.Popen([exe, 'resume', str(Path(folder) / 'alpha'), f'127.0.0.1:{b}', str(a), 'alpha'],
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True),
                    subprocess.Popen([exe, 'resume', str(Path(folder) / 'beta'), f'127.0.0.1:{a}', str(b), 'beta'],
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True),
                ]
                try:
                    for process in resumed:
                        output, _ = process.communicate(timeout=20)
                        self.assertEqual(process.returncode, 0, output)
                        self.assertIn('PASS friends reconnect outside public lobby after restart', output)
                finally:
                    for process in resumed:
                        if process.poll() is None:
                            process.kill()
                            process.wait()
            identities = []
            for name in ('alpha', 'beta'):
                result = subprocess.run([exe, 'inspect', str(Path(folder) / name)],
                                        capture_output=True, text=True, timeout=10, check=True)
                identities.append(json.loads(result.stdout))
            for me, other in ((identities[0], identities[1]), (identities[1], identities[0])):
                self.assertEqual(len(me['friends']), 1)
                self.assertEqual(me['friends'][0]['id'], other['self']['id'])
                self.assertEqual(me['friends'][0]['status'], 'Offline')

    @unittest.skipUnless(os.environ.get('MELEE_LOBBY_PEER_TEST_EXE'), 'Peer test executable not specified')
    def test_direct_exchange(self):
        self.run_pair('127.0.0.1', outside=True)

    @unittest.skipUnless(os.environ.get('MELEE_LOBBY_PEER_TEST_EXE') and os.environ.get('MELEE_DHT_TEST_HOST'),
                         'DHT test requires a LAN address')
    def test_dht_discovery_without_direct_seed(self):
        self.run_pair(os.environ['MELEE_DHT_TEST_HOST'], 'dht://', 110)


if __name__ == '__main__':
    unittest.main()
