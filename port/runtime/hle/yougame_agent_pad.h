// Test-only port-1 input, upstream of the settings chord and UI snapshot. Include only in hook builds.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../abi/mu_host.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>

namespace yougame::agent_test {
struct PadStep { uint64_t frames; uint16_t buttons; const char* name; };
inline constexpr std::array<PadStep, 11> pad_back_steps{{
  {30, 0, "neutral"},
  {10, 0x1000 | 0x0004 | 0x0010, "chord"},
  {20, 0, "release-chord"},
  {6, 0x0002, "right-1"}, {14, 0, "release-right-1"},
  {6, 0x0002, "right-2"}, {14, 0, "release-right-2"},
  {6, 0x0100, "a-1"}, {20, 0, "release-a-1"},
  {6, 0x0100, "a-2"}, {20, 0, "release-a-2"},
}};

// UI-consumed frames, not 60 Hz ticks: chord, Right, Right, A, A, with releases between presses.
inline constexpr std::array<PadStep, 11> pad_back_ui_steps{{
  {2, 0, "neutral"}, {1, 0x1014, "chord"}, {3, 0, "release-chord"},
  {1, 0x0002, "right-1"}, {3, 0, "release-right-1"},
  {1, 0x0002, "right-2"}, {3, 0, "release-right-2"},
  {1, 0x0100, "a-1"}, {3, 0, "release-a-1"},
  {1, 0x0100, "a-2"}, {3, 0, "release-a-2"},
}};

struct PadFrame {
  bool active = false;
  MuPadStatus pad = {0, 0, 0, 0, 0, 0, 0, 0, 0, -1};
  int step = -1;
};

// Frames are absolute 60 Hz ticks since process startup. No override before the delay or after
// the last release; no modulo, scene dependency, window or device access, and no repeated run.
inline PadFrame pad_back_frame(uint64_t frame, int seconds) {
  if (seconds < 10 || seconds > 900) return {};
  const uint64_t start = static_cast<uint64_t>(seconds) * 60;
  if (frame < start) return {};
  frame -= start;
  for (std::size_t i = 0; i < pad_back_steps.size(); ++i) {
    const auto& step = pad_back_steps[i];
    if (frame < step.frames) {
      PadFrame result;
      result.active = true;
      result.pad = {};
      result.pad.button = step.buttons;
      result.step = static_cast<int>(i);
      return result;
    }
    frame -= step.frames;
  }
  return {};
}

class PadBack {
public:
  using Clock = std::chrono::steady_clock;
  void arm(int seconds, Clock::time_point process_start) {
    seconds_ = seconds;
    process_start_ = process_start;
  }
  PadFrame sample(Clock::time_point now) const {
    if (!seconds_ || now < process_start_) return {};
    // Input can be read several times in a tick, and replay menus can pause retraces. Neither
    // changes the script's 60 Hz timeline. A monotonic epoch also prevents a completed run restarting.
    using Frames = std::chrono::duration<int64_t, std::ratio<1, 60>>;
    const auto frame = std::chrono::duration_cast<Frames>(now - process_start_).count();
    return pad_back_frame(static_cast<uint64_t>(frame), seconds_);
  }
  // The startup delay is wall time; the live hook's steps are acknowledged by settings_frame.
  // Keep sample() above as the original fixed-rate reference, covered by exhaustive tests.
  PadFrame sample_for_ui(Clock::time_point now) {
    std::lock_guard<std::mutex> lock(ui_mutex_);
    if (seconds_ < 10 || seconds_ > 900 || now < process_start_ ||
        now - process_start_ < std::chrono::seconds(seconds_) || ui_step_ == pad_back_ui_steps.size()) return {};
    PadFrame result;
    result.active = true;
    result.pad = {};
    result.pad.button = pad_back_ui_steps[ui_step_].buttons;
    result.step = static_cast<int>(ui_step_);
    return result;
  }
  // Called only when the UI actually reads the published snapshot. Reject stale snapshots after
  // an acknowledgement: a renderer faster than input polling must not repeat an old D-pad press.
  bool consume_ui(int published_step) {
    std::lock_guard<std::mutex> lock(ui_mutex_);
    if (published_step < 0 || static_cast<std::size_t>(published_step) != ui_step_ ||
        ui_step_ == pad_back_ui_steps.size()) return false;
    // One UI frame per press prevents typematic repeats even with a long hidden-frame DeltaTime.
    // Neutral frames also allow nav arming, deferred focus moves and confirmation focus to settle.
    if (++ui_seen_ == pad_back_ui_steps[ui_step_].frames) { ++ui_step_; ui_seen_ = 0; }
    return true;
  }
private:
  int seconds_ = 0;
  Clock::time_point process_start_{};
  std::mutex ui_mutex_;
  std::size_t ui_step_ = 0;
  unsigned ui_seen_ = 0;
};

// Armed once by main before any input thread starts. Input publishes; settings UI acknowledges.
inline PadBack pad_back;
}  // namespace yougame::agent_test
