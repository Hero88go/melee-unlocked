"""An ask that opens a YouGame sheet (the friend picker's mpFriend, the feedback panel's openFeedback) first lets
YouGame take the foreground (AllowSetForegroundWindow(ASFW_ANY)). Without it Windows' foreground lock keeps focus
with the game after the player's own key or pad press, and the sheet opens unseen behind the game."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


def source(name):
    return (ROOT / name).read_text(encoding="utf-8-sig")


def body(text, marker):
    start = text.index(") {", text.index(marker)) + 2
    depth, end = 1, start + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start + 1:end - 1]


class AppSheetForeground(unittest.TestCase):
    def setUp(self):
        self.link = source("port/runtime/hle/yougame_link.cpp")
        self.channel = source("port/runtime/hle/yougame_channel.h")

    def test_the_sheet_asks_are_named_once(self):
        ask = body(self.channel, "inline bool opens_app_sheet(")
        self.assertIn('"mpFriend"', ask)
        self.assertIn('"openFeedback"', ask)

    def test_the_grant_is_windows_only_and_keyed_on_the_ask(self):
        grant = body(self.link, "void allow_app_foreground(const json& body)")
        self.assertIn("#ifdef _WIN32", grant)
        self.assertIn("channel::opens_app_sheet(", grant)
        self.assertIn("AllowSetForegroundWindow(ASFW_ANY);", grant)

    def test_both_send_paths_grant_before_sending(self):
        for marker, send in (("json bridge(const json& body", "bridge_stdio("),
                             ("json bridge_channel(const json& body", "bridge_stdio(")):
            text = body(self.link, marker)
            self.assertIn("allow_app_foreground(body);", text, marker)
            self.assertLess(text.index("allow_app_foreground(body);"), text.index(send), marker)


if __name__ == "__main__":
    unittest.main()
