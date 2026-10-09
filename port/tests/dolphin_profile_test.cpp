// Dolphin controller profiles: keyboard and XInput layouts read into this project's bindings.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "dolphin_profile.h"
#include <cstdio>

namespace {
int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)
// BindAction order.
enum { A, B, X, Y, Z, Start, L, R, DUp, DDown, DLeft, DRight, CUp, CDown, CLeft, CRight, SUp, SDown, SLeft, SRight, LAnalog, RAnalog };
}

int main() {
  // An XInput pad, as Dolphin writes it: names in backticks, sticks on their axes.
  const host::DolphinProfile pad = host::dolphin_profile_read(
      "[Profile]\nDevice = XInput/0/Gamepad\nButtons/A = `Button A`\nButtons/B = `Button X`\nButtons/X = `Button B`\n"
      "Buttons/Y = `Button Y`\nButtons/Z = `Shoulder R`\nButtons/Start = Start\nMain Stick/Up = `Left Y+`\n"
      "Main Stick/Down = `Left Y-`\nC-Stick/Up = `Right Y+`\nTriggers/L = `Trigger L`\nTriggers/R = `Trigger R`\n"
      "Triggers/L-Analog = `Trigger L`\nTriggers/R-Analog = `Trigger R`\nD-Pad/Up = `Pad N`\nD-Pad/Left = `Pad W`\n"
      "Main Stick/Modifier = `Thumb L`\nRumble/Motor = `Motor L` | `Motor R`\n");
  CHECK(pad.ok && pad.device == host::ProfileDevice::XInput);
  CHECK(pad.bindings[A] == 0x1000 && pad.bindings[B] == 0x4000 && pad.bindings[X] == 0x2000 && pad.bindings[Y] == 0x8000);
  CHECK(pad.bindings[Z] == 0x0200 && pad.bindings[Start] == 0x0010);
  CHECK(pad.bindings[L] == 0x0400 && pad.bindings[R] == 0x0800 && pad.bindings[LAnalog] == 0x0400 && pad.bindings[RAnalog] == 0x0800);
  CHECK(pad.bindings[DUp] == 0x0001 && pad.bindings[DLeft] == 0x0004 && pad.bindings[DDown] == 0);
  // The analog stick axes are this project's to read: neither bound nor counted as not understood.
  CHECK(pad.bindings[SUp] == 0 && pad.bindings[CUp] == 0 && pad.skipped == 0);
  CHECK(pad.bound == 12);

  // The keyboard: plain key names, a qualified name, and bindings that are left alone.
  const host::DolphinProfile keys = host::dolphin_profile_read(
      "[Profile]\r\nDevice = DInput/0/Keyboard Mouse\r\nButtons/A = X\r\nButtons/B = Z\r\nButtons/Start = RETURN\r\n"
      "Main Stick/Up = UP\r\nMain Stick/Left = `DInput/0/Keyboard Mouse:LEFT`\r\nC-Stick/Up = NUMPAD8\r\n"
      "Triggers/L = LSHIFT\r\nButtons/Z = F5\r\nButtons/X = `A` | `S`\r\nButtons/Y = `Click 0`\r\n");
  CHECK(keys.ok && keys.device == host::ProfileDevice::Keyboard);
  CHECK(keys.bindings[A] == 'X' && keys.bindings[B] == 'Z' && keys.bindings[Start] == 0x0D);
  CHECK(keys.bindings[SUp] == 0x26 && keys.bindings[SLeft] == 0x25 && keys.bindings[CUp] == 0x68);
  CHECK(keys.bindings[L] == 0xA0 && keys.bindings[Z] == 0x74);
  CHECK(keys.bindings[X] == 0 && keys.bindings[Y] == 0 && keys.skipped == 2 && keys.bound == 8);

  // Not readable: another kind of device, a file that is not a profile, a profile with nothing known.
  const host::DolphinProfile sdl = host::dolphin_profile_read("[Profile]\nDevice = SDL/0/Xbox One Controller\nButtons/A = `Button S`\n");
  CHECK(!sdl.ok && sdl.message.find("SDL/0/Xbox One Controller") != std::string::npos);
  CHECK(!host::dolphin_profile_read("backend d3d12\nvolume 40\n").ok);
  CHECK(!host::dolphin_profile_read("Device = XInput/0/Gamepad\nButtons/A = `Button Q`\n").ok);
  std::string name;
  CHECK(!host::dolphin_profile_read_file("no-such-folder/My Pad.ini", &name).ok && name == "My Pad");

  if (g_failures) { std::printf("%d failure(s)\n", g_failures); return 1; }
  std::printf("dolphin profiles: ok\n");
  return 0;
}
