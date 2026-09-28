// Training Lab overlay (Source Port, Training mode): per player, the action and its frame, percent,
// speed, and an input display of the controller the game read that frame.
//
// The simulation thread publishes what the game read and the fighters' state once per game frame
// (the same record the state digests use); the UI thread draws the latest copy. Nothing here writes
// game state.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include "../abi/mu_host.h"

namespace training_overlay {

// Simulation thread, once per game frame, only while the overlay is wanted.
void publish(const MuStatePod& state, const MuPadStatus pads[4]);
// UI thread, between ImGui::NewFrame and ImGui::Render. Draws when enabled and the game is in
// Training mode (or any match, for scripted checks: MELEE_LAB_ANY_MODE).
void draw(bool enabled, float width, float height);
// The game's latest frame advantage reading (player 1 vs the dummy); kind 1 on hit, 2 on shield.
void set_advantage(int frames, int kind);
// 20XX TE "Input display": each player's controller in any offline match or replay, nothing else.
void set_input_display(bool on);
bool input_display();
// The common action name for an action id ("" when it is a character's own action).
const char* action_name(uint32_t action);

}  // namespace training_overlay
