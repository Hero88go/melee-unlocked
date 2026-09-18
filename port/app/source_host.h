// The source port inside the full application (see source_host.cpp).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace source_port {
// Maps the game's memory where the console had it. Call first in main, before other allocations.
bool reserve_memory();
// Loads melee_game.dll and runs the game. `shutdown` runs the application's exit sequence when the
// game ends from inside (it cannot unwind through the game's frames), then the process exits.
int run(void (*shutdown)(int code));
}
