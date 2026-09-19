// The player's own Gecko codes, read from GeckoCodes.ini (Dolphin's format: a [Gecko] section of
// "$Name" headers followed by "XXXXXXXX YYYYYYYY" lines; a [Gecko_Enabled] section is honoured on
// first read). Each code is switched in the PC settings panel.
//
// WHAT CAN WORK HERE
// The game's code is translated to PC code ahead of time, so an instruction written into RAM is
// never executed. Codes that only write data (types 00, 02, 04 and 06 into RAM outside the game's
// code) work: they are applied every frame, as the Gecko handler does. Codes that patch the game's
// code (a write into .text, C2 injections, anything that runs PowerPC) are listed but cannot be
// switched on, with the reason shown. The codes Slippi ships, and the port's own (widescreen, PAL
// stock icons, screen shake), are translated in and do not come from this file.
//
// Every code changes the game, so an online match only stays in sync when both players run the
// same ones. The panel says so whenever any code is on.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace user_gecko {

struct Code {
  std::string name;
  std::vector<std::string> notes;   // "*" lines under the header
  std::vector<std::pair<uint32_t, uint32_t>> lines;
  bool supported = false;
  std::string reason;               // why not, when unsupported
  bool enabled = false;
};

// Reads the file (missing is fine: no codes). `enabled_names` are the codes saved as on in the
// settings file; until the panel has saved a choice (`chosen`), the file's [Gecko_Enabled] is used.
void load(const std::string& path, const std::vector<std::string>& enabled_names, bool chosen);
const std::string& path();
std::vector<Code>& codes();
bool any_enabled();
// Called once per game frame (from HLE(PADRead)) to apply the enabled codes' writes.
void apply();

}  // namespace user_gecko
