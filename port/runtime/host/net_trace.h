// Online session trace: one record per online simulation tick (what the player actually got: waits
// for the other player's inputs, rollbacks, time sync sheds and advances, ping, the local pad), kept
// in a ring of the last 12 seconds for the "Network and timing" overlay (gx/net_overlay.h) and
// formatted as CSV rows for the session trace file.
//
// The simulation thread pushes, the UI thread takes a copy. Nothing here reads or writes game
// state, and nothing here is sent to the other player.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace net_trace {

enum : uint8_t {
  kWait = 1,      // the tick did not advance: the other player's inputs had not arrived
  kShed = 2,      // the tick did not advance: time sync halted this side (it was ahead)
  kAdvance = 4,   // the game ran an extra frame this tick: time sync (this side was behind)
  kMark = 8,      // the player pressed the mark key (F8) during the tick
  kMarkVisual = 16,   // marked from the controller as "that looked wrong" (D-pad Left)
  kMarkInput = 32,    // marked from the controller as "my input came out wrong or late" (D-pad Right)
  kMarkAudio = 64,    // marked as "that sounded wrong" (D-pad Down)
};

struct Record {
  double wall = 0;             // host::now_seconds() when the tick asked for the frame's inputs
  int32_t frame = 0;           // online simulation frame (Slippi numbering, the match starts at 1)
  float sim_ms = 0;            // the tick's work time (sleep excluded), rollback re-simulation included
  int32_t offset_us = 0;       // time sync offset, as last measured (every 30 frames); above 0 = ahead
  uint16_t wait_frames = 0;    // ticks spent so far in the running wait for the other player's inputs
  uint16_t ping_ms = 0;
  uint16_t presents = 0;       // frames the renderer presented during the tick (generated frames not counted)
  uint8_t rollbacks = 0;       // savestate loads in the tick
  uint8_t rollback_depth = 0;  // frames re-simulated by the deepest of them; 0 = not known
  uint8_t flags = 0;
  uint8_t pad[8] = {};         // the local pad as sent: buttons (big-endian), stick, C-stick, L, R
};

constexpr size_t kCapacity = 720;   // 12 seconds at 60 ticks a second

// Fixed ring, oldest first. Not synchronized: the shared instance in net_trace.cpp is.
class Ring {
 public:
  void clear() { head_ = 0; count_ = 0; }
  void push(const Record& record) {
    records_[(head_ + count_) % kCapacity] = record;
    if (count_ < kCapacity) ++count_;
    else head_ = (head_ + 1) % kCapacity;
  }
  size_t size() const { return count_; }
  const Record& at(size_t i) const { return records_[(head_ + i) % kCapacity]; }   // 0 = oldest
  // The newest `max` records, oldest first. Returns how many were written.
  size_t copy(Record* out, size_t max) const {
    const size_t n = count_ < max ? count_ : max;
    for (size_t i = 0; i < n; ++i) out[i] = at(count_ - n + i);
    return n;
  }

 private:
  std::array<Record, kCapacity> records_{};
  size_t head_ = 0, count_ = 0;
};

// Session trace file: one header line, then one line per record. `sync` is -1 for a shed frame,
// 1 for an advanced frame, 0 otherwise.
inline const char* csv_header() {
  return "frame,wall_s,sim_ms,wait_frames,rollbacks,rollback_depth,offset_us,sync,ping_ms,"
         "buttons,stick_x,stick_y,cstick_x,cstick_y,trigger_l,trigger_r,presents,mark\n";
}
// Writes the record's line (with the newline) and returns its length, 0 when `size` is too small.
inline size_t csv_row(const Record& r, char* out, size_t size) {
  const int sync = (r.flags & kShed) ? -1 : (r.flags & kAdvance) ? 1 : 0;
  const int n = std::snprintf(out, size, "%d,%.4f,%.2f,%u,%u,%u,%d,%d,%u,%04X,%d,%d,%d,%d,%u,%u,%u,%d\n",
                              (int)r.frame, r.wall, (double)r.sim_ms, (unsigned)r.wait_frames, (unsigned)r.rollbacks,
                              (unsigned)r.rollback_depth, (int)r.offset_us, sync, (unsigned)r.ping_ms,
                              (unsigned)((r.pad[0] << 8) | r.pad[1]), (int)(int8_t)r.pad[2], (int)(int8_t)r.pad[3],
                              (int)(int8_t)r.pad[4], (int)(int8_t)r.pad[5], (unsigned)r.pad[6], (unsigned)r.pad[7],
                              (unsigned)r.presents, (r.flags & kMarkAudio) ? 4 : (r.flags & kMarkInput) ? 3 : (r.flags & kMarkVisual) ? 2 : (r.flags & kMark) ? 1 : 0);
  return n > 0 && (size_t)n < size ? (size_t)n : 0;
}

// The shared ring (net_trace.cpp).
// Simulation thread: a new online match starts, the ring is emptied.
void begin_match();
// Simulation thread, once per online tick. Also queued for the session trace file while one is open.
void push(const Record& record);
// Session trace file (net_trace_file.h), simulation thread; the file itself is written by a worker
// thread. file_begin: a match starts and its replay is being recorded in `replay_dir`; `slp_path`
// is the replay's file when it is already named (Static Recomp), else null (Source Port, see
// replay_saved). file_end: the match is over, or the connection is gone.
void file_begin(const char* replay_dir, const char* slp_path);
void file_end();
// The Source Port's recorder wrote a replay to `slp_path`: the trace of that match takes its name.
void replay_saved(const char* slp_path);
// Any thread: the newest `max` records, oldest first.
size_t snapshot(Record* out, size_t max);
// UI thread, once per presented frame; the simulation thread takes the count once per tick.
void note_present();
uint32_t take_presents();

}  // namespace net_trace
