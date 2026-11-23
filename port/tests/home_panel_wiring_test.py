"""122.19: the YouGame box Shift+Tab opens in a game started on its own (yougame_signin.h Home).

- Esc and the pad's B close it as Back does (pc_settings.cpp's Esc, signin_overlay.cpp's B).
- After Send feedback it says the page is open only when openFeedback's answer says opened; otherwise an
  honest line (already open, not opened, or the app's own words for a refusal).
- Open on YouGame: the installed app's link (the app on the game's page, as open_app), or the site's game
  page in the browser with no app. In the box only; no corner badge.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import os
import pathlib
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def body(source, name):
    at = source.index(name)
    at = source.index("{", at)
    depth, end = 1, at + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[at:end]


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
#include <cstdlib>
using namespace yougame::signin;
using json = nlohmann::json;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
static bool says(const Home& h, const char* line) { return h.view().message.find(line) != std::string::npos; }
int main() {
  Home h;
  CHECK(h.view().screen == Screen::Closed);
  h.open("Ada");
  View v = h.view();
  CHECK(v.screen == Screen::Home);
  CHECK(v.buttons[0] == Button::Feedback && v.buttons[1] == Button::OpenOnYouGame && v.buttons[2] == Button::Back);
  CHECK(says(h, "Signed in as Ada.") && !says(h, kFeedbackOpened));
  h.feedback_answer(json{{"ok", true}, {"result", {{"opened", true}}}});
  CHECK(says(h, kFeedbackOpened));
  h.feedback_answer(json{{"ok", true}, {"result", {{"opened", false}}}});   // the app's minute between opens
  CHECK(says(h, kFeedbackNotOpened) && !says(h, kFeedbackOpened));
  h.feedback_answer(json{{"ok", true}, {"result", {{"opened", false}, {"alreadyOpen", true}}}});
  CHECK(says(h, kFeedbackAlready) && !says(h, kFeedbackOpened));
  h.feedback_answer(json{{"ok", true}});                                    // no result at all
  CHECK(says(h, kFeedbackNotOpened));
  h.feedback_answer(json{{"ok", false}, {"error", "The connection closed."}, {"code", "closed"}});
  CHECK(says(h, "The connection closed.") && !says(h, kFeedbackOpened));
  h.feedback_answer(json{{"ok", false}, {"error", std::string(150, 'x')}});  // too long: our words
  CHECK(says(h, kFeedbackFailed));
  h.feedback_answer(json{{"ok", false}, {"error", "bad\nline"}});            // not one printable line
  CHECK(says(h, kFeedbackFailed));
  h.feedback_answer(json());                                                // no answer
  CHECK(says(h, kFeedbackFailed) && !says(h, kFeedbackOpened));
  h.opened(Home::Opened::App);  CHECK(says(h, kOpenedApp));
  h.opened(Home::Opened::Site); CHECK(says(h, kOpenedSite));
  h.opened(Home::Opened::Failed); CHECK(says(h, kOpenFailed) && !says(h, kOpenedSite));
  h.close();
  CHECK(!h.is_open() && h.view().screen == Screen::Closed);
  h.open("");                                                               // a fresh open forgets the last line
  CHECK(says(h, "Signed in to YouGame.") && !says(h, kOpenFailed));
  // Model's press ignores the Home rows (they belong to yougame_link.cpp's home_press).
  Model m;
  CHECK(m.press(Button::OpenOnYouGame, 0) == Ask::None);
  std::puts("ok");
}
'''


class HomeBehaviour(unittest.TestCase):
    def test_home_model(self):
        cxx = compiler()
        if not cxx:
            self.skipTest("no C++ compiler")
        with tempfile.TemporaryDirectory() as tmp:
            src, exe = pathlib.Path(tmp) / "home.cpp", pathlib.Path(tmp) / "home"
            src.write_text(HARNESS)
            subprocess.run(cxx + ["-std=c++17", "-Wall", "-Wswitch", "-Werror", "-I", str(ROOT / "port/runtime/hle"),
                                  "-I", str(ROOT / "port/third_party"), str(src), "-o", str(exe)], check=True)
            out = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, out.stderr)
            self.assertEqual(out.stdout.strip(), "ok")


class HomeWiring(unittest.TestCase):
    def setUp(self):
        self.link = read("port/runtime/hle/yougame_link.cpp")
        self.overlay = read("port/runtime/gx/signin_overlay.cpp")
        self.settings = read("port/runtime/gx/pc_settings.cpp")

    def test_esc_and_pad_b_are_back(self):
        # Esc (standalone Esc, or F1 under the app) closes whichever YouGame box is up, before Settings sees it.
        esc = "if (signin_up && !state.open && !state.menu_open && host::window_take_escape()) signin_overlay::back();"
        self.assertIn(esc, self.settings)
        self.assertLess(self.settings.index(esc), self.settings.index("} else if (host::window_take_escape() && state.rebind_action < 0) {"))
        self.assertIn("void back() { yougame::sign_in_press(Button::Back); }", self.overlay)
        self.assertIn("ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) pressed = Button::Back;", self.overlay)
        # The box takes the pads while it shows, so B reaches it as ImGui's FaceRight.
        self.assertIn("const bool practice_nav = state.practice_open || practice.phase == slippi::native_practice::Phase::Failure || signin_up;", self.settings)
        # Back reaches the Home box first when it shows, and closes it.
        press = body(self.link, "void sign_in_press(signin::Button button)")
        self.assertIn("if (home_showing()) { home_press(button); return; }", press)
        home = body(self.link, "void home_press(signin::Button button)")
        self.assertIn("g_home.close();", home[home.index("signin::Button::Back"):])

    def test_feedback_line_follows_the_answer(self):
        home = body(self.link, "void home_press(signin::Button button)")
        self.assertIn('bridge_channel({{"type", "openFeedback"}}, 3000)', home)
        self.assertIn("g_home.feedback_answer(answer);", home)
        self.assertNotIn("feedback_sent", self.link)
        model = read("port/runtime/hle/yougame_signin.h")
        fn = body(model, "void feedback_answer(const nlohmann::json& answer)")
        self.assertIn('true_of(result, "opened") ? kFeedbackOpened', fn)

    def test_open_on_yougame_reuses_the_app_link(self):
        self.assertIn("case Button::OpenOnYouGame: return words::kOpenOnYouGame;", self.overlay)
        home = body(self.link, "void home_press(signin::Button button)")
        self.assertIn("if (button == signin::Button::OpenOnYouGame)", home)
        fn = body(self.link, "void home_open_on_yougame()")
        self.assertIn("app_installed_now()", fn)
        self.assertIn('open_app("Open on YouGame pressed")', fn)    # the same link the Online menu opens
        self.assertIn('may_open_outside("Open on YouGame picked, page")', fn)   # test runs open nothing
        self.assertIn('"https://yougame.co/g/melee-unlocked"', fn)
        self.assertIn('"https://staging.yougame.co/g/melee-unlocked"', fn)
        self.assertIn("g_home.opened(how);", fn)
        # Menu only: nothing draws it outside the box.
        self.assertEqual(self.overlay.count("kOpenOnYouGame"), 1)
        self.assertNotIn("kOpenOnYouGame", self.settings)

    def test_three_buttons_wrap_instead_of_clipping(self):
        draw = body(self.overlay, "void draw(float x0, float y0, float x1, float y1)")
        self.assertIn("ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + w <= row_x2) ImGui::SameLine();", draw)


class VendoredStartHook(unittest.TestCase):
    def test_offer_says_sign_in_comes_after_install(self):
        start = read("port/third_party/yougame_start/yougame_start.h")
        # YouGame 2698b0cc5: the Play online offer is one line; the needs-YouGame window names sign-in.
        self.assertIn('"Online play needs the YouGame app."', start)
        self.assertIn("Get it, sign in or make an account, then start", start)


if __name__ == "__main__":
    unittest.main()
