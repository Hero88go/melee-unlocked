"""Opt-in native client integration: set MELEE_LOBBY_CLIENT_TEST_EXE to the built test exe."""
import os
import socket
import subprocess
import threading
import unittest
from http.server import ThreadingHTTPServer
from server import Lobby, handler


class ClientTests(unittest.TestCase):
    @unittest.skipUnless(os.environ.get('MELEE_LOBBY_CLIENT_TEST_EXE'), 'Native test executable not specified')
    def test_windows_client(self):
        lobby = Lobby()
        http = ThreadingHTTPServer(('127.0.0.1', 0), handler(lobby))
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp.bind(('127.0.0.1', http.server_port))
        udp.settimeout(0.2)
        stop = threading.Event()
        def receive():
            while not stop.is_set():
                try:
                    data, address = udp.recvfrom(1024)
                    lobby.register_udp(data, address)
                except socket.timeout:
                    pass
        threads = [threading.Thread(target=http.serve_forever), threading.Thread(target=receive)]
        for thread in threads:
            thread.start()
        try:
            result = subprocess.run([os.environ['MELEE_LOBBY_CLIENT_TEST_EXE'], f'http://127.0.0.1:{http.server_port}'],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())
        finally:
            stop.set()
            http.shutdown()
            for thread in threads:
                thread.join()
            http.server_close()
            udp.close()
            lobby.db.close()
