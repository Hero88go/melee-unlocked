// Staging-only protocol key taps. No OS input, platform APIs, or game state.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifdef YOUGAME_AGENT_TEST_HOOK
#include "yougame_parity.h"
#include <algorithm>
#include <chrono>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace yougame::agent_test {
struct ScriptedKey {
  bool overlay;
  double seconds;
  std::string time;
};

// ASCII decimal seconds only: no whitespace, signs, exponent, or partial parses.
inline bool key_seconds(const std::string& text, double& seconds) {
  if (text.empty()) return false;
  std::size_t i = 0;
  int whole = 0;
  while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
    whole = whole * 10 + (text[i++] - '0');
    if (whole > 600) return false;
  }
  if (!i) return false;
  bool nonzero_fraction = false;
  if (i < text.size() && text[i] == '.') {
    const auto begin = ++i;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
      const auto digit = text[i++] - '0';
      nonzero_fraction |= digit != 0;
    }
    if (i == begin) return false;
  }
  if (i != text.size() || (whole == 600 && nonzero_fraction)) return false;
  std::istringstream input(text);
  input.imbue(std::locale::classic());
  input >> seconds;
  return input && seconds > 0 && seconds <= 600;
}

inline std::vector<ScriptedKey> parse_key_script(const std::string& text, std::string& why) {
  why.clear();
  std::vector<ScriptedKey> result;
  auto invalid = [&](const char* reason) {
    why = reason;
    return std::vector<ScriptedKey>{};
  };
  if (text.empty()) return invalid("empty script");
  if (text.size() > 4096) return invalid("script too long");
  std::size_t begin = 0;
  for (;;) {
    if (result.size() == 16) return invalid("more than 16 entries");
    const auto end = text.find(',', begin);
    const auto entry = text.substr(begin, end == std::string::npos ? end : end - begin);
    if (entry.empty()) return invalid("empty entry");
    const auto at = entry.find('@');
    if (at == std::string::npos || entry.find('@', at + 1) != std::string::npos)
      return invalid("expected name@seconds");
    const auto name = entry.substr(0, at);
    if (name != "overlay" && name != "escape") return invalid("unknown key name");
    const auto time = entry.substr(at + 1);
    double seconds = 0;
    if (!key_seconds(time, seconds)) return invalid("invalid time (expected 0 < seconds <= 600)");
    result.push_back({name == "overlay", seconds, time});
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
    return a.seconds < b.seconds;
  });
  return result;
}

// Main arms before window/render threads start. Only the window thread pumps keys;
// Signals publishes the first visible frame's epoch from the presentation thread.
class KeyScript {
 public:
  using Clock = std::chrono::steady_clock;
  void arm(bool stdio, const std::vector<ScriptedKey>& keys) {
    keys_ = stdio ? keys : std::vector<ScriptedKey>{};
    done_.assign(keys_.size(), false);
    tapping_ = false;
  }
  template<class Escape, class Log>
  void pump(bool stdio, Clock::time_point now, parity::Signals& signals,
            parity::OverlayKey& overlay, Escape escape, Log log) {
    if (!stdio || keys_.empty()) return;
    Clock::time_point first;
    if (!signals.first_present(first) || now < first) return;
    if (tapping_ && now - down_at_ >= std::chrono::milliseconds(100)) {
      signals.emit(overlay.script_up());
      tapping_ = false;
    }
    for (std::size_t i = 0; i < keys_.size(); ++i) {
      const auto& key = keys_[i];
      if (done_[i] || std::chrono::duration<double>(now - first).count() < key.seconds) continue;
      // Serialize overlapping taps without dropping an entry or shortening its hold.
      // Escape can still run while an overlay tap is pending.
      if (key.overlay && tapping_) continue;
      done_[i] = true;
      if (key.overlay) {
        const auto line = overlay.script_down();
        if (!line) {
          log("agent-test: key overlay at " + key.time + " s skipped: real Shift+Tab held");
          continue;
        }
        signals.emit(line);
        down_at_ = now;
        tapping_ = true;
      } else {
        escape();
      }
      log(std::string("agent-test: key ") + (key.overlay ? "overlay" : "escape") + " at " + key.time + " s");
    }
  }
 private:
  std::vector<ScriptedKey> keys_;
  std::vector<bool> done_;
  bool tapping_ = false;
  Clock::time_point down_at_{};
};
inline KeyScript key_script;
}  // namespace yougame::agent_test
#endif
