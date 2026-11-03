// Source content views preserve standalone behavior and isolate YouGame Ranked/Unranked mods.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_online_policy.h"
#include "yougame_compatibility.h"
#include <cstdio>
#include <initializer_list>
#include <string>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main() {
  using namespace slippi::online;
  const std::string fp = "0123456789abcdef" + std::string(48, 'a');
  struct FingerprintCase { std::string value; bool valid; };
  const FingerprintCase fingerprints[] = {
    {fp, true}, {"", false}, {fp.substr(0, 63), false}, {fp + "0", false},
    {std::string(64, 'g'), false}, {std::string(64, 'A'), false}
  };

  // Full cross product, including every mode, two offline IDs and an unknown online ID.
  // Today's standalone table: no mod -> retail; offline -> mod; Direct -> mod iff
  // enabled and not a plain opponent; Ranked/Unranked/Teams/Party/unknown -> retail.
  int rows = 0;
  for (bool has_mod : {false, true})
    for (int mode : {-2, -1, 0, 1, 2, 3, 4, 5})
      for (bool yougame : {false, true})
        for (bool plain : {false, true})
          for (bool setting : {false, true})
            for (const auto& fingerprint : fingerprints) {
              const bool today = has_mod && (mode < 0 || (mode == 2 && !plain && setting));
              const bool added = has_mod && yougame && (mode == 0 || mode == 1) && fingerprint.valid;
              const bool actual = native_source_mod_view(has_mod, mode, yougame, plain, setting,
                                                         fingerprint.value);
              if (actual != (today || added)) {
                std::fprintf(stderr, "view mismatch: mod=%d mode=%d yougame=%d plain=%d setting=%d valid=%d\n",
                             has_mod, mode, yougame, plain, setting, fingerprint.valid);
                return 1;
              }
              if (!yougame) CHECK(actual == today);
              ++rows;
            }
  CHECK(rows == 768);

  // A Source mod view feeds both the room key and the full-fingerprint peer verdict.
  for (int mode : {0, 1}) {
    const bool mod_view = native_source_mod_view(true, mode, true, false, false, fp);
    CHECK(mod_view);
    const auto mod_key = yougame::compatibility_key("0.9.52", mod_view, fp);
    CHECK(mod_key == "mu-0.9.52-mod-0123456789abcdef");
    CHECK(mod_key != yougame::compatibility_key("0.9.52", false, ""));
    CHECK(!native_yougame_online_builds_match(mod_view, fp, false, ""));
    CHECK(native_yougame_online_builds_match(mod_view, fp, true, fp));
    // Matching room suffixes alone cannot bypass the full build check.
    std::string same_prefix = fp;
    same_prefix.back() = 'b';
    CHECK(mod_key == yougame::compatibility_key("0.9.52", true, same_prefix));
    CHECK(!native_yougame_online_builds_match(mod_view, fp, true, same_prefix));

    // Showing mod files alone never relaxes the fixed-rules roster/stage limits.
    CHECK(!native_yougame_online_mod_content(true, mode, mod_view, false, fp));
    CHECK(native_yougame_online_mod_content(true, mode, mod_view, true, fp));
    CHECK(!native_yougame_online_mod_content(true, mode, mod_view, true, ""));
  }
  std::printf("source_mod_view: %d decision rows, room isolation and content gates ok\n", rows);
}
