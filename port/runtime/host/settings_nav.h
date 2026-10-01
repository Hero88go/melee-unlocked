// Which controller drives the settings panel, and when the game gets its controllers back after a
// menu closes. Pure rules, no device access, so they are tested on their own.
//
// Both used to look at every connected controller every frame. The panel followed the first pad
// that was "active", and the game stayed neutral until all pads were at rest. A second controller
// that never rests (a drifting stick, a trigger resting on the desk, a held button) then took the
// panel away from the pad in the player's hands and kept the game neutral for good, while the
// Controls picture, which reads the device directly, still lit up.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstdlib>

namespace host::settings_nav {

struct Pad {
  bool connected = false;
  uint16_t button = 0;
  int8_t stick_x = 0, stick_y = 0;
};

inline constexpr int kStickActive = 30;
// Long enough for a thumb to leave the button that closed the menu, short enough that a pad which
// never rests cannot keep the game from its controllers.
inline constexpr float kReleaseTimeout = 1.0f;

inline bool active(const Pad& pad) {
  return pad.connected && (pad.button || std::abs(pad.stick_x) >= kStickActive || std::abs(pad.stick_y) >= kStickActive);
}

// The pad that drives the panel. It changes only when another pad goes from rest to active: a pad
// that is always active, or already active when it connects, never produces that edge, so it
// cannot hold the panel. `was_at_rest` is the caller's memory of the previous frame (connected and
// not active) and is updated on every call, open or closed.
inline int follow(const Pad pads[4], bool was_at_rest[4], int current, bool nav_open) {
  int picked = current < 0 || current > 3 ? 0 : current;
  bool moved = false;
  for (int i = 0; i < 4; ++i) {
    const bool now = active(pads[i]);
    if (nav_open && !moved && now && was_at_rest[i]) { picked = i; moved = true; }
    was_at_rest[i] = pads[i].connected && !now;
  }
  if (!pads[picked].connected)
    for (int i = 0; i < 4; ++i) if (pads[i].connected) { picked = i; break; }
  return picked;
}

// True while the game should still read neutral controllers after a menu closed: the pad that
// drove the menu has not come back to rest, some button is still down, and the timeout has not
// run out. Sticks and triggers of the other pads never hold the game back.
inline bool hold_release(const Pad pads[4], int nav_port, float seconds_since_close) {
  if (seconds_since_close >= kReleaseTimeout) return false;
  for (int i = 0; i < 4; ++i) {
    if (!pads[i].connected) continue;
    if (pads[i].button) return true;
    if (i == nav_port && active(pads[i])) return true;
  }
  return false;
}

}  // namespace host::settings_nav
