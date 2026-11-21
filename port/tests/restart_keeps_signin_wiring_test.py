"""A game started by the launcher stays signed in to YouGame when it starts itself again (a setting the device
cannot adopt, a lost graphics device, a mod's looks): the bridge it took out of its environment goes back in
right before every replacement process starts."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


def source(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


def body(text, start, end):
    a = text.index(start)
    return text[a:text.index(end, a)]


class RestartKeepsSigninWiringTest(unittest.TestCase):
    def test_every_restart_runs_the_hook_before_it_starts_the_new_process(self):
        host = source("port/runtime/host/host.cpp")
        for start, end in [("void request_restart() {", "bool restart_after_device_loss("),
                           ("bool restart_after_device_loss(", "static std::filesystem::path launcher_folder()"),
                           ("bool restart_with_mod_looks() {", "int exit_code()")]:
            fn = body(host, start, end)
            self.assertIn("if (g_before_restart) g_before_restart();", fn, start)
            self.assertLess(fn.index("g_before_restart();"), fn.index("CreateProcessW("), start)
        self.assertIn("void set_before_restart(void (*hook)());", source("port/runtime/host/host.h"))

    def test_the_link_puts_the_launcher_bridge_back(self):
        link = source("port/runtime/hle/yougame_link.cpp")
        restore = body(link, "void restore_bridge_for_restart() {", "\n}\n")
        self.assertIn('_putenv_s("YOUGAME_BRIDGE", g_bridge_url.c_str());', restore)
        self.assertIn('_putenv_s("YOUGAME_TOKEN", g_bridge_token.c_str());', restore)
        self.assertIn('g_hybrid_env ? "MELEE_YOUGAME_MATCH_BRIDGE" : "MELEE_YOUGAME_LAUNCHER"', restore)
        start = body(link, "void start() {", "host::test_env::yougame_session = active();")
        # Registered only after the bridge was taken, and never for the app's stdio bridge (its pipes do not carry over).
        self.assertLess(start.index('g_bridge_token = take("YOUGAME_TOKEN");'), start.index("host::set_before_restart("))
        self.assertIn("if (!g_stdio && !g_bridge_url.empty() && !g_bridge_token.empty()) host::set_before_restart(restore_bridge_for_restart);", start)

    def test_the_launcher_still_accepts_an_earlier_launch_token(self):
        launcher = source("port/app/launcher_yougame.cpp")
        token = body(launcher, "std::string new_launch_token() {", "\n}\n")
        self.assertIn("g_tokens.push_front(token);", token)
        self.assertIn("while (g_tokens.size() > 4) g_tokens.pop_back();", token)


if __name__ == "__main__":
    unittest.main()
