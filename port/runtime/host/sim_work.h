// Allocation-free simulation work accounting, using the host steady clock only.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace host::perf {
struct Histogram {
  // Upper-bound buckets at 0.05 ms, through 100 ms, then overflow. Overflow
  // percentiles conservatively return the observed maximum, never a false 100 ms.
  std::array<uint64_t, 2002> buckets{};
  uint64_t count = 0;
  double sum = 0, maximum = 0;
  void add(double ms) {
    ms = std::max(0.0, ms);
    const size_t bucket = ms > 100.0 ? 2001 : static_cast<size_t>(std::ceil(ms * 20.0));
    ++buckets[bucket]; ++count; sum += ms; maximum = std::max(maximum, ms);
  }
  double mean() const { return count ? sum / count : 0; }
  double p99() const {
    if (!count) return 0;
    const uint64_t rank = count - count / 100; // ceil(0.99 * count), without floating point
    uint64_t seen = 0;
    for (size_t i = 0; i < buckets.size(); ++i) {
      seen += buckets[i];
      if (seen >= rank) return i == 2001 ? maximum : i * 0.05;
    }
    return maximum;
  }
};

enum class Stage { Step, Decode, Submit, Input, Wait, Count };
struct Frame {
  std::array<double, static_cast<size_t>(Stage::Count)> ms{};
  double work_ms() const {
    return ms[0] + ms[1] + ms[2] + ms[3];
  }
  double get(Stage stage) const { return ms[static_cast<size_t>(stage)]; }
};

struct Timer {
  using Clock = std::chrono::steady_clock;
  bool running = false, resim = false;
  Stage stage = Stage::Step;
  Clock::time_point last{};
  Frame frame;
  Histogram work, step;
  std::array<double, static_cast<size_t>(Stage::Count)> totals{};
  uint64_t resim_frames = 0;
  uint32_t resim_start = 0;
  bool have_resim_start = false;

  void advance(Clock::time_point now) {
    if (running && !resim)
      frame.ms[static_cast<size_t>(stage)] += std::chrono::duration<double, std::milli>(now - last).count();
    last = now;
  }
  Stage enter(Stage next, Clock::time_point now = Clock::now()) {
    advance(now);
    const auto previous = stage; stage = next; return previous;
  }
  void set_resim(bool enabled, Clock::time_point now = Clock::now()) {
    advance(now); resim = enabled;
  }
  void set_resim(bool enabled, uint32_t engine_frame, bool valid, Clock::time_point now = Clock::now()) {
    if (enabled == resim) return; // repeated phase notifications do not restart a count
    if (enabled) {
      resim_start = engine_frame; have_resim_start = valid;
    } else {
      if (have_resim_start && valid && engine_frame >= resim_start)
        resim_frames += engine_frame - resim_start;
      have_resim_start = false;
    }
    set_resim(enabled, now);
  }
  void begin(Clock::time_point now = Clock::now()) {
    frame = {}; stage = Stage::Step; last = now; running = true;
  }
  Frame finish(Clock::time_point now = Clock::now()) {
    advance(now);
    if (running && !resim) {
      work.add(frame.work_ms()); step.add(frame.get(Stage::Step));
      for (size_t i = 0; i < totals.size(); ++i) totals[i] += frame.ms[i];
    }
    running = false;
    return frame;
  }
  double mean(Stage s) const { return work.count ? totals[static_cast<size_t>(s)] / work.count : 0; }
};
// Only the simulation thread binds active; storage survives that thread for shutdown reporting.
inline Timer sim_timer;
inline thread_local Timer* active = nullptr;
// Nesting makes the stage split exclusive: submission inside decode is submission,
// and a wait inside either stage is only a wait. Inactive threads never read the clock.
struct Scope {
  Timer* timer;
  Stage previous;
  explicit Scope(Stage stage) : timer(active), previous(timer ? timer->enter(stage) : Stage::Step) {}
  ~Scope() { if (timer) timer->enter(previous); }
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
};
} // namespace host::perf
