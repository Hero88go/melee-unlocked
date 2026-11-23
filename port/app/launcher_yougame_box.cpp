// YouGame's plugin box in the launcher (yougame_start.h, YouGame's native plugin, vendored in
// third_party/yougame_start): the "Sign in to YouGame" box with the sign-in code, drawn over the launcher's
// window while Connect waits for the player (launcher.cpp). Only yg_box_show, yg_box_poll and yg_box_close are
// used here; the start hook itself stays the game's (runtime/hle/yougame_link.cpp). Windows only: elsewhere the
// launcher writes the code in its own status line.
// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef _WIN32
#define YOUGAME_START_IMPLEMENTATION
#include "yougame_start/yougame_start.h"
#endif
