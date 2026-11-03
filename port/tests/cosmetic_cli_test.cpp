// SPDX-License-Identifier: GPL-2.0-or-later
#include "cosmetic_mods.h"

#include <cstdio>

int main() {
  using namespace host::cosmetics;
  int failures = 0;
  const auto check = [&](bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
  };
  AssetInfo costume, effect, portrait;
  costume.target_path = "PlFxGr.dat";
  effect.target_path = "PlFx.dat"; effect.scope = "shine";
  portrait.target_path = "PlFxGr.dat#portrait";
  const std::vector<AssetInfo> catalog{costume, effect, portrait};
  check(cli::slot_key(costume) == "PlFxGr.dat", "unscoped slot key");
  check(cli::slot_key(effect) == "PlFx.dat#shine", "scoped slot key");
  check(cli::known_slot(catalog, "PlFxGr.dat"), "catalog costume slot accepted");
  check(cli::known_slot(catalog, "PlFx.dat#shine"), "catalog effect scope accepted");
  check(cli::known_slot(catalog, "PlFxGr.dat#portrait"), "portrait slot accepted");
  for (const auto* invalid : {"", "PlFxNr.dat", "PlFx.dat", "PlFx.dat#upb",
                              "PlFxGr.dat#", "plfxgr.dat", "../PlFxGr.dat"})
    check(!cli::known_slot(catalog, invalid), "unknown or inexact slot rejected");
  check(!cli::known_slot({}, "PlFxGr.dat"), "empty catalog rejects slot");
  check(cli::online_column("character_costume", false, true, "") ==
            " | online unknown (pass --iso)", "unknown online verdict");
  check(cli::online_column("character_costume", true, true, "3 joints match") ==
            " | online ok", "allowed online verdict");
  check(cli::online_column("character_costume", true, false, "bone count differs (4 instead of 3)") ==
            " | online off: bone count differs (4 instead of 3)", "refused online verdict");
  for (const auto* kind : {"stage_visual", "effect_visual", "character_portrait"})
    check(cli::online_column(kind, true, false, "reason").empty(), "other kinds keep their status line");
  return failures ? 1 : 0;
}
