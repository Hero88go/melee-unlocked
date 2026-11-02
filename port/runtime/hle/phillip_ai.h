// Phillip (slippi-ai by Vlad Firoiu, MIT): its published, already trained ONNX policies play the
// "Fight a pro" fighter. onnxruntime (MIT) runs them on the CPU from the ProAI folder beside the game.
// Each policy answers 18 frames late by design (it was trained with that reaction delay), so a step
// runs on a worker thread and the game thread only collects the answer it asked for 18 frames ago.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../abi/mu_ghost_ai.h"

namespace phillip_ai {
bool available(unsigned pro);                      // a policy for this pro is installed and onnxruntime loads
bool act(const MuGhostObs& obs, MuGhostAct& out);  // false: no policy for this pro, character or matchup
}  // namespace phillip_ai
