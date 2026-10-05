// SPDX-License-Identifier: GPL-2.0-or-later
#include "replay_revisions.h"
#include <cstdio>

namespace replay_revisions {
namespace {

// A damaged file gives no schedule at all: half of one would restore to the wrong frame.
Schedule fail(size_t position, const char* what, long long frame, long long expected) {
  char text[160];
  std::snprintf(text, sizeof text, "entry %zu: frame %lld %s %lld", position, frame, what, expected);
  Schedule out;
  out.error = text;
  return out;
}

}  // namespace

size_t Schedule::final_position(int32_t frame) const {
  if (!valid || frame < first_frame || frame > last_frame) return npos;
  return final_positions[(size_t)((int64_t)frame - first_frame)];
}

size_t Schedule::first_shown_position(int32_t frame) const {
  if (!valid || frame < first_frame || frame > last_frame) return npos;
  return shown_positions[(size_t)((int64_t)frame - first_frame)];
}

Schedule::Next Schedule::next_kind(size_t position, int32_t* rollback_target) const {
  if (!valid || position >= entries.size() || position + 1 >= entries.size()) return Next::End;
  const Entry& now = entries[position];
  const Entry& next = entries[position + 1];
  if (!now.shown) return Next::Hidden;
  if (next.shown) return Next::Normal;
  // build() lets only a re-simulation follow a shown entry with a frame that is not greater.
  if (rollback_target) *rollback_target = next.frame;
  return Next::Rollback;
}

Schedule build(const std::vector<int32_t>& frames, int max_rollback) {
  Schedule s;
  if (frames.empty()) {
    s.error = "no frames";
    return s;
  }
  if (max_rollback < 0) max_rollback = 0;
  s.entries.reserve(frames.size());
  s.first_frame = frames[0];
  const int64_t first = frames[0];
  int64_t top = first;   // the highest frame so far: the last one shown
  size_t hidden = 0;     // length of the re-simulation in progress
  int32_t run_first = 0;

  for (size_t i = 0; i < frames.size(); ++i) {
    const int64_t frame = frames[i];
    // Inside a re-simulation every entry is the one before plus 1, up to the shown frame that
    // follows the highest one; anything else is a skipped or lost entry.
    if (hidden && frame != (int64_t)frames[i - 1] + 1)
      return fail(i, "inside a rollback, expected", frame, (int64_t)frames[i - 1] + 1);

    if (i == 0 || frame > top) {
      if (i && frame != top + 1) return fail(i, "skips ahead, expected", frame, top + 1);
      Entry e;
      e.frame = frames[i];
      e.shown = true;
      e.hidden_before = (uint8_t)(hidden > 255 ? 255 : hidden);
      if (hidden) s.rollbacks.push_back({run_first, (int32_t)top, (int)hidden, i});
      hidden = 0;
      top = frame;
      s.entries.push_back(e);
      s.shown_positions.push_back(i);
      s.final_positions.push_back(i);
      continue;
    }

    if (!hidden) {
      if (frame < first) return fail(i, "is before the first frame", frame, first);
      if (top - frame + 1 > max_rollback)
        return fail(i, "rolls back too far from frame", frame, top);
      run_first = frames[i];
    }
    ++hidden;
    s.entries.push_back({frames[i], false, 0});
    s.final_positions[(size_t)(frame - first)] = i;
  }

  s.trailing_hidden = hidden;
  s.last_frame = (int32_t)top;
  s.shown_count = s.shown_positions.size();
  s.valid = true;
  return s;
}

}  // namespace replay_revisions
