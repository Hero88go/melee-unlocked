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
    {"Mang0", "Falco", 22, 4813, 0,
     {2, 2, 17, 7, 4, 36, 9, 11, 11, 1}, {2, 1, 2, 2, 2, 0, 1, 0, 2, 0, 10, 2, 18, 2, 26, 15, 0, 0, 15, 0}, {27, 52, 4, 5, 5, 7}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0},
     {26, 16, 15, 43}, {29, 23, 27, 21, 0}, {0, 3, 4, 6, 67, 20}, {0, 0, 0, 0, 0}, 92, 58},
    {"Zain", "Marth", 18, 4731, 0,
     {2, 2, 40, 5, 3, 25, 6, 8, 8, 1}, {1, 0, 5, 6, 3, 0, 0, 1, 15, 0, 9, 21, 2, 9, 3, 7, 1, 0, 17, 0}, {27, 55, 4, 4, 4, 6}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0},
     {22, 11, 15, 52}, {13, 33, 26, 28, 0}, {3, 3, 4, 3, 83, 4}, {0, 0, 0, 0, 0}, 93, 78},
    {"Hungrybox", "Jigglypuff", 15, 1316, 0,
     {2, 2, 2, 6, 3, 50, 12, 11, 11, 1}, {1, 0, 1, 0, 1, 1, 0, 1, 6, 0, 6, 15, 33, 9, 4, 8, 1, 0, 13, 0}, {27, 55, 4, 4, 4, 6}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0},
     {17, 7, 9, 67}, {25, 32, 29, 14, 0}, {1, 4, 26, 4, 60, 5}, {0, 0, 0, 0, 0}, 96, 95},
    {"Cody Schwab", "Fox", 1, 4037, 0,
     {2, 2, 46, 5, 3, 23, 6, 6, 6, 1}, {4, 0, 5, 4, 1, 4, 0, 2, 7, 0, 11, 2, 17, 11, 10, 8, 0, 0, 14, 0}, {27, 48, 8, 5, 5, 7}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0},
     {26, 12, 13, 49}, {23, 19, 30, 28, 0}, {1, 2, 3, 5, 47, 42}, {0, 0, 0, 0, 0}, 92, 65},
};

}  // namespace practice_ghosts
