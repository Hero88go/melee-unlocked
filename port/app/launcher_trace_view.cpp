// SPDX-License-Identifier: GPL-2.0-or-later
#include "launcher_trace_view.h"
#include <algorithm>
#include <cmath>

namespace launcher::trace {

Overview summarize(const net_trace::Trace& trace, size_t max_bins) {
  Overview out;
  out.present = true;
  out.skipped = trace.skipped;
  double first = 0, last = 0;
  bool have_first = false;
  for (const auto& r : trace.records) {
    if (!std::isfinite(r.wall)) { ++out.skipped; continue; }
    if (!have_first) { first = last = r.wall; have_first = true; }
    last = std::max(last, r.wall);
    ++out.ticks;
  }
  if (!out.ticks) { out.error = "No complete timing records."; return out; }
  out.seconds = last - first;
  if (!std::isfinite(out.seconds)) {
    out.error = "The timing range is invalid.";
    return out;
  }
  const size_t count = std::min(out.ticks, std::clamp<size_t>(max_bins, 1, 2048));
  out.bins.resize(count);
  double previous = first;
  bool have_previous = false;
  constexpr uint8_t mark_flags[] = {net_trace::kMark, net_trace::kMarkVisual,
                                    net_trace::kMarkInput, net_trace::kMarkAudio};
  for (const auto& r : trace.records) {
    if (!std::isfinite(r.wall)) continue;
    // A damaged or older trace can move its clock backwards. Keep file order
    // and event counts without plotting a negative interval or jumping left.
    const double wall = std::max(previous, r.wall);
    const double elapsed = wall - first;
    const size_t index = out.seconds > 0 ? std::min(count - 1,
        size_t(std::clamp(elapsed / out.seconds, 0.0, 1.0) * double(count))) : 0;
    Bin& b = out.bins[index];
    ++b.ticks;
    const double interval = have_previous ? (wall - previous) * 1000.0 : 0;
    b.interval_ms = std::max(b.interval_ms, interval);
    if (std::isfinite(r.sim_ms)) b.work_ms = std::max(b.work_ms, r.sim_ms);
    b.ping_ms = std::max(b.ping_ms, r.ping_ms);
    b.depth = std::max(b.depth, r.rollback_depth);
    b.flags |= r.flags;
    const unsigned wait = (r.flags & net_trace::kWait) ? 1 : 0;
    const unsigned shed = (r.flags & net_trace::kShed) ? 1 : 0;
    const unsigned advance = (r.flags & net_trace::kAdvance) ? 1 : 0;
    b.waits += wait; b.sheds += shed; b.advances += advance;
    b.rollbacks += r.rollbacks;
    out.waits += wait; out.sheds += shed; out.advances += advance;
    out.rollbacks += r.rollbacks;
    for (size_t m = 0; m < 4; ++m) if (r.flags & mark_flags[m]) ++out.marks[m];
    previous = wall;
    have_previous = true;
  }
  return out;
}

Overview load(std::filesystem::path replay) {
  replay.replace_extension(L".trace");
  Overview out;
  std::error_code ec;
  if (!std::filesystem::is_regular_file(replay, ec)) return out;
  out.present = true;
  // Trace reading runs beside full replay inspection. Bound an unrelated or
  // damaged file before the parser allocates its text and record arrays.
  const auto size = std::filesystem::file_size(replay, ec);
  if (ec || size > 64 * 1024 * 1024) {
    out.error = "The timing file cannot be read or is larger than 64 MB.";
    return out;
  }
  net_trace::Trace trace;
  if (!net_trace::read_trace(replay.u8string(), &trace, &out.error)) return out;
  return summarize(trace);
}

} // namespace launcher::trace
