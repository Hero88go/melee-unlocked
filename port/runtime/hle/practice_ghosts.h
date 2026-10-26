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
    {"Zain", "Marth", 18, 1200, 0,
     {2, 2, 38, 5, 3, 26, 7, 8, 8, 1}, {1, 0, 5, 5, 3, 0, 0, 1, 15, 0, 9, 21, 2, 9, 4, 8, 1, 0, 16, 0}, {27, 56, 4, 3, 3, 7}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0},
     {21, 11, 16, 52}, {13, 34, 25, 28, 0}, {3, 3, 4, 3, 82, 5}, {0, 0, 0, 0, 0}, 93, 79},
    {"Hungrybox", "Jigglypuff", 15, 1200, 0,
     {2, 2, 2, 5, 3, 50, 12, 12, 11, 1}, {1, 0, 1, 0, 1, 1, 0, 1, 6, 0, 6, 15, 34, 9, 4, 8, 1, 0, 12, 0}, {27, 55, 4, 4, 4, 6}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0},
     {17, 7, 9, 67}, {24, 33, 28, 15, 0}, {1, 4, 25, 4, 61, 5}, {0, 0, 0, 0, 0}, 96, 95},
    {"Cody Schwab", "Fox", 1, 1200, 0,
     {2, 2, 46, 5, 3, 23, 6, 6, 6, 1}, {4, 0, 5, 4, 1, 4, 1, 2, 9, 0, 10, 2, 17, 11, 10, 6, 0, 0, 14, 0}, {27, 48, 8, 5, 5, 7}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0},
     {26, 12, 12, 50}, {21, 21, 29, 29, 0}, {1, 2, 3, 4, 47, 43}, {0, 0, 0, 0, 0}, 91, 66},
};

}  // namespace practice_ghosts
