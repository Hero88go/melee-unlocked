// Lab view: draws the match the way Slippi Lab (github.com/frankborden/slippilab) draws a replay,
// flat character silhouettes on a plain stage, instead of the game's 3D scene.
//
// It needs nothing from the renderer. The Slippi recording codes already hand the EXI device the
// full state of every frame (the same events a .slp is made of), so this watches those events,
// keeps the latest frame, and draws it with Dear ImGui over the game image. It only reads: nothing
// here touches guest memory or the simulation, so it cannot desync a netplay match, and the player
// on the other end sees and runs exactly what they would otherwise.
//
// The silhouettes are Slippi Lab's own animation frames, converted by tools/build_lab_assets.py
// into Lab\<internal character id>.lab. Without them the view still draws the stage, items and
// HUD, with a plain marker where each character is.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>

namespace lab {

// Simulation thread: one Slippi recording event, command byte first, exactly as the EXI device
// received it. Cheap; copies the fields the view uses and returns.
void feed(const uint8_t* event, uint32_t size);

// Render thread, between ImGui::NewFrame and ImGui::Render. Covers the window when enabled and a
// match is in progress; does nothing otherwise, so menus and the character select stay normal.
void draw(bool enabled, float width, float height);

// True while a match is running and the view has a frame to show.
bool match_in_progress();

// Render thread: whether the most recent draw() covered the window. The backends ask this after
// the settings panel has run for the frame (which is where draw() is called) and before they
// replay the game's draws, so they can skip a 3D scene nobody will see. It is the decision draw()
// actually made, not a fresh check, so the scene and the cover can never disagree for a frame.
bool covering();

}  // namespace lab
