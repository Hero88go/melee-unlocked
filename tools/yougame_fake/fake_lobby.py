"""The Friends lobby (yg.mu.lobby v1) as the game reads it, for fake_site.py: a room whose ticket carried a
friends-lobby-1 compatibility key keeps one of these. It is a stand-in written from the game's side of the
codec (port/runtime/hle/yougame_lobby_wire.h, yougame_lobby.h, yougame_lobby_setup.h), not YouGame's code:
states, refusals, the frozen setup and the finish, enough for hidden runs of the lobby on this PC.

Six seats (Triples): the room hold and the guest's ticket ask for lobby.seats 6. How this stand-in answers is
fake_site.py --lobby-seats:
  six     a six-seat room: its state has six seats (what the request to YouGame asks for)
  four    the key is left alone: four seats, as today's YouGame
  refuse  a ticket carrying the key is refused (the game asks again without it)
SPDX-License-Identifier: GPL-2.0-or-later
"""
import json
import random

T, V = 'yg.mu.lobby', 1
RANDOM_STAGES = [2, 3, 8, 28, 31, 32]
CHAR_MAX, COLOR_MAX, TEAM_MAX = 25, 5, 2


def stage_ok(s):
    return isinstance(s, int) and not isinstance(s, bool) and 2 <= s <= 32 and s not in (21, 26)


def is_int(v, lo, hi):
    return isinstance(v, int) and not isinstance(v, bool) and lo <= v <= hi


def fresh_rules():
    return {'teams': False, 'friendlyFire': False, 'mode': 'stock', 'stocks': 4, 'minutes': 8, 'pause': False}


def rules_ok(r):
    if not isinstance(r, dict) or set(r) - {'teams', 'friendlyFire', 'mode', 'stocks', 'minutes', 'pause', 'squad'}:
        return False
    if not all(isinstance(r.get(k), bool) for k in ('teams', 'friendlyFire', 'pause')):
        return False
    if r.get('mode') not in ('stock', 'time') or not is_int(r.get('stocks'), 1, 99):
        return False
    if not is_int(r.get('minutes'), 1 if r['mode'] == 'time' else 0, 99):
        return False
    return not r.get('squad')   # this stand-in seats no Squad Strike


class Lobby:
    def __init__(self, seats, log):
        self.size = seats
        self.log = log
        self.rev = 0
        self.seats = [{'seat': i, 'kind': 'open'} for i in range(seats)]
        self.rules = fresh_rules()
        self.host = None          # a seat
        self.setup = None         # the frozen setup during a match
        self.match_id = None
        self.matches = 0
        self.stage_select = False
        self.hellos = {}          # connection id -> (settingsDigest, cardDigest)
        self.finishes = []        # every finish received: (connection id, frame)

    # ---- views ----
    def state(self, **extra):
        seats = []
        any_hello = bool(self.hellos)
        for s in self.seats:
            e = {'seat': s['seat'], 'kind': s['kind']}
            if s['kind'] == 'human':
                e['id'] = s['id']; e['name'] = s['name']
                if any_hello: e['ready'] = s['id'] in self.hellos
            if s['kind'] == 'cpu':
                e['level'] = s['level']
            if s['kind'] != 'open':
                if 'pick' in s: e['pick'] = dict(s['pick'])
                if 'hover' in s: e['hover'] = dict(s['hover'])
            seats.append(e)
        m = {'t': T, 'v': V, 'op': 'state', 'rev': self.rev, 'host': self.host, 'seats': seats,
             'rules': dict(self.rules), 'setup': self.setup, 'stageSelect': self.stage_select}
        m.update(extra)
        return m

    def seat_of(self, cid):
        for s in self.seats:
            if s['kind'] == 'human' and s['id'] == cid: return s['seat']
        return -1

    # ---- the room ----
    def join(self, cid, name):
        if self.seat_of(cid) >= 0: return True
        for i, s in enumerate(self.seats):
            if s['kind'] == 'open':
                self.seats[i] = {'seat': i, 'kind': 'human', 'id': cid, 'name': name}
                break
        else:
            return False
        if self.host is None: self.host = self.seat_of(cid)
        self.rev += 1
        return True

    def leave(self, cid):
        seat = self.seat_of(cid)
        if seat < 0: return
        self.seats[seat] = {'seat': seat, 'kind': 'open'}
        self.hellos.pop(cid, None)
        if self.host == seat:
            humans = [s['seat'] for s in self.seats if s['kind'] == 'human']
            self.host = humans[0] if humans else None
        self.rev += 1

    # ---- a frame from cid: (replies to cid, broadcast or None) ----
    def frame(self, cid, m):
        op = m.get('op')
        seat = self.seat_of(cid)

        def refuse(error):
            self.log(route='lobby-refuse', conn=cid, op=op, error=error)
            return [self.state(error=error, action=op)], None

        if op == 'hello':
            if not isinstance(m.get('settingsDigest'), str) or not isinstance(m.get('cardDigest'), str):
                return refuse('shape')
            if set(m) - {'t', 'v', 'op', 'settingsDigest', 'cardDigest'}: return refuse('shape')
            self.hellos[cid] = (m['settingsDigest'], m['cardDigest'])
            return [], self.state()
        if op == 'leave':
            self.leave(cid)
            return [], self.state()
        if seat < 0: return refuse('not_seated')
        if self.setup is not None and op not in ('hover',): return refuse('match_active')
        shared = op in ('rules', 'seat', 'start', 'stageSelect') or (op in ('pick', 'unpick') and 'rev' in m)
        if shared and seat != self.host: return refuse('host_only')
        if shared and m.get('rev') != self.rev: return refuse('revision')
        if op == 'hover' or op == 'pick':
            target = m.get('seat')
            if not is_int(target, 0, self.size - 1): return refuse('shape')
            if not (is_int(m.get('char'), 0, CHAR_MAX) and is_int(m.get('color'), 0, COLOR_MAX)): return refuse('shape')
            team = m.get('team', 0)
            if not is_int(team, 0, TEAM_MAX): return refuse('shape')
            s = self.seats[target]
            cpu = 'rev' in m
            if (cpu and s['kind'] != 'cpu') or (not cpu and target != seat): return refuse('seat_owner')
            if op == 'hover' and self.setup is not None: return [], None
            s[op] = {'char': m['char'], 'color': m['color'], 'team': team}
            if cpu: self.rev += 1
            return [], self.state()
        if op == 'unpick':
            target = m.get('seat')
            if not is_int(target, 0, self.size - 1): return refuse('shape')
            s = self.seats[target]
            cpu = 'rev' in m
            if (cpu and s['kind'] != 'cpu') or (not cpu and target != seat): return refuse('seat_owner')
            s.pop('pick', None)
            if cpu: self.rev += 1
            return [], self.state()
        if op == 'rules':
            if not rules_ok(m.get('rules')): return refuse('shape')
            self.rules = dict(m['rules']); self.rules.pop('squad', None)
            self.rev += 1
            return [], self.state()
        if op == 'seat':
            target, kind = m.get('seat'), m.get('kind')
            if not is_int(target, 0, self.size - 1): return refuse('shape')
            s = self.seats[target]
            if s['kind'] == 'human': return refuse('seat_occupied')
            if kind == 'cpu':
                if not is_int(m.get('level'), 1, 9): return refuse('shape')
                level = m['level']
                self.seats[target] = {'seat': target, 'kind': 'cpu', 'level': level, **({'pick': s['pick']} if s.get('kind') == 'cpu' and 'pick' in s else {})}
            elif kind == 'open':
                self.seats[target] = {'seat': target, 'kind': 'open'}
            else:
                return refuse('shape')
            self.rev += 1
            return [], self.state()
        if op == 'stageSelect':
            if not isinstance(m.get('open'), bool): return refuse('shape')
            self.stage_select = m['open']
            self.rev += 1
            return [], self.state()
        if op == 'start':
            filled = [s for s in self.seats if s['kind'] != 'open']
            humans = [s for s in filled if s['kind'] == 'human']
            if any('pick' not in s for s in filled): return refuse('pick_required')
            if len(filled) < 2: return refuse('players_required')
            if len(humans) < 2: return refuse('humans_required')
            if self.hellos and any(s['id'] not in self.hellos for s in humans): return refuse('digest_missing')
            if self.rules['teams'] and len({s['pick']['team'] for s in filled}) < 2: return refuse('teams_required')
            stage = m.get('stage')
            if stage == 'random': stage = random.choice(RANDOM_STAGES)
            if not stage_ok(stage): return refuse('shape')
            self.matches += 1
            self.match_id = f'lobby-{random.randrange(1 << 30):08x}-{self.matches}'
            order = humans + [s for s in filled if s['kind'] == 'cpu']
            seats = []
            for port, s in enumerate(order):
                p = s['pick']
                e = {'seat': s['seat'], 'port': port, 'kind': s['kind'], 'char': p['char'], 'color': p['color'],
                     'team': p['team'] if self.rules['teams'] else 0}
                if s['kind'] == 'human':
                    e['id'] = s['id']; e['name'] = s['name']; e['connectionId'] = s['id']
                    if s['id'] in self.hellos:
                        e['settingsDigest'], e['cardDigest'] = self.hellos[s['id']]
                else:
                    e['id'] = f"cpu:{s['seat']}"; e['name'] = f"CPU {s['seat'] + 1}"; e['level'] = s['level']
                seats.append(e)
            self.setup = {'seed': random.randrange(1 << 32), 'seats': seats, 'rules': dict(self.rules), 'stage': stage}
            if is_int(m.get('inputDelay'), 1, 9): self.setup['inputDelay'] = m['inputDelay']
            self.stage_select = False
            self.rev += 1
            self.log(route='lobby-start', match=self.match_id, seats=len(seats), humans=len(humans), stage=stage,
                     teams=self.rules['teams'])
            setup = {'t': T, 'v': V, 'op': 'setup', 'matchId': self.match_id, 'rev': self.rev, **self.setup}
            return [], [setup, self.state()]
        return refuse('shape')

    # ---- room-level frames ----
    def finish(self, cid, m):
        """A machine's finish. The first one for the running match settles it: the result goes out, then the
        select (a state with no setup)."""
        self.finishes.append((cid, m))
        self.log(route='lobby-finish', conn=cid, match=m.get('matchId'), ranking=m.get('ranking'), void=m.get('void', False),
                 draw=m.get('draw', False), setup_echo=m.get('setup') == self.setup)
        if m.get('matchId') != self.match_id or self.setup is None: return None
        result = {'t': 'result', 'matchId': self.match_id, 'ranking': m.get('ranking'), 'void': m.get('void', False)}
        self.setup = None
        self.match_id = None
        self.rev += 1
        return [result, self.state()]

    def end_session(self, cid, m):
        if self.setup is None or m.get('sessionId') != self.match_id: return None
        self.log(route='lobby-end-session', conn=cid, match=self.match_id)
        ended = {'t': 'sessionEnded', 'sessionId': self.match_id, 'reason': 'host_ended'}
        self.setup = None
        self.match_id = None
        self.rev += 1
        return [ended, self.state()]
