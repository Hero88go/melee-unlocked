// "Network and timing" overlay (Overlays tab, off by default): a strip graph of the last 10 seconds
// of an online match as this player got it. Frame time per simulation tick, red blocks where the
// game waited for the other player's inputs, rollbacks with their depth, time sync sheds and
// advances, and the ping. A Slippi replay shows none of this.
//
// The records come from the online command path (host/net_trace.h); the UI thread draws a copy.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace net_overlay {

// The setting (ini key `netoverlay`). Any thread.
void set_enabled(bool on);
bool enabled();
// UI thread, between ImGui::NewFrame and ImGui::Render, once per presented frame whether the
// overlay is on or not (it also counts presented frames for the trace). Draws while an online
// match is feeding records. MELEE_TEST_NET_OVERLAY=1 draws it with sample data, for screenshots.
void draw(float width, float height);
// A replay shown as it was played (host/replay_trace.h feeds the ring from the session trace file
// beside the replay): the overlay is drawn whatever the setting says, its records are never taken
// for a finished match, and the title says what it is. Any thread.
void set_replay(bool on);
// A line under the legend for `seconds`: the true length of a freeze the viewer shortened. Any thread.
void set_replay_note(const char* text, double seconds);

}  // namespace net_overlay
