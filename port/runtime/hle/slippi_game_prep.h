// Slippi GamePrep's reliable P2P payload and polling queue.
// Ported from SlippiNetplay.cpp (GPL-2.0-or-later).
#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <mutex>

namespace slippi {
using GamePrepStep = std::array<uint8_t, 5>; // step, character, colour, stage[2]

class GamePrepQueue {
 public:
  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    steps_.clear();
  }
  void receive(const GamePrepStep& step) {
    std::lock_guard<std::mutex> lock(mutex_);
    steps_.push_back(step);
  }
  std::array<uint8_t, 6> fetch(uint8_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Slippi discards other indices and retains a matching front for repeated polls.
    // Indices belong to the game. There is no host-side timeout or automatic skip.
    while (!steps_.empty()) {
      const auto& s = steps_.front();
      if (s[0] == index) return {1, 0, s[1], s[2], s[3], s[4]};
      steps_.pop_front();
    }
    return {};
  }
 private:
  std::mutex mutex_;
  std::deque<GamePrepStep> steps_;
};
} // namespace slippi
