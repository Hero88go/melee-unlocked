// SPDX-License-Identifier: GPL-2.0-or-later
#include "net_trace_read.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace net_trace {
namespace {

enum Column {
  C_FRAME, C_WALL, C_SIM, C_WAIT, C_ROLLBACKS, C_DEPTH, C_OFFSET, C_SYNC, C_PING, C_BUTTONS,
  C_STICK_X, C_STICK_Y, C_CSTICK_X, C_CSTICK_Y, C_TRIGGER_L, C_TRIGGER_R, C_PRESENTS, C_MARK,
  C_MARK_FLAGS, C_DESYNC, C_COUNT
};
const char* const kColumnNames[C_COUNT] = {
    "frame", "wall_s", "sim_ms", "wait_frames", "rollbacks", "rollback_depth", "offset_us", "sync", "ping_ms", "buttons",
    "stick_x", "stick_y", "cstick_x", "cstick_y", "trigger_l", "trigger_r", "presents", "mark", "mark_flags", "desync"};

struct Field {
  const char* begin;
  const char* end;
};

// The fields of one line (no line end in [begin, end)).
void split(const char* begin, const char* end, std::vector<Field>& out) {
  out.clear();
  const char* start = begin;
  for (const char* p = begin;; ++p) {
    if (p == end || *p == ',') {
      out.push_back({start, p});
      if (p == end) break;
      start = p + 1;
    }
  }
}

// A field as text the C library can parse; false when it is empty or too long to be a number.
bool field_text(const Field& f, char* buf, size_t size) {
  const char* b = f.begin;
  const char* e = f.end;
  while (b < e && (*b == ' ' || *b == '\t')) ++b;
  while (e > b && (e[-1] == ' ' || e[-1] == '\t')) --e;
  const size_t n = (size_t)(e - b);
  if (n == 0 || n >= size) return false;
  std::memcpy(buf, b, n);
  buf[n] = 0;
  return true;
}
bool field_long(const Field& f, long* value, int base = 10) {
  char buf[40];
  if (!field_text(f, buf, sizeof buf)) return false;
  char* stop = nullptr;
  *value = std::strtol(buf, &stop, base);
  return stop != buf && *stop == 0;
}
bool field_double(const Field& f, double* value) {
  char buf[40];
  if (!field_text(f, buf, sizeof buf)) return false;
  char* stop = nullptr;
  *value = std::strtod(buf, &stop);
  return stop != buf && *stop == 0;
}

}  // namespace

bool parse_trace(const char* text, size_t size, Trace* out, std::string* error) {
  out->records.clear();
  out->skipped = 0;
  const char* p = text;
  const char* const end = text + size;
  // A UTF-8 byte order mark, in case the file went through an editor.
  if (size >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;
  int column[C_COUNT];
  std::fill(column, column + C_COUNT, -1);
  size_t header_fields = 0;
  bool have_header = false;
  std::vector<Field> fields;
  while (p < end) {
    const char* nl = (const char*)std::memchr(p, '\n', (size_t)(end - p));
    if (!nl) {
      // No line end: the writer was cut off inside this row.
      if (have_header) ++out->skipped;
      break;
    }
    const char* line_end = nl;
    if (line_end > p && line_end[-1] == '\r') --line_end;
    const char* line = p;
    p = nl + 1;
    if (line == line_end) continue;
    split(line, line_end, fields);
    if (!have_header) {
      have_header = true;
      header_fields = fields.size();
      for (size_t i = 0; i < fields.size(); ++i) {
        char name[40];
        if (!field_text(fields[i], name, sizeof name)) continue;
        for (int c = 0; c < C_COUNT; ++c)
          if (column[c] < 0 && std::strcmp(name, kColumnNames[c]) == 0) column[c] = (int)i;
      }
      if (column[C_FRAME] < 0 || column[C_WALL] < 0) {
        if (error) *error = "the first line is not a session trace header (no frame or wall_s column)";
        return false;
      }
      continue;
    }
    if (fields.size() < header_fields) { ++out->skipped; continue; }
    Record r;
    long v = 0;
    double d = 0;
    if (!field_long(fields[column[C_FRAME]], &v) || !field_double(fields[column[C_WALL]], &d)) { ++out->skipped; continue; }
    r.frame = (int32_t)v;
    r.wall = d;
    // The other columns are optional: a file from a build that did not write one reads as zero.
    auto integer = [&](int c, long lo, long hi, int base = 10) -> long {
      long value = 0;
      if (column[c] < 0 || !field_long(fields[column[c]], &value, base)) return 0;
      return std::clamp(value, lo, hi);
    };
    if (column[C_SIM] >= 0 && field_double(fields[column[C_SIM]], &d)) r.sim_ms = (float)d;
    r.wait_frames = (uint16_t)integer(C_WAIT, 0, 0xFFFF);
    r.rollbacks = (uint8_t)integer(C_ROLLBACKS, 0, 0xFF);
    r.rollback_depth = (uint8_t)integer(C_DEPTH, 0, 0xFF);
    r.offset_us = (int32_t)integer(C_OFFSET, INT32_MIN, INT32_MAX);
    r.ping_ms = (uint16_t)integer(C_PING, 0, 0xFFFF);
    r.presents = (uint16_t)integer(C_PRESENTS, 0, 0xFFFF);
    const long sync = integer(C_SYNC, -1, 1);
    // The file has no column for a wait: a tick that counts wait frames is one (slippi_online.cpp
    // counts them only while the other player's inputs are missing).
    if (sync < 0) r.flags |= kShed;
    else if (sync > 0) r.flags |= kAdvance;
    else if (r.wait_frames > 0) r.flags |= kWait;
    const long mark = integer(C_MARK, 0, 4);   // 1 plain, 2 looked wrong, 3 input wrong, 4 sounded wrong
    if (mark) r.flags |= mark == 4 ? kMarkAudio : mark == 3 ? kMarkInput : mark == 2 ? kMarkVisual : kMark;
    r.flags |= (uint8_t)integer(C_MARK_FLAGS, 0, 0xFF) & (kMark | kMarkVisual | kMarkInput | kMarkAudio);
    if (integer(C_DESYNC, 0, 1)) r.flags |= kDesync;
    const long buttons = integer(C_BUTTONS, 0, 0xFFFF, 16);
    r.pad[0] = (uint8_t)(buttons >> 8);
    r.pad[1] = (uint8_t)buttons;
    r.pad[2] = (uint8_t)(int8_t)integer(C_STICK_X, -128, 127);
    r.pad[3] = (uint8_t)(int8_t)integer(C_STICK_Y, -128, 127);
    r.pad[4] = (uint8_t)(int8_t)integer(C_CSTICK_X, -128, 127);
    r.pad[5] = (uint8_t)(int8_t)integer(C_CSTICK_Y, -128, 127);
    r.pad[6] = (uint8_t)integer(C_TRIGGER_L, 0, 255);
    r.pad[7] = (uint8_t)integer(C_TRIGGER_R, 0, 255);
    out->records.push_back(r);
  }
  if (!have_header) {
    if (error) *error = "the file is empty";
    return false;
  }
  return true;
}

bool read_trace(const std::string& path, Trace* out, std::string* error) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) {
    if (error) *error = "cannot open the file";
    return false;
  }
  std::vector<char> text;
  char buf[65536];
  for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) {
    text.insert(text.end(), buf, buf + n);
    if (text.size() > 256u * 1024u * 1024u) break;   // hours of play are a few megabytes
  }
  std::fclose(f);
  return parse_trace(text.data(), text.size(), out, error);
}

void Schedule::build(const Trace& trace, int32_t frame_offset, double max_gap) {
  records_ = trace.records;
  gap_.assign(records_.size(), 0.0);
  true_gap_.assign(records_.size(), 0.0);
  first_.clear();
  count_.clear();
  first_frame_ = 0;
  if (records_.empty()) return;
  // Nothing says how long the first tick took: one tick, so the schedule starts with no balance.
  gap_[0] = true_gap_[0] = kTickSeconds;
  int64_t lo = INT64_MAX, hi = INT64_MIN;
  for (size_t i = 0; i < records_.size(); ++i) {
    if (i > 0) {
      const double d = std::max(0.0, records_[i].wall - records_[i - 1].wall);
      true_gap_[i] = d;
      gap_[i] = std::min(d, max_gap);
    }
    const int64_t frame = (int64_t)records_[i].frame - frame_offset;
    lo = std::min(lo, frame);
    hi = std::max(hi, frame);
  }
  // A frame column that is not a match's frame numbers (hours long would be 216000 an hour).
  if (hi - lo > 4000000) {
    records_.clear();
    gap_.clear();
    true_gap_.clear();
    return;
  }
  first_frame_ = (int32_t)lo;
  first_.assign((size_t)(hi - lo + 1), -1);
  count_.assign((size_t)(hi - lo + 1), 0);
  for (size_t i = 0; i < records_.size(); ++i) {
    const size_t f = (size_t)((int64_t)records_[i].frame - frame_offset - lo);
    if (first_[f] < 0) {
      first_[f] = (int32_t)i;
      count_[f] = 1;
    } else if ((size_t)(first_[f] + count_[f]) == i) {
      ++count_[f];   // the ticks of one frame follow each other; a later return to it is not shown
    }
  }
}

bool Schedule::frame_records(int32_t frame, size_t* first, size_t* count) const {
  if (first_.empty() || frame < first_frame_ || frame > last_frame()) return false;
  const size_t f = (size_t)(frame - first_frame_);
  if (first_[f] < 0) return false;
  *first = (size_t)first_[f];
  *count = (size_t)count_[f];
  return true;
}

double Pacer::next(int32_t frame, std::vector<Step>* steps, double* true_seconds) {
  steps->clear();
  if (true_seconds) *true_seconds = 0.0;
  size_t first = 0, count = 0;
  if (!schedule_ || !schedule_->frame_records(frame, &first, &count)) return 0.0;
  // The gap before a tick is the time the tick before it took, so a frame that ran long holds the
  // picture one frame later than it did in the session. The length is what matters.
  double shown = 0.0, real = 0.0;
  for (size_t i = first; i < first + count; ++i) {
    shown += schedule_->gap(i);
    real += schedule_->true_gap(i);
  }
  balance_ += shown - kTickSeconds;
  // The session ran frames faster than the viewer can (time sync's extra frames): the credit is
  // capped at one tick so it cannot swallow a later freeze.
  if (balance_ < -kTickSeconds) balance_ = -kTickSeconds;
  double hold = 0.0;
  if (balance_ > kHoldThreshold) {
    hold = balance_;
    balance_ = 0.0;
  }
  if (true_seconds && hold > 0.0) *true_seconds = hold + (real - shown);
  for (size_t i = first; i < first + count; ++i)
    steps->push_back({i, hold > 0.0 && shown > 0.0 ? hold * schedule_->gap(i) / shown : 0.0});
  return hold;
}

}  // namespace net_trace
