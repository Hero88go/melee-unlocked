// SPDX-License-Identifier: GPL-2.0-or-later
#include "replay_scan.h"

#include "slippilib/SlippiGame.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <array>

namespace replay_scan {
namespace {

using replay_coach::Event;
using replay_coach::Kind;

// Action states (the game's motion ids, ftCommon/forward.h).
constexpr int kWait = 14, kRun = 21, kJumpAerialF = 27, kFallAerialB = 34, kDamageFall = 38, kSquat = 39,
              kSquatWait = 40, kLandingAirN = 70, kLandingAirLw = 74, kGuardOn = 178, kGuard = 179,
              kGuardReflect = 182, kDownBoundU = 183, kDownBoundD = 191, kPassive = 199, kPassiveStandB = 201,
              kCapturePulledHi = 223, kCapturePulledLw = 226;

using Frame = Slippi::PlayerFrameData;

// State flags are the fighter's bytes 0x2218.. as the console holds them, first field in the top bit:
// hitlag 0x221A field 3 (0x10), hitstun 0x221C field 6 (0x02), can act early 0x2218 field 0 (0x80).
bool in_hitlag(const Frame& f) { return f.hitlag > 0.0f || (f.stateFlags[1] & 0x10) != 0; }
bool shielding(int m) { return m >= kGuardOn && m <= kGuardReflect; }

// mu_framedata.c hit_state: taking a hit (the attacker's own hitlag leaves it in its attack).
bool hit_state(int m) {
  return (m >= 75 && m <= 91) || m == 156 || m == 157 || m == 185 || m == 193 || m == 225 || m == 228 || m == 325;
}

// mu_framedata.c can_act: an interruptible animation or a free state.
bool can_act(const Frame& f) {
  if (in_hitlag(f) || (f.stateFlags[3] & 0x02)) return false;
  if (f.stateFlags[0] & 0x80) return true;
  const int m = f.postAction;
  return (m >= kWait && m <= kRun) || (m >= kJumpAerialF && m <= kFallAerialB) || m == kSquat || m == kSquatWait ||
         m == kDamageFall || m == kGuard;
}

struct Track {
  bool prev_hitlag = false;
  int prev_action = -1;
  bool active = false;
  Kind kind = Kind::Hit;
  int attacker = -1, frame = 0, count = 0, attacker_at = -1, defender_at = -1;
  int move = 0, hitlag = 0, to_land = -1, lcancel = 0;
  bool airborne = false;
  bool attacker_hit = false;   // the attacker was itself taking a hit when the reading began
};

Event make(Kind kind, int32_t frame, int actor, int target, const Slippi::FrameData& fd) {
  Event e;
  e.kind = kind;
  e.frame = frame;
  e.actor = actor;
  e.target = target;
  auto a = fd.players.find((uint8_t)actor);
  if (a != fd.players.end()) {
    e.actor_percent = (int)a->second.postPercent;
    e.actor_character = a->second.internalCharacterId;
  }
  auto t = target >= 0 ? fd.players.find((uint8_t)target) : fd.players.end();
  if (t != fd.players.end()) {
    e.target_percent = (int)t->second.postPercent;
    e.target_character = t->second.internalCharacterId;
  }
  return e;
}

}  // namespace

std::vector<Event> scan(Slippi::SlippiGame& game) {
  std::vector<Event> out;
  std::array<Track, 4> tracks;
  const int32_t last = game.GetLatestIndex();
  for (int32_t f = Slippi::GAME_FIRST_FRAME; f <= last; ++f) {
    Slippi::FrameData* fd = game.GetFrame(f);
    if (!fd) continue;
    // The game's tracker sees a frame's result at the next frame's input read: its frame number.
    const int32_t seen = f + 1;
    for (auto& kv : fd->players) {
      const int port = kv.first;
      if (port > 3 || !kv.second.hasPost) continue;
      const Frame& d = kv.second;
      Track& t = tracks[(size_t)port];
      const int m = d.postAction;
      const bool hitlag = in_hitlag(d);

      // Aerial landings, with the game's own L-cancel result (reported in the landing frame).
      if (m >= kLandingAirN && m <= kLandingAirLw && !(t.prev_action >= kLandingAirN && t.prev_action <= kLandingAirLw)) {
        Event e = make(Kind::Landing, f, port, -1, *fd);
        e.move = t.prev_action >= 65 && t.prev_action <= 69 ? t.prev_action : m - 5;
        e.lcancel = d.lCancel;
        out.push_back(e);
      }
      if (m != t.prev_action) {
        if (m >= kPassive && m <= kPassiveStandB) {
          Event e = make(Kind::Tech, seen, port, -1, *fd);
          e.advantage = 1;
          e.move = m;
          out.push_back(e);
        } else if (m == kDownBoundU || m == kDownBoundD) {
          Event e = make(Kind::Tech, seen, port, -1, *fd);
          e.move = m;
          out.push_back(e);
        } else if (m == kCapturePulledHi || m == kCapturePulledLw) {
          int by = -1;
          float best = 0;
          for (auto& o : fd->players) {
            if (o.first == port || o.first > 3) continue;
            const float dx = o.second.postX - d.postX, dy = o.second.postY - d.postY, dist = dx * dx + dy * dy;
            if (by < 0 || dist < best) { by = o.first; best = dist; }
          }
          if (by >= 0) {
            Event e = make(Kind::Grab, seen, by, port, *fd);
            e.move = fd->players[(uint8_t)by].postAction;
            out.push_back(e);
          }
        }
      }
      t.prev_action = m;

      if (hitlag && !t.prev_hitlag && (shielding(m) || hit_state(m))) {
        const bool shield = shielding(m);
        int by = -1;
        if (!shield && d.postLastHitBy < 4 && d.postLastHitBy != port && fd->players.count(d.postLastHitBy)) {
          by = d.postLastHitBy;
        } else {
          // A shield hit has no damage source: the nearest other fighter frozen in its own hitlag.
          float best = 0;
          for (auto& o : fd->players) {
            if (o.first == port || o.first > 3 || !in_hitlag(o.second) || shielding(o.second.postAction)) continue;
            const float dx = o.second.postX - d.postX, dy = o.second.postY - d.postY, dist = dx * dx + dy * dy;
            if (by < 0 || dist < best) { by = o.first; best = dist; }
          }
        }
        t.active = by >= 0;
        if (t.active) {
          const Frame& a = fd->players[(uint8_t)by];
          t.kind = shield ? Kind::Shield : Kind::Hit;
          t.attacker = by;
          t.frame = seen;
          t.count = 0;
          t.attacker_at = t.defender_at = -1;
          t.move = a.postAction;
          t.hitlag = (int)d.hitlag;
          t.airborne = a.airborne != 0;
          t.to_land = -1;
          t.lcancel = 0;
        }
      }
      t.prev_hitlag = hitlag;
      if (!t.active) continue;
      auto ai = fd->players.find((uint8_t)t.attacker);
      t.count++;
      if (ai == fd->players.end() || m < kWait || ai->second.postAction < kWait) {
        t.active = false;   // a KO or respawn: no reading
        continue;
      }
      const Frame& a = ai->second;
      // MELEE_TEST_SCAN_TRACE=<frame> (tests only): each step of the reading that starts at that frame.
      static const int trace_frame = [] { const char* v = std::getenv("MELEE_TEST_SCAN_TRACE"); return v ? std::atoi(v) : -1000; }();
      if (t.frame == trace_frame)
        std::printf("scan trace %d: step %d att act %d hl %.0f f %02X %02X %02X %02X can %d | def act %d hl %.0f f %02X %02X %02X %02X can %d\n",
                    t.frame, t.count, a.postAction, a.hitlag, a.stateFlags[0], a.stateFlags[1], a.stateFlags[2], a.stateFlags[3],
                    can_act(a), m, d.hitlag, d.stateFlags[0], d.stateFlags[1], d.stateFlags[2], d.stateFlags[3], can_act(d));
      if (t.airborne && t.to_land < 0 && !a.airborne) {
        t.to_land = t.count;
        t.lcancel = a.lCancel;
      }
      if (t.attacker_at < 0 && !in_hitlag(a) && can_act(a)) t.attacker_at = t.count;
      // Punished as its lag ended: the game saw it free the frame before the hit (the file's end-of-frame
      // state never shows that frame), so a missed L-cancel read -127 here where the game read -12.
      if (t.count == 1) t.attacker_hit = hit_state(a.postAction);   // a trade: no punish rule
      if (t.attacker_at < 0 && !t.attacker_hit && t.count > 1 && hit_state(a.postAction) && in_hitlag(a)) t.attacker_at = t.count - 1;
      if (t.defender_at < 0 && !hitlag && can_act(d)) t.defender_at = t.count;
      if (t.attacker_at >= 0 && t.defender_at >= 0) {
        t.active = false;
        Event e = make(t.kind, t.frame, t.attacker, port, *fd);
        e.advantage = t.defender_at - t.attacker_at;
        e.move = t.move;
        e.lcancel = t.lcancel;
        e.to_land = t.airborne ? t.to_land : -1;
        e.hitlag = t.hitlag;
        e.free_at = t.defender_at;
        out.push_back(e);
      } else if (t.count > 300) {
        t.active = false;
      }
    }
  }
  std::stable_sort(out.begin(), out.end(), [](const Event& a, const Event& b) { return a.frame < b.frame; });
  return out;
}

}  // namespace replay_scan
