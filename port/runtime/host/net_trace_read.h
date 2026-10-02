// Reading a session trace file back (net_trace_file.h writes it), and the presentation schedule a
// replay viewer derives from it: for each replay frame, the online ticks the session spent on it
// and how long they took. Pure code, no host services, so the unit test links it alone.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "net_trace.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace net_trace {

struct Trace {
  std::vector<Record> records;   // file order: one per online tick
  size_t skipped = 0;            // lines that were not a whole row (a truncated last line, a short row)
};

// Parses the text of a ".trace" file. Columns are found by the header's names, so extra columns and
// another column order are accepted; a last line with no line end (the game closed while writing)
// is dropped. False, with `error` set, when the header has no "frame" or no "wall_s" column.
bool parse_trace(const char* text, size_t size, Trace* out, std::string* error);
bool read_trace(const std::string& path, Trace* out, std::string* error);

// One simulation tick at the game's rate, as the host paces it.
constexpr double kTickSeconds = 1.0 / 60.0;
// An online match's frame 1 is the replay's first frame (-123).
constexpr int32_t kOnlineToReplay = 124;

// The trace laid out by replay frame.
class Schedule {
 public:
  // `frame_offset` is the online frame number minus the replay frame number. A gap between two
  // ticks longer than `max_gap` seconds is shown as `max_gap` (a long freeze must not stall viewing).
  void build(const Trace& trace, int32_t frame_offset = kOnlineToReplay, double max_gap = 2.0);
  bool empty() const { return records_.empty(); }
  size_t size() const { return records_.size(); }
  const Record& record(size_t i) const { return records_[i]; }
  int32_t first_frame() const { return first_frame_; }   // replay numbering
  int32_t last_frame() const { return first_frame_ + (int32_t)first_.size() - 1; }
  // The ticks spent on replay frame `frame`: records [*first, *first + *count). False when the
  // trace has none for it.
  bool frame_records(int32_t frame, size_t* first, size_t* count) const;
  // Seconds between record i and the one before it, as shown (clamped) and as it was.
  double gap(size_t i) const { return i < gap_.size() ? gap_[i] : 0.0; }
  double true_gap(size_t i) const { return i < true_gap_.size() ? true_gap_[i] : 0.0; }

 private:
  std::vector<Record> records_;
  std::vector<double> gap_, true_gap_;
  std::vector<int32_t> first_, count_;   // per replay frame from first_frame_: record index (-1 none), run length
  int32_t first_frame_ = 0;
};

// Turns the schedule into holds. The viewer's own pacing gives every replay frame one tick; the
// session gave a frame one tick too when all went well, more when it waited for the other player
// or the frame ran long, and none when time sync made the game run an extra frame. The difference
// is kept as a balance, so ordinary jitter of a millisecond either way cancels out and only a real
// delay holds the picture.
class Pacer {
 public:
  struct Step {
    size_t record;   // index into the schedule
    double wait;     // seconds to hold before this record is shown
  };
  explicit Pacer(const Schedule* schedule = nullptr) : schedule_(schedule) {}
  void set(const Schedule* schedule) { schedule_ = schedule; reset(); }
  // Playback jumped: the balance starts again.
  void reset() { balance_ = 0.0; }
  // Replay frame `frame` is next. Fills `steps` with the frame's records in order and returns the
  // total hold in seconds (0 for a frame that ran on time). `true_seconds`, when given, receives
  // what the session really spent beyond one tick: more than the hold when a gap was clamped.
  double next(int32_t frame, std::vector<Step>* steps, double* true_seconds = nullptr);

 private:
  const Schedule* schedule_;
  double balance_ = 0.0;
};

constexpr double kHoldThreshold = 0.008;   // a balance under half a tick is jitter, not a hold

}  // namespace net_trace
