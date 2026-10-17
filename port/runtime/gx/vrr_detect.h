// Tells a display that follows the frames (G-Sync, FreeSync) from one that refreshes at a fixed rate,
// from DXGI's own frame statistics: how many frames reached the screen (PresentCount) and how many
// refreshes went by (SyncRefreshCount). While frames come slower than the refresh, a fixed display
// still refreshes at its full rate and shows some frames twice; a variable one refreshes once per
// frame. Frames as fast as the refresh look the same on both, so no verdict is taken then.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cmath>
#include <cstdint>

namespace gx::vrr {

// One reading of DXGI_FRAME_STATISTICS: PresentCount, SyncRefreshCount, SyncQPCTime in seconds.
struct Sample {
  uint64_t presents = 0, refreshes = 0;
  double seconds = 0;
};

// One window's verdict from frames shown and refreshes per second: 1 follows the frames, 0 fixed,
// -1 cannot tell.
inline int judge(double shown_per_s, double refreshes_per_s, double nominal_hz) {
  if (nominal_hz < 75.0 || shown_per_s < 30.0) return -1;   // a 60 Hz panel, or an idle window
  if (shown_per_s > 0.96 * nominal_hz) return -1;           // as fast as the refresh: alike on both
  if (refreshes_per_s > 0.98 * nominal_hz) return 0;        // full-rate refreshes for fewer frames
  if (std::fabs(refreshes_per_s - shown_per_s) < 0.04 * shown_per_s) return 1;   // one per frame
  return -1;
}

// Windows of at least a second; a verdict is taken when two windows in a row agree, and kept until
// two in a row say otherwise.
class Detector {
 public:
  // True when the verdict changed.
  bool add(const Sample& s, double nominal_hz) {
    if (!have_ || s.seconds <= last_.seconds || s.presents < last_.presents || s.refreshes < last_.refreshes) {
      last_ = s;
      have_ = true;
      return false;
    }
    const double dt = s.seconds - last_.seconds;
    if (dt < 1.0) return false;
    shown_ = (double)(s.presents - last_.presents) / dt;
    refreshes_ = (double)(s.refreshes - last_.refreshes) / dt;
    last_ = s;
    const int v = judge(shown_, refreshes_, nominal_hz);
    if (v < 0) return false;
    if (v != pending_) { pending_ = v; votes_ = 1; return false; }
    if (++votes_ < 2 || v == state_) return false;
    state_ = v;
    return true;
  }
  // The statistics stopped (Windows composes the window, the mode changed): the next reading starts
  // a new window. The verdict stays.
  void restart() { have_ = false; }
  int state() const { return state_; }
  double shown() const { return shown_; }
  double refreshes() const { return refreshes_; }

 private:
  Sample last_{};
  bool have_ = false;
  int state_ = -1, pending_ = -1, votes_ = 0;
  double shown_ = 0, refreshes_ = 0;
};

// Where a frame rate sits on a display that follows the frames: just under the refresh, as NVIDIA
// places it for G-Sync (refresh * (1 - refresh / 3600): 224 at 240 Hz, 189 at 200 Hz, 138 at 144),
// so the display never waits on a sync and a frame never lands mid-refresh.
inline double under_refresh(double hz) { return hz * (1.0 - hz / 3600.0); }

}  // namespace gx::vrr
