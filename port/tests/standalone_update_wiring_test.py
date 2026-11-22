"""Standalone update and restart (yougame_signin.h Update): a copy the player started itself, connected to the
YouGame app over the local channel, whose search was refused as out of date (old_build, build_replaced) offers
Update. The app downloads the latest build for a copy it installed (updateGame, gameUpdate pushes), and Restart
now asks it to relaunch the new build (restartGame) before this game quits cleanly. A copy the app didn't
install (not_app_copy) keeps the "get the latest" wording with no Update button."""
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
using json = nlohmann::json;
using Step = Update::Step;
static bool has(const View& v, Button a, Button b = Button::None, Button c = Button::None) {
  return v.buttons[0] == a && v.buttons[1] == b && v.buttons[2] == c;
}
static json refusal(const char* code, const char* current = nullptr) {
  json j = {{"re", 1}, {"ok", false}, {"code", code}, {"error", "refused"}};
  if (current) j["current"] = current;
  return j;
}
static json ok(json result) { return {{"re", 1}, {"ok", true}, {"result", result}}; }
int main() {
  // Only a known, replaced build is update-able: never unknown_build (a fresh upload's brief window).
  if (!Update::out_of_date(refusal("old_build")) || !Update::out_of_date(refusal("build_replaced"))) return 10;
  if (Update::out_of_date(refusal("unknown_build")) || Update::out_of_date(ok(json::object())) ||
      Update::out_of_date(json()) || Update::out_of_date(refusal("notYet"))) return 11;
  if (Update::request(Step::Update)["type"] != "updateGame" || Update::request(Step::Cancel)["type"] != "cancelUpdate" ||
      Update::request(Step::Restart)["type"] != "restartGame") return 12;

  Update u;
  if (u.is_open() || u.view().screen != Screen::Closed) return 13;
  u.offer(refusal("build_replaced", "1.4.2"));
  View v = u.view();
  if (v.screen != Screen::UpdateOffered || v.title != kUpdateTitle || !has(v, Button::Update, Button::Offline)) return 14;
  if (v.message != "This copy of Melee is out of date (the latest is 1.4.2). Update it to play online.") return 15;

  // Update -> downloading -> percent pushes -> ready -> Restart now -> exit.
  if (u.press(Button::Update, 0) != Step::Update || u.view().screen != Screen::UpdateChecking) return 20;
  if (!has(u.view(), Button::Cancel) || u.view().message != kUpdating) return 21;
  if (u.press(Button::Update, 0) != Step::None) return 22;   // no second ask while one is on its way
  if (u.answered(Step::Update, ok({{"state", "downloading"}}), 10) != Step::None) return 23;
  v = u.view();
  if (v.screen != Screen::UpdateDownloading || v.message != kUpdating || !has(v, Button::Cancel, Button::Offline)) return 24;
  if (!u.push({{"type", "gameUpdate"}, {"state", "downloading"}, {"percent", 42}})) return 25;
  if (u.view().message != "Updating... 42%") return 26;
  u.push({{"type", "gameUpdate"}, {"state", "downloading"}, {"percent", 250}});
  if (u.view().message != "Updating... 100%") return 27;
  if (u.push({{"type", "signedIn"}})) return 28;            // not ours
  u.push({{"type", "gameUpdate"}, {"state", "ready"}, {"version", "1.4.2"}});
  v = u.view();
  if (v.screen != Screen::UpdateReady || v.message != "Version 1.4.2 is ready." || !has(v, Button::Restart, Button::Offline)) return 29;
  if (u.press(Button::Restart, 20) != Step::Restart || u.view().screen != Screen::UpdateRestarting) return 30;
  if (u.view().buttons[0] != Button::None) return 31;
  if (u.answered(Step::Restart, ok({{"version", "1.4.2"}}), 30) != Step::Exit) return 32;

  // Restart refused (not_ready, unavailable, or no answer): say so and stay.
  for (const char* code : {"not_ready", "unavailable"}) {
    Update r;
    r.offer(refusal("old_build"));
    r.press(Button::Update, 0);
    r.answered(Step::Update, ok({{"state", "ready"}, {"version", "2.0"}}), 0);
    if (r.view().message != "Version 2.0 is ready.") return 33;
    r.press(Button::Restart, 0);
    if (r.answered(Step::Restart, refusal(code), 0) != Step::None) return 34;
    v = r.view();
    if (v.screen != Screen::UpdateRestartFailed || v.message != kRestartFailed || !has(v, Button::Offline)) return 35;
  }

  // A failed download: Try again asks again.
  Update f;
  f.offer(refusal("old_build"));
  if (f.view().message != "This copy of Melee is out of date. Update it to play online.") return 40;
  f.press(Button::Update, 0);
  f.answered(Step::Update, ok({{"state", "downloading"}}), 0);
  f.push({{"type", "gameUpdate"}, {"state", "failed"}, {"error", "disk full"}});
  v = f.view();
  if (v.screen != Screen::UpdateFailed || v.message != kUpdateFailed || !has(v, Button::TryAgain, Button::Offline)) return 41;
  if (f.press(Button::TryAgain, 0) != Step::Update || f.view().screen != Screen::UpdateChecking) return 42;
  if (f.answered(Step::Update, json(), 0) != Step::None || f.view().message != kNoAnswer) return 43;   // no answer

  // Cancel: back to Update; a late push from the cancelled download changes nothing.
  Update c;
  c.offer(refusal("old_build"));
  c.press(Button::Update, 0);
  c.answered(Step::Update, ok({{"state", "downloading"}}), 0);
  if (c.press(Button::Cancel, 0) != Step::Cancel || c.view().screen != Screen::UpdateOffered) return 50;
  c.push({{"type", "gameUpdate"}, {"state", "ready"}, {"version", "9"}});
  if (c.view().screen != Screen::UpdateOffered) return 51;
  if (c.answered(Step::Cancel, ok(json::object()), 0) != Step::None || c.view().screen != Screen::UpdateOffered) return 52;
  // Cancel before the first answer: that answer, arriving late, changes nothing either.
  c.press(Button::Update, 0);
  c.press(Button::Cancel, 0);
  if (c.answered(Step::Update, ok({{"state", "downloading"}}), 0) != Step::None || c.view().screen != Screen::UpdateOffered) return 53;

  // A copy YouGame didn't install: today's "get the latest" words, no Update.
  Update g;
  g.offer(refusal("old_build", "1.4.2"));
  g.press(Button::Update, 0);
  g.answered(Step::Update, refusal("not_app_copy"), 0);
  v = g.view();
  if (v.screen != Screen::UpdateNotAppCopy || !has(v, Button::Offline)) return 60;
  if (v.message != "This copy of Melee is out of date (the latest is 1.4.2). Get the latest from YouGame, then play online.") return 61;
  g.press(Button::Offline, 0);
  g.offer(refusal("old_build"));               // the next refused search this run: no Update to press again
  if (g.view().screen != Screen::UpdateNotAppCopy || !has(g.view(), Button::Offline)) return 63;
  // The app says why (it opened this game's page in the browser): the panel shows the app's words instead.
  Update b;
  b.offer(refusal("old_build", "1.4.2"));
  b.press(Button::Update, 0);
  json browser = refusal("not_app_copy");
  browser["error"] = "YouGame opened this game's page in your browser. Get the latest version there, then play online.";
  b.answered(Step::Update, browser, 0);
  if (b.view().screen != Screen::UpdateNotAppCopy || !has(b.view(), Button::Offline)) return 64;
  if (b.view().message != "This copy of Melee is out of date (the latest is 1.4.2). YouGame opened this game's page in "
                          "your browser. Get the latest version there, then play online.") return 65;
  // Not a sentence, or not printable: this game's own words.
  Update w;
  w.offer(refusal("old_build"));
  w.press(Button::Update, 0);
  json odd = refusal("not_app_copy");
  odd["error"] = "Bad\nline.";
  w.answered(Step::Update, odd, 0);
  if (w.view().message != "This copy of Melee is out of date. Get the latest from YouGame, then play online.") return 66;
  // An app with no updateGame yet (notYet): today's "Open YouGame to update it" words.
  Update o;
  o.offer(refusal("old_build"));
  o.press(Button::Update, 0);
  o.answered(Step::Update, refusal("notYet"), 0);
  if (o.view().message != "This copy of Melee is out of date. Open YouGame to update it, then play online." || !has(o.view(), Button::Offline)) return 62;

  // up_to_date: the copy is current now; search again.
  Update l;
  l.offer(refusal("old_build"));
  l.press(Button::Update, 0);
  l.answered(Step::Update, refusal("up_to_date"), 0);
  v = l.view();
  if (v.screen != Screen::UpdateLatest || v.message != kLatest || !has(v, Button::Back)) return 70;
  l.press(Button::Back, 0);
  if (l.is_open()) return 71;

  // not_checked: asked again after retryAfter (kept 1..10 s), twice at most, then say so.
  Update n;
  n.offer(refusal("old_build"));
  n.press(Button::Update, 1000);
  json nc = refusal("not_checked");
  nc["retryAfter"] = 2;
  if (n.answered(Step::Update, nc, 1000) != Step::None || n.view().screen != Screen::UpdateChecking) return 80;
  if (n.tick(2999) != Step::None) return 81;
  if (n.tick(3000) != Step::Update) return 82;
  if (n.tick(3001) != Step::None) return 83;   // once
  n.answered(Step::Update, nc, 3500);
  if (n.tick(5500) != Step::Update) return 84;
  n.answered(Step::Update, nc, 6000);
  v = n.view();
  if (v.screen != Screen::UpdateNoCheck || v.message != kNoCheck || !has(v, Button::TryAgain, Button::Offline)) return 85;
  if (n.press(Button::TryAgain, 7000) != Step::Update) return 86;
  n.answered(Step::Update, nc, 7000);          // a fresh try gets its two retries again
  if (n.view().screen != Screen::UpdateChecking) return 87;
  // A not_checked then ready on the retry.
  if (n.tick(9000) != Step::Update) return 88;
  n.answered(Step::Update, ok({{"state", "ready"}}), 9000);
  if (n.view().screen != Screen::UpdateReady || n.view().message != kUpdateReady) return 89;

  // Play offline closes the panel; a new refusal shows an update already on its way as it stands.
  Update d;
  d.offer(refusal("old_build"));
  d.press(Button::Update, 0);
  d.answered(Step::Update, ok({{"state", "downloading"}}), 0);
  d.press(Button::Offline, 0);
  if (d.is_open() || d.view().screen != Screen::Closed) return 90;
  d.push({{"type", "gameUpdate"}, {"state", "downloading"}, {"percent", 70}});   // the download carries on
  d.offer(refusal("old_build"));
  if (d.view().screen != Screen::UpdateDownloading || d.view().message != "Updating... 70%") return 91;
  d.press(Button::Offline, 0);
  d.offer(refusal("old_build"));               // closed again mid-download: still shown as it stands
  if (d.view().screen != Screen::UpdateDownloading) return 92;
  Update e;
  e.offer(refusal("old_build"));
  e.press(Button::Offline, 0);
  e.offer(refusal("old_build"));
  if (e.view().screen != Screen::UpdateOffered) return 93;

  // Off-contract values read as missing, never throw.
  Update x;
  x.offer({{"ok", false}, {"code", "old_build"}, {"current", 12}});
  if (x.view().message != "This copy of Melee is out of date. Update it to play online.") return 94;
  x.press(Button::Update, 0);
  x.answered(Step::Update, {{"ok", true}, {"result", 5}}, 0);
  if (x.view().screen != Screen::UpdateFailed) return 95;
  std::puts("ok");
  return 0;
}
'''


class StandaloneUpdate(unittest.TestCase):
    def test_model(self):
        cxx = compiler()
        if not cxx:
            self.skipTest("no C++ compiler")
        with tempfile.TemporaryDirectory() as tmp:
            src, exe = Path(tmp) / "t.cpp", Path(tmp) / "t"
            src.write_text(HARNESS)
            subprocess.run(cxx + ["-std=c++17", "-Wall", "-Wswitch", "-I", str(ROOT / "port/runtime/hle"),
                                  "-I", str(ROOT / "port/third_party"), str(src), "-o", str(exe)], check=True)
            out = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, out.stdout + out.stderr)

    def test_offered_only_on_the_channel_after_a_refused_search(self):
        link = source("port/runtime/hle/yougame_link.cpp")
        start = link.index("json bridge(const json& body, int timeout_ms = 15000")
        bridge = link[start:link.index("// MU already owns stdio and the local channel.", start)]
        # bridge() only keeps the refusal: every mpTicket goes through it, renewals and room re-joins included.
        self.assertIn("g_ticket_out_of_date = json();", bridge)
        # Only an answer that came over the app's channel: the launcher's hybrid (extension on) sends tickets to
        # its own bridge (routed_to_bridge), and the app never updates a copy it did not start.
        channel = bridge[bridge.index("if (!g_stdio && g_channel_allowed && !routed_to_bridge(body)) {"):]
        self.assertIn('if (body.value("type", "") == "mpTicket" && signin::Update::out_of_date(j)) g_ticket_out_of_date = j;',
                      channel[:channel.index("const std::string ours = channel::refusal_text(j);")])
        self.assertNotIn("update_offer(", bridge)
        # Offered from find_match's first ticket only: the one catch around the player's own ticket.
        self.assertEqual(link.count("update_offer(g_ticket_out_of_date"), 1)
        first = link[link.index("Match find_match(const Search& search"):]
        catch = first[first.index('host::log("yougame: ticket refused (%s)"'):]
        self.assertIn("if (!cancelled() && !g_ticket_out_of_date.is_null()) update_offer(g_ticket_out_of_date, epoch, blend);",
                      catch[:catch.index("return fail(e.what());")])
        offer = body(link, "void update_offer(const json& refusal, uint64_t epoch, bool blend) {")
        # Never an app-started copy (stdio, the loopback bridge or the launcher): the app updates those itself.
        self.assertIn("if (g_stdio || !g_channel_allowed || !g_channel_up.load() || !g_search_generation.current(epoch)) return;", offer)
        opened = body(link, "void update_open(const json& refusal, uint64_t epoch) {")
        self.assertIn("if (g_stdio || !g_channel_allowed || !g_channel_up.load()) return;", opened)
        # Closed by the player: not again for that search.
        self.assertIn("if (epoch == g_update_closed_epoch) return;", opened)
        self.assertIn("g_update_closed_epoch = g_update_epoch;", body(link, "void update_press(signin::Button button) {"))

    def test_blend_offers_only_when_over(self):
        link = source("port/runtime/hle/yougame_link.cpp")
        offer = body(link, "void update_offer(const json& refusal, uint64_t epoch, bool blend) {")
        blend = offer[offer.index("if (blend) {"):]
        self.assertIn("g_update_deferred = refusal;", blend)
        self.assertIn("return;", blend[:blend.index("update_open(refusal, epoch);")])
        over = body(link, "void blend_over(uint64_t epoch) {")
        self.assertIn("if (g_update_deferred_epoch != epoch || g_update_deferred.is_null()) return;", over)
        self.assertIn("if (g_search_generation.current(epoch)) update_open(refusal, epoch);", over)
        net = source("port/runtime/hle/slippi_net.cpp")
        over_branch = net[net.index("    if (over) {\n      dropSlippi();"):]
        self.assertIn("yougame::blend_over(search.epoch);", over_branch[:over_branch.index("return;")])
        self.assertEqual(net.count("yougame::blend_over("), 1)

    def test_match_start_closes_the_panel(self):
        link = source("port/runtime/hle/yougame_link.cpp")
        self.assertIn("if (open) g_update_match_started.store(true);", body(link, "void online_match_state(bool open) {"))
        showing = body(link, "bool update_showing() {")
        self.assertIn("if (g_update_match_started.exchange(false) && g_update.is_open()) {", showing)
        self.assertIn("g_update.close();", showing)

    def test_pushes_and_panel_routing(self):
        link = source("port/runtime/hle/yougame_link.cpp")
        self.assertIn("if (!g_stdio && g_channel_allowed) { sign_in_push(push); update_push(push); }", link)
        self.assertIn("if (reach_showing() || update_showing() || home_showing()) return true;", body(link, "bool sign_in_showing() {"))
        self.assertIn("if (reach_view(out) || update_view(out) || home_view(out)) return;", body(link, "void sign_in_view(signin::View& out) {"))
        self.assertIn("if (update_showing()) { update_press(button); return; }", body(link, "void sign_in_press(signin::Button button) {"))

    def test_asks_off_the_frame_and_clean_exit(self):
        link = source("port/runtime/hle/yougame_link.cpp")
        send = body(link, "void update_send(signin::Update::Step step) {")
        self.assertIn("std::thread([step] {", send)
        self.assertIn("bridge_channel(signin::Update::request(asked), 15000)", send)
        exit_at = send.index("if (next == Step::Exit) {")
        self.assertIn("host::request_exit(0);", send[exit_at:exit_at + 200])

    def test_overlay_words(self):
        overlay = source("port/runtime/gx/signin_overlay.cpp")
        for line in ("case Button::Update: return words::kUpdate;", "case Button::Cancel: return words::kCancel;",
                     "case Button::Restart: return words::kRestartNow;", "case Screen::UpdateNoCheck:\n      case Screen::Home: wrapped_text(v.message"):
            self.assertIn(line, overlay)
        words = source("port/runtime/hle/yougame_signin.h")
        for line in ('kUpdate = "Update"', 'kCancel = "Cancel"', 'kRestartNow = "Restart now"',
                     'kRestartFailed = "Couldn\'t restart. Close the game and start it again."',
                     'kUpdateFailed = "The update didn\'t finish."', 'kLatest = "You have the latest. Try again."',
                     'kNoCheck = "YouGame couldn\'t check for updates. Try again."'):
            self.assertIn(line, words)


if __name__ == "__main__":
    unittest.main()
