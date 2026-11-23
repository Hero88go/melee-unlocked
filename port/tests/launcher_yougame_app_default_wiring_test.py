"""The launcher's YouGame default is YouGame's own way in: Connect asks YouGame's standard start hook first, a game
started with no launcher sign-in is handed to the YouGame app, and that game's YouGame rows ask with the hook.
The launcher's own browser sign-in stays as the fallback where the hook cannot run."""
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


class LauncherUsesYouGameApp(unittest.TestCase):
    def setUp(self):
        self.launcher = source("port/app/launcher.cpp")
        self.yougame = source("port/app/launcher_yougame.cpp")
        self.app = source("port/app/launcher_yougame_app.cpp")
        self.app_h = source("port/app/launcher_yougame_app.h")
        self.link = source("port/runtime/hle/yougame_link.cpp")
        self.online = source("port/runtime/hle/slippi_online.cpp")

    def test_the_launcher_holds_the_hook_once(self):
        # One definition per program: the launcher's in launcher_yougame_app.cpp (MSVC only), the game's in
        # yougame_link.cpp. The launcher's other files only read the results.
        windows = self.app.split("#if defined(_WIN32) && defined(_MSC_VER)", 1)[1]
        self.assertIn('#define YOUGAME_START_IMPLEMENTATION\n#include "yougame_start/yougame_start.h"', windows)
        for name in ("port/app/launcher.cpp", "port/app/launcher_yougame.cpp", "port/app/launcher_yougame_web.cpp"):
            self.assertNotIn("YOUGAME_START_IMPLEMENTATION", source(name), name)
        self.assertIn('#include "yougame_start/yougame_start.h"', self.yougame)

    def test_the_hook_is_asked_on_use_as_the_game_asks_it(self):
        hook = body(self.app, "int start_hook(bool staging) {")
        self.assertIn("if (elevated()) return YG_START_UNAVAILABLE;", hook)
        # The launcher's "raised" is the hook's own rule, so it agrees with the game (Windows Sandbox: UAC off).
        self.assertIn("type == TokenElevationTypeFull;", self.app)
        self.assertNotIn("yg__start_raised", self.app)   # only public calls from the YouGame headers
        self.assertNotIn("TokenIsElevated", self.app)
        self.assertIn('o.game = "melee-unlocked";', hook)
        self.assertIn("o.ask = YG_START_ASK_ON_USE;", hook)
        self.assertIn('o.name = "Melee Unlocked";', hook)
        self.assertIn("o.open_wait_ms = 20000;", hook)
        self.assertNotIn("o.needs", hook)   # offline play works: Play offline, never Quit
        self.assertIn("return (int)yg_start(&o);", hook)
        # Elsewhere (Linux's launcher, MinGW checks) the stub says the hook cannot run.
        stubs = self.app_h.split("#else", 1)[1]
        self.assertIn("inline int start_hook(bool) { return 4; }", stubs)

    def test_hook_results_map_to_three_answers(self):
        ask = body(self.yougame, "AppHook ask_app_hook(std::string* detail) {")
        for result, answer in (("YG_START_RUNNING", "kRunning"), ("YG_START_OPENED", "kRunning"),
                               ("YG_START_UPDATING", "kRunning"), ("YG_START_OFFERED", "kChose"),
                               ("YG_START_DECLINED", "kChose")):
            line = ask[ask.index("case " + result + ":"):]
            line = line[:min(i for i in (line.find("\n    case ", 1), line.find("\n    default:")) if i > 0)]
            self.assertIn("return AppHook::" + answer + ";", line, result)
        # Get YouGame: the hook installs the YouGame app in the background; installed and started, its account is
        # taken (kRunning); a failed or unfinished install ends Connect (kChose).
        getting = ask[ask.index("case YG_START_GETTING: {"):ask.index("default:")]
        self.assertIn("app::start_get_progress(&percent)", getting)
        self.assertIn("if (state == YG_START_GET_READY) {", getting)
        self.assertIn("return AppHook::kRunning;", getting)
        self.assertIn("return AppHook::kChose;", getting)
        # An app the hook just started opens its channel a little later: Connect waits for it before asking the app,
        # so a fresh install hands off to the app, never to the launcher's own sign-in window.
        wait = body(ask, "auto channel_up = [&]() {")
        self.assertIn("app::running(staging)", wait)
        ready = getting[getting.index("if (state == YG_START_GET_READY) {"):]
        self.assertLess(ready.index("channel_up()"), ready.index("return AppHook::kRunning;"))
        opened = ask[ask.index("case YG_START_OPENED:"):ask.index("case YG_START_UPDATING:")]
        self.assertLess(opened.index("channel_up()"), opened.index("return AppHook::kRunning;"))
        self.assertIn("default:", ask)
        self.assertIn("return AppHook::kUnavailable;", ask.split("default:", 1)[1])
        available = body(self.yougame, "bool app_hook_available() {")
        self.assertIn('std::getenv("MELEE_LAUNCHER_TEST") && !std::getenv("MELEE_LAUNCHER_TEST_YOUGAME_APP")', available)
        self.assertIn("return !app::elevated();", available)

    def test_a_signed_out_app_signs_in_in_its_own_window(self):
        # After the hook, a running but signed-out YouGame app opens its own window for sign-in and Connect takes the
        # account once it has one: never the launcher's own sign-in window (or its WebView2 install) on that path.
        thread = body(self.launcher, "void yougame_sign_in_thread(bool use_app) {")
        running = thread[thread.index("if (hook == launcher::yougame::AppHook::kRunning) {"):]
        signed_out = running[running.index("if (app == launcher::yougame::AppSignIn::kSignedOut) {"):]
        signed_out = signed_out[:signed_out.index("// The app did not answer")]
        self.assertIn("launcher::yougame::open_target()", signed_out)
        self.assertIn("sign_in_app(false, version, &step)", signed_out)
        self.assertIn("g_yg_app_sign_in = true;", signed_out)
        self.assertLess(signed_out.rindex("return;"), len(signed_out))
        self.assertNotIn("install_web_runtime", signed_out)
        self.assertIn('L"YouGame: sign in in the YouGame app..."', self.launcher)

    def test_connect_asks_the_hook_before_the_browser_sign_in(self):
        thread = body(self.launcher, "void yougame_sign_in_thread(bool use_app) {")
        hook = thread.index("launcher::yougame::ask_app_hook(&detail)")
        self.assertLess(thread.index("launcher::yougame::resume_web(&resume_why)"), hook)   # a saved sign-in first
        self.assertLess(hook, thread.index("launcher::yougame::sign_in_web("))
        gate = thread[:hook]
        self.assertIn("if (use_app_asked && !launcher::yougame::account().app && launcher::yougame::app_hook_available()) {", gate)
        after = thread[hook:thread.index("launcher::yougame::sign_in_web(")]
        # Get YouGame or Play offline: Connect ends there, no browser window, no message box.
        chose = after[after.index("AppHook::kChose"):]
        self.assertIn('PostMessageW(g_main, WM_APP_YOUGAME_WEB, 0, (LPARAM)new std::string("hook"));', chose[:300])
        self.assertIn("return;", chose[:300])
        # A running app: its account, asked, never started a second time.
        running = after[after.index("AppHook::kRunning) {"):]
        self.assertIn("launcher::yougame::sign_in_app(false, version, &detail);", running)
        self.assertIn('new std::string("app")', running)
        handler = self.launcher[self.launcher.index("case WM_APP_YOUGAME_WEB: {"):]
        handler = handler[:handler.index("return 0;")]
        self.assertIn('if (!wp && !why->empty() && *why != "hook") MessageBoxW(', handler)

    def test_no_launcher_sign_in_hands_the_game_to_the_app(self):
        start = self.launcher.index("const bool app_backed = ")
        decision = self.launcher[start:self.launcher.index("PROCESS_INFORMATION pi{};", start)]
        rule = decision[:decision.index(";")]
        for part in ("!vanilla_active()", "!(yg_account.signed_in && !yg_account.app)", "yg_same_env",
                     "launcher::yougame::listed_current()", "launcher::yougame::app_hook_available()",
                     "!launcher::yougame::app_declined()"):
            self.assertIn(part, rule)
        self.assertIn("const bool hybrid = !vanilla_active() && !app_backed && launcher::yougame::extension();", decision)
        self.assertIn('SetEnvironmentVariableW(L"MELEE_YOUGAME_EXTENSION", hybrid || app_backed ? L"1" : nullptr);', decision)
        # No bridge for it: the game's channel is allowed only without YOUGAME_BRIDGE (or with the hybrid).
        self.assertIn("vanilla_active() || app_backed ? std::string() : launcher::yougame::bridge_url();", decision)
        # A launcher sign-in of its own keeps the bridge and the extension option exactly as before.
        self.assertIn('SetEnvironmentVariableW(hybrid ? L"MELEE_YOUGAME_MATCH_BRIDGE" : L"MELEE_YOUGAME_LAUNCHER", L"1");', decision)

    def test_the_handed_game_asks_with_the_hook_on_a_row(self):
        ask = body(self.link, "bool ask_on_use() {")
        for part in ("!kNoApp", "!g_stdio", "g_channel_allowed.load()", "!g_channel_up.load()", "!g_launcher",
                     "!g_match_bridge", "!active()"):
            self.assertIn(part, ask)
        # The menu marks the rows not ready (bit 6), and the pick (0x20) runs first_row_picked.
        self.assertIn("if (yougame::coming_soon() || yougame::ask_on_use()) yougame::first_row_picked();", self.online)
        self.assertIn("(yougame::coming_soon() || yougame::ask_on_use() || yougame::sign_in_needed()) && !yougame::active() && (q[0] & 2)) q[0] |= 64;", self.online)
        picked = body(self.link, "void first_row_picked() {")
        self.assertIn("if (g_launcher || g_match_bridge) return;", picked)   # the launcher's bridge keeps its own sign-in
        self.assertIn("reach_ask_on_use();", picked)
        # Get YouGame in the game's window: installing in the background, then the rows open by themselves.
        on_use = body(self.link, "void reach_ask_on_use() {")
        self.assertIn("if (r == YG_START_GETTING) { reach_after_get(); return; }", on_use)
        after = body(self.link, "void reach_after_get() {")
        self.assertIn("yg_start_get_progress(&percent)", after)
        self.assertIn("reach_wait_for_channel(25)", after)
        self.assertNotIn("yg__start_u.getting", self.link)   # not in YouGame's public header
        # The game reads the extension once: MELEE_YOUGAME_EXTENSION=1 turns the app channel on in the GitHub build.
        self.assertIn('const bool kNoApp = [] { const char* v = std::getenv("MELEE_YOUGAME_EXTENSION"); return !(v && v[0] == \'1\'); }();', self.link)


if __name__ == "__main__":
    unittest.main()
