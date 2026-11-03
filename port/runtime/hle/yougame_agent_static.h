// Static Recomp's staging harness: pure snapshot adapter and online event counting.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifdef YOUGAME_AGENT_TEST_HOOK
#include "yougame_agent_fight.h"
#include "online_test_quit.h"
#include <atomic>
#include <optional>

namespace yougame::agent_test {
// digest_state's MuStatePod carries vanilla NTSC 1.02 Player/Fighter fields. m-ex keeps these
// for vanilla fighters; an added fighter or a relocated player table may need another reader.
// Scene ids differ from Source's Debug VS harness, so the EXI start/end interval gates Static.
inline std::optional<FightState> static_fight_state(const FightContext& c, const MuStatePod& digest,
                                                   const MuLcancelView& air) {
  if (!c.enabled || !c.yougame || !c.online_test || !c.in_match || c.replay ||
      c.local_slot < 0 || c.local_slot >= 4 || c.local_port < 0 || c.local_port >= 4 ||
      !digest.match_frame) return std::nullopt;
  const auto& self = digest.player[c.local_slot];
  if (!self.present || !self.stocks) return std::nullopt;
  const MuFighterState* opponent = nullptr;
  for (int slot = 0; slot < 4; ++slot) {
    if (slot == c.local_slot || !digest.player[slot].present || !digest.player[slot].stocks) continue;
    if (opponent) return std::nullopt;  // Duel only.
    opponent = &digest.player[slot];
  }
  const auto& fighter = air.port[c.local_port];
  if (!opponent || !fighter.present || fighter.slot != c.local_slot) return std::nullopt;
  FightState s{digest.match_frame, fight_float(self.pos_x), fight_float(self.pos_y), fight_float(self.facing),
               fighter.ground_or_air != 0, self.action, fight_float(self.percent),
               fight_float(opponent->pos_x), fight_float(opponent->pos_y), fight_float(opponent->percent),
               c.stage_edge};
  if (!std::isfinite(s.self_x) || !std::isfinite(s.self_y) || !std::isfinite(s.facing) ||
      !std::isfinite(s.self_percent) || !std::isfinite(s.opponent_x) || !std::isfinite(s.opponent_y) ||
      !std::isfinite(s.opponent_percent) || !std::isfinite(s.stage_edge) || s.stage_edge <= 0)
    return std::nullopt;
  return s;
}

enum class StaticEvent { GameStart, GameEnd, Report, Cancel };

class StaticMatch {
 public:
  StaticMatch() = default;
  StaticMatch(bool armed, const char* after) : armed_(armed), quit_(0, armed ? after : nullptr) {}

  template<class Leave>
  void event(StaticEvent e, bool eligible, Leave leave) {
    if (!armed_ || !eligible || e == StaticEvent::Cancel) {
      in_match_ = ended_ = false;
      return;
    }
    if (quit_.triggered()) return;
    switch (e) {
      case StaticEvent::GameStart:
        if (!in_match_) { in_match_ = true; ended_ = false; }
        break;
      case StaticEvent::GameEnd:
        if (in_match_) { in_match_ = false; ended_ = true; }
        break;
      case StaticEvent::Report:
        if (!ended_) break;
        ended_ = false;
        ++finished_;
        quit_.poll(finished_, leave);  // Count once, after Static publishes the game's result.
        break;
      case StaticEvent::Cancel: break;
    }
  }
  bool in_match() const { return in_match_; }
  bool armed() const { return armed_; }
  int finished() const { return finished_; }
  bool quitting() const { return quit_.triggered(); }

 private:
  bool armed_ = false, in_match_ = false, ended_ = false;
  int finished_ = 0;
  yougame::online_test::QuitAfter quit_;
};

// Armed on main before the simulation starts; events and samples stay on its thread.
inline StaticMatch static_match;
// close_room can also run on matchmaking's worker; never read simulation-owned counters there.
inline std::atomic<bool> static_quit_requested{false};

// The room pump owns result delivery. Keep it alive until its queue settles, with the same
// 30-second ceiling as the Ranked harness; the retiring worker exits after closing the room.
template<class Pending, class Clock, class Pause>
inline bool drain_static_results(Pending pending, Clock clock, Pause pause) {
  const uint64_t start = clock();
  while (pending()) {
    if (clock() - start >= 30000) return false;
    pause(50);
  }
  return true;
}
}  // namespace yougame::agent_test
#endif
