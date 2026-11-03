// Boot menu policy shared by the Source Port and Static Recomp.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace yougame::memcard {
// gmscmemcard.c: decision 5 asks to create a missing save; decision 7 acknowledges
// successful creation. Errors, formatting, existing saves and slot B need no answer.
constexpr bool confirm(bool active, unsigned channel, unsigned decision) {
  return active && channel == 0 && (decision == 5 || decision == 7);
}

// The menu builds these two messages before assigning decisions 5 and 7.
constexpr bool hide_message(bool active, unsigned channel, unsigned message) {
  return active && channel == 0 && (message == 0xA || message == 0xC);
}

// Installed after the Static Recomp's disc and dispatch table are ready.
void install_static_boot();
}  // namespace yougame::memcard
