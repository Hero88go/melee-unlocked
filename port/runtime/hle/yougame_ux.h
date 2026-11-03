// Page pause requests: stdin only queues; the simulation thread owns game state.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace yougame::ux {
class PagePause {
 public:
  enum class Outcome {
    None, Applied, Cleared, IgnoredOnline, IgnoredNotInMatch,
    IgnoredAlreadyPaused, IgnoredUnownedResume
  };
  struct Result {
    Outcome outcome = Outcome::None;
    bool released_scene = false;
    // The state to send the page (yg {"event":{"type":"pause",...}}): set when pausable or the page's
    // own pause changed, and once after every page request, as its answer. paused is the page's pause
    // only (a Start pause is the player's). reason: online, not-in-match, scene-changed, or null.
    bool event = false, pausable = false, paused = false;
    const char* reason = nullptr;
  };
  bool push(const nlohmann::json& push) {
    if (!push.is_object() || !push.count("type") || !push["type"].is_string() ||
        push["type"] != "pause" || !push.count("paused") || !push["paused"].is_boolean())
      return false;
    pending_.store(push["paused"].get<bool>() ? 1 : 0);
    return true;
  }
  // Snapshot and setter are supplied by the Source bridge or Static console memory/API.
  template<class Set>
  Result tick(bool stdio, bool online, bool match, uint32_t scene, uint32_t frame,
              bool already_paused, Set set) {
    const int request = pending_.exchange(-1);
    Result result;
    result.released_scene = owned_ && (scene != scene_ || frame < frame_);
    if (!stdio || online || !match || scene != scene_ || frame < frame_) owned_ = false;
    scene_ = scene;
    frame_ = frame;
    if (!stdio || online || !match) {
      // Discard, never defer into a later local match.
      if (request != -1)
        result.outcome = online ? Outcome::IgnoredOnline : Outcome::IgnoredNotInMatch;
      report(result, request != -1, stdio, online, match);
      return result;
    }
    if (request == 1)
      result.outcome = already_paused ? Outcome::IgnoredAlreadyPaused : Outcome::Applied;
    if (request == 0)
      result.outcome = owned_ ? Outcome::Cleared : Outcome::IgnoredUnownedResume;
    if (request == 1 && !already_paused) { set(true); owned_ = true; }
    if (request == 0 && owned_) { set(false); owned_ = false; }
    report(result, request != -1, stdio, online, match);
    return result;
  }
 private:
  void report(Result& result, bool answered, bool stdio, bool online, bool match) {
    if (!stdio) return;   // no app to tell
    const bool pausable = !online && match;
    const int state = (pausable ? 2 : 0) | (owned_ ? 1 : 0);
    if (!answered && !result.released_scene && state == sent_) return;
    sent_ = state;
    result.event = true;
    result.pausable = pausable;
    result.paused = owned_;
    result.reason = result.released_scene ? "scene-changed" : pausable ? nullptr : online ? "online" : "not-in-match";
  }
  std::atomic<int> pending_{-1};
  bool owned_ = false;
  uint32_t scene_ = 0, frame_ = 0;
  int sent_ = -1;   // last state sent to the page (pausable << 1 | paused); -1 = none yet
};
inline PagePause page_pause;
}  // namespace yougame::ux
