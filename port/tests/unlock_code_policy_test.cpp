// Runtime unlock toggles preserve each mod's own unlock/message instructions.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "unlock_code_policy.h"
#include <array>
#include <cstdio>
#include <map>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
  using namespace slippi::unlock_codes;
  CHECK(switched_address(0x8017229Cu) && switched_address(0x8015D984u));
  CHECK(!switched_address(0x80172298u));
  // A mod already suppresses its pending-message result. Off must retain that
  // zero rather than restoring retail's li r3,1 and generating another notice.
  const std::vector<Install> installs{{0x8017229Cu, 8, 0x38600000u, 0x38600000u},
                                    {0x8015D984u, 24, 0x4E800020u, 0x7C0802A6u}};
  std::map<uint32_t, uint32_t> memory{{installs[0].address, installs[0].original},
                                      {installs[1].address, installs[1].original}};
  auto read = [&](uint32_t a) { return memory.at(a); };
  auto write = [&](uint32_t a, uint32_t w) { memory[a] = w; };
  std::array<uint8_t, 40> table{};
  table[16] = 0xC2; table[32] = 0xFF;
  for (int round = 0; round < 3; ++round) {
    change(installs, true, read, write); write_table(table.data(), installs, true);
    CHECK(memory.at(installs[0].address) == 0x38600000u);
    CHECK(memory.at(installs[1].address) == 0x4E800020u);
    CHECK(read_word(table.data() + 8) == 0x0417229Cu);
    change(installs, false, read, write); write_table(table.data(), installs, false);
    CHECK(memory.at(installs[0].address) == 0x38600000u);
    CHECK(memory.at(installs[1].address) == 0x7C0802A6u);
    CHECK(read_word(table.data() + 8) == 0xE0000000u && read_word(table.data() + 12) == 0);
    CHECK(table[16] == 0xC2 && table[32] == 0xFF);
  }
  memory[installs[1].address] = 0x48001234u;
  change(installs, true, read, write);
  CHECK(memory.at(installs[1].address) == 0x48001234u);
  std::puts("mod unlock toggles preserve the disc and cave layout");
}
