// SPDX-License-Identifier: GPL-2.0-or-later
#include "pack_skin_rule.h"
#include <cstdio>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
  if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
}

int main() {
  using source_port::skins::is_costume_file;
  using source_port::skins::resolve_open;

  // Costume files: "pl" + fighter + color, .dat or the English .usd, with or without a folder.
  struct Case { const char* path; bool costume; };
  const Case cases[] = {
      {"plfxgr.dat", true},        // Fox, green
      {"/plfxnr.dat", true},       // the way the pack table spells a root file
      {"/plcare.usd", true},       // Captain Falcon's red costume, English copy
      {"files/plmswh.dat", true},
      {"mods\\plpknr.dat", true},
      {"plfx.dat", false},         // fighter data
      {"plfxaj.dat", false},       // animation bank
      {"/plzdaj.dat", false},
      {"plco.dat", false},         // common fighter file
      {"plkbcpfx.dat", false},     // Kirby's copy hat
      {"plkbnrcpfx.dat", false},
      {"grnba.dat", false},        // a stage
      {"effxdata.dat", false},
      {"plfxgr.png", false},
      {"plfxgr.dat.bak", false},
      {"plfxgr", false},
      {"plfx1r.dat", false},       // not a letter code
      {"xlfxgr.dat", false},
      {"", false},
      {"/", false},
  };
  for (const Case& item : cases)
    check(is_costume_file(item.path) == item.costume, item.path);

  using source_port::skins::display_name;
  check(display_name("/plfxgr.dat") == "PlFxGr.dat", "a costume is named the way the disc spells it");
  check(display_name("plcare.usd") == "PlCaRe.usd", "the English copy keeps its extension");
  check(display_name("/audio/us/fox.ssm") == "fox.ssm", "another file is named without its folders");

  // Entry 10: a costume the pack serves online. Entry 11: a changed stage with a standard copy at
  // 40. Entry 12: a file only the pack has. Entry 13: a file the pack did not change.
  const std::unordered_map<int32_t, int32_t> alias{{10, 39}, {11, 40}, {12, -1}};
  const std::unordered_set<int32_t> served{10};
  check(resolve_open(true, 10, alias, served) == 10, "a served costume keeps its own entry in the standard view");
  check(resolve_open(true, 11, alias, served) == 40, "another changed file opens its standard copy");
  check(resolve_open(true, 12, alias, served) == -1, "a file only the pack has does not exist in the standard view");
  check(resolve_open(true, 13, alias, served) == 13, "an unchanged file keeps its entry");
  for (int32_t entry = 10; entry <= 13; ++entry)
    check(resolve_open(false, entry, alias, served) == entry, "offline every file is the pack's own");
  check(resolve_open(true, 10, alias, {}) == 39, "a costume that is not served opens the standard costume");
  check(resolve_open(true, 13, {}, {}) == 13, "with no pack nothing is redirected");

  if (failures) std::fprintf(stderr, "%d pack skin rule test(s) failed\n", failures);
  else std::puts("pack skin rule tests passed");
  return failures ? 1 : 0;
}
