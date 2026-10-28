// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>

namespace slippi::online {
// Online match speed protection (slippi_online.cpp budget_watch). Sub-frames add work to the game
// thread, and a computer that cannot finish a 60 Hz frame in time falls behind its opponent: Slippi
// then holds both games back (57 frames a second, late inputs). This judges falling behind from the
// frame pacer itself, one sample per game frame: did the frame finish with time to spare on the
// 60 Hz schedule? The pacer absorbs single slow frames (a deep rollback, a hitch) by catching up on
// the next ones, so those never count against a computer that keeps up. Only when nearly every frame
// of three seconds ran late are sub-frames paused, and then only while pausing them is what fixes it:
// if the game is still behind without them, they come back on for the rest of the match. A decision
// holds until the match ends, so the frame rate never switches back and forth during play.
class FrameBudget {
 public:
  static constexpr unsigned kWindow = 180;         // three seconds of game frames
  static constexpr unsigned kSuspendBehind = 162;  // 90% of them late: the game cannot keep up
  static constexpr unsigned kSettle = 60;          // after pausing: the carried-over lateness clears
  static constexpr unsigned kHelpedBehind = 18;    // then at most 10% late: pausing fixed it
  enum class Change { None, Suspend, Resume };
  enum class State { Watching, Suspended, Kept, Done };

  void reset() { window_ = {}; position_ = samples_ = behind_ = 0; state_ = State::Watching; }
  bool suspended() const { return state_ == State::Suspended || state_ == State::Kept; }
  State state() const { return state_; }
  unsigned behind_in_window() const { return behind_; }

  // One game frame. `behind`: the frame did not finish before its 60 Hz deadline.
  Change update(bool enabled, bool active_match, bool subframes_active, bool behind) {
    if (!enabled || !active_match) {
      const bool was = suspended();
      reset();
      return was ? Change::Resume : Change::None;
    }
    switch (state_) {
      case State::Watching:
        if (!subframes_active) { clear(); return Change::None; }   // only sub-frames' own effect is judged
        push(behind);
        if (samples_ >= kWindow && behind_ >= kSuspendBehind) { state_ = State::Suspended; clear(); return Change::Suspend; }
        return Change::None;
      case State::Suspended:
        push(behind);
        if (samples_ < kSettle + kWindow) return Change::None;
        if (behind_ <= kHelpedBehind) { state_ = State::Kept; return Change::None; }
        state_ = State::Done;   // still behind without them: they were not the cause
        return Change::Resume;
      case State::Kept:
      case State::Done:
        return Change::None;
    }
    return Change::None;
  }

 private:
  void clear() { window_ = {}; position_ = samples_ = behind_ = 0; }
  // The window counts the last kWindow frames; during the settle period after a pause the samples
  // still go in, so by the end only frames from after the settle period are in the window.
  void push(bool behind) {
    const uint8_t b = behind ? 1 : 0;
    behind_ += b;
    behind_ -= window_[position_];
    window_[position_] = b;
    position_ = (position_ + 1) % kWindow;
    ++samples_;
  }
  std::array<uint8_t, kWindow> window_{};
  unsigned position_ = 0, samples_ = 0, behind_ = 0;
  State state_ = State::Watching;
};
}  // namespace slippi::online
