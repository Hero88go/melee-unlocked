// The replay viewer's bar: a strip along the bottom of the picture while a replay plays, shown
// while the mouse moves, a control is used, or playback is paused or jumping. Current time and
// total time, a progress line that can be clicked to jump there, the playback state, and the keys.
// The controls themselves are host/replay_control.h; this only draws and sends clicks.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace replay_bar {

// UI thread, between ImGui::NewFrame and ImGui::Render, once per presented frame. `menu_open`: the
// settings panel, the Esc menu or the practice overlay has the keyboard and the pads; the bar is
// not drawn then and the viewer's keys do nothing.
void draw(float width, float height, bool menu_open);
// The bar is up because the mouse moved: the pointer is shown so the progress line can be clicked.
bool wants_cursor();
// A replay is playing: the viewer's keys are in use (Tab is fast forward, not the practice overlay).
bool active();

}  // namespace replay_bar

// A short label near the bottom of the picture for a moment (the stage skin just picked on the
// stage select screen). Any thread may set it; the UI thread draws it.
namespace screen_label {
void show(const char* text, double seconds);
void draw(float width, float height);
}  // namespace screen_label
