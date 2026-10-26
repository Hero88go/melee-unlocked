// The replay viewer's controller layer (practice from a replay, after Tekken 8's replays): one row of
// GameCube buttons along the bottom while a replay plays. A takes over the player named top left (D-pad
// Up/Down picks another), X and Y jump to the previous and next tip, D-pad Left/Right go 5 seconds,
// Z pauses (D-pad Right then steps a frame), R fast-forwards, Start opens the settings. Tips show as a
// banner at their moment and as markers on a timeline. Taken over, a LIVE badge counts down; Start
// retries from the takeover point, held it goes back to the replay.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace practice_menu { struct Frame; }

namespace replay_hud {

// UI thread, once per overlay frame while a replay plays. `f` carries the game picture's rectangle, the
// controller and the time (its `open` is ignored); `style` is the practice menu look for the settings.
void frame(int style, const practice_menu::Frame& f);

}  // namespace replay_hud
