// Discord Rich Presence and game invites, spoken directly to the Discord client's documented local
// IPC socket (https://docs.discord.com/developers/topics/rpc). No Discord SDK is linked and no
// Discord binary is redistributed: the Social SDK and the legacy Game SDK are closed-source DLLs
// whose terms forbid the redistribution and modification GPL-2 requires us to allow, so this port
// implements the published protocol itself over a named pipe. See scratchpad/discord_invite_design.md.
//
// Off unless the player turns it on. With the setting off no thread exists, no pipe is opened and
// nothing is published. Everything that touches Discord runs on one dedicated thread; the
// simulation and render threads only ever copy a small struct under an uncontended mutex.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>

namespace host::discord {

// What to show on the player's Discord profile. Built by the caller, handed over by value.
struct Presence {
  std::string details;     // first line, e.g. "Direct match"
  std::string state;       // second line, e.g. "vs GUEST"
  // The LOCAL player's Slippi connect code, published as the Discord join secret so a friend who
  // clicks Join learns which code to enter. Empty means no Join button. Never an IP address: a
  // presence is public, and a connect code carries no network location (see the design document).
  std::string join_code;
  int party_size = 0;      // 0 = show no party; Discord greys Join out at size == max
  int party_max = 0;
};

// The Discord application id (a snowflake from discord.com/developers/applications). Empty means
// the feature cannot run; nothing is baked into the binary.
void configure(const std::string& application_id);

// Starts or stops the presence thread. enable(false) clears the presence and joins the thread.
void enable(bool on);

// Cheap enough to call per frame: one relaxed atomic load when the feature is off.
bool enabled();

// Replaces the presence Discord should show. Safe from any thread, never does I/O, and returns
// immediately when the feature is off. Updates are coalesced and rate limited on the worker.
void publish(const Presence& p);

// Blanks the presence (keeps the connection).
void clear();

// One short line for the settings panel. Safe from the render thread.
std::string status();

// A Slippi connect code that arrived from a friend's Discord invite, or "" when there is none.
// Returned once and then forgotten. Validated as a connect code before it ever gets here.
std::string take_join_code();

// Stops the thread. Safe to call when it never started. Call before process exit.
void shutdown();

}  // namespace host::discord
