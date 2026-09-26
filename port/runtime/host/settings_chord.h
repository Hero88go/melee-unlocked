// Edge detection and input suppression for the in-game settings shortcut.
#pragma once
#include <cstdint>

namespace host::settings_shortcut {

// The chord is Start + D-pad Down + Z: three buttons nobody presses together in play, so it never
// fires by accident (Z + Start alone did, and it also sat on the input overlay's Z + Start test).
inline bool poll_z_start(bool connected, uint16_t& buttons, bool& was_down) {
  constexpr uint16_t chord = 0x1000u | 0x0004u | 0x0010u;  // GameCube Start + D-pad Down + Z
  const bool down = connected && (buttons & chord) == chord;
  const bool pressed = down && !was_down;
  // Latched until every chord button is up: releasing Down and Z before Start used to hand the
  // UI a lone Start press, and Start closes the panel, so the menu shut the moment it opened.
  if (pressed) was_down = true;
  else if (was_down && (!connected || (buttons & chord) == 0)) was_down = false;
  if (connected && was_down) buttons &= static_cast<uint16_t>(~chord);
  return pressed;
}

}  // namespace host::settings_shortcut
