#!/usr/bin/env python3
"""Connect with no YouGame app signs in with YouGame's public sign-in by code (OAuth device flow): the game's
client id from /oauth/game-client, scope player, a Bearer token on the launcher's requests to YouGame, the
refresh token sealed in launcher.ini, and the code shown in the plugin's YouGame box (or the status line).
Source wiring only; no network, no app and no user profile access.

SPDX-License-Identifier: GPL-2.0-or-later
"""
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8-sig")


def body(text, signature):
    """The definition of `signature` (a declaration ending in ';' is skipped), up to its closing brace."""
    start = text.find(signature)
    while start >= 0:
        i, depth = text.index("(", start), 0
        while True:
            depth += {"(": 1, ")": -1}.get(text[i], 0)
            if depth == 0:
                break
            i += 1
        rest = text[i + 1:].lstrip()
        if rest.startswith("const"):
            rest = rest[5:].lstrip()
        if rest.startswith("{"):
            open_at = len(text) - len(rest)
            depth = 0
            for j in range(open_at, len(text)):
                depth += {"{": 1, "}": -1}.get(text[j], 0)
                if depth == 0:
                    return text[start:j + 1]
        start = text.find(signature, start + 1)
    raise AssertionError("no definition of " + signature)


class LauncherCodeSignIn(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.yg = read("port/app/launcher_yougame.cpp")
        cls.header = read("port/app/launcher_yougame.h")
        cls.launcher = read("port/app/launcher.cpp")
        cls.cmake = read("port/CMakeLists.txt")

    def test_client_id_comes_from_game_client_per_site(self):
        fetch = body(self.yg, "std::string client_id(")
        self.assertIn('"/oauth/game-client?game=" + kSlug', fetch)
        self.assertIn("g_clients[", fetch)
        self.assertIn('"yougame_client_" + which + "="', self.yg)   # cached in launcher.ini, one line per site
        # No client id is built in: the production id is never hardcoded.
        self.assertNotRegex(self.yg, r'client_id=880b2eeb')

    def test_device_flow_asks_for_player_scope(self):
        flow = body(self.yg, "CodeSignIn sign_in_code(")
        self.assertIn('"/oauth/device_authorization"', flow)
        self.assertIn('&scope=player&', flow)
        self.assertIn("urn:ietf:params:oauth:grant-type:device_code", flow)
        for answer in ('"authorization_pending"', '"slow_down"', '"access_denied"', '"expired_token"', '"invalid_grant"'):
            self.assertIn(answer, flow)
        self.assertIn("interval + 5", flow)
        self.assertIn("retry_after", flow)

    def test_requests_to_yougame_carry_the_bearer_token(self):
        site = body(self.yg, "Reply site(")
        self.assertIn('"Authorization: Bearer "', site)
        self.assertNotIn("Origin:", site)
        self.assertNotIn("Sec-Fetch-Site", site)
        for call in ('site("GET", "/api/multiplayer/code"', 'site("GET", std::string("/api/multiplayer/rank?slug=")',
                     'site("POST", "/api/multiplayer/ticket"'):
            self.assertIn(call, self.yg)
        self.assertNotRegex(self.yg, r"Cookie:")

    def test_refresh_token_is_sealed_and_saved_before_use(self):
        lines = body(self.yg, "std::string ini_lines()")
        self.assertIn('{"refresh", g_session.refresh}', lines)
        self.assertIn("protect(j.dump())", lines)
        self.assertNotIn("g_session.access", lines)   # the access token stays in memory
        refresh = body(self.yg, "std::string access_token(")
        self.assertIn("std::lock_guard<std::mutex> one(g_refresh_mutex);", refresh)   # one refresh at a time
        self.assertLess(refresh.index("save_now();"), refresh.rindex("return "))
        self.assertIn("invalid_grant", refresh)
        self.assertIn("set_save_now([] { SendMessageTimeoutW(g_main, WM_APP_YOUGAME_SAVE", self.launcher)

    def test_sign_out_revokes(self):
        self.assertIn('"/oauth/revoke"', body(self.yg, "void revoke("))
        self.assertIn("revoke(e, was.client, was.refresh);", body(self.yg, "void sign_out()"))

    def test_code_shows_in_the_yougame_box_or_the_status_line(self):
        show = body(self.launcher, "void yougame_show_code_box()")
        self.assertIn("o.screen = YG_BOX_SIGN_IN;", show)
        self.assertIn("o.window = g_main;", show)
        self.assertIn('o.name = "Melee Unlocked";', show)
        self.assertIn('launcher::yougame::env().id == "staging"', show)
        self.assertIn("yg_box_show(&o) == YG_BOX_SHOWN", show)
        poll = body(self.launcher, "void yougame_poll_code_box()")
        self.assertIn("YG_BOX_BACK", poll)
        self.assertIn("launcher::yougame::cancel_sign_in_code();", poll)
        self.assertIn('L"YouGame: go to {link} and enter {code}"', self.launcher)
        self.assertIn("yg_box_close();", self.launcher)
        self.assertIn("#define YOUGAME_START_IMPLEMENTATION", read("port/app/launcher_yougame_box.cpp"))
        self.assertIn("launcher_yougame_box.cpp", self.cmake)

    def test_test_hook_writes_only_link_and_code(self):
        where = body(self.yg, "std::filesystem::path test_code_file()")
        self.assertIn('"YOUGAME_TEST_CODE_SIGNIN"', where)
        self.assertIn('"sign-in-code.json"', where)
        self.assertIn("is_absolute()", where)
        written = body(self.yg, "void write_test_code(")
        self.assertIn('json{{"link", link}, {"code", code}}.dump()', written)
        self.assertIn("write_test_code(test_file, link, code);", body(self.yg, "CodeSignIn sign_in_code("))

    def test_no_webview_sign_in_left(self):
        for text in (self.yg, self.header, self.launcher, self.cmake):
            self.assertNotIn("launcher_yougame_web", text)
            self.assertNotIn("WebView2Loader", text)
        self.assertFalse((ROOT / "port/app/launcher_yougame_web.cpp").exists())
        self.assertFalse((ROOT / "port/third_party/webview2").exists())

    def test_old_web_session_line_is_dropped(self):
        load = body(self.yg, "bool load_line(")
        self.assertIn('j.value("kind", "") == "code"', load)
        self.assertIn("g_old_session_line = true;", load)

    def test_sign_in_code_is_never_logged(self):
        # The code signs in whoever enters it until it expires, and players attach the launcher's logs.
        for text in (self.launcher, self.yg):
            for line in re.findall(r"log(?:_line)?\([^;]*;", text):
                self.assertNotRegex(line, r"\bcode\.c_str\(\)|g_yg_code\.c_str\(\)|user_code|\bopen\.c_str\(\)|code->open|verification_uri_complete", line)

    def test_code_opens_the_browser_by_itself(self):
        handler = self.launcher[self.launcher.index("case WM_APP_YOUGAME_CODE:"):]
        handler = handler[:handler.index("return 0;")]
        # It opens the page with the code already filled in, so a player who signs in to the site first keeps it.
        self.assertIn("ShellExecuteW(nullptr, L\"open\", widen(code->open)", handler)
        self.assertIn('string_or_empty(j, "verification_uri_complete")', self.yg)
        self.assertIn("if (open.rfind(e.site + \"/\", 0) != 0", self.yg)

    def test_vendored_headers_name_their_youGame_commit(self):
        for name in ("yougame_start.h", "yougame.h"):
            first = read("port/third_party/yougame_start/" + name).splitlines()[0]
            self.assertTrue(re.search(r"claude/v2 [0-9a-f]{9}", first), first)


if __name__ == "__main__":
    unittest.main(argv=sys.argv[:1])
