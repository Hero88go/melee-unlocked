// Window-free deterministic host-clock and presentation accounting.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "rollback_perf.h"
#include <cstdlib>
#include <cstdio>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
using namespace host::rollback;
void timing() {
  Presentation p;
  Accumulator a;
  a.begin(1, 2, p); CHECK(!a.catching_up && p.catchup_sequence == 0);
  a.start(1000000000, p);
  a.start(1000000001, p); CHECK(a.started_ns == 1000000000);
  CHECK(!a.due(15999999999) && a.due(16000000000));
  auto empty = line("interval", 15000000000, a.interval, {});
  CHECK(empty.find("resim_ms_mean=n/a p99_le=n/a max=n/a overflow=0") != std::string::npos);
  a.begin(2000000000, 40, p);
  CHECK(p.catchup_sequence == 1);
  a.begin(2000000001, 40, p); // Duplicate phases cannot restart timing.
  a.frame(2000000100, 40); CHECK(a.match.frames == 0); // Repeated callback, same body.
  a.frame(2001000000, 41);
  a.frame(2003000000, 42);
  a.end(2006000000, 43, p); // Final frame ends without another poll/retrace.
  CHECK(p.catchup_sequence == 2 && !a.catching_up);
  CHECK(a.match.frames == 3 && a.match.events == 1 && a.match.deepest == 3);
  CHECK(a.match.samples.count == 3 && a.match.sum_ns == 6000000);
  CHECK(a.match.samples.mean() == 2.0 && a.match.samples.p99() == 3.0);
  CHECK(a.match.event_sum_ns == 6000000 && a.match.event_samples.count == 1);
  a.end(2010000000, 44, p); CHECK(a.match.events == 1); // Duplicate finalization.
  a.next_window(16000000000, {}); CHECK(a.interval.frames == 0 && a.match.frames == 3);
  a.begin(17000000000, 70, p); a.end(17001000000, 71, p); // Scene-exit closure.
  CHECK(a.interval.frames == 1 && a.match.frames == 4 && a.match.events == 2);
  a.begin(18000000000, 80, p); a.end(18000000100, 80, p); // Zero-frame event.
  CHECK(a.match.frames == 4 && a.match.events == 3);
  a.begin(19000000000, 90, p); a.end(19101000000, 91, p); // 101 ms overflow.
  CHECK(a.match.samples.buckets.back() == 1);
  auto overflow = line("match", 19000000000, a.match, {});
  CHECK(overflow.find("p99_le=overflow") != std::string::npos);
  CHECK(overflow.find("overflow=1") != std::string::npos);
  CHECK(overflow.find("max=101.000") != std::string::npos);
  a.begin(20000000000, 100, p); a.end(20006000000, 103, p);
  CHECK(a.match.frames == 8 && a.match.missed_samples == 3);
  CHECK(a.match.samples.count == 5); // Never invent per-frame timings for a missing callback.
  a.stop(p); CHECK(!p.online && !a.online);
  a.start(30000000000, p); CHECK(a.match.frames == 0 && p.catchup_sequence % 2 == 0);
  // Known p99 rank and overflow outside p99: reuse the sim-work fixed buckets.
  Stats s;
  for (int i = 0; i < 99; ++i) s.sample(1000000);
  s.sample(101000000);
  CHECK(s.samples.p99() == 1.0);
  CHECK(distribution("resim", s.samples, s.sum_ns).find("p99_le=1.000") != std::string::npos);
  CHECK(distribution("resim", s.samples, s.sum_ns).find("mean=2.000") != std::string::npos);
}
void presents() {
  Presentation p;
  PresentObserver r;
  r.present(p, 1); CHECK(p.snapshot().intervals == 0);
  p.online = true; ++p.session;
  r.present(p, 1000000000);
  r.present(p, 1010000000); CHECK(p.snapshot().intervals == 0);
  p.catchup_sequence += 2; // Begin AND end entirely between two presents.
  r.present(p, 1030000000);
  auto c = p.snapshot(); CHECK(c.intervals == 1 && c.over16 == 1 && c.over33 == 0);
  ++p.catchup_sequence; // Active catch-up.
  r.present(p, 1063000000); c = p.snapshot();
  CHECK(c.intervals == 2 && c.over16 == 2 && c.over33 == 0); // Strictly > 33.0 ms.
  r.present(p, 1096000001); c = p.snapshot();
  CHECK(c.intervals == 3 && c.over33 == 1);
  ++p.catchup_sequence;
  r.present(p, 1112700001); c = p.snapshot();
  CHECK(c.intervals == 4 && c.over16 == 3); // Strictly > 16.7 ms.
  r.present(p, 1150000000); CHECK(p.snapshot().intervals == 4); // No catch-up overlap.
  p.catchup_sequence += 4; // Multiple complete catch-ups, one presentation interval.
  r.present(p, 1150000001); CHECK(p.snapshot().intervals == 5);
  p.online = false; r.present(p, 1200000000);
  p.online = true; ++p.session; r.present(p, 1300000000);
  CHECK(p.snapshot().intervals == 5); // No interval crossing session reset.
  CHECK((p.snapshot() - c).intervals == 1);
}
int main() { timing(); presents(); std::puts("rollback host timing and catch-up presentation tests passed"); }
