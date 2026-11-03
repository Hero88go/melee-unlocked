// The online menu's split layout (GitHub build): YouGame Ranked, YouGame Direct, Slippi Unranked,
// Slippi Direct, Teams, Party, Log in, Update. The labels are images in MnMaAll; apply() rewrites
// them in the built file before the game reads it. The game's online menu (mu_slippi_menu.c) uses
// the split layout only when the host says the labels were rewritten.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace online_rows {
// Rewrites the online rows of a built MnMaAll.usd or .dat in place. False, with the file untouched,
// when it is not the menu file the rows were drawn for; `why` says what did not match.
bool apply(std::vector<uint8_t>& file, std::string* why);
// The host rewrote the labels this run (set by the Source Port's system-file layer).
void set_applied(bool applied);
bool applied();
}  // namespace online_rows
