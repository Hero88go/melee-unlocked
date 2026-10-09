// Reads a Dolphin GameCube controller profile (User/Config/Profiles/GCPad/<name>.ini) into this
// project's bindings, so a layout made in Dolphin does not have to be rebuilt button by button.
//
// Read: the keyboard ("DInput/0/Keyboard Mouse") and XInput pads ("XInput/0/Gamepad"), a single
// control per action. Left alone, and counted: expressions (`A | B`, modifiers), SDL and other
// device kinds, and a pad's analog stick axes, which this project reads directly.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include "controller_profiles.h"

namespace host {

struct DolphinProfile {
  bool ok = false;                 // a known device kind, and at least one action read
  ProfileDevice device = ProfileDevice::Count;
  ProfileBindings bindings{};      // 0 = left unbound
  int bound = 0, skipped = 0;      // actions read, and bindings present but not understood
  std::string message;             // why not, when !ok
};

// `text` is the profile file's content.
DolphinProfile dolphin_profile_read(const std::string& text);
// The same from a file (UTF-8 path); `name` gets the file's name without its extension.
DolphinProfile dolphin_profile_read_file(const std::string& path, std::string* name);
// A file dialog for a Dolphin profile (*.ini). Empty when the player cancels.
std::string dolphin_profile_choose_file();

}  // namespace host
