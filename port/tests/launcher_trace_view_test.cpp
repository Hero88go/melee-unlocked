// SPDX-License-Identifier: GPL-2.0-or-later
#include "launcher_trace_view.h"
#include <cmath>
#include <cstdio>
#include <limits>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (0)

int main() {
  net_trace::Trace trace;
  // A single marked stalled tick must survive reduction of a ten-minute match
  // to just two columns. Simulation frame numbers deliberately go backwards.
  for (int i = 0; i <= 36000; ++i) {
    net_trace::Record r;
    r.wall = 1000.0 + i / 60.0 + (i >= 17900 ? 0.25 : 0);
    r.frame = i % 11;
    r.sim_ms = 2;
    r.ping_ms = 30;
    if (i == 17900) {
      r.flags = net_trace::kMarkVisual | net_trace::kWait;
      r.sim_ms = 45; r.rollbacks = 2; r.rollback_depth = 7; r.ping_ms = 140;
    }
    if (i == 0) r.flags |= net_trace::kMark;
    if (i == 36000) r.flags |= net_trace::kMarkAudio | net_trace::kAdvance;
    if (i == 18001) r.flags |= net_trace::kMarkInput | net_trace::kShed;
    trace.records.push_back(r);
  }
  auto view = launcher::trace::summarize(trace, 2);
  CHECK(view.present && view.error.empty());
  CHECK(view.ticks == 36001 && view.bins.size() == 2);
  CHECK(std::abs(view.seconds - 600.25) < 1e-8);
  CHECK(view.waits == 1 && view.rollbacks == 2 && view.sheds == 1 && view.advances == 1);
  for (auto count : view.marks) CHECK(count == 1);
  CHECK(view.bins[0].interval_ms > 266 && view.bins[0].work_ms == 45);
  CHECK(view.bins[0].depth == 7 && view.bins[0].ping_ms == 140);
  CHECK(view.bins[0].flags & net_trace::kMarkVisual);
  CHECK(view.bins[1].flags & net_trace::kMarkAudio);
  CHECK(view.bins[0].ticks + view.bins[1].ticks == view.ticks);
  CHECK(view.bins[0].rollbacks + view.bins[1].rollbacks == view.rollbacks);
  // Empty, coincident, reversed and non-finite clocks cannot create a giant
  // allocation, negative timing bar, divide by zero or invalid bin index.
  trace = {};
  CHECK(!launcher::trace::summarize(trace).error.empty());
  net_trace::Record r;
  r.wall = 3; trace.records.push_back(r);
  r.wall = 2; r.flags = net_trace::kMarkInput; trace.records.push_back(r);
  r.wall = std::numeric_limits<double>::quiet_NaN(); trace.records.push_back(r);
  trace.skipped = 2;
  view = launcher::trace::summarize(trace, 0);
  CHECK(view.bins.size() == 1 && view.bins[0].ticks == 2);
  CHECK(view.seconds == 0 && view.bins[0].interval_ms == 0);
  CHECK(view.marks[2] == 1 && view.skipped == 3);
  r.wall = 4; r.sim_ms = std::numeric_limits<float>::infinity(); trace.records.push_back(r);
  view = launcher::trace::summarize(trace, 1000000);
  CHECK(view.bins.size() == 3 && std::isfinite(view.bins.back().work_ms));
  // Parse a truncated session and preserve its marker and discarded-row count.
  const char csv[] = "frame,wall_s,mark\n1,42,2\n1,42.25,4\n2,42.3";
  std::string error;
  CHECK(net_trace::parse_trace(csv, sizeof(csv) - 1, &trace, &error));
  view = launcher::trace::summarize(trace);
  CHECK(view.ticks == 2 && view.skipped == 1);
  CHECK(view.marks[1] == 1 && view.marks[3] == 1);
  std::printf("launcher trace overview: %s\n", failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}
