"""Opt-in two-node LAN DHT smoke; requires a non-loopback address on this host."""

import os
import re
import socket
import subprocess
import unittest


class DhtProbeTest(unittest.TestCase):
    @unittest.skipUnless(os.environ.get('MELEE_DHT_PROBE_EXE') and
                         os.environ.get('MELEE_DHT_TEST_HOST'),
                         'DHT probe executable and LAN address not specified')
    def test_two_nodes_announce_and_discover(self):
        executable = os.environ['MELEE_DHT_PROBE_EXE']
        host = os.environ['MELEE_DHT_TEST_HOST']
        ports, reservations = [], []
        try:
            for _ in range(2):
                sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                sock.bind((host, 0))
                reservations.append(sock)
                ports.append(sock.getsockname()[1])
        finally:
            for sock in reservations:
                sock.close()
        self.assertNotEqual(*ports)
        peers = []
        for port, bootstrap in [(ports[0], ports[1]), (ports[1], ports[0])]:
            peers.append(subprocess.Popen(
                [executable, '--port', str(port), '--bootstrap',
                 f'{host}:{bootstrap}', '--announce', '--seconds', '50'],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True))
        outputs = []
        try:
            for peer in peers:
                out, _ = peer.communicate(timeout=65)
                outputs.append(out)
                self.assertEqual(peer.returncode, 0, out)
        finally:
            for peer in peers:
                if peer.poll() is None:
                    peer.kill()
                    peer.wait()
        for out in outputs:
            self.assertRegex(out, r'DHT_NODES [1-9]')
        self.assertTrue(any(re.search(r'PEER ' + re.escape(host) + r':', out)
                            for out in outputs), '\n---\n'.join(outputs))


if __name__ == '__main__':
    unittest.main()
