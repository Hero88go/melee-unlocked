"""The launcher's update check asks GitHub again quietly before it reports a failure, reports it in plain words,
and the launcher draws that with a faint dot, not the red one a failed download keeps (updater_check_retry.h)."""
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


class UpdaterCheckRetryWiring(unittest.TestCase):
    def setUp(self):
        self.updater = source("port/runtime/host/updater.cpp")
        self.header = source("port/runtime/host/updater.h")
        self.launcher = source("port/app/launcher.cpp")

    def test_check_asks_again_before_it_reports(self):
        check = body(self.updater, "void check(const std::string& current_version, bool install_experimental) {")
        self.assertIn("const bool quiet = g_state.load() != State::Failed;", check)   # Retry reports at once
        self.assertIn("check_retry::delay_ms(++failures, quiet)", check)
        self.assertIn("check_retry::failed_text(", check)
        self.assertIn("g_check_failed = true;", check)
        self.assertNotIn("Update check failed", check)   # the text comes from failed_text alone
        # A wait to ask again never holds the launcher's exit.
        self.assertIn("!g_stopping.load()", check)
        stop = body(self.updater, "void shutdown() {")
        self.assertLess(stop.index("g_stopping = true;"), stop.index("join();"))

    def test_downloads_are_untouched(self):
        install = body(self.updater, "void download_and_install() {")
        self.assertNotIn("check_retry", install)
        self.assertNotIn("g_check_failed = true", install)

    def test_launcher_dot_is_faint_for_a_failed_check(self):
        self.assertIn("bool check_failed();", self.header)
        update = body(self.launcher, "void refresh_updater() {")
        self.assertIn("st == State::Failed ? (host::updater::check_failed() ? C_FAINT : C_BAD)", update)


if __name__ == "__main__":
    unittest.main()
