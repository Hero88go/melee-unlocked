// Window-free policy for staging hook input at the live pad boundary.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifdef YOUGAME_AGENT_TEST_HOOK
#include <cstdint>
#include <mutex>
#include <string>

namespace yougame::agent_test {
enum class InputKey { Script, Fight, PadBack, Keys };
struct InputState {
  uint64_t session = 0;
  int mode = -2;  // -1 means positively offline; other unknown modes fail closed.
  bool ranked = false;  // Also covers a ranked teams/doubles room, regardless of mode id.
  unsigned players = 0;
  bool all_agents = false;
};
inline constexpr bool ranked_mode(int mode) { return mode == 0; }
inline constexpr bool gameplay_input_allowed(const InputState& s) {
  return !s.ranked && !ranked_mode(s.mode) && (s.mode == -1 ||
      (s.mode >= 1 && s.mode <= 4 && s.players && s.all_agents));
}
// Consume only the server's room roster, including the local connection. Never coerce hn.
template<class Json>
InputState room_input_state(uint64_t session, int mode, bool ranked,
                            const Json& participants, const std::string& me) {
  InputState s{session, mode, ranked || ranked_mode(mode)};
  if (!participants.is_array() || participants.empty()) return s;
  bool local = false;
  s.all_agents = true;
  for (const auto& p : participants) {
    ++s.players;
    if (!p.is_object() || !p.count("hn") || !p["hn"].is_boolean() || p["hn"] != true)
      s.all_agents = false;
    if (p.is_object() && p.count("connectionId") && p["connectionId"].is_string() &&
        p["connectionId"] == me && !me.empty()) local = true;
  }
  s.all_agents = s.all_agents && local;
  return s;
}
inline const char* input_key_name(InputKey key) {
  switch (key) {
    case InputKey::Script: return "script";
    case InputKey::Fight: return "fight";
    case InputKey::PadBack: return "padBack";
    case InputKey::Keys: return "keys";
  }
  return "keys";
}
class InputPolicy {
 public:
  template<class Log>
  bool allowed(const InputState& s, InputKey key, Log log) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Roster changes never reset the per-session diagnostics.
    if (session_ != s.session || (offline_ && s.mode != -1)) {
      session_ = s.session;
      ranked_ = room_ = allowed_ = 0;
    }
    offline_ = s.mode == -1 && !s.ranked;
    const unsigned bit = 1u << static_cast<unsigned>(key);
    const bool ok = gameplay_input_allowed(s);
    if (ok && offline_) return true;
    unsigned& reported = ok ? allowed_ : (s.ranked || ranked_mode(s.mode)) ? ranked_ : room_;
    if (!(reported & bit)) {
      reported |= bit;
      const std::string line = std::string("agent-test: ") + input_key_name(key) +
          (ok ? " allowed: agent-only room (" + std::to_string(s.players) + " agent players)" :
           (s.ranked || ranked_mode(s.mode)) ? " refused in ranked" : " refused: not an agent-only room");
      log(line.c_str());
    }
    return ok;
  }
 private:
  std::mutex mutex_;
  uint64_t session_ = 0;
  bool offline_ = false;
  unsigned ranked_ = 0, room_ = 0, allowed_ = 0;
};
inline InputPolicy input_policy;
// Adapter takes the room mutex and reads the current roster at each input boundary.
InputState live_input_state(int live_mode);
}  // namespace yougame::agent_test
#endif
