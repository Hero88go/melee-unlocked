// No window, game, network or guest memory: exercise the production L-cancel decisions.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lcancel_policy.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main() {
  using namespace lcancel::policy;
  for (int mode : {-1, 0, 1, 2, 3, 4, 5}) {
    for (bool saved : {false, true}) {
      const bool expected = saved && (mode == -1 || mode == 2);
      CHECK(automatic_allowed(mode, saved) == expected);
      CHECK(!automatic_allowed(mode, saved, true));
      CHECK(automatic_setting_locked(mode) == (mode == 0));
      for (bool requested : {false, true}) {
        const bool after = automatic_after_toggle(mode, saved, requested);
        CHECK(after == (mode == 0 ? saved : requested));
        if (mode == 0) CHECK(!automatic_allowed(mode, after));
      }
    }
  }
  // A set can refuse repeated attempts without losing the preference for the next casual session.
  for (bool saved : {false, true}) {
    bool choice = saved;
    for (bool requested : {true, false, true}) choice = automatic_after_toggle(0, choice, requested);
    CHECK(choice == saved);
    CHECK(!automatic_allowed(0, choice));
    CHECK(automatic_allowed(-1, choice) == saved);
    CHECK(automatic_allowed(2, choice) == saved);
  }
  CHECK(std::strcmp(auto_suppressed_mode(0), "Ranked") == 0);
  CHECK(std::strcmp(auto_suppressed_mode(3), "Teams") == 0);
  CHECK(std::strcmp(ranked_start_log(true), "ranked: auto L-cancel off (enforced; saved setting on)") == 0);
  CHECK(std::strcmp(ranked_start_log(false), "ranked: auto L-cancel off (enforced; saved setting off)") == 0);
  CHECK(std::strcmp(ranked_hint, "Auto L-cancel: off (enforced in Ranked).") == 0);
  std::puts("L-cancel mode/setting matrix, ranked toggle refusal and notices passed");
}
