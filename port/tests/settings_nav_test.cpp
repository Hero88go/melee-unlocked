#include "settings_nav.h"
#include <cstdlib>

static void expect(bool value) { if (!value) std::abort(); }

using host::settings_nav::Pad;

int main() {
  // Port 1 at rest, port 3 with a stick that never rests: the panel stays on port 1.
  Pad pads[4] = {{true, 0, 0, 0}, {}, {true, 0, 60, 0}, {}};
  bool rest[4] = {};
  int port = 0;
  for (int frame = 0; frame < 5; ++frame) port = host::settings_nav::follow(pads, rest, port, true);
  expect(port == 0);

  // Port 1 presses A: it is followed, and stays followed after the release.
  pads[0].button = 0x0100;
  port = host::settings_nav::follow(pads, rest, port, true);
  expect(port == 0);
  pads[0].button = 0;
  for (int frame = 0; frame < 5; ++frame) port = host::settings_nav::follow(pads, rest, port, true);
  expect(port == 0);

  // A pad that goes from rest to active takes the panel; one already active when it connects does not.
  pads[2] = {true, 0, 0, 0};
  port = host::settings_nav::follow(pads, rest, port, true);
  pads[2].stick_y = -80;
  port = host::settings_nav::follow(pads, rest, port, true);
  expect(port == 2);
  pads[3] = {true, 0x0200, 0, 0};
  port = host::settings_nav::follow(pads, rest, port, true);
  expect(port == 2);

  // With the panel closed nothing moves, but the memory is kept current.
  pads[0].button = 0x0100;
  port = host::settings_nav::follow(pads, rest, port, false);
  expect(port == 2);
  port = host::settings_nav::follow(pads, rest, port, true);
  expect(port == 2);   // already held when the panel opened: no edge

  // The followed pad unplugged: the first connected one.
  pads[2] = {};
  port = host::settings_nav::follow(pads, rest, port, true);
  expect(port == 0);

  // Giving the game its controllers back.
  Pad idle[4] = {{true, 0, 0, 0}, {}, {true, 0, 0, 0}, {}};
  expect(!host::settings_nav::hold_release(idle, 0, 0.0f));
  Pad closing[4] = {{true, 0x0100, 0, 0}, {}, {true, 0, 0, 0}, {}};
  expect(host::settings_nav::hold_release(closing, 0, 0.1f));       // A that closed the menu is still down
  Pad stick[4] = {{true, 0, 0, -90}, {}, {}, {}};
  expect(host::settings_nav::hold_release(stick, 0, 0.1f));         // the menu pad's stick is still down
  Pad drift[4] = {{true, 0, 0, 0}, {}, {true, 0, 70, 0}, {}};
  expect(!host::settings_nav::hold_release(drift, 0, 0.0f));        // another pad's stick never holds the game
  Pad stuck[4] = {{true, 0, 0, 0}, {}, {true, 0x0400, 0, 0}, {}};
  expect(host::settings_nav::hold_release(stuck, 0, 0.5f));         // a held button does, for a moment
  expect(!host::settings_nav::hold_release(stuck, 0, host::settings_nav::kReleaseTimeout));
  Pad none[4] = {};
  expect(!host::settings_nav::hold_release(none, 0, 0.0f));
  return 0;
}
