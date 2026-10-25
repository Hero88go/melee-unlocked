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
struct Status { bool driving = false; int plan = 0; uint32_t frame = 0; };
Status status();
void note_status(bool driving, int plan, uint32_t frame);
}  // namespace practice_cpu
