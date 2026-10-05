// The --p2p-* command line of the build without the Slippi layer: one peer-to-peer rollback session
// (runtime/mu_net) wired to the game's command channel (see source_p2p.cpp). --p2p-games <n> plays
// up to n games over the one connection (0: until a player quits); without it the session is one
// game. Game n after the first writes its result beside --p2p-result as "<name>.g<n><extension>".
// The Source Port build with the Slippi layer links this too: there the options pick this session
// over Slippi's for the run, and the process ends when the session has nothing more (exit code 2
// when no game was played, 0 after one), because that game library has no scene to return to.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>

namespace source_p2p {
// One "--p2p-<name> <value>" pair from the command line. False for an unknown name or a malformed
// value; the reason is printed to stderr.
bool option(const std::string& name, const char* value);
// True once both --p2p-port and --p2p-peer were given: the game is to enter a match at boot.
bool requested();
// After the whole command line is read: false (reason on stderr) when a match was requested and the
// options do not describe one.
bool check();
// Before source_port::run. Loads the identity, starts the session and asks the game to boot into
// the match. On a failure it logs the reason and leaves the game to boot normally. `harness` marks
// an automated run (hidden or scripted), which lets the game honor its test-only settings.
void start(bool harness);
// At exit: writes the result file once more (the other side's agreement may have arrived since the
// game ended) and closes the session. Safe to call when nothing was started.
void shutdown();
}  // namespace source_p2p
