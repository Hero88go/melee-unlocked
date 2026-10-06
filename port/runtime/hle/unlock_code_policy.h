// Keep the unlock switch consistent for compiled and mod-disc code.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <vector>

namespace slippi::unlock_codes {
inline bool switched_address(uint32_t address) {
  switch (address) {
    case 0x8015EE98u: case 0x8015EDDCu: case 0x80164B14u: case 0x801648F4u:
    case 0x8015EE4Cu: case 0x8015EE14u: case 0x8015D968u: case 0x8015D9D8u:
    case 0x8017229Cu: case 0x801737B0u: case 0x80164658u: case 0x801644E8u:
    case 0x8030490Cu: case 0x803044F0u: case 0x8015D94Cu: case 0x8015D984u:
      return true;
    default: return false;
  }
}
inline uint32_t read_word(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
inline void write_word(uint8_t* p, uint32_t value) {
  p[0] = uint8_t(value >> 24); p[1] = uint8_t(value >> 16);
  p[2] = uint8_t(value >> 8); p[3] = uint8_t(value);
}
struct Install {
  uint32_t address, offset, installed, original;
};
// Every line keeps its position, so no C2 cave moves when unlocking is switched off.
inline void write_table(uint8_t* table, const std::vector<Install>& installs, bool on) {
  for (const auto& w : installs) {
    write_word(table + w.offset, on ? 0x04000000u | (w.address & 0x01FFFFFFu) : 0xE0000000u);
    write_word(table + w.offset + 4, on ? w.installed : 0u);
  }
}
template<class Read, class Write>
void change(const std::vector<Install>& installs, bool on, Read&& read, Write&& write) {
  for (const auto& w : installs) {
    // A disc can replace an instruction after boot. Restore only our own word;
    // the off state is the disc's captured instruction, never the retail one.
    if (read(w.address) == (on ? w.original : w.installed))
      write(w.address, on ? w.installed : w.original);
  }
}
}  // namespace slippi::unlock_codes
