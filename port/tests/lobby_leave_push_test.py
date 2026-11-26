"""YouGame's Leave for a lobby the game shows: the app's lobby overlay sends {"push":{"cmd":"leave"}}
on the local channel (or stdio). The 4-door Friends select and the 2-seat Doubles home back out to Online
as their B does; a running match is left alone. The flag itself is unit tested in
port/tests/yougame_lobby_screen_test.cpp.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


def body(source, name):
    at = source.index(name)
    at = source.index('{', at)
    depth, end = 1, at + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[at:end]


class LobbyLeavePush(unittest.TestCase):
    def test_wire_flag(self):
        self.assertIn('#define MU_LOBBY_F_LEAVE 0x80', read('port/runtime/abi/mu_lobby_wire.h'))

    def test_the_leave_push_reaches_the_armed_lobby(self):
        link = read('port/runtime/hle/yougame_link.cpp')
        at = link.index('push.value("cmd",json()) == "leave"')
        self.assertIn('slippi::online::ask_friends_lobby_leave();', link[at:at + 400])

    def test_host_sets_and_clears_it(self):
        online = read('port/runtime/hle/slippi_online.cpp')
        ask = body(online, 'void ask_friends_lobby_leave()')
        self.assertIn('if (!g_friends_lobby.armed) return;', ask)
        self.assertIn('g_in_online_match', ask)   # a match keeps its lobby
        self.assertIn('flags.leave = g_lobby_leave_asked.load();', online)
        arm = body(online, 'void arm_friends_lobby(const std::string& room, yougame::lobby::Profile profile)')
        self.assertIn('g_lobby_leave_asked = false;', arm)
        self.assertIn('g_lobby_leave_asked = false;', body(online, 'void disarm_friends_lobby()'))
        at = online.index('if (op == MU_LOBBY_OP_LEAVE)')
        self.assertIn('g_lobby_leave_asked = false;', online[at:at + 600])

    def test_the_select_backs_out_as_b_does(self):
        inc = read('sourceport/game/include/mu_friends4_css.inc')
        frame = body(inc, 'static void mu_friends4_css_frame(void)')
        at = frame.index('MU_LOBBY_F_LEAVE')
        self.assertIn('mnCharSel_804D6CF6 = 2;', frame[at:at + 300])   # Melee's own B exit (sfxBack, scene end)
        self.assertLess(frame.index('v = mu_lobby_read();'), at)


if __name__ == '__main__':
    unittest.main()
