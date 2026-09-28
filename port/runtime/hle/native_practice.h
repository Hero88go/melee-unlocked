// Native practice matchmaking coordinator. Renderers exchange snapshots/commands with it; only
// tick() touches Slippi networking or guest scene state, and tick() runs on the simulation thread.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "native_practice_model.h"

#include <cstdint>
#include <string>

namespace slippi::native_practice {

// Values intentionally match Slippi's OnlinePlayMode (Melee Unlocked has no Ranked).
enum class MatchMode : uint8_t { None = 0xFF, Unranked = 1, Direct = 2 };

struct Snapshot {
  Phase phase = Phase::Idle;
  MatchMode mode = MatchMode::None;
  bool in_practice = false;
  bool tab_available = false;
  bool can_start = false;
  bool cosmetic_profile_locked = false;
  int controller_port = 0;
  int matchmaking_state = 0;
  uint32_t generation = 0;
  uint32_t search_ticks = 0;
  std::string connect_code;
  std::string status;
  std::string detail;
  std::string opponent;
};

Snapshot snapshot();
void submit_start_unranked();
void submit_start_direct(const std::string& connect_code);
void submit_cancel();
void submit_acknowledge_failure();

// Called once per 60 Hz retrace, before the guest advances its frame.
void tick();
void shutdown();

// Narrow integration hook for cosmetic work: do not change the active cosmetic profile from the
// start of a search through online cleanup. No cosmetic implementation is required by this module.
bool cosmetic_profile_locked();
const char* match_mode_name(MatchMode mode);

// The Source Port game's practice operations (MuGameApi::practice, MU_PRACTICE_* in mu_host.h).
// Unset, the coordinator works on the static recomp's console memory.
using NativeBridge = int32_t (*)(int32_t op, int32_t* args, int32_t count);
void set_native_bridge(NativeBridge bridge);

}  // namespace slippi::native_practice
