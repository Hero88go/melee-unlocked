// Lists the costumes of every mod disc the Mods folder scan found as skins (cosmetic_mods.h).
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Both engines call this once at boot, after the Mods folder scan and before the skin selections
// are applied. A mod disc's costumes then show up in the skin list of the normal game, where the
// usual online rule applies to them, whichever engine the disc itself needs. With no mod disc in
// the Mods folder this does nothing: no read, no log line, no file.
#pragma once
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include "cosmetic_mods.h"
#include "host.h"
#include "mod_scan.h"

namespace source_port::mods {

// `open_disc` is the disc the game compares against (the player's own Melee disc). A mod disc that
// is itself the open disc is skipped: compared with itself it has no changed costume.
inline void scan_detected_disc_skins(const std::filesystem::path& open_disc) {
  namespace fs = std::filesystem;
  const PanelView view = panel_view();
  std::string listed;
  for (const auto& d : view.result.items) {
    if (d.kind != Kind::TmCe && d.kind != Kind::AssetMod && d.kind != Kind::Mex && d.kind != Kind::HackPack &&
        d.kind != Kind::CodeMod) continue;
    std::error_code ec;
    if (!open_disc.empty() && fs::equivalent(d.path, open_disc, ec)) continue;
    const std::string name = (d.name.empty() ? d.path.stem().u8string() : d.name) + " disc";
    const auto result = host::cosmetics::scan_disc_skins(d.path.u8string(), name);
    if (!result.ok) { host::log("mods: skins of %s not listed: %s", d.path.u8string().c_str(), result.message.c_str()); continue; }
    // The sets the disc has ("alt L,alt R"), so the launcher can name them.
    std::string sets;
    const std::string key = fs::absolute(d.path, ec).u8string();
    for (const auto& pack : host::cosmetics::packs()) {
      if (pack.key != key) continue;
      for (const auto& variant : pack.variants) if (!variant.empty()) sets += (sets.empty() ? "" : ",") + variant;
    }
    listed += key + "\t" + std::to_string(host::cosmetics::disc_skin_count(d.path.u8string())) + "\t" + sets + "\n";
  }
  if (listed.empty()) return;
  // For the launcher's Mods page, which does not load the skin catalog: disc path, tab, skin count,
  // tab, the alternate sets by name.
  std::error_code ec;
  const fs::path cache = mods_folder() / ".cache";
  fs::create_directories(cache, ec);
  std::ofstream out(cache / "disc-skins.txt", std::ios::binary | std::ios::trunc);
  out << listed;
}

}  // namespace source_port::mods
