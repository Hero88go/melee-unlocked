// The source port inside the full application (see source_host.cpp).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>

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
// Explicit loose-file packs, whose roots contain disc-relative filenames. May be repeated.
void set_mod_directory(const char* path);
// Import changed files from a user-provided patched ISO over the retail base disc.
// May be repeated; conflicting replacements stop startup with both profile names.
void set_mod_iso(const char* path);
// Mount a user-provided .gci in slot A. The source file stays untouched; card
// writes create a private copy under --card-dir. May be repeated.
void set_mod_gci(const char* path);
// Mods/Profiles/<name>.ini (or a path to an .ini): the profile's layers load first, in order.
bool set_mod_profile(const char* name);
// Content identity of the enabled mod layers; empty when none are enabled.
const std::string& mod_fingerprint();
// True once any --mod-profile/--mod-iso/--mod-dir/--mod-gci was given.
bool has_mod_layers();
// --replay <file.slp>: plays a Slippi replay natively (boots straight into the match, applies the
// recorded game start data and inputs where Slippi's playback codes do) and writes the played-back
// game as a new .slp to --replay-dir. Returns false when the file cannot be read.
bool set_replay(const char* path);
// --online-test mode[:character[:color]]: automated tests only. The game boots straight into an
// online match (no menus) found through --local-peer. Mode is direct, unranked or teams.
// Returns false for a malformed spec.
bool set_online_test(const char* spec);
// --slippi-menus on|off (default on): Slippi's online menus in the native game. The host serves the
// patched menu files and sets MU_GAME_OPT_SLIPPI_MENUS; forced off with --replay, --online-test,
// --vanilla-game or when the system files cannot be built.
void set_slippi_menus(bool on);
// Runs an isolated native card round-trip without booting an ISO. Intended for the M10 acceptance
// test; the caller must provide a new scratch directory.
bool card_self_test(const char* directory);
}
