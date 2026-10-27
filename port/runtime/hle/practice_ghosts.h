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
    {"Mang0", "Falco", 22, 10000, 0,
     {2, 2, 18, 7, 4, 35, 9, 11, 11, 1}, {2, 1, 2, 2, 2, 0, 1, 0, 2, 0, 10, 2, 18, 2, 26, 15, 0, 0, 15, 0}, {27, 52, 4, 5, 5, 7}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0},
     {26, 16, 15, 43}, {27, 23, 28, 22, 0}, {0, 3, 4, 7, 66, 20}, {0, 0, 0, 0, 0}, 92, 57},
    {"Zain", "Marth", 18, 10000, 0,
     {2, 2, 40, 5, 3, 25, 6, 8, 8, 1}, {1, 0, 5, 6, 3, 0, 0, 1, 15, 0, 9, 21, 2, 9, 3, 7, 1, 0, 17, 0}, {27, 55, 4, 4, 4, 6}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0},
     {23, 11, 15, 51}, {12, 33, 26, 29, 0}, {3, 3, 4, 3, 83, 4}, {0, 0, 0, 0, 0}, 93, 79},
    {"Hungrybox", "Jigglypuff", 15, 1316, 0,
     {2, 2, 2, 6, 3, 50, 12, 11, 11, 1}, {1, 0, 1, 0, 1, 1, 0, 1, 6, 0, 6, 15, 33, 9, 4, 8, 1, 0, 13, 0}, {27, 55, 4, 4, 4, 6}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0},
     {17, 7, 9, 67}, {25, 32, 29, 14, 0}, {1, 4, 26, 4, 60, 5}, {0, 0, 0, 0, 0}, 96, 95},
    {"Cody Schwab", "Fox", 1, 10000, 0,
     {2, 2, 47, 5, 3, 22, 6, 6, 6, 1}, {4, 0, 5, 4, 1, 4, 0, 2, 7, 0, 11, 2, 19, 11, 10, 7, 0, 0, 13, 0}, {27, 48, 8, 5, 5, 7}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0},
     {26, 12, 13, 49}, {24, 19, 29, 28, 0}, {1, 2, 3, 5, 49, 40}, {0, 0, 0, 0, 0}, 92, 64},
};

}  // namespace practice_ghosts
