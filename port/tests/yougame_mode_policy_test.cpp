// Window-free checks of YouGame's spectator and mod-source boundaries.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_mode_policy.h"
#include "hle_card_filter.h"
#include "../runtime/gx/render_options.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

int main() {
  using namespace yougame::mode_policy;
  CHECK(hle::card::file_filter == nullptr);  // Standalone card tests and mounts need no app filter.
  CHECK(spectator_allowed(false, true));
  CHECK(!spectator_allowed(false, false));
  CHECK(!spectator_allowed(true, false));
  CHECK(!spectator_allowed(true, true));  // A stale saved 'on' cannot open the listener.
  // The crash dialog is for a player who started the game themselves; in the player box the app reports.
  CHECK(crash_dialog_allowed(false, false));
  CHECK(!crash_dialog_allowed(false, true));
  CHECK(!crash_dialog_allowed(true, false));
  CHECK(!crash_dialog_allowed(true, true));
  // The app shows the last stderr line of a stopped run: one line, registers cut, never a path.
  CHECK(crash_line("guest fault: interpreter jump outside RAM (C7B6B105) in TRK_flush_cache (80328F50); lr=80673588 r1=804EEA30") ==
        "crash: guest fault: interpreter jump outside RAM (C7B6B105) in TRK_flush_cache (80328F50)");
  CHECK(crash_line("cannot open C:\\Users\\someone\\melee.iso") == "crash: fatal error");
  CHECK(crash_line("cannot open /home/someone/melee.iso") == "crash: fatal error");
  CHECK(crash_line("") == "crash: fatal error");
  CHECK(crash_line(nullptr) == "crash: fatal error");
  CHECK(crash_line("two\nlines") == "crash: two");
  CHECK(crash_line(std::string(300, 'x').c_str()).size() == 7 + 160);

  for (bool yougame_mode : {false, true}) {
    for (bool explicit_args : {false, true}) {
      for (bool saved_profile : {false, true}) {
        for (std::size_t choices : {std::size_t(0), std::size_t(1), std::size_t(7)}) {
          const auto sources = mod_sources(yougame_mode, explicit_args, saved_profile, choices);
          CHECK(sources.explicit_args == explicit_args);  // Listing args survive saved state.
          CHECK(sources.saved_profile == (!yougame_mode && !explicit_args && saved_profile));
          CHECK(sources.folders == !yougame_mode);  // Includes newly discovered packs and Mods/Files.
          CHECK(sources.ignored_choices == (yougame_mode ? choices + (saved_profile ? 1 : 0) : 0));
        }
      }
    }
  }
  const auto clean = mod_sources(true);
  CHECK(!clean.explicit_args && !clean.saved_profile && !clean.folders && clean.ignored_choices == 0);
  // Auxiliary loaders (Gecko/replay code files, AI discs, boot discs, alternate game libraries
  // and test-only TE ownership)
  // must stay off even when a listing explicitly selected some other mod layers.
  for (bool explicit_args : {false, true}) {
    const auto listing = mod_sources(true, explicit_args, true, 3);
    CHECK(!listing.folders);
    CHECK(!listing.saved_profile);
    CHECK(listing.explicit_args == explicit_args);
    CHECK(listing.ignored_choices == 4);
  }
  // Card mount policy: ordinary saves always work, and explicitly imported mod saves retain their
  // private saved copy. A mod ISO/profile alone cannot authorize an ambient TE save.
  for (bool yougame_mode : {false, true}) {
    for (bool mod_save : {false, true}) {
      for (bool explicit_import : {false, true}) {
        CHECK(card_save_allowed(yougame_mode, mod_save, explicit_import) ==
              (!yougame_mode || !mod_save || explicit_import));
      }
    }
  }
  CHECK(!card_save_allowed(true, true));
  CHECK(card_save_allowed(true, false));
  CHECK(card_save_allowed(true, true, true));
  // Upstream owns the parent listing's feature gate. Suspending it must leave the saved choices
  // intact, and a subsequent remix launch must regain mod support without restoring ambient loaders.
  gx::RenderOptions saved;
  saved.mod_profile = "saved-profile";
  saved.mod_choices["te"] = 1;
  host::gameplay::vanilla_only_session = true;
  CHECK(!gx::RenderOptions::mod_features_available());
  CHECK(saved.mod_profile == "saved-profile" && saved.mod_choices.at("te") == 1);
  for (const char* arg : {"--mod-iso", "--mod-base-iso", "--mod-profile", "--mod-dir", "--mod-gci",
                          "--gecko-codes", "--te-options", "--te-options2", "--ram-translator", "--lag-test"})
    CHECK(host::gameplay::conflicts_with_vanilla(arg));
  for (const char* arg : {"--iso", "--settings-path", "--card-dir", "--no-mod-autodetect",
                          "--slippi-menus", "--online-test"})
    CHECK(!host::gameplay::conflicts_with_vanilla(arg));
  host::gameplay::vanilla_only_session = false;
  host::gameplay::mod_autodetect_disabled = true;
  CHECK(gx::RenderOptions::mod_features_available());  // Explicit remix imports remain available.
  CHECK(!mod_sources(true, true, true, 1).folders);
  CHECK(!card_save_allowed(true, true));
  host::gameplay::mod_autodetect_disabled = false;
  std::puts("yougame_mode_policy: spectator matrix and mod-source precedence/counts ok");
}
