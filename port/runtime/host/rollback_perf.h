// Host-only rollback measurements. No restored guest memory, per-frame allocations or ABI changes.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "sim_work.h"
#include <atomic>
#include <cstdio>
#include <string>

namespace host::rollback {
inline uint64_t now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
struct Counts {
  uint64_t intervals = 0, over16 = 0, over33 = 0;
  Counts operator-(const Counts& b) const {
    return {intervals - b.intervals, over16 - b.over16, over33 - b.over33};
  }
};
struct Presentation {
  std::atomic<bool> online{false};
  std::atomic<uint64_t> session{0}, catchup_sequence{0};
  std::atomic<uint64_t> intervals{0}, over16{0}, over33{0}, publication{0};
  Counts snapshot() const {
    for (;;) {
      const auto before = publication.load();
      if (before & 1) continue;
      Counts c{intervals.load(), over16.load(), over33.load()};
      if (before == publication.load()) return c;
    }
  }
  // Render thread is the only writer of these cumulative counters.
  void add(uint64_t ns) {
    ++publication;
    ++intervals;
    if (ns > 16700000) ++over16;
    if (ns > 33000000) ++over33;
    ++publication;
  }
};
struct PresentObserver {
  uint64_t session = 0, sequence = 0, last_ns = 0;
  bool have_last = false;
  void present(Presentation& p, uint64_t ns) {
    const auto current_session = p.session.load();
    const auto current_sequence = p.catchup_sequence.load();
    if (!p.online.load()) { have_last = false; return; }
    if (have_last && session == current_session &&
        ((current_sequence & 1) || current_sequence != sequence)) p.add(ns - last_ns);
    session = current_session; sequence = current_sequence; last_ns = ns; have_last = true;
  }
};
struct Stats {
  uint64_t frames = 0, events = 0, deepest = 0, sum_ns = 0, event_sum_ns = 0;
  uint64_t missed_samples = 0;
  perf::Histogram samples, event_samples;
  void sample(uint64_t ns) { sum_ns += ns; samples.add(ns / 1e6); }
  void event(uint64_t ns, uint64_t depth) {
    ++events; deepest = std::max(deepest, depth);
    event_sum_ns += ns; event_samples.add(ns / 1e6);
  }
};
class Accumulator {
 public:
  bool online = false, catching_up = false;
  Stats interval, match;
  uint64_t started_ns = 0, window_ns = 0;
  Counts match_present, interval_present;
  void start(uint64_t ns, Presentation& p) {
    if (online) return;
    interval = {}; match = {}; started_ns = window_ns = ns; online = true;
    match_present = interval_present = p.snapshot();
    ++p.session; p.online.store(true);
  }
  void begin(uint64_t ns, uint32_t frame, Presentation& p) {
    if (!online || catching_up) return;
    catching_up = true; event_start_ = sample_start_ = ns;
    frame_ = frame; event_frames_ = 0; ++p.catchup_sequence;
  }
  // Observe the read-only engine-body counter at host callbacks. Repeated poll,
  // clock and retrace calls in one body cannot manufacture extra samples.
  // A delta > 1 counts frames but is explicitly reported as missing samples,
  // never misrepresented as one very slow frame or divided into invented samples.
  // Phase begin is AFTER snapshot load: both frame and event timings exclude it.
  // The final body is closed by end(), including scene exits during catch-up.
  void frame(uint64_t ns, uint32_t frame) {
    if (!catching_up || frame <= frame_) return;
    const uint64_t n = frame - frame_;
    event_frames_ += n; interval.frames += n; match.frames += n;
    if (n == 1) { interval.sample(ns - sample_start_); match.sample(ns - sample_start_); }
    else { interval.missed_samples += n; match.missed_samples += n; }
    sample_start_ = ns; frame_ = frame;
  }
  void end(uint64_t ns, uint32_t frame_id, Presentation& p) {
    if (!catching_up) return;
    frame(ns, frame_id);
    interval.event(ns - event_start_, event_frames_); match.event(ns - event_start_, event_frames_);
    catching_up = false; ++p.catchup_sequence;
  }
  uint32_t last_frame() const { return frame_; }
  bool due(uint64_t ns) const { return online && ns - window_ns >= 15000000000ull; }
  void next_window(uint64_t ns, Counts present) {
    interval = {}; window_ns = ns; interval_present = present;
  }
  void stop(Presentation& p) { online = false; p.online.store(false); }
 private:
  uint64_t event_start_ = 0, sample_start_ = 0, event_frames_ = 0;
  uint32_t frame_ = 0;
};
inline std::string distribution(const char* prefix, const perf::Histogram& h, uint64_t sum_ns) {
  char text[256];
  if (!h.count) {
    std::snprintf(text, sizeof text, "%s_ms_mean=n/a p99_le=n/a max=n/a overflow=0", prefix);
  } else {
    const auto rank = h.count - h.count / 100;
    const bool overflow = rank > h.count - h.buckets.back();
    char p99[32];
    if (overflow) std::snprintf(p99, sizeof p99, "overflow");
    else std::snprintf(p99, sizeof p99, "%.3f", h.p99());
    std::snprintf(text, sizeof text, "%s_ms_mean=%.3f p99_le=%s max=%.3f overflow=%llu",
        prefix, double(sum_ns) / h.count / 1e6, p99, h.maximum,
        (unsigned long long)h.buckets.back());
  }
  return text;
}
inline std::string line(const char* scope, uint64_t window_ns, const Stats& s, Counts c) {
  char text[384];
  std::snprintf(text, sizeof text,
      "rollback: scope=%s window=%.1fs resim_frames=%llu events=%llu deepest=%llu %s "
      "catchup_present_intervals=%llu over16.7=%llu over33=%llu resim_samples=%llu missed_samples=%llu",
      scope, window_ns / 1e9, (unsigned long long)s.frames, (unsigned long long)s.events,
      (unsigned long long)s.deepest, distribution("resim", s.samples, s.sum_ns).c_str(),
      (unsigned long long)c.intervals, (unsigned long long)c.over16, (unsigned long long)c.over33,
      (unsigned long long)s.samples.count, (unsigned long long)s.missed_samples);
  // Event time is begin->end, separate from the per-frame distribution above.
  auto event = distribution("event", s.event_samples, s.event_sum_ns);
  for (const char* field : {"p99_le", "max", "overflow"}) {
    const auto at = event.find(std::string(" ") + field + "=");
    if (at != std::string::npos) event.insert(at + 1, "event_");
  }
  return std::string(text) + " " + event;
}
inline Presentation presentation;
inline Accumulator accumulator; // simulation thread only
} // namespace host::rollback
