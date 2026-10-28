// Practice CPU (port/runtime/abi/mu_practice_cpu.h): the settings the menu edits, and the Static
// Recomp's side of it. The Source Port's game asks for the settings through a host command
// (source_host.cpp) and runs the same model in its own code (shim/mu_practice_cpu.c).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../abi/mu_practice_cpu.h"

#include <cstdint>
#include <string>

namespace practice_cpu {
MuPcConfig config();                       // a copy of the current settings
void set_config(const MuPcConfig& c);
uint32_t generation();                     // changes whenever the settings do
// Settings file form: one hex string of the whole struct (version first). False when it does not fit.
std::string to_text(const MuPcConfig& c);
bool from_text(const std::string& text, MuPcConfig& c);
// Static Recomp: drive the Training CPU from the model (a hook on Fighter_procInput).
void install_static();
// What the CPU is doing, for the Practice page (both engines report it).
struct Status { bool driving = false; int plan = 0; uint32_t frame = 0; int kind = -1; };
Status status();
void note_status(bool driving, int plan, uint32_t frame, int kind = -1);
// The player's own setups (Presets page): three slots saved with the settings.
constexpr int kSlots = 3;
bool slot(int index, MuPcConfig& out);
void set_slot(int index, const MuPcConfig& c);
// A habit ghost (practice_ghosts.h): the defaults with what that player's games measured in place.
// False for an index past the list.
bool ghost(int index, MuPcConfig& out);
// Source Port test selection must happen after the neural runtime is enabled, not in static init.
void initialize_source_test_ghost();
// The special moves' names for a character (FighterKind), for the attack rows; nullptr if unknown.
const char* special_name(int kind, int which);   // which: 0 neutral, 1 side, 2 up, 3 down
}  // namespace practice_cpu
