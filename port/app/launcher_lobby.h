// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
#include <string>

namespace launcher::lobby {
struct Match { std::string id, code; int character = 2; std::string build, opponent; };
void set_account(const std::string& name, const std::string& code);
void init(HWND owner, const std::string& directory);
void open(const std::string& build, bool can_play);
void hide();
bool take_match(Match& match);
void game_running(bool running);
void shutdown();
void refresh_theme();
}
