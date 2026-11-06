// The vertical blank's period and position from the times a waiting thread woke (vblank_clock.h).
// No Windows or host dependencies, so the arithmetic is unit-tested (tests/vblank_model_test.cpp).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace gx {

// The arithmetic, separate from the thread so it can be tested with made-up wake times.
class VBlankModel {
 public:
  void add(double t) {
    if (count_ && t <= times_[(at_ + kRing - 1) % kRing]) return;
    times_[at_] = t; at_ = (at_ + 1) % kRing; if (count_ < kRing) ++count_;
  }
  void clear() { count_ = 0; at_ = 0; }
  // Period and the most recent blank, or false with too few or inconsistent samples.
  bool fit(double* period, double* last) const {
    if (count_ < 16) return false;
    std::vector<double> gaps;
    gaps.reserve(count_);
    for (int i = 1; i < count_; ++i) {
      const double a = times_[(at_ + kRing - count_ + i - 1) % kRing], b = times_[(at_ + kRing - count_ + i) % kRing];
      gaps.push_back(b - a);
    }
    // The median gap is only good enough to say how many blanks each gap spans (one, or two after a
    // missed wake: a few percent of error cannot move that rounding). The period is then the whole
    // span over the blank count, which late wakes can only move by their lateness over the count.
    std::nth_element(gaps.begin(), gaps.begin() + gaps.size() / 2, gaps.end());
    const double rough = gaps[gaps.size() / 2];
    if (!(rough > 1.0 / 600.0 && rough < 1.0 / 23.0)) return false;
    double blanks = 0;
    for (double g : gaps) blanks += std::max(1.0, std::round(g / rough));
    const double oldest = times_[(at_ + kRing - count_) % kRing];
    const double newest = times_[(at_ + kRing - 1) % kRing];
    if (blanks < 8) return false;
    const double p = (newest - oldest) / blanks;
    double best = newest;
    for (int i = 0; i < std::min(count_, 16); ++i) {
      const double t = times_[(at_ + kRing - 1 - i) % kRing];
      best = std::min(best, t + std::round((newest - t) / p) * p);
    }
    *period = p; *last = best;
    return true;
  }
  double newest() const { return count_ ? times_[(at_ + kRing - 1) % kRing] : 0.0; }

 private:
  static constexpr int kRing = 64;
  double times_[kRing] = {};
  int at_ = 0, count_ = 0;
};

// Which returns of the wait are vertical blanks. A display that is asleep or switched off returns
// from the wait at once instead of waiting for a blank (seen 2026-11-05: millions of returns a
// second, a core spinning at the waiting thread's priority). No display refreshes faster than about
// every 1.7 ms (600 Hz), so a return within a millisecond of the previous one is not a blank and is
// not counted. One such return also follows a wake that came late (the next blank is just after
// it), which is no reason to stop waiting; only a run of them means the wait is not waiting, and
// then the thread sleeps between waits.
class WakeFilter {
 public:
  enum Verdict { kBlank, kSkip, kSleep };
  Verdict add(double now) {
    const bool quick = have_ && now - last_ < 0.001;
    have_ = true; last_ = now;
    if (!quick) { run_ = 0; return kBlank; }
    return ++run_ >= kRun ? kSleep : kSkip;
  }
  // The time a sleep ended: the next return is measured from it, so the display still not waiting
  // is caught by the first return after the sleep.
  void slept(double now) { last_ = now; have_ = true; }

 private:
  static constexpr int kRun = 4;
  double last_ = 0;
  bool have_ = false;
  int run_ = 0;
};

}  // namespace gx
