// The source port inside the full application (see source_host.cpp).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace source_port {
// Maps the game's memory where the console had it. Call first in main, before other allocations.
bool reserve_memory();
// Loads melee_game.dll and runs the game. `shutdown` runs the application's exit sequence when the
// game ends from inside (it cannot unwind through the game's frames), then the process exits.
int run(void (*shutdown)(int code));

// --match <stage>:<p1>[:<p2>...], each player <kind>[/c<level>][/x<costume>]. The sweep uses this
// so it does not have to drive the character and stage screens by cursor position for every
// combination. The menus still run; only the resulting match is forced. Returns false on a
// malformed spec, which main reports and treats as a usage error.
bool set_match(const char* spec);
// Runs an isolated native card round-trip without booting an ISO. Intended for the M10 acceptance
// test; the caller must provide a new scratch directory.
bool card_self_test(const char* directory);
}
