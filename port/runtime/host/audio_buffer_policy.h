// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>

namespace host {
// Output buffering only. It never changes the game's mixer, alarms or rollback state.
struct AudioBufferPolicy {
  bool adaptive = true;
  uint32_t floor_ms = 40;
  uint32_t target_ms = 40;
  uint64_t clean_frames = 0;

  // Modes: 0 = Auto (adaptive: starts at the given buffer, grows after real gaps, settles back),
  // 1 = Low latency (fixed), 2 = Exclusive (fixed; the device is opened in WASAPI exclusive mode).
  void configure(int mode, int milliseconds) {
    adaptive = mode == 0;
    floor_ms = (uint32_t)std::clamp(milliseconds, adaptive ? 10 : 5, 120);
    target_ms = floor_ms;
    clean_frames = 0;
  }
  // The Exclusive UI has no buffer slider. Do not inherit the hidden default or a saved
  // Low-latency target; use the endpoint floor already chosen from its period and scheduling slack.
  void configure_for_output(int mode, int milliseconds, int device_floor_ms) {
    configure(mode, mode == 2 ? device_floor_ms : std::max(milliseconds, device_floor_ms));
  }
  bool gap(uint64_t frames, uint32_t sample_rate) {
    clean_frames = 0;
    if (!adaptive || !frames || frames > (uint64_t)sample_rate * 60 / 1000) return false;
    const auto before = target_ms;
    target_ms = std::min(120u, target_ms + 4);
    return target_ms != before;
  }
  void clean(uint32_t frames, uint32_t sample_rate) {
    clean_frames += frames;
    if (adaptive && clean_frames >= (uint64_t)sample_rate * 10 && target_ms > floor_ms) {
      --target_ms;
      clean_frames = 0;
    }
  }
};
} // namespace host
