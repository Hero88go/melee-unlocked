// Decoding one input report from a Sony pad (DualShock 4 or DualSense), apart from the Raw Input
// plumbing that delivers it, so the byte layouts can be tested without the hardware.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>

namespace host {

struct PadState;

// Fills `pad` (sticks and analog triggers) and `buttons` (DS4_* bits from input_bindings.h).
// `dualsense` says which pad sent it: the two share report IDs but not always a layout. Returns false
// for a report too short to hold the fields, leaving the outputs untouched.
bool playstation_parse(const uint8_t* report, size_t size, bool dualsense, PadState& pad, uint16_t& buttons);

}  // namespace host
