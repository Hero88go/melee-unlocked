"""A YouGame row pressed while the launcher is not signed in to YouGame opens the launcher's own Connect window
instead of a dead end: the menu asks the host (EF 0x22), the host asks the launcher's bridge (muOpenConnect), and
the launcher starts Connect on its window thread (or brings its sign-in window to the front). The menu then waits
on the row and goes on into it once the rows open."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


def source(name):
    return (ROOT / name).read_text(encoding="utf-8-sig")


def body(text, marker):
    start = text.index("{", text.index(marker))
    depth, end = 1, start + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start + 1:end - 1]


class OpenConnectWiring(unittest.TestCase):
    def setUp(self):
        self.menu = source("sourceport/game/shim/mu_slippi_menu.c")
        self.online = source("port/runtime/hle/slippi_online.cpp")
        self.link = source("port/runtime/hle/yougame_link.cpp")
        self.header = source("port/runtime/hle/yougame_link.h")
        self.bridge = source("port/app/launcher_yougame.cpp")
        self.launcher = source("port/app/launcher.cpp")

    def test_menu_asks_for_connect_and_waits_on_the_row(self):
        start = self.menu.index("!(yg_flags & YG_ROWS_OPEN)) {")
        branch = self.menu[start:self.menu.index("} else if", start)]
        self.assertIn("yougame_query(0x22, NULL, 0) == 1", branch)
        asked = branch[branch.index("== 1"):branch.index("} else {")]
        # The same wait as a row picked before YouGame was connected: the menu goes on into the row by itself.
        self.assertIn("yg_wait_slot = mn_804A04F0.hovered_selection;", asked)
        self.assertIn("yg_wait_frames = 0;", asked)
        self.assertIn('pages_notice_show("Sign in to YouGame in the new window.");', asked)
        # No launcher to ask: the old refusal stays.
        self.assertIn('pages_notice_show("Connect YouGame in the launcher first.");', branch[branch.index("} else {"):])
        wait = body(self.menu, "if (yg_wait_slot >= 0) {")
        self.assertIn("if (flags & YG_ROWS_OPEN)", wait)
        self.assertIn("auto_slot = yg_wait_slot;", wait)

    def test_host_answers_0x22_from_the_launcher_ask(self):
        self.assertIn("payload[0] == 0x22) { s_yg_pick = 0; q.assign(1, yougame::open_launcher_connect() ? 1 : 0); return true; }",
                      self.online)
        # Answered before the catch-all pick (any other one byte is a row pick).
        self.assertLess(self.online.index("payload[0] == 0x22)"), self.online.index("yougame::set_menu_service(payload[0] <= 2"))
        self.assertIn("bool open_launcher_connect();", self.header)

    def test_game_asks_the_launchers_bridge_directly(self):
        ask = body(self.link, "bool open_launcher_connect() {")
        self.assertIn("(g_launcher || g_match_bridge)", ask)
        # Straight to the launcher's HTTP bridge, never the YouGame app's channel, and off the game's thread.
        self.assertIn('bridge_http({{"type", "muOpenConnect"}}', ask)
        self.assertNotIn("bridge_channel(", ask)
        self.assertIn("std::thread(", ask)
        self.assertIn("AllowSetForegroundWindow(ASFW_ANY);", ask)

    def test_bridge_handles_it_without_a_sign_in(self):
        handle = body(self.bridge, "BridgeReply handle(const HttpRequest& request")
        self.assertIn('if (type == "muOpenConnect")', handle)
        # Before the signed-out refusal of the match requests: it is the way to sign in.
        self.assertLess(handle.index('type == "muOpenConnect"'), handle.index('{"code", "signed_out"}'))
        self.assertIn("b.open_connect = []", self.bridge)

    def test_launcher_runs_connect_on_its_window_thread(self):
        self.assertIn("set_open_connect([] { PostMessageW(g_main, WM_APP_YOUGAME_OPEN_CONNECT, 0, 0); });", self.launcher)
        case = self.launcher[self.launcher.index("case WM_APP_YOUGAME_OPEN_CONNECT:"):]
        self.assertIn("yougame_open_connect_from_game();", case[:200])
        handler = body(self.launcher, "void yougame_open_connect_from_game() {")
        self.assertIn("launcher::yougame::web::raise_sign_in();", handler)   # already open: to the front
        self.assertIn("yougame_sign_in_thread(true);", handler)
        self.assertNotIn("Disconnect", handler)   # never the Connect button's Disconnect question


if __name__ == "__main__":
    unittest.main()
