// Test-only local input. No wall clock, RNG, device state or remote-client state.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifdef YOUGAME_AGENT_TEST_HOOK
#include "../abi/mu_host.h"
#include "yougame_agent_input.h"
#include <cmath>
#include <cstring>

namespace yougame::agent_test {
struct FightState {
  uint32_t frame = 0;
  float self_x = 0, self_y = 0, facing = 1;
  bool airborne = false;
  uint32_t action = 0;
  float self_percent = 0, opponent_x = 0, opponent_y = 0, opponent_percent = 0;
  float stage_edge = 68.4f;
};

inline MuPadStatus fight_input(const FightState& s) {
  MuPadStatus pad{};  // Connected; only A, B and the main stick can ever be emitted.
  const float dx = s.opponent_x - s.self_x;
  const int toward = dx > 0 ? 1 : dx < 0 ? -1 : (s.facing < 0 ? -1 : 1);
  if (s.airborne && (std::fabs(s.self_x) > s.stage_edge || s.self_y < -5)) {
    pad.stick_x = s.self_x > 0 ? -60 : s.self_x < 0 ? 60 : 0;
    pad.stick_y = 80;
    if (s.frame % 12 < 2) pad.button = 0x0200;  // Fresh up-B edges; drift/aim between them.
    return pad;
  }
  if (std::fabs(dx) > 16) {
    pad.stick_x = static_cast<int8_t>(80 * toward);
    return pad;
  }
  if (s.airborne) {
    pad.stick_x = static_cast<int8_t>(40 * toward);
    if (s.frame % 12 < 2) pad.button = 0x0100;
    return pad;
  }
  // Turn at walking strength before attacking; never dash into an accidental dash attack.
  if ((s.facing < 0 ? -1 : 1) != toward) {
    pad.stick_x = static_cast<int8_t>(40 * toward);
    return pad;
  }
  const uint32_t phase = s.frame % 240;
  if (phase < 48) {
    if (phase % 12 < 2) pad.button = 0x0100;  // Jab, release, repeat.
  } else if (phase < 96) {
    pad.stick_x = static_cast<int8_t>(40 * toward);
    if (phase % 24 == 6 || phase % 24 == 7) pad.button = 0x0100;  // Forward tilt.
  } else if (phase < 168) {
    if (phase >= 102 && phase < 140) pad.button = 0x0100;  // Hold A to charge, then release.
    if (phase == 102 || phase == 103) pad.stick_y = 80;    // Smash flick, no held-up jump.
  } else {
    if (phase == 174 || phase == 175) {
      pad.stick_y = -80;
      pad.button = 0x0100;  // Down smash, then enough neutral frames for its animation.
    }
  }
  return pad;
}

// The online harness chooses among the six legal stages (external stage ids). For an unknown
// stage, recover conservatively using Battlefield's edge. All stage floors here are at y = 0.
inline float fight_stage_edge(int stage) {
  switch (stage) {
    case 2: return 63.35f;  // Fountain of Dreams
    case 3: return 87.75f;  // Pokemon Stadium
    case 8: return 56.0f;   // Yoshi's Story
    case 28: return 77.27f; // Dream Land
    case 32: return 85.56f; // Final Destination
    default: return 68.4f;  // Battlefield (31)
  }
}

struct FightContext {
  bool enabled = false, yougame = false, online_test = false, in_match = false, replay = false;
  int local_slot = -1, local_port = -1;
  float stage_edge = 68.4f;
};
struct FightSample {
  int port = -1;
  MuPadStatus pad{};
};

enum class InputOwner { Devices, Script, Fight };
// A valid sample is supplied only by the online test match's PADRead scope.
inline constexpr InputOwner input_owner(bool script_active, bool fight_sample_valid) {
  return fight_sample_valid ? InputOwner::Fight : script_active ? InputOwner::Script : InputOwner::Devices;
}

inline float fight_float(uint32_t bits) {
  float value;
  std::memcpy(&value, &bits, sizeof value);
  return value;
}

// Use the native game's existing snapshot and airborne readers, never guest-memory offsets or
// recorded-event caches (which can lag or survive a rollback). Debug VS minor 1 is the harness match.
inline FightSample fight_sample(const FightContext& c, const MuStatePod& state, const MuLcancelView& air) {
  if (!c.enabled || !c.yougame || !c.online_test || !c.in_match || c.replay ||
      c.local_slot < 0 || c.local_slot >= 4 || c.local_port < 0 || c.local_port >= 4 ||
      state.scene_major != 0x0E || state.scene != 1 || !state.match_frame) return {};
  const auto& self = state.player[c.local_slot];
  if (!self.present || !self.stocks) return {};
  const MuFighterState* opponent = nullptr;
  for (int slot = 0; slot < 4; ++slot) {
    if (slot != c.local_slot && state.player[slot].present && state.player[slot].stocks) {
      if (opponent) return {};  // This policy is only for a duel.
      opponent = &state.player[slot];
    }
  }
  if (!opponent) return {};
  const MuLcancelFighter* fighter = nullptr;
  for (const auto& p : air.port) if (p.present && p.slot == c.local_slot) fighter = &p;
  if (!fighter) return {};
  FightState s{state.match_frame, fight_float(self.pos_x), fight_float(self.pos_y), fight_float(self.facing),
               fighter->ground_or_air != 0, self.action, fight_float(self.percent),
               fight_float(opponent->pos_x), fight_float(opponent->pos_y), fight_float(opponent->percent),
               c.stage_edge};
  if (!std::isfinite(s.self_x) || !std::isfinite(s.self_y) || !std::isfinite(s.facing) ||
      !std::isfinite(s.opponent_x) || !std::isfinite(s.opponent_y)) return {};
  return {c.local_port, fight_input(s)};
}

// main arms once before threads start. h_pad_read scopes this thread's sample to input_poll;
// other input consumers (settings-only windows, replay controls) see no active sample.
inline bool fight_enabled = false;
inline thread_local FightSample current_fight_sample;

template <class Pad, class Log>
inline bool apply_fight(Pad* pads, const FightSample& sample, const InputState& live, Log log) {
  if (!input_policy.allowed(live, InputKey::Fight, log)) return false;
  if (sample.port < 0 || sample.port >= 4) return false;
  auto& pad = pads[sample.port];
  pad = {};
  pad.button = sample.pad.button;
  pad.stick_x = sample.pad.stick_x;
  pad.stick_y = sample.pad.stick_y;
  return true;
}
}  // namespace yougame::agent_test
#endif
