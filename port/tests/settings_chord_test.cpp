#include "settings_chord.h"
#include <cstdlib>

static void expect(bool value) { if (!value) std::abort(); }

// The settings chord is Start + D-pad Down + Z (0x1000 | 0x0004 | 0x0010).
int main() {
  bool held = false;
  uint16_t buttons = 0;
  expect(!host::settings_shortcut::poll_z_start(true, buttons, held));
  buttons = 0x0010;
  expect(!host::settings_shortcut::poll_z_start(true, buttons, held));
  buttons = 0x1000;
  expect(!host::settings_shortcut::poll_z_start(true, buttons, held));
  buttons = 0x1010;   // the old Z + Start pair alone must not open the panel any more
  expect(!host::settings_shortcut::poll_z_start(true, buttons, held));
  expect(buttons == 0x1010);
  buttons = 0x1014 | 0x0100;
  expect(host::settings_shortcut::poll_z_start(true, buttons, held));
  expect(buttons == 0x0100);  // The shortcut never sends Start, Down or Z to the game/UI.
  buttons = 0x1014;
  expect(!host::settings_shortcut::poll_z_start(true, buttons, held));
  expect(buttons == 0);
  buttons = 0;
  expect(!host::settings_shortcut::poll_z_start(true, buttons, held));
  buttons = 0x1014;
  expect(host::settings_shortcut::poll_z_start(true, buttons, held));
  expect(buttons == 0);
  buttons = 0x1014;
  expect(!host::settings_shortcut::poll_z_start(false, buttons, held));
  expect(buttons == 0x1014);
  // Releasing Down and Z first must not leak a lone Start (which closes the panel).
  held = false;
  buttons = 0x1014;
  expect(host::settings_shortcut::poll_z_start(true, buttons, held));
  buttons = 0x1000;
  expect(!host::settings_shortcut::poll_z_start(true, buttons, held));
  expect(buttons == 0);
  buttons = 0;
  expect(!host::settings_shortcut::poll_z_start(true, buttons, held));
  buttons = 0x1000;   // a fresh Start after full release reaches the UI
  expect(!host::settings_shortcut::poll_z_start(true, buttons, held));
  expect(buttons == 0x1000);
  return 0;
}
