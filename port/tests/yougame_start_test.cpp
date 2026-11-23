// Native header seams only: never inspect or launch an installed app.
// SPDX-License-Identifier: GPL-2.0-or-later
#define YOUGAME_START_TESTING
#define YOUGAME_START_IMPLEMENTATION
#include "yougame_start.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

static int running, installed, launches, offers, signed_ok = 1, answer, last_offer = -1, last_on_use = -1, opened_page, marker_ok;
static unsigned version[3] = {0, 13, 65535};
static unsigned clock_ms;
static int yg__start_running(int) { return running; }
static int yg__start_installed(int) { return installed; }
static int yg__start_app_find(int, yg__start_app*) { return installed; }
static int yg__start_app_named(yg__start_app*, int) { return 1; }
static int yg__start_app_version(yg__start_app*, unsigned have[3]) { std::memcpy(have, version, sizeof version); return 1; }
static int yg__start_app_signed(yg__start_app*) { return signed_ok; }
static int yg__start_app_launch(yg__start_app*, int) { ++launches; return 1; }
static void yg__start_app_close(yg__start_app*) {}
static int yg__start_shell_open(const char* url) { CHECK(!std::strcmp(url, "https://yougame.co/desktop?from=game&game=melee-unlocked")); ++opened_page; return 1; }
static int yg__start_offer(const yg__start_win* w) { ++offers; last_offer = w->which; last_on_use = w->on_use; return answer; }
static int yg__start_offline(int, const char*, int) { return 0; }   // MU asks on use: never an at-start mark
static int yg__start_quiet_env() { return 0; }
static void yg__start_sleep(unsigned ms) { clock_ms += ms; }
static unsigned long yg__start_ms() { return clock_ms; }
static int yg__start_installed_version(int, unsigned*) { return 0; }
static int yg__start_marker(int, yg__start_mark*, char*) { CHECK(marker_ok); return 0; }
static int yg__start_raised() { return 0; }   // the administrator test is Windows-only (v2 a678bb7b seam)

int main() {
  yg_start_options options{};
  options.game = "melee-unlocked";
  options.tray_from = "0.13.1407";
  yg__start_test_macos = 1;
  installed = 1;
  options.quiet = 1;
  CHECK(yg_start(&options) == YG_START_INSTALLED && launches == 0 && offers == 0);
  options.quiet = 0;
  CHECK(yg_start(&options) == YG_START_OPENED && launches == 1 && offers == 0);
  signed_ok = 0;
  CHECK(yg_start(&options) == YG_START_DECLINED && launches == 1 && offers == 1);
  signed_ok = 1;
  version[2] = 1;
  CHECK(yg_start(&options) == YG_START_DECLINED && launches == 1 && offers == 2);
  running = 1;
  CHECK(yg_start(&options) == YG_START_RUNNING && launches == 1 && offers == 2);
  // Windows "isn't running" (no tray start asked): Open YouGame starts it with the same checks.
  running = 0;
  yg__start_test_macos = 0;
  yg__start_test_windows = 1;
  marker_ok = 1;
  options.no_open = 1;
  answer = YG__START_ANSWER_OPEN;
  CHECK(yg_start(&options) == YG_START_OPENED && launches == 2 && offers == 3 && last_offer == YG__START_TEXT_NOT_RUNNING);
  // It would not start (no valid signature): the download page instead.
  signed_ok = 0;
  CHECK(yg_start(&options) == YG_START_OFFERED && launches == 2 && opened_page == 1);
  signed_ok = 1;
  // Not installed: no Open on offer, so an Open answer is never taken there.
  installed = 0;
  CHECK(yg_start(&options) == YG_START_DECLINED && launches == 2 && last_offer == YG__START_TEXT_MISSING);
  // The install offer promises nothing the player can't get today (no "smaller helper", YouGame 823c0f3ce).
  {   // The C3 window (YouGame 063f1abcf): MU's name in the title, Get YouGame and Play offline, no "Not now".
    yg__start_win w;
    CHECK(yg__start_compose(YG__START_TEXT_MISSING, 1, "Melee Unlocked", 0, 0, &w));
    CHECK(std::strstr(w.title, "Melee Unlocked") || std::strstr(w.body, "Melee Unlocked"));
    CHECK(w.main_label && std::strcmp(w.main_label, "Get YouGame") == 0);
    CHECK(w.later_label && std::strcmp(w.later_label, "Play offline") == 0);
    CHECK(!std::strstr(w.body, "Not now") && !std::strstr(w.body, "helper"));
  }
  answer = YG__START_ANSWER_GET;
  CHECK(yg_start(&options) == YG_START_OFFERED && opened_page == 2);
  // Asked on first use: at launch ASK_LATER never shows the window; the row press
  // (ASK_ON_USE) shows it, and Play offline is a plain DECLINED, never remembered.
  const int offers_before = offers;
  options.ask = YG_START_ASK_LATER;
  CHECK(yg_start(&options) == YG_START_MISSING && offers == offers_before);
  options.ask = YG_START_ASK_ON_USE;
  answer = YG__START_ANSWER_LATER;
  CHECK(yg_start(&options) == YG_START_DECLINED && offers == offers_before + 1 && last_on_use == 1);
  CHECK(yg_start(&options) == YG_START_DECLINED && offers == offers_before + 2);
  options.ask = YG_START_ASK_AT_START;
  CHECK(YG__START_OFFERS_OPEN(YG__START_TEXT_DIDNT_OPEN) && !YG__START_OFFERS_OPEN(YG__START_TEXT_UPDATE) &&
        !YG__START_OFFERS_OPEN(YG__START_TEXT_RAISED));
  char path[] = "/Applications/YouGame.app", *argv[8];
  yg__start_mac_open_argv(path, argv);
  CHECK(!std::strcmp(argv[0], "/usr/bin/open"));
  CHECK(!std::strcmp(argv[1], "-g") && !std::strcmp(argv[2], "-j"));
  CHECK(!std::strcmp(argv[3], "-a") && argv[4] == path);
  CHECK(!std::strcmp(argv[5], "--args") && !std::strcmp(argv[6], "--yougame-tray") && !argv[7]);
  // Raised (Windows, YouGame v2 6db35802e): a UAC full token always; another elevated token unless the
  // player's shell is the same user and elevated too (UAC off, Windows Sandbox); never a plain token.
  CHECK(yg__start_raised_rule(1, 1, 0) && yg__start_raised_rule(1, 1, 1) && yg__start_raised_rule(1, 0, 0));
  CHECK(yg__start_raised_rule(0, 1, 0));
  CHECK(!yg__start_raised_rule(0, 1, 1));
  CHECK(!yg__start_raised_rule(0, 0, 0) && !yg__start_raised_rule(0, 0, 1));
  return 0;
}
