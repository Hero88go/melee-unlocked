// Whole-session timing overview, independent of the Win32 drawing code.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "net_trace_read.h"
#include <array>
#include <filesystem>

namespace launcher::trace {

struct Bin {
  uint32_t ticks = 0, waits = 0, rollbacks = 0, sheds = 0, advances = 0;
  double interval_ms = 0;
  float work_ms = 0;
  uint16_t ping_ms = 0;
  uint8_t depth = 0, flags = 0;
};

struct Overview {
  bool present = false;
  std::string error;
  size_t skipped = 0, ticks = 0, waits = 0, rollbacks = 0, sheds = 0, advances = 0;
  std::array<size_t, 4> marks{}; // general, visual, input, audio
  double seconds = 0;
  std::vector<Bin> bins;
};

// Columns cover equal spans of elapsed wall time. Each retains peaks and all
// event flags, so reducing a long match never erases an isolated hitch or mark.
Overview summarize(const net_trace::Trace& trace, size_t max_bins = 600);
// Called on the replay loader thread, never while painting the page.
Overview load(std::filesystem::path replay);

} // namespace launcher::trace
