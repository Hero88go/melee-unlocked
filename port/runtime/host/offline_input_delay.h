// A frame-counted local input queue, shared by both engines.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "host.h"
#include <array>
#include <algorithm>
#include <cstring>

namespace host::offline_delay {
class Queue {
  std::array<std::array<PadState, 4>, 9> history_{};
  std::array<PadState, 4> output_{};
  uint32_t tick_ = 0, scene_ = 0;
  int delay_ = 0, position_ = 0;
  bool active_ = false;
public:
  void apply(PadState pads[4], bool active, int delay, uint32_t tick, uint32_t scene) {
    delay = std::clamp(delay, 1, 9);
    if (!active) { active_ = false; return; }
    const bool reset = !active_ || delay != delay_ || scene != scene_ || tick < tick_ || tick - tick_ > 1;
    if (reset) {
      history_ = {}; output_ = {}; position_ = 0;
      // Empty history is a neutral pad, retaining controller connection status.
      for (auto& frame : history_) for (int p = 0; p < 4; ++p) frame[p].err = pads[p].err;
    } else if (tick == tick_) {
      std::memcpy(pads, output_.data(), sizeof output_);
      return;
    }
    active_ = true; delay_ = delay; tick_ = tick; scene_ = scene;
    output_ = history_[position_];
    for (int p = 0; p < 4; ++p) {
      // Connection changes are immediate, and old input is discarded on a
      // disconnect/reconnect so a previous controller's held buttons cannot leak.
      if (output_[p].err != pads[p].err || pads[p].err != 0) {
        for (auto& frame : history_) { frame[p] = {}; frame[p].err = pads[p].err; }
        output_[p] = {}; output_[p].err = pads[p].err;
      }
    }
    std::memcpy(history_[position_].data(), pads, sizeof output_);
    position_ = (position_ + 1) % delay;
    std::memcpy(pads, output_.data(), sizeof output_);
  }
};
void apply(PadState pads[4], bool gameplay);
}  // namespace host::offline_delay
