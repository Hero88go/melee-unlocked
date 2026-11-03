"""A local stand-in for YouGame's site and rooms, for the launcher bridge's end-to-end run.

Routes (the ones the launcher calls, see port/app/launcher_yougame.cpp):
  POST /auth/v1/token?grant_type=password|refresh_token   sign-in
  GET  /api/multiplayer/code                               the player's connect code
  GET  /api/multiplayer/rank                               a fixed standing
  POST /api/multiplayer/ticket                             pairs two players into one room
  GET  /api/native/builds                                  the builds allowed (--allowed VERSION:EXE:GAME ...)
  GET  /room/<code>?ticket=...  (WebSocket)                welcome, join, and relays {t:"msg"} by "to"
Every request is logged as one JSON line to --log. Nothing here talks to YouGame.

    py -3.12 tools/yougame_fake/fake_site.py --port 0 --log fake.jsonl --portfile fake.port
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import base64
import hashlib
import json
import socket
import socketserver
import struct
import threading
import time
from urllib.parse import urlsplit, parse_qs

GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'


class State:
    def __init__(self, log_path):
        self.lock = threading.Lock()
        self.log_file = open(log_path, 'a', buffering=1, encoding='utf-8') if log_path else None
        self.users = {}          # access token -> user
        self.waiting = {}        # pairing key -> (room code, first user id)
        self.rooms = {}          # code -> {'conns': [Conn], 'size': 2}
        self.next_room = 1
        self.allowed = None      # /api/native/builds: None answers 404 (YouGame said nothing)

    def log(self, **entry):
        entry['at'] = round(time.time(), 3)
        if self.log_file:
            self.log_file.write(json.dumps(entry) + '\n')

    def user_for(self, headers):
        auth = headers.get('authorization', '')
        token = auth[7:] if auth.startswith('Bearer ') else ''
        with self.lock:
            return self.users.get(token)


STATE = None


class Conn:
    def __init__(self, sock, cid):
        self.sock, self.id, self.lock = sock, cid, threading.Lock()

    def send(self, obj):
        data = json.dumps(obj).encode()
        head = bytes([0x81])
        n = len(data)
        head += bytes([n]) if n < 126 else bytes([126]) + struct.pack('>H', n) if n < 65536 else bytes([127]) + struct.pack('>Q', n)
        with self.lock:
            try:
                self.sock.sendall(head + data)
            except OSError:
                pass


def read_frame(sock):
    def need(n):
        b = b''
        while len(b) < n:
            c = sock.recv(n - len(b))
            if not c:
                raise ConnectionError
            b += c
        return b
    h = need(2)
    op, ln = h[0] & 0x0F, h[1] & 0x7F
    if ln == 126:
        ln = struct.unpack('>H', need(2))[0]
    elif ln == 127:
        ln = struct.unpack('>Q', need(8))[0]
    mask = need(4) if h[1] & 0x80 else b'\0\0\0\0'
    data = bytes(b ^ mask[i % 4] for i, b in enumerate(need(ln)))
    return op, data


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        sock = self.request
        sock.settimeout(60)
        buf = b''
        while b'\r\n\r\n' not in buf:
            c = sock.recv(65536)
            if not c:
                return
            buf += c
        head, _, rest = buf.partition(b'\r\n\r\n')
        lines = head.decode('latin-1').split('\r\n')
        method, target, _ = lines[0].split(' ', 2)
        headers = {}
        for l in lines[1:]:
            k, _, v = l.partition(':')
            headers[k.strip().lower()] = v.strip()
        length = int(headers.get('content-length', '0') or 0)
        while len(rest) < length:
            rest += sock.recv(65536)
        body = rest[:length].decode('utf-8', 'replace')
        url = urlsplit(target)
        query = {k: v[0] for k, v in parse_qs(url.query).items()}
        if headers.get('upgrade', '').lower() == 'websocket':
            return self.websocket(sock, url.path, query, headers)
        status, answer = self.route(method, url.path, query, headers, body)
        out = json.dumps(answer).encode()
        sock.sendall(f'HTTP/1.1 {status} X\r\nContent-Type: application/json\r\nContent-Length: {len(out)}\r\n'
                     f'Connection: close\r\n\r\n'.encode() + out)

    def route(self, method, path, query, headers, body):
        s = STATE
        if path == '/auth/v1/token':
            q = json.loads(body or '{}')
            s.log(route='token', grant=query.get('grant_type'), apikey=bool(headers.get('apikey')))
            with s.lock:
                if query.get('grant_type') == 'password':
                    email = q.get('email', '')
                    if q.get('password') != 'pw':
                        return 400, {'error': 'invalid_grant', 'error_description': 'Invalid login credentials'}
                    tag = email.split('@')[0].upper()[:4] or 'USER'
                    user = {'id': 'user-' + email, 'email': email, 'code': f'{tag}#{len(s.users) + 1}', 'name': email.split('@')[0].title()}
                else:
                    old = next((u for t, u in s.users.items() if u.get('refresh') == q.get('refresh_token')), None)
                    if not old:
                        return 400, {'error': 'invalid_grant'}
                    user = old
                access, refresh = 'acc-' + hashlib.sha1(str(time.time()).encode() + user['id'].encode()).hexdigest()[:16], 'ref-' + user['id'] + str(time.time())
                user = dict(user, refresh=refresh)
                s.users[access] = user
            return 200, {'access_token': access, 'refresh_token': refresh, 'expires_in': 3600, 'token_type': 'bearer',
                         'user': {'id': user['id'], 'email': user['email'], 'user_metadata': {'name': user['name']}}}
        if path == '/api/native/builds':
            s.log(route='builds')
            if s.allowed is None:
                return 404, {'error': 'Not found'}
            return 200, {'allowed': s.allowed}
        user = s.user_for(headers)
        cookie_ok = any(c.strip().startswith('sb-') for c in headers.get('cookie', '').split(';'))
        if not user:
            s.log(route=path, refused='signed out')
            return 401, {'error': 'Sign in first', 'code': 'signed_out'}
        if path == '/api/multiplayer/code':
            s.log(route='code', user=user['id'], cookie=cookie_ok)
            return 200, {'code': user['code']}
        if path == '/api/multiplayer/rank':
            s.log(route='rank', user=user['id'], mode=query.get('mode'))
            return 200, {'rank': 'Unranked', 'rating': 1500}
        if path == '/api/multiplayer/ticket' and method == 'POST':
            q = json.loads(body or '{}')
            s.log(route='ticket', user=user['id'], body=q, cookie=cookie_ok, origin=headers.get('origin'))
            with s.lock:
                if 'direct' in q:   # Direct: both name each other's code
                    me = user['code']
                    key = ('direct',) + tuple(sorted([me, q['direct']]))
                else:
                    key = ('queue', q.get('queue'), q.get('lobby', {}).get('compatibility'))
                if key in s.waiting and s.waiting[key][1] != user['id']:
                    code = s.waiting.pop(key)[0]
                else:
                    code = f'room{s.next_room}'
                    s.next_room += 1
                    s.waiting[key] = (code, user['id'])
            host = headers.get('host', '127.0.0.1')
            return 200, {'url': f'ws://{host}', 'ticket': 'tk-' + user['id'], 'room': code, 'queue': q.get('queue'),
                         'player': {'id': user['id']}, 'rank': 'Unranked'}
        return 404, {'error': 'Not found'}

    def websocket(self, sock, path, query, headers):
        s = STATE
        key = headers.get('sec-websocket-key', '')
        accept = base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()
        sock.sendall(('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
                      f'Sec-WebSocket-Accept: {accept}\r\n\r\n').encode())
        # The client finishes its upgrade before the first frame arrives (WinHTTP refuses an upgrade
        # whose response already carries frame bytes).
        time.sleep(0.25)
        code = path.rsplit('/', 1)[-1]
        ticket = query.get('ticket', '')
        conn = Conn(sock, ticket.replace('tk-', 'c-'))
        with s.lock:
            room = s.rooms.setdefault(code, {'conns': []})
            room['conns'] = [c for c in room['conns'] if c.id != conn.id] + [conn]
            conns = list(room['conns'])
        s.log(route='room-open', room=code, conn=conn.id, seats=len(conns))

        def roster(cs):
            return [{'id': c.id, 'connectionId': c.id, 'slot': i, 'localIndex': 0, 'user': c.id, 'hn': True, 'name': c.id}
                    for i, c in enumerate(cs)]
        conn.send({'t': 'welcome', 'me': conn.id, 'room': {'code': code}, 'participants': roster(conns), 'revision': len(conns)})
        for other in conns:
            if other is not conn:
                other.send({'t': 'join', 'participants': roster(conns), 'revision': len(conns)})
        sock.settimeout(None)
        try:
            while True:
                op, data = read_frame(sock)
                if op == 8:
                    break
                if op == 9:
                    continue
                if op != 1:
                    continue
                m = json.loads(data.decode('utf-8', 'replace'))
                t = m.get('t')
                if t == 'msg':
                    with s.lock:
                        targets = [c for c in s.rooms[code]['conns'] if c.id == m.get('to')]
                    for c in targets:
                        c.send({'t': 'msg', 'from': conn.id, 'd': m.get('d')})
                    if (m.get('d') or {}).get('kind') == 'mu-cand':
                        s.log(route='room-cand', room=code, conn=conn.id, compat=m['d'].get('compat'))
                elif t != 'ping':
                    s.log(route='room-msg', room=code, conn=conn.id, t=t)
        except (ConnectionError, OSError, ValueError):
            pass
        s.log(route='room-close', room=code, conn=conn.id)


class Server(socketserver.ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = True


def main():
    global STATE
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--port', type=int, default=0)
    ap.add_argument('--log', default='')
    ap.add_argument('--portfile', default='')
    ap.add_argument('--allowed', nargs='*', help='VERSION:EXE_SHA256:GAME_SHA256 entries for /api/native/builds')
    a = ap.parse_args()
    STATE = State(a.log)
    if a.allowed is not None:
        STATE.allowed = [dict(zip(('version', 'exe_sha256', 'game_sha256'), x.split(':'))) for x in a.allowed]
    srv = Server(('127.0.0.1', a.port), Handler)
    if a.portfile:
        with open(a.portfile, 'w') as f:
            f.write(str(srv.server_address[1]))
    srv.serve_forever()


if __name__ == '__main__':
    main()
