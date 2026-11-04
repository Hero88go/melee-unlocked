// The online menu's split layout (GitHub build), two pages of the same eight rows: Ranked, Unranked,
// Friends (YouGame) and Slippi; then Slippi Unranked, Slippi Direct, Slippi Teams, Slippi Party. The
// labels are images in MnMaAll; apply() rewrites them in the built file before the game reads it.
// The game's online menu (mu_slippi_menu.c) uses the split layout only when the host says the labels
// were rewritten.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace online_rows {
// Rewrites the online rows of a built MnMaAll.usd or .dat in place. False, with the file untouched,
// when it is not the menu file the rows were drawn for; `why` says what did not match.
// `coming_soon`: YouGame is not open yet, so its three rows get the Coming soon! sticker labels.
bool apply(std::vector<uint8_t>& file, std::string* why, bool coming_soon = false);
// The host rewrote the labels this run (set by the Source Port's system-file layer).
void set_applied(bool applied);
bool applied();
}  // namespace online_rows
