// The replay coach's events read straight from a replay file: every hit, shield hit, aerial landing,
// tech and grab, found from the frames' recorded results in well under a second, so a replay's tips and
// timeline markers are there as soon as it loads. The rules are the game's own (sourceport/game/shim/
// mu_framedata.c) applied to the recorded state; the game's live numbers replace these as the moments play.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "replay_coach.h"
#include <vector>

namespace Slippi { class SlippiGame; }

namespace replay_scan {

// Events in frame order. Empty for a file without frame results (Slippi before 0.2.0).
std::vector<replay_coach::Event> scan(Slippi::SlippiGame& game);

}  // namespace replay_scan
