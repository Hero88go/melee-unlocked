// Tap jump off: pushing the control stick up no longer jumps. X and Y still do, and up-tilt and
// up-smash are untouched.
//
// This is done to the pad, not to the game. Melee decides a tap jump from
//     lstick.y >= tap_jump_threshold && tilt_timer < tap_jump_window
// and an up-smash from the same two things plus A pressed on that frame (ftCo_Jump.c,
// ftCo_AttackHi4.c). A is the only thing separating them, and A is visible in the pad, so the stick
// can be held just under the threshold on every frame A is not pressed. Tap jump then cannot fire,
// up-tilt is unaffected because its threshold is far lower, and an up-smash still works because
// pressing A that frame lets the full value through.
//
// Doing it here rather than inside the game is what makes it safe online. The clamp is part of the
// input the game reads, so it is recorded in the replay and sent to the opponent exactly like a
// stick position the player chose, and both machines simulate the same thing. Changing the decision
// inside the game instead would leave the opponent's game still jumping, and desync both players.
// This is the same reason automatic L-cancel can be used online.
//
// The cost, and it is a real one: while A is not pressed the stick reads just under the tap jump
// threshold instead of full up, so upward drift and up-angled attacks are very slightly short of
// maximum. That is also roughly what a box controller does, since it simply never sends the
// coordinate that would jump.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace host { struct PadState; }

namespace tapjump {

void set_enabled(bool on);
bool enabled();

// Applies the clamp to the pads the game is about to read. Called from PADRead, upstream of
// everything the game does with the pad, alongside automatic L-cancel.
void apply(host::PadState pads[4]);

// What the game's own tables say the thresholds are, for the settings panel to show. Zero until the
// game has started and the tables exist.
float threshold_normalised();
int threshold_raw();

}  // namespace tapjump
