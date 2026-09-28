"""Melee Unlocked lobby rendezvous. No Slippi credentials or gameplay traffic.

Run behind HTTPS for Internet use; HTTP is for loopback development only.
UDP on the same port registers observed peer endpoints for direct RTT probes.
"""
import argparse
import json
import re
import secrets
import socket
import sqlite3
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

CHARACTERS = ['Captain Falcon', 'Donkey Kong', 'Fox', 'Mr. Game & Watch', 'Kirby',
              'Bowser', 'Link', 'Luigi', 'Mario', 'Marth', 'Mewtwo', 'Ness', 'Peach',
              'Pikachu', 'Ice Climbers', 'Jigglypuff', 'Samus', 'Yoshi', 'Zelda',
              'Sheik', 'Falco', 'Young Link', 'Dr. Mario', 'Roy', 'Pichu', 'Ganondorf']


class Error(Exception):
    def __init__(self, status, message):
        self.status, self.message = status, message


class Lobby:
    def __init__(self, clock=time.monotonic, database=':memory:'):
        self.clock = clock
        self.lock = threading.RLock()
        self.players = {}
        self.requests = {}
        self.joins = {}
        self.messages = []
        self.db = sqlite3.connect(database, check_same_thread=False)
        self.db.execute('CREATE TABLE IF NOT EXISTS social (id INTEGER PRIMARY KEY, data TEXT NOT NULL)')
        row = self.db.execute('SELECT data FROM social WHERE id=1').fetchone()
        self.accounts = json.loads(row[0]) if row else {}

    def save(self):
        self.db.execute('INSERT OR REPLACE INTO social VALUES (1, ?)', (json.dumps(self.accounts),))
        self.db.commit()

    def sweep(self):
        now = self.clock()
        for token, player in list(self.players.items()):
            if now - player['seen'] > 20:
                del self.players[token]
        ids = {p['id'] for p in self.players.values()}
        for rid, req in list(self.requests.items()):
            if req['expires'] < now or req['from'] not in ids or req['to'] not in ids:
                del self.requests[rid]
        self.joins = {ip: times for ip, times in self.joins.items()
                      if times and now - times[-1] < 60}

    def player(self, token):
        if token not in self.players:
            raise Error(401, 'Session expired. Join the lobby again.')
        return self.players[token]

    def public(self, p):
        result = {k: p[k] for k in ('id', 'name', 'code', 'location', 'mains', 'build', 'busy', 'probe', 'status', 'stocks', 'ready')}
        if self.clock() - p.get('endpoint_seen', -100) < 15:
            result['endpoint'] = p['endpoint']
        return result

    def call(self, action, body, token='', ip='127.0.0.1'):
        with self.lock:
            self.sweep()
            now = self.clock()
            if action == 'sync':
                identity = self.call('presence', body, token, ip)
                state = self.call('poll', {}, token, ip)
                state['self'] = identity
                return state
            if action == 'presence':
                if token not in self.accounts:
                    raise Error(401, 'Create your lobby profile first.')
                if token not in self.players:
                    self.players[token] = dict(self.accounts[token], seen=now, busy=False, visible=False,
                                               probe=secrets.token_hex(16), last_request=-100,
                                               udp_token=secrets.token_hex(32),
                                               status='Online', stocks=[], ready=False)
                p = self.players[token]
                p['seen'] = now
                status = body.get('status', 'Online')
                if status not in ('Online', 'Launching', 'In game', 'In match'):
                    raise Error(400, 'Invalid status')
                stocks = body.get('stocks', [])
                if not isinstance(stocks, list) or len(stocks) > 4 or any(type(s) is not int or not 0 <= s <= 99 for s in stocks):
                    raise Error(400, 'Invalid stocks')
                p['status'], p['stocks'] = status, stocks if status == 'In match' else []
                p['busy'] = status != 'Online' or any(r['state'] == 'accepted' and p['id'] in (r['from'], r['to']) for r in self.requests.values())
                return dict(id=p['id'], probe=p['probe'], udp_token=p['udp_token'], visible=p['visible'])
            if action == 'join':
                times = [t for t in self.joins.get(ip, []) if now - t < 60]
                if len(times) >= 10 or len(self.players) >= 256:
                    raise Error(429, 'Lobby is busy. Try again shortly.')
                times.append(now)
                self.joins[ip] = times
                def field(key, length):
                    value = body.get(key)
                    if not isinstance(value, str) or not 1 <= len(value.strip()) <= length or any(ord(c) < 32 for c in value):
                        raise Error(400, 'Invalid ' + key)
                    return value.strip()
                code = field('code', 10).upper()
                if not re.fullmatch(r'[A-Z0-9]{1,4}#[0-9]{1,5}', code):
                    raise Error(400, 'Use your Slippi connect code, for example FOX#123.')
                mains = body.get('mains')
                if not isinstance(mains, list) or not 1 <= len(mains) <= 3 or any(type(c) is not int or not 0 <= c < 26 for c in mains) or len(set(mains)) != len(mains):
                    raise Error(400, 'Choose three different main characters.')
                if any(p['code'] == code for key, p in self.accounts.items() if key != token):
                    raise Error(409, 'That connect code is already in the lobby.')
                old = self.accounts.get(token, {})
                profile = dict(id=old.get('id', secrets.token_hex(12)), probe=secrets.token_hex(16),
                               name=field('name', 32), code=code, location=field('location', 48),
                               mains=mains, build=field('build', 80), busy=False, seen=now, last_request=-100,
                               visible=True, status='Online', stocks=[], ready=body.get('ready', True) is True)
                profile['udp_token'] = secrets.token_hex(32)
                token = token if old else secrets.token_hex(32)
                self.accounts[token] = {k: profile[k] for k in ('id', 'name', 'code', 'location', 'mains', 'build')}
                self.accounts[token].update(friends=old.get('friends', []), incoming=old.get('incoming', []))
                self.save()
                self.players[token] = profile
                return dict(token=token, id=profile['id'], probe=profile['probe'], udp_token=profile['udp_token'])
            p = self.player(token)
            p['seen'] = now
            if action == 'leave':
                p['visible'] = False
                for rid, r in list(self.requests.items()):
                    if p['id'] in (r['from'], r['to']) and r['state'] == 'pending':
                        del self.requests[rid]
                return {}
            if action == 'offline':
                del self.players[token]
                self.sweep()
                return {}
            if action == 'poll':
                account = self.accounts[token]
                def friend_view(a):
                    live = next((q for q in self.players.values() if q['id'] == a['id']), None)
                    return dict(id=a['id'], name=a['name'], status=live['status'] if live else 'Offline',
                                stocks=live['stocks'] if live else [])
                return dict(players=[self.public(q) for q in self.players.values() if q is not p and q['visible']] if p['visible'] else [],
                            friends=[friend_view(a) for a in self.accounts.values() if a['id'] in account['friends']],
                            friend_requests=[friend_view(a) for a in self.accounts.values() if a['id'] in account['incoming']],
                            messages=[m for m in self.messages if now - m['time'] < 3600] if p['visible'] else [],
                            requests=[dict(r, remaining=max(0, int(r['expires'] - now)))
                                      for r in self.requests.values() if p['id'] in (r['from'], r['to'])])
            if action in ('friend', 'friend_accept', 'friend_decline', 'unfriend'):
                other = next((a for a in self.accounts.values() if a['id'] == body.get('target')), None)
                me = self.accounts[token]
                if not other or other['id'] == p['id']:
                    raise Error(404, 'Player not found')
                if action == 'friend':
                    if other['id'] in me['friends']:
                        return {}
                    if len(other['incoming']) >= 100 or len(me['friends']) >= 100:
                        raise Error(429, 'Friend list is full')
                    if p['id'] not in other['incoming']:
                        other['incoming'].append(p['id'])
                elif action in ('friend_accept', 'friend_decline'):
                    if other['id'] not in me['incoming']:
                        raise Error(404, 'Friend request no longer exists')
                    me['incoming'].remove(other['id'])
                    if action == 'friend_accept':
                        if other['id'] not in me['friends']:
                            me['friends'].append(other['id'])
                        if p['id'] not in other['friends']:
                            other['friends'].append(p['id'])
                        if p['id'] in other['incoming']:
                            other['incoming'].remove(p['id'])
                else:
                    me['friends'] = [i for i in me['friends'] if i != other['id']]
                    other['friends'] = [i for i in other['friends'] if i != p['id']]
                self.save()
                return {}
            if action == 'chat':
                message = body.get('text', '')
                if not p['visible'] or not isinstance(message, str) or not 1 <= len(message.strip()) <= 300 or any(ord(c) < 32 for c in message):
                    raise Error(400, 'Join the lobby and enter a message (up to 300 characters).')
                if now - p.get('last_chat', -100) < 1:
                    raise Error(429, 'Please slow down.')
                p['last_chat'] = now
                self.messages.append(dict(id=secrets.token_hex(8), sender=p['id'], name=p['name'], text=message.strip(), time=now))
                self.messages = self.messages[-100:]
                return {}
            if action == 'available':
                p['busy'] = False
                for rid, r in list(self.requests.items()):
                    if p['id'] in (r['from'], r['to']):
                        del self.requests[rid]
                return {}
            if action == 'request':
                target = next((q for q in self.players.values() if q['id'] == body.get('target')), None)
                if target is None or target is p or not p['visible'] or not target['visible']:
                    raise Error(404, 'Player left the lobby.')
                if p['busy'] or target['busy']:
                    raise Error(409, 'Player is already in a match.')
                if not p['ready'] or not target['ready']:
                    raise Error(409, 'A player needs to finish game setup and rejoin before playing.')
                if p['build'] != target['build']:
                    raise Error(409, 'Choose the same game build and version before playing.')
                if any(p['id'] in (r['from'], r['to']) or target['id'] in (r['from'], r['to']) for r in self.requests.values()):
                    raise Error(409, 'A player already has a pending request.')
                if now - p['last_request'] < 3:
                    raise Error(429, 'Please wait before sending another request.')
                p['last_request'] = now
                rid = secrets.token_hex(16)
                self.requests[rid] = dict(id=rid, **{'from': p['id']}, to=target['id'],
                                         state='pending', expires=now + 30, transport='slippi-direct')
                return {'id': rid}
            rid = body.get('request')
            r = self.requests.get(rid)
            if not r or p['id'] not in (r['from'], r['to']):
                raise Error(404, 'Request expired or cancelled.')
            if action == 'cancel':
                if r['state'] != 'pending':
                    raise Error(409, 'This match has already been accepted.')
                del self.requests[rid]
                return {}
            if action == 'accept':
                if p['id'] != r['to'] or r['state'] != 'pending':
                    raise Error(409, 'Only the invited player can accept a pending request.')
                peer = next(q for q in self.players.values() if q['id'] == r['from'])
                if p['busy'] or peer['busy']:
                    raise Error(409, 'Player is no longer available.')
                p['busy'] = peer['busy'] = True
                r['state'], r['expires'] = 'accepted', now + 120
                return {}
            raise Error(404, 'Unknown action')

    def register_udp(self, payload, address):
        # Only possession of the session secret registers an endpoint; never accept an IP in JSON.
        try:
            token = payload.decode('ascii')
        except UnicodeDecodeError:
            return
        with self.lock:
            p = next((p for p in self.players.values() if p['udp_token'] == token), None)
            if p and self.clock() - p['seen'] <= 20:
                p['endpoint'] = [address[0], address[1]]
                p['endpoint_seen'] = self.clock()


def handler(lobby):
    class Handler(BaseHTTPRequestHandler):
        def do_POST(self):
            try:
                size = int(self.headers.get('Content-Length', '0'))
                if not 0 < size <= 4096:
                    raise Error(413, 'Invalid request size')
                self.connection.settimeout(5)
                body = json.loads(self.rfile.read(size))
                if not isinstance(body, dict):
                    raise Error(400, 'Expected an object')
                result = lobby.call(self.path.removeprefix('/v1/'), body,
                                    self.headers.get('Authorization', '').removeprefix('Bearer '),
                                    self.client_address[0])
                status = 200
            except Error as exc:
                status, result = exc.status, {'error': exc.message}
            except (ValueError, TypeError, TimeoutError):
                status, result = 400, {'error': 'Invalid request'}
            data = json.dumps(result).encode()
            self.send_response(status)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            try:
                self.wfile.write(data)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def log_message(self, *_):
            pass  # Do not log profile data or session tokens.
    return Handler


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=8787)
    parser.add_argument('--database', default='lobby.sqlite3')
    args = parser.parse_args()
    lobby = Lobby(database=args.database)
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.bind((args.host, args.port))
    def receive():
        while True:
            data, address = udp.recvfrom(1024)
            lobby.register_udp(data, address)
    threading.Thread(target=receive, daemon=True).start()
    print(f'Lobby HTTP and UDP: {args.host}:{args.port}', flush=True)
    ThreadingHTTPServer((args.host, args.port), handler(lobby)).serve_forever()


if __name__ == '__main__':
    main()
