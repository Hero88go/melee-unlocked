// Habit ghosts: the practice CPU's odds from players' own games, made by tools/ghost_build.py from the
// per-game stats of Lunar Melee (lunarmelee.com). Do not edit by hand; run the tool again.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>

namespace practice_ghosts {

struct Ghost {
  const char* name;        // "Mang0"
  const char* character;   // "Falco"
  int kind;                // the character's internal fighter kind
  int games;               // games the habits come from
  int sampled;             // games read frame by frame (specials, DI, out of shield, after hitstun)
  uint8_t move[10], attack[20], defend[6], oos[10], di[6], tech[4], getup[5], ledge[6], after[5];
  uint8_t l_cancel, aggression;   // a group all zero: no data, the default stays
};

inline const Ghost kGhosts[] = {
    {"Mang0", "Falco", 22, 1200, 0,
     {3, 2, 15, 7, 4, 37, 9, 11, 11, 1}, {2, 1, 2, 2, 2, 1, 1, 0, 2, 0, 10, 1, 17, 2, 25, 16, 0, 0, 16, 0}, {27, 54, 3, 5, 5, 6}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0},
     {25, 15, 16, 44}, {30, 21, 28, 21, 0}, {0, 1, 4, 5, 72, 18}, {0, 0, 0, 0, 0}, 92, 59},
};

}  // namespace practice_ghosts
