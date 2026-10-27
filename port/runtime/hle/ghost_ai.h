// Pro AI: runs the network in ghost_ai.bin (made by the ghost-ai tools from each pro's games) for a
// practice CPU's fighter, one frame at a time. See port/runtime/abi/mu_ghost_ai.h.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../abi/mu_ghost_ai.h"

namespace ghost_ai {
void enable();                                     // the Source Port calls this; elsewhere the AI stays off
bool available();                                  // the file is beside the game and readable
bool act(const MuGhostObs& obs, MuGhostAct& out);  // false: no file, or a bad request
}  // namespace ghost_ai
