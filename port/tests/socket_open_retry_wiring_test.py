"""The first search right after the game starts can meet name lookups that are not ready yet (WinHTTP 12007, name
not resolved, or 12029, cannot connect). The room socket tries once more a second later, for those two errors only,
and stops at once when the search is cancelled."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


def source(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


def body(text, start, end):
    a = text.index(start)
    return text[a:text.index(end, a)]


class SocketOpenRetryWiringTest(unittest.TestCase):
    def setUp(self):
        self.link = source("port/runtime/hle/yougame_link.cpp")
        self.open = body(self.link, "  bool open(const std::string& address, std::string* error, int timeout,", "  bool open_once(")
        self.once = body(self.link, "  bool open_once(", "  bool send(const json& j) {")

    def test_one_more_try_only_for_name_and_connect_errors(self):
        self.assertIn("kNameNotResolved = 12007, kCannotConnect = 12029", self.open)
        self.assertIn("if (last_net_error_ != kNameNotResolved && last_net_error_ != kCannotConnect) return false;", self.open)
        self.assertEqual(self.open.count("open_once(address, error, timeout, cancelled)"), 2)

    def test_the_wait_ends_when_the_search_is_cancelled(self):
        wait = body(self.open, "for (int waited = 0;", "host::log(")
        self.assertIn("if (cancelled && cancelled()) return false;", wait)

    def test_every_open_records_its_own_error(self):
        self.assertIn("last_net_error_ = 0;", self.once.split("if (ws_ || reader_.joinable()) close();")[0])
        self.assertIn("last_net_error_ = GetLastError();", body(self.once, "if (!conn) {", "return false;"))
        self.assertIn("if (!ws_ && !status) last_net_error_ = step_error;", self.once)


if __name__ == "__main__":
    unittest.main()
