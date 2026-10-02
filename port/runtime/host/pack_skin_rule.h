// Which files of a mod pack are costumes, and which entry the game opens for one online.
// SPDX-License-Identifier: GPL-2.0-or-later
//
// A mod pack loaded live changes disc files. Online modes that play the standard game open the
// standard copy of every changed file, with one exception: a costume file that only changes looks
// (same skeleton as the standard costume) stays on, because the other player never sees it and it
// does not move a hurtbox. The rule lives here, with no dependency on the game or the disc, so it
// is unit tested on its own.
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace source_port::skins {

// True for a fighter costume file: "plfxgr.dat" or "plcare.usd", with or without folders in front.
// The name is "Pl", a two-letter fighter code and a two-letter color code. Fighter data ("plfx.dat"),
// animation banks ("plfxaj.dat"), the common file ("plco.dat") and Kirby's copy hats
// ("plkbcpfx.dat") are not costumes. The path must already be lower case.
bool is_costume_file(const std::string& lower_path);
// The disc's own spelling of a costume file for messages: "/plfxgr.dat" gives "PlFxGr.dat".
// Any other path comes back without its folders, unchanged.
std::string display_name(const std::string& lower_path);

// What was decided for one costume file of a live pack. `reason` is the short text shown to the
// player and logged: "61 joints match", "rest pose differs", "same as the standard costume".
struct Verdict {
  std::string path, layer, reason;
  int32_t entry = -1;
  bool served = false;      // stays on in online modes that play the standard game
  bool identical = false;   // byte for byte the standard costume
};

// The entry number the game gets when it opens `entry`.
// Offline, or in an online mode that keeps the mod (retail_view false): the entry itself.
// In the standard view: a served costume keeps its own entry; any other changed file gives its
// standard copy from `view_alias` (-1 when the standard game has no such file); a file the pack
// did not change is not in `view_alias` and keeps its entry.
int32_t resolve_open(bool retail_view, int32_t entry,
                     const std::unordered_map<int32_t, int32_t>& view_alias,
                     const std::unordered_set<int32_t>& served_online);

}  // namespace source_port::skins
