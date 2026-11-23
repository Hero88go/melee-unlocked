"""Build 106: online rows say why they can't start, the start window's Open YouGame, the disc picker's
explanation, and the plain window title."""
from pathlib import Path
import os
import shlex
import shutil
import subprocess
import tempfile
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


def compiler():
    if os.environ.get("CXX"):
        return shlex.split(os.environ["CXX"])
    for name in ("c++", "g++", "clang++"):
        if path := shutil.which(name):
            return [path]
    return None


HARNESS = r'''
#include "yougame_signin.h"
#include <cstdio>
using namespace yougame::signin;
static bool has(const View& v, Button a, Button b = Button::None, Button c = Button::None) {
  return v.buttons[0] == a && v.buttons[1] == b && v.buttons[2] == c;
}
int main() {
  Reach r;
  if (r.is_open() || r.view().screen != Screen::Closed) return 10;
  r.open(false);
  View v = r.view();
  if (v.screen != Screen::Looking || v.title != kReachTitle || !has(v, Button::Offline)) return 11;
  r.looked(false, true);
  v = r.view();
  if (v.screen != Screen::AppClosed || v.message != kAppClosed || !has(v, Button::OpenApp, Button::Offline)) return 12;
  r.opening();
  if (r.view().screen != Screen::Opening) return 13;
  r.open_failed(kDidntOpen);
  v = r.view();
  if (v.screen != Screen::AppClosed || v.message.find(kDidntOpen) != 0 || !has(v, Button::OpenApp, Button::GetApp, Button::Offline)) return 14;
  r.close();
  r.open(false);
  r.looked(false, false);
  v = r.view();
  if (v.screen != Screen::NoApp || v.message != kNoApp || !has(v, Button::GetApp, Button::Offline)) return 15;
  r.page_opened();
  if (r.view().message.find(kPageOpen) == std::string::npos) return 16;
  r.close();
  r.open(true);
  v = r.view();
  if (v.screen != Screen::Admin || v.message != kAdmin || !has(v, Button::Offline)) return 17;
  r.looked(false, true);                 // a late look never replaces the administrator screen
  if (r.view().screen != Screen::Admin) return 18;
  r.close();
  r.looked(true, true);                  // nor reopens a closed panel
  if (r.is_open()) return 19;
  r.open(false);                         // reopened while an earlier wait runs: its failure lands here
  r.open_failed(kDidntOpen);
  if (r.view().screen != Screen::AppClosed) return 20;
  std::puts("ok");
  return 0;
}
'''


class Build106Fixes(unittest.TestCase):
    def test_online_rows_open_the_reach_panel(self):
        link = source("port/runtime/hle/yougame_link.cpp")
        picked = body(link, "void first_row_picked() {")
        # Asked on first use: Windows and Mac ask through YouGame's own window at the press;
        # Linux (no window in the hook) keeps the Reach panel.
        win_mac = picked.split("#if defined(_WIN32) || defined(__APPLE__)", 1)[1].split("#else", 1)
        self.assertIn("reach_ask_on_use();", win_mac[0])
        self.assertIn("reach_open();", win_mac[1].split("#endif", 1)[0])
        self.assertNotIn("run_start_hook", picked)
        ask = body(link, "void reach_ask_on_use() {")
        self.assertIn('run_start_hook("YouGame row pressed", false, YG_START_ASK_ON_USE)', ask)
        self.assertIn("g_reach_busy.compare_exchange_strong(idle, true)", ask)
        self.assertIn("std::thread(", ask)
        self.assertIn("reach_wait_for_channel(25)", ask)
        self.assertIn("g_reach.opening();", ask)
        self.assertIn("if (r == YG_START_DECLINED || r == YG_START_OFFERED) return;", ask)
        self.assertIn("reach_open();", ask)
        self.assertNotIn("yg__start_raised", ask)   # the hook's own administrator window says it
        boot = body(link, "void start_hook_at_boot(")
        self.assertIn('run_start_hook("game start", automated, YG_START_ASK_LATER)', boot)
        once = body(link, "int start_once(")
        self.assertIn("o.ask = ask;", once)
        reach = body(link, "void reach_open() {")
        self.assertIn("admin = yg__start_raised() != 0;", reach)
        self.assertIn("look_for_app(staging)", reach)
        opened = body(link, "void reach_open_app() {")
        self.assertIn('run_start_hook("Open YouGame pressed", false, YG_START_ASK_ON_USE)', opened)
        self.assertIn('open_app("Open YouGame pressed")', opened)
        self.assertIn("reach_wait_for_channel(25)", opened)
        press = body(link, "void reach_press(signin::Button button) {")
        self.assertIn("open_get_page();", press)
        # Get YouGame goes to production's page unless the launcher chose staging (launcher.ini yougame_env=staging).
        get = body(link, "void open_get_page() {")
        self.assertIn('start_staging() ? "https://staging.yougame.co/desktop" : "https://yougame.co/desktop"', get)
        self.assertIn("if (reach_showing() || update_showing() || home_showing()) return true;", body(link, "bool sign_in_showing() {"))
        self.assertIn("if (reach_view(out) || update_view(out) || home_view(out)) return;", body(link, "void sign_in_view(signin::View& out) {"))
        self.assertIn("if (reach_showing()) { reach_press(button); return; }", body(link, "void sign_in_press(signin::Button button) {"))
        overlay = source("port/runtime/gx/signin_overlay.cpp")
        self.assertIn("case Button::OpenApp: return words::kOpenApp;", overlay)
        self.assertIn("v.title.empty() ? words::kTitle : v.title.c_str()", overlay)

    def test_reach_words(self):
        words = source("port/runtime/hle/yougame_signin.h")
        for line in ('"YouGame isn\'t running. Open YouGame to play online."',
                     '"Online play needs the YouGame app."',
                     '"Melee Unlocked was started as administrator, so it can\'t reach YouGame. Start it normally to play online."'):
            self.assertIn(line, words)

    def test_start_window_offers_open(self):
        start = source("port/third_party/yougame_start/yougame_start.h")
        self.assertIn('#define YG__START_OPEN "Open YouGame"', start)
        self.assertIn('#define YG__START_LATER "Play offline"', start)   # the C3 window (YouGame 55e1965c6)
        # v2 a678bb7b: a tray start waits for YouGame to come up; a slow one is DECLINED, never the download page.
        # The mini app is retired (YouGame 569624f25): a tray start of the full app only, then the same wait.
        self.assertIn("tray = yg__start_tray(staging, want, have);", start)
        self.assertIn("if (tray == YG__START_TRAY_STARTED) {", start)
        self.assertIn("return up >= 0 ? (yg_start_result)up : YG_START_DECLINED;", start)
        self.assertIn("if (installed && YG__START_UPDATE_SYSTEM && yg__start_raised()) text = YG__START_TEXT_RAISED;", start)

    def test_disc_picker_closed_explains(self):
        launch = source("port/app/standalone_launch_win.h")
        self.assertIn("Melee Unlocked needs your own copy of Super Smash Bros. Melee (NTSC 1.02, an .iso file) to start.", launch)
        self.assertIn('SetDlgItemTextW(box, IDOK, L"Choose disc");', launch)
        self.assertIn('SetDlgItemTextW(box, IDCANCEL, L"Quit");', launch)
        pick = body(launch, "inline fs::path pick() {")
        self.assertIn("if (choose_again(owner.hwnd)) continue;", pick)

    def test_plain_window_title(self):
        main = source("port/app/main.cpp")
        self.assertIn("host::window_title_debug() = base;", main)
        self.assertNotIn("host::window_title_base() = base;", main)
        backend = source("port/runtime/gx/threaded_backend.cpp")
        stats = backend[backend.index("static bool stats_title = false;"):]
        self.assertIn("if (live_options.performance_overlay) {", stats[:400])
        self.assertIn('L"Melee Unlocked"', source("port/runtime/host/window.h"))

    def test_reach_model(self):
        cxx = compiler()
        if not cxx:
            self.skipTest("no C++ compiler")
        with tempfile.TemporaryDirectory() as tmp:
            src, exe = Path(tmp) / "t.cpp", Path(tmp) / "t"
            src.write_text(HARNESS)
            subprocess.run(cxx + ["-std=c++17", "-I", str(ROOT / "port/runtime/hle"), "-I", str(ROOT / "port/third_party"),
                                  str(src), "-o", str(exe)], check=True)
            out = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, out.stdout + out.stderr)


if __name__ == "__main__":
    unittest.main()
