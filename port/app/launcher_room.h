// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "launcher_room_state.h"
#include <functional>
namespace launcher::room {
struct Callbacks {
  std::function<std::string(int)> available; // Empty when the selected version can launch.
  std::function<void(int, const std::string&, bool, const std::string&)> launch;
  std::function<void()> changed;
  std::string version, directory;
};
void init(Callbacks callbacks);
void open(const std::string& code, int variant, bool public_room);
void start();
void invite();
void leave();
void shutdown();
State current();
}
