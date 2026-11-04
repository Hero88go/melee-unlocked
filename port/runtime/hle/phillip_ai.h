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

// "Stronger Pro AI (Requires additional download)": Phillip's newest policies, about 90 MB each, are
// downloaded only when the player says so. Ask: the Practice menu asks when a pro with them is picked.
// Always: each is downloaded the first time its matchup is played. Never: only the lighter ones play.
enum StrongerChoice { kStrongerAsk = 0, kStrongerAlways = 1, kStrongerNever = 2 };
void set_stronger_choice(int choice);
int stronger_choice();
// One pro's stronger policies (pro < 0: every pro's): how many there are, how many are not
// downloaded yet and their size, and whether a download is running.
struct StrongerStatus { int total = 0, missing = 0; double missing_mb = 0; bool downloading = false; };
StrongerStatus stronger_status(int pro);
// Downloads every missing stronger policy of that pro (pro < 0: every pro), one after another in the
// background, each checked against its SHA-256. They play from the next match on.
void download_stronger(int pro);
}  // namespace phillip_ai
