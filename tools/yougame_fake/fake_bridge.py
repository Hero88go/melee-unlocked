"""A local stand-in for the bridge a game asks for YouGame (POST <bridge>/sdk {type, ...}), one per player, in
front of fake_site.py: the answers the Melee Unlocked launcher's bridge gives (port/app/launcher_yougame.cpp
handle), with the site calls made to the stand-in site over plain HTTP. The launcher's own bridge reaches the
site only through its signed-in browser view, so a hidden run on this PC talks to this one instead.

    py -3.12 tools/yougame_fake/fake_bridge.py --site http://127.0.0.1:PORT --players 6
Prints one line per player, "BRIDGE <url> <token> <name> <code>", then serves until stdin closes.
Answers: hello, mpCode, mpRank, mpRoom, mpTicket, mpFriend hold (the Friends lobby's room, "seats" passed on in
its lobby), matchState and gameState; anything else is refused as unsupported (the invite picker, directOpen).
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import http.server
import json
import secrets
import sys
import threading
import urllib.error
import urllib.request

ALPHABET = 'abcdefghjkmnpqrstuvwxyz23456789'


def site_call(site, method, path, body=None, token=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(site + path, data=data, method=method)
    req.add_header('Content-Type', 'application/json')
    if token: req.add_header('Authorization', 'Bearer ' + token)
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            return r.status, json.loads(r.read().decode() or '{}')
    except urllib.error.HTTPError as e:
        try: return e.code, json.loads(e.read().decode() or '{}')
        except ValueError: return e.code, {}


TICKET_KEYS = ('url', 'ticket', 'room', 'queue', 'rating', 'rank', 'ice', 'server', 'direct', 'player',
               'bestOf', 'winsRequired', 'rulesVersion', 'reason')


class Player:
    def __init__(self, site, email):
        self.site = site
        status, j = site_call(site, 'POST', '/auth/v1/token?grant_type=password', {'email': email, 'password': 'pw'})
        if status != 200: raise SystemExit(f'sign-in failed for {email}: {j}')
        self.access = j['access_token']
        self.id = j['user']['id']
        self.name = j['user']['user_metadata']['name']
        status, c = site_call(site, 'GET', '/api/multiplayer/code', token=self.access)
        self.code = c.get('code', '')
        self.token = secrets.token_hex(16)

    def ticket(self, body):
        status, j = site_call(self.site, 'POST', '/api/multiplayer/ticket', body, self.access)
        if status != 200 or 'error' in j:
            return {'ok': False, 'error': j.get('error', 'YouGame could not find a match'), 'code': j.get('code', 'refused')}
        return {'ok': True, 'result': {k: j[k] for k in TICKET_KEYS if k in j}}

    def answer(self, q):
        t = q.get('type')
        base = {'slug': 'melee-unlocked', 'player_id': self.id, 'name': self.name, 'anonymous': False, 'links': False}
        if t == 'hello':
            return {'ok': True, 'result': {'player': {'id': self.id, 'name': self.name}, 'nameShared': True}}
        if t == 'mpCode':
            return {'ok': True, 'result': {'code': self.code, 'account': self.id}}
        if t == 'mpRank':
            return {'ok': True, 'result': {'rank': 'Unranked', 'rating': 1500}}
        if t in ('mpRoom', 'matchState', 'gameState'):
            return {'ok': True, 'result': {}}
        if t == 'mpFriend' and q.get('hold') is True:
            players = q.get('players', 4)
            if players not in (2, 4) or not q.get('compatibility'):
                return {'ok': False, 'error': 'Bad request'}
            code = ''.join(secrets.choice(ALPHABET) for _ in range(12))
            lobby = {'version': 1, 'minPlayers': 2, 'maxLocalPlayers': 1, 'compatibility': q['compatibility']}
            if players == 4 and q.get('seats') == 6: lobby['seats'] = 6
            r = self.ticket(dict(base, players=players, mode='singles', queue='private', room=code, lobby=lobby))
            if r['ok'] and 'room' not in r['result']: r['result']['room'] = code
            return r
        if t == 'mpTicket':
            body = dict(base, players=q.get('players', 2), mode=q.get('mode', 'singles'),
                        queue='ranked' if q.get('ranked') else 'casual')
            if not q.get('ranked') and isinstance(q.get('room'), str) and 'queue' not in q: body['queue'] = 'private'
            for k in ('lobby', 'room', 'direct'):
                if k in q: body[k] = q[k]
            return self.ticket(body)
        return {'ok': False, 'error': 'Not supported by this stand-in', 'code': 'unsupported'}


def serve(player):
    class H(http.server.BaseHTTPRequestHandler):
        def log_message(self, *a): pass

        def do_GET(self):
            self.reply(200, {'ok': True, 'bridge': 'yougame', 'version': 1})

        def do_POST(self):
            n = int(self.headers.get('Content-Length', '0') or 0)
            body = self.rfile.read(n).decode('utf-8', 'replace')
            if self.path.split('?')[0] != '/sdk': return self.reply(404, {'ok': False, 'error': 'Not found'})
            if self.headers.get('Authorization', '') != 'Bearer ' + player.token:
                return self.reply(401, {'ok': False, 'error': 'Unauthorized'})
            try: q = json.loads(body)
            except ValueError: return self.reply(400, {'ok': False, 'error': 'Bad request'})
            self.reply(200, player.answer(q))

        def reply(self, status, obj):
            out = json.dumps(obj).encode()
            self.send_response(status)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(out)))
            self.end_headers()
            self.wfile.write(out)

    srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), H)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return f'http://127.0.0.1:{srv.server_address[1]}'


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--site', required=True)
    ap.add_argument('--players', type=int, default=2)
    a = ap.parse_args()
    for i in range(a.players):
        p = Player(a.site, f'user{i + 1}@example.test')
        print(f'BRIDGE {serve(p)} {p.token} {p.name} {p.code}', flush=True)
    sys.stdin.read()


if __name__ == '__main__':
    main()
