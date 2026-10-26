// SPDX-License-Identifier: GPL-2.0-or-later
#include "replay_coach.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdio>
#include <map>
#include <mutex>

namespace replay_coach {
namespace {

std::mutex g_mutex;
std::map<uint64_t, Event> g_events;   // by frame, then kind and players
Event g_latest;
bool g_have_latest = false;
std::chrono::steady_clock::time_point g_latest_at;

uint64_t key(const Event& e) {
  return (uint64_t)(uint32_t)(e.frame + 0x40000000) << 24 | (uint64_t)e.kind << 16 | (uint64_t)(e.actor & 0xFF) << 8 |
         (uint64_t)(e.target & 0xFF);
}

bool aerial(int move) { return move >= 65 && move <= 69; }

std::string signed_value(int v) {
  char b[16];
  std::snprintf(b, sizeof b, v > 0 ? "+%d" : "%d", v);
  return b;
}

}  // namespace

bool parse(const uint8_t* p, uint32_t n, Event& out) {
  if (n != 22 || p[4] < 1 || p[4] > 5) return false;
  out = Event{};
  out.frame = (int32_t)((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]);
  out.kind = (Kind)p[4];
  out.actor = p[5] & 3;
  out.target = p[6] == 0xFF ? -1 : p[6] & 3;
  out.advantage = (int8_t)p[7];
  out.move = p[8] << 8 | p[9];
  out.lcancel = p[10];
  out.to_land = p[11] == 0xFF ? -1 : p[11];
  out.tilt = (int8_t)p[12];
  out.hitlag = p[13];
  out.actor_percent = p[14] << 8 | p[15];
  out.target_percent = p[16] << 8 | p[17];
  out.actor_character = p[18] == 0xFF ? -1 : p[18];
  out.target_character = p[19] == 0xFF ? -1 : p[19];
  out.late = p[20] == 0xFF ? -1 : p[20];
  out.free_at = p[21] == 0xFF ? -1 : p[21];
  return true;
}

void add(const Event& e) {
  // Shield stun never leaves anyone 40 frames behind: such a reading measured something else (a
  // grab, a shield break), so it is not kept.
  if (e.kind == Kind::Shield && e.advantage < -40) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  g_events[key(e)] = e;
  if (e.kind == Kind::Hit || e.kind == Kind::Shield) {
    g_latest = e;
    g_have_latest = true;
    g_latest_at = std::chrono::steady_clock::now();
  }
}

void clear() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_events.clear();
  g_have_latest = false;
}

std::vector<Event> events() {
  std::lock_guard<std::mutex> lock(g_mutex);
  std::vector<Event> out;
  out.reserve(g_events.size());
  for (const auto& kv : g_events) out.push_back(kv.second);
  return out;
}

bool latest(Event& e, double& age_seconds) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_have_latest) return false;
  e = g_latest;
  age_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_latest_at).count();
  return true;
}

std::string move_name(int m) {
  switch (m) {
    case 44: case 45: case 46: return "Jab";
    case 47: case 48: case 49: return "Rapid jab";
    case 50: return "Dash attack";
    case 51: case 52: case 53: case 54: case 55: return "Forward tilt";
    case 56: return "Up tilt";
    case 57: return "Down tilt";
    case 58: case 59: case 60: case 61: case 62: return "Forward smash";
    case 63: return "Up smash";
    case 64: return "Down smash";
    case 65: return "Nair";
    case 66: return "Fair";
    case 67: return "Bair";
    case 68: return "Uair";
    case 69: return "Dair";
    case 212: case 214: return "Grab";
    case 217: return "Pummel";
    case 219: return "Forward throw";
    case 220: return "Back throw";
    case 221: return "Up throw";
    case 222: return "Down throw";
    case 187: case 195: return "Getup attack";
    case 256: case 257: return "Ledge attack";
    default: break;
  }
  if (m >= 94 && m <= 119) return "Item throw";
  if (m >= 341) return "Special";
  return "Attack";
}

std::string character_name(int kind) {
  static const char* const kNames[] = {
      "Mario", "Fox", "Captain Falcon", "Donkey Kong", "Kirby", "Bowser", "Link", "Sheik", "Ness",
      "Peach", "Popo", "Nana", "Pikachu", "Samus", "Yoshi", "Jigglypuff", "Mewtwo", "Luigi", "Marth",
      "Zelda", "Young Link", "Dr. Mario", "Falco", "Pichu", "Mr. Game & Watch", "Ganondorf", "Roy"};
  return kind >= 0 && kind < (int)(sizeof kNames / sizeof kNames[0]) ? kNames[kind] : "";
}

Readout readout(const Event& e) {
  Readout r;
  const std::string move = move_name(e.move);
  switch (e.kind) {
    case Kind::Hit:
    case Kind::Shield: {
      r.head = move + (e.kind == Kind::Shield ? " on shield" : " hit");
      r.value = signed_value(e.advantage);
      r.sign = e.advantage > 0 ? 1 : e.advantage < 0 ? -1 : 0;
      if (aerial(e.move)) {
        if (e.lcancel == 1) r.detail = "L-cancelled.";
        else if (e.lcancel == 2) r.detail = "L-cancel missed.";
        if (e.to_land > 0) {
          char b[64];
          std::snprintf(b, sizeof b, "%sHit %d frame%s before landing.", r.detail.empty() ? "" : "  ", e.to_land,
                        e.to_land == 1 ? "" : "s");
          r.detail += b;
        }
      }
      break;
    }
    case Kind::Landing:
      r.head = move + " landing";
      r.detail = e.lcancel == 1 ? "L-cancelled." : e.lcancel == 2 ? "L-cancel missed." : "";
      break;
    case Kind::Tech:
      r.head = e.advantage ? "Tech" : "Missed tech";
      break;
    case Kind::Grab:
      r.head = "Grab";
      break;
  }
  return r;
}

std::string advice(const Event& e) {
  char b[200];
  if (e.kind == Kind::Shield && aerial(e.move)) {
    if (e.advantage >= 0) return "Safe on shield: you can act first.";
    if (e.lcancel == 2) return "L-cancel it: press L, R or Z just before you land. It halves the landing lag.";
    // Hitting later in the fall gains a frame each, while the hitbox is still out to do it.
    const int gain = e.to_land > 1 ? (e.late >= 0 ? std::min(e.to_land - 1, e.late) : e.to_land - 1) : 0;
    if (gain > 0 && e.advantage + gain >= 0) {
      const int best = e.advantage + gain;
      std::snprintf(b, sizeof b, "Hit lower on the shield, later in your fall: up to %s.%s", signed_value(best).c_str(),
                    e.tilt > 30 ? "  Their shield was tilted up, which meets aerials early." : "");
      return b;
    }
    return "Still minus even hit as low as it can: space it so they cannot reach you, or mix up what follows.";
  }
  if (e.kind == Kind::Shield && e.advantage < 0) {
    std::snprintf(b, sizeof b, "Punishable: any option out of shield faster than %d frames beats it.", -e.advantage);
    return b;
  }
  if (e.kind == Kind::Landing && e.lcancel == 2) return "Press L, R or Z just before you land.";
  if (e.kind == Kind::Tech && !e.advantage) return "Press L or R just before you hit the ground.";
  return "";
}

std::vector<Moment> moments(const std::vector<Event>& events, int player) {
  std::vector<Moment> out;
  int32_t last_shield = INT32_MIN;   // the frame of this player's last shield moment
  for (size_t i = 0; i < events.size(); ++i) {
    const Event& e = events[i];
    if (e.actor != player) continue;
    Moment m;
    m.frame = e.frame;
    const Readout r = readout(e);
    m.what = r.head;
    m.value = r.value;
    m.sign = r.sign;
    m.advice = advice(e);
    if (e.kind == Kind::Shield && e.advantage < 0) {
      // Punished: the defender hit or grabbed this player before they could do much else.
      const int32_t window = e.frame + e.hitlag + 45;
      for (size_t k = i + 1; k < events.size() && events[k].frame <= window; ++k) {
        const Event& n = events[k];
        if ((n.kind == Kind::Hit || n.kind == Kind::Grab) && n.actor == e.target && n.target == player) {
          m.punished = true;
          m.what += ", punished";
          m.advice = "Punished by " + move_name(n.move) + ".  " + m.advice;
          break;
        }
      }
      last_shield = e.frame;
      out.push_back(m);
    } else if (e.kind == Kind::Hit && e.advantage >= 15 && e.advantage <= 60 && e.free_at > 0 &&
               move_name(e.move) != "Special" && move_name(e.move) != "Item throw") {
      // Dropped combo: well ahead, and nothing more landed before they could act.
      bool followed = false;
      for (size_t k = i + 1; k < events.size() && events[k].frame <= e.frame + e.free_at + 2; ++k) {
        const Event& n = events[k];
        if ((n.kind == Kind::Hit || n.kind == Kind::Grab) && n.actor == player && n.target == e.target) {
          followed = true;
          break;
        }
      }
      if (!followed) {
        char b[160];
        m.what = "Dropped: " + move_name(e.move);
        std::snprintf(b, sizeof b, "You could act %d frames before them and nothing followed. Practice the follow-up from here.",
                      e.advantage);
        m.advice = b;
        out.push_back(m);
      }
    } else if (e.kind == Kind::Landing && e.lcancel == 2 && e.frame - last_shield > 60) {
      m.what = "No L-cancel: " + move_name(e.move);
      out.push_back(m);
    } else if (e.kind == Kind::Tech && !e.advantage) {
      out.push_back(m);
    }
  }
  return out;
}

std::string clock(int32_t frame) {
  const int32_t s = std::max<int32_t>(0, frame) / 60;
  char b[16];
  std::snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
  return b;
}

}  // namespace replay_coach
