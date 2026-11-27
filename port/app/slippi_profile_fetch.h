// The Slippi account's current display name and connect code from Slippi's user API (the launcher's
// account line; user.json is rewritten only when the player logs in to Slippi Launcher).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>

namespace launcher::slippi_profile {
// Blocking: call off the window thread. False when the server did not answer with a profile.
bool fetch(const std::string& uid, std::string* name, std::string* code);
}
