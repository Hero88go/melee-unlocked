// Staging-only matchmaking policy for scripts supplied through agent-test.json.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifdef YOUGAME_AGENT_TEST_HOOK
#include "yougame_agent_input.h"
namespace yougame::agent_test {
inline bool agent_script = false;  // Set once on main, before the matchmaking worker starts.

inline constexpr bool matchmaking_allowed(bool local_peer, bool server_allowed,
                                           bool agent_script, bool yougame_active, int mode) {
  // An agent script can never reach local peering or Slippi, even with explicit CLI overrides.
  return agent_script ? !ranked_mode(mode) && yougame_active && !local_peer
                      : local_peer || server_allowed;
}
}  // namespace yougame::agent_test
#endif
