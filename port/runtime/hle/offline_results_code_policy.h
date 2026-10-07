// Upgrade older embedded results caves without moving their offsets or branches.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "unlock_code_policy.h"
#include <cstddef>

namespace slippi::results_codes {
constexpr uint32_t kSaveNext = 0x5085442Eu, kRestoreNext = 0x579BC63Eu;
// Upgraded prebuilt functions run these caves from RAM. Change the restore
// instruction with the live setting, preserving handler-written return branches.
inline bool set_restore_enabled(uint8_t* table, size_t size, bool enabled) {
  bool changed = false;
  for (size_t off = 8; off + 8 <= size;) {
    const uint32_t a = unlock_codes::read_word(table + off), b = unlock_codes::read_word(table + off + 4);
    const uint32_t type = (a >> 24) & 0xFEu;
    uint64_t span = 8;
    if (type == 0xC0 || type == 0xC2) span += uint64_t(b) * 8;
    else if (type == 0x06) span += (uint64_t(b) + 7) & ~uint64_t(7);
    else if (type == 0x08) span = 16;
    else if ((a >> 28) == 0xF && b == 0) break;
    if (span > size - off) break;
    if (a == 0xC21A5B18u && b == 3 && unlock_codes::read_word(table + off + 8) == 0x2C1B0000u &&
        unlock_codes::read_word(table + off + 12) == 0x40820008u &&
        unlock_codes::read_word(table + off + 20) == 0x881F0064u) {
      const uint32_t word = unlock_codes::read_word(table + off + 16);
      const uint32_t want = enabled ? kRestoreNext : 0x60000000u;
      if ((word == kRestoreNext || word == 0x60000000u) && word != want) {
        unlock_codes::write_word(table + off + 16, want); changed = true;
      }
    }
    off += size_t(span);
  }
  return changed;
}
inline bool upgrade(uint8_t* table, size_t size) {
  bool changed = false;
  for (size_t off = 8; off + 8 <= size;) {
    const uint32_t a = unlock_codes::read_word(table + off), b = unlock_codes::read_word(table + off + 4);
    const uint32_t type = (a >> 24) & 0xFEu;
    uint64_t span = 8;
    if (type == 0xC0 || type == 0xC2) span += uint64_t(b) * 8;
    else if (type == 0x06) span += (uint64_t(b) + 7) & ~uint64_t(7);
    else if (type == 0x08) span = 16;
    else if ((a >> 28) == 0xF && b == 0) break;
    if (span > size - off) break;
    if (a == 0xC21A5B00u && b == 2 && unlock_codes::read_word(table + off + 8) == 0x3B640000u &&
        unlock_codes::read_word(table + off + 12) == 0x90810008u) {
      unlock_codes::write_word(table + off + 12, kSaveNext); changed = true;
    }
    if (a == 0xC21A5B18u && b == 3 && unlock_codes::read_word(table + off + 8) == 0x2C1B0000u &&
        unlock_codes::read_word(table + off + 12) == 0x40820008u &&
        unlock_codes::read_word(table + off + 16) == 0x83610008u &&
        unlock_codes::read_word(table + off + 20) == 0x881F0064u) {
      unlock_codes::write_word(table + off + 16, kRestoreNext); changed = true;
    }
    off += size_t(span);
  }
  return changed;
}
}  // namespace slippi::results_codes
