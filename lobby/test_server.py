import concurrent.futures
import json
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
from http.server import ThreadingHTTPServer
from pathlib import Path

from server import Error, Lobby, handler


class LobbyTests(unittest.TestCase):
    def setUp(self):
        self.now = 100
        self.lobby = Lobby(lambda: self.now)
        self.a = self.join('FOX#1')
        self.b = self.join('FOX#2')

    def tearDown(self):
        self.lobby.db.close()

    def profile(self, code):
        return dict(name=code, code=code, location='Phoenix, AZ', mains=[2, 20, 9], build='0.7.1:recomp')

    def join(self, code):
        return self.lobby.call('join', self.profile(code))

    def call(self, who, action, **body):
        return self.lobby.call(action, body, who['token'])

    def request(self):
        return self.call(self.a, 'request', target=self.b['id'])['id']

    def test_accept_only_recipient_and_both_observe_same_match(self):
        rid = self.request()
        with self.assertRaises(Error):
            self.call(self.a, 'accept', request=rid)
        self.call(self.b, 'accept', request=rid)
        self.call(self.a, 'presence', status='Online')
        for player in (self.a, self.b):
            state = self.call(player, 'poll')
            self.assertEqual(state['requests'][0]['state'], 'accepted')
            self.assertEqual(state['requests'][0]['transport'], 'slippi-direct')
            self.assertTrue(state['players'][0]['busy'])

    def test_cancel_decline_expiry_and_duplicate(self):
        rid = self.request()
        with self.assertRaises(Error):
            self.request()
        self.call(self.b, 'cancel', request=rid)
        with self.assertRaises(Error):
            self.call(self.b, 'accept', request=rid)
        self.now += 4
        rid = self.request()
        self.now += 31
        self.call(self.a, 'presence')
        self.call(self.b, 'presence')
        with self.assertRaises(Error):
            self.call(self.b, 'accept', request=rid)

    def test_setup_required_and_reconnect_is_not_public(self):
        self.lobby.players[self.a['token']]['ready'] = False
        with self.assertRaises(Error):
            self.request()
        self.now += 21
        sync = self.call(self.a, 'sync')
        self.assertFalse(sync['self']['visible'])
        self.assertEqual(sync['players'], [])

    def test_leave_cancels_and_hides_but_presence_continues(self):
        self.request()
        self.call(self.a, 'leave')
        state = self.call(self.b, 'poll')
        self.assertEqual(state['requests'], [])
        self.assertEqual(state['players'], [])
        self.assertIn(self.a['token'], self.lobby.players)

    def test_crash_expires_presence(self):
        self.request()
        self.now += 21
        self.call(self.b, 'presence')
        self.assertEqual(self.call(self.b, 'poll')['players'], [])
        self.assertEqual(self.call(self.b, 'poll')['requests'], [])

    def test_accept_cancel_race_is_atomic(self):
        rid = self.request()
        def attempt(who, action):
            try:
                self.call(who, action, request=rid)
                return True
            except Error:
                return False
        with concurrent.futures.ThreadPoolExecutor(2) as pool:
            results = list(pool.map(lambda pair: attempt(*pair), [(self.a, 'cancel'), (self.b, 'accept')]))
        self.assertEqual(sum(results), 1)

    def test_build_and_busy_guards(self):
        self.lobby.players[self.b['token']]['build'] = '0.7.1:source'
        with self.assertRaises(Error):
            self.request()
        self.lobby.players[self.b['token']]['build'] = '0.7.1:recomp'
        self.call(self.b, 'presence', status='In match', stocks=[3])
        with self.assertRaises(Error):
            self.request()

    def test_friend_consent_and_live_stocks_outside_lobby(self):
        self.call(self.a, 'friend', target=self.b['id'])
        self.assertEqual(self.call(self.a, 'poll')['friends'], [])
        self.assertEqual(self.call(self.b, 'poll')['friend_requests'][0]['id'], self.a['id'])
        with self.assertRaises(Error):
            self.call(self.a, 'friend_accept', target=self.b['id'])
        self.call(self.b, 'friend_accept', target=self.a['id'])
        self.call(self.a, 'leave')
        self.call(self.a, 'presence', status='In match', stocks=[2])
        friend = self.call(self.b, 'poll')['friends'][0]
        self.assertEqual((friend['status'], friend['stocks']), ('In match', [2]))
        self.assertEqual(self.call(self.b, 'poll')['players'], [])
        self.call(self.a, 'offline')
        self.assertEqual(self.call(self.b, 'poll')['friends'][0]['status'], 'Offline')
        self.call(self.a, 'presence')
        self.assertEqual(self.call(self.b, 'poll')['friends'][0]['status'], 'Online')

    def test_unfriend_and_decline(self):
        self.call(self.a, 'friend', target=self.b['id'])
        self.call(self.b, 'friend_decline', target=self.a['id'])
        self.assertEqual(self.call(self.b, 'poll')['friend_requests'], [])
        self.call(self.a, 'friend', target=self.b['id'])
        self.call(self.b, 'friend_accept', target=self.a['id'])
        self.call(self.a, 'unfriend', target=self.b['id'])
        self.assertEqual(self.call(self.b, 'poll')['friends'], [])

    def test_friendship_persists_after_service_restart(self):
        with tempfile.TemporaryDirectory() as directory:
            database = str(Path(directory) / 'social.sqlite3')
            first = Lobby(database=database)
            a = first.call('join', self.profile('A#1'))
            b = first.call('join', self.profile('B#1'))
            first.call('friend', {'target': b['id']}, a['token'])
            first.call('friend_accept', {'target': a['id']}, b['token'])
            first.db.close()
            second = Lobby(database=database)
            second.call('presence', {}, a['token'])
            self.assertEqual(second.call('poll', {}, a['token'])['friends'][0]['status'], 'Offline')
            second.db.close()

    def test_chat_limits_and_no_chat_outside_lobby(self):
        self.call(self.a, 'chat', text='<script>literal text</script>')
        self.assertEqual(self.call(self.b, 'poll')['messages'][0]['name'], 'FOX#1')
        with self.assertRaises(Error):
            self.call(self.a, 'chat', text='spam')
        self.now += 2
        with self.assertRaises(Error):
            self.call(self.a, 'chat', text='x' * 301)
        self.call(self.a, 'leave')
        self.assertEqual(self.call(self.a, 'poll')['messages'], [])
        with self.assertRaises(Error):
            self.call(self.a, 'chat', text='hidden')

    def test_udp_registration_is_separate_from_account_authentication(self):
        self.lobby.register_udp(self.a['token'].encode(), ('1.2.3.4', 4567))
        self.assertNotIn('endpoint', self.call(self.b, 'poll')['players'][0])
        self.lobby.register_udp(self.a['udp_token'].encode(), ('1.2.3.4', 4567))
        public = self.call(self.b, 'poll')['players'][0]
        self.assertEqual(public['endpoint'], ['1.2.3.4', 4567])
        self.assertNotIn('udp_token', public)
        self.assertNotIn('token', public)
        self.now += 16
        self.assertNotIn('endpoint', self.call(self.b, 'poll')['players'][0])

    def test_profile_validation_and_no_impersonating_existing_code(self):
        for change in ({'mains': [2, 2, 2]}, {'code': 'X#1" --evil'}, {'location': ''}, {'name': 'bad\nname'}):
            with self.assertRaises(Error):
                self.lobby.call('join', dict(self.profile('X#3'), **change))
        with self.assertRaises(Error):
            self.join('FOX#1')

    def test_authentication_and_third_party_request_access(self):
        with self.assertRaises(Error):
            self.lobby.call('poll', {}, 'invalid')
        rid = self.request()
        third = self.join('FOX#3')
        for action in ('accept', 'cancel'):
            with self.assertRaises(Error):
                self.call(third, action, request=rid)

    def test_http_transport(self):
        server = ThreadingHTTPServer(('127.0.0.1', 0), handler(self.lobby))
        thread = threading.Thread(target=server.serve_forever)
        thread.start()
        try:
            url = f'http://127.0.0.1:{server.server_port}/v1/poll'
            request = urllib.request.Request(url, b'{}', {'Authorization': 'Bearer ' + self.a['token']})
            with urllib.request.urlopen(request) as response:
                self.assertEqual(json.load(response)['players'][0]['code'], 'FOX#2')
            with self.assertRaises(urllib.error.HTTPError) as error:
                urllib.request.urlopen(urllib.request.Request(url, b'{}'))
            self.assertEqual(error.exception.code, 401)
        finally:
            server.shutdown()
            thread.join()
            server.server_close()


if __name__ == '__main__':
    unittest.main()
