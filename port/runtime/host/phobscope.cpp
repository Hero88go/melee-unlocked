// PhobScope's capture modes, ported from PhobGCC rp2040/src/main.cpp. See phobscope.h.
// SPDX-License-Identifier: GPL-3.0-only
#include "phobscope.h"
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace host::phobscope {
namespace {
constexpr int kIntOrigin = 127;
constexpr float kFloatOrigin = 127.5f;

uint8_t abxyszrl(const Ms& m) {
  return (uint8_t)(m.a << 0 | m.b << 1 | m.x << 2 | m.y << 3 | m.start << 4 | m.z << 5 | m.l << 6 | m.r << 7);
}
uint8_t out1(const Capture& c, const Ms& m) { return c.c_stick ? (c.y_axis ? m.cy : m.cx) : (c.y_axis ? m.ay : m.ax); }
float unf1(const Capture& c, const Ms& m) { return c.c_stick ? (c.y_axis ? m.cy_unfilt : m.cx_unfilt) : (c.y_axis ? m.ay_unfilt : m.ax_unfilt); }
uint8_t unf_byte(float v) { return (uint8_t)(v + kFloatOrigin); }

// The single-axis modes record continuously into a 200-sample ring.
void record1(Capture& c, const Ms& m) {
  c.a1[c.end_index] = out1(c, m);
  c.a1_unfilt[c.end_index] = unf_byte(unf1(c, m));
}
}  // namespace

void start(Capture& c, Mode mode) {
  c.mode = mode;
  c.begin = c.triggered = c.done = false;
  c.start_index = c.end_index = 0;
  c.delay_ms = 0;
  std::memset(c.percents, 0, sizeof c.percents);
  std::memset(c.a1, kIntOrigin, sizeof c.a1); std::memset(c.a2, kIntOrigin, sizeof c.a2);
  std::memset(c.a1_unfilt, kIntOrigin, sizeof c.a1_unfilt); std::memset(c.a2_unfilt, kIntOrigin, sizeof c.a2_unfilt);
  std::memset(c.buttons, 0, sizeof c.buttons); std::memset(c.analog_marks, 0, sizeof c.analog_marks);
  // A trigger at rest is 0, not the stick's centre.
  if (mode == Mode::Trigger) { std::memset(c.a1, 0, sizeof c.a1); std::memset(c.a1_unfilt, 0, sizeof c.a1_unfilt); }
}

bool feed(Capture& c, const Ms& m) {
  if (c.done || c.mode == Mode::None) return false;
  switch (c.mode) {
    case Mode::Reaction: {
      // firmware CM_REACTION: count until any button or a stick / trigger past its threshold
      c.delay_ms++;
      const bool moved = std::abs((int)m.ax - 127) >= c.stick_thresh || std::abs((int)m.ay - 127) >= c.stick_thresh ||
                         std::abs((int)m.cx - 127) >= c.stick_thresh || std::abs((int)m.cy - 127) >= c.stick_thresh ||
                         m.la >= c.trigger_thresh || m.ra >= c.trigger_thresh;
      // ABXY, the D-pad, L, R and Z (not Start)
      if (m.a || m.b || m.x || m.y || m.du || m.dd || m.dl || m.dr || m.z || m.l || m.r || moved) { c.done = true; return true; }
      return false;
    }
    case Mode::StickMap: {
      // CM_STICK_RISE2: a rolling 100-sample buffer; recording starts once the unfiltered stick has
      // moved more than stick_thresh from where it was when the recording was started.
      const float ux = c.c_stick ? m.cx_unfilt : m.ax_unfilt, uy = c.c_stick ? m.cy_unfilt : m.ay_unfilt;
      const uint8_t ox = c.c_stick ? m.cx : m.ax, oy = c.c_stick ? m.cy : m.ay;
      if (!c.begin) {
        c.begin = true;
        c.start_index = 0;
        c.end_index = 255;
        c.x0 = (int)ux; c.y0 = (int)uy;
        c.a1[0] = ox; c.a2[0] = oy; c.a1_unfilt[0] = unf_byte(ux); c.a2_unfilt[0] = unf_byte(uy);
        c.buttons[0] = abxyszrl(m);
        return false;
      }
      if (c.end_index == 255) {
        c.start_index = (c.start_index + 1) % 100;
        c.a1[c.start_index] = ox; c.a2[c.start_index] = oy;
        c.a1_unfilt[c.start_index] = unf_byte(ux); c.a2_unfilt[c.start_index] = unf_byte(uy);
        if (std::abs((int)ux - c.x0) > c.stick_thresh || std::abs((int)uy - c.y0) > c.stick_thresh)
          c.end_index = (c.start_index + 1) % 100;
        c.buttons[c.start_index] = abxyszrl(m);
        return false;
      }
      if (c.end_index != c.start_index) {
        c.a1[c.end_index] = ox; c.a2[c.end_index] = oy;
        c.a1_unfilt[c.end_index] = unf_byte(ux); c.a2_unfilt[c.end_index] = unf_byte(uy);
        c.buttons[c.end_index] = abxyszrl(m);
        c.end_index = (c.end_index + 1) % 100;
        return false;
      }
      c.done = true;
      return true;
    }
    case Mode::Dashback: {
      // CM_STICK_RISE: starts when the unfiltered axis reaches 23, keeps 50 ms before and 150 after
      record1(c, m);
      if (!c.begin && std::fabs(unf1(c, m)) >= 23) { c.begin = true; c.start_index = (c.end_index + 150) % 200; }
      bool finished = false;
      if (c.begin && c.start_index == c.end_index) {
        // dashback chance: ms spent in the tilt zone over one frame
        int counter = 0;
        for (int i = 0; i < 200; i++) {
          const int v = std::abs((int)c.a1[(i + c.start_index + 1) % 200] - kIntOrigin);
          if (v >= 23) { if (v >= 64) break; counter++; }
        }
        c.percents[0] = (float)std::fmax(0, (1 - (counter / 16.666667f)) * 100);
        // UCF: a two frame window, but the stick must have moved 75 units
        counter = 0;
        for (int offset = 0; offset < 16; offset++)
          for (int i = offset; i < (200 - 32); i += 16) {
            const int wait = std::abs((int)c.a1[(i + c.start_index + 1) % 200] - kIntOrigin);
            const int van = std::abs((int)c.a1[(i + c.start_index + 1 + 16) % 200] - kIntOrigin);
            const int ucf = std::abs((int)c.a1[(i + c.start_index + 1 + 32) % 200] - kIntOrigin);
            bool dashback = wait < 23 && van >= 64;
            if (wait < 23 && ucf >= 64 && ucf - wait > 75) dashback = true;
            if (dashback) counter++;
          }
        const float ucf_db = (float)std::fmax(0, std::fmin(100, counter * 100 / 16.0f));
        c.percents[1] = (float)std::fmax(c.percents[0], ucf_db);   // never less than vanilla
        c.done = true;
        finished = true;
      }
      c.end_index = (c.end_index + 1) % 200;
      return finished;
    }
    case Mode::Snapback: {
      // CM_STICK_FALL: waits for the unfiltered axis at 80, triggers when it drops under 23
      record1(c, m);
      const float u = std::fabs(unf1(c, m));
      if (!c.begin && !c.triggered && u >= 80) c.begin = true;
      if (c.begin && !c.triggered && u < 23) { c.triggered = true; c.start_index = (c.end_index + 150) % 200; }
      bool finished = false;
      if (c.triggered && c.start_index == c.end_index) {
        // the chance the last excursion read at ~60 Hz was positive
        c.percents[0] = 0;
        for (int offset = 0; offset < 17; offset++) {
          bool positive = false;
          for (int i = offset; i < 200; i += 17) {
            const int data = (int)c.a1[(i + c.start_index + 1) % 200] - kIntOrigin;
            if (data >= 23) positive = true;
            else if (data <= -23) positive = false;
          }
          if (positive) c.percents[0] += 100 / 17.0f;
        }
        c.done = true;
        finished = true;
      }
      c.end_index = (c.end_index + 1) % 200;
      return finished;
    }
    case Mode::Pivot: {
      // CM_STICK_PIVOT: waits for the output at 80 one way, triggers at 80 the other way
      record1(c, m);
      const int v = (int)out1(c, m) - kIntOrigin;
      if (!c.begin && !c.triggered) {
        if (v >= 80) { c.begin = true; c.negative_pivot = true; }
        else if (v <= -80) { c.begin = true; c.negative_pivot = false; }
      }
      if (c.begin && !c.triggered && v * (c.negative_pivot ? -1 : 1) >= 80) {
        c.triggered = true;
        c.start_index = (c.end_index + 150) % 200;
      }
      bool finished = false;
      if (c.triggered && c.start_index == c.end_index) {
        int counter = 0;
        for (int i = 0; i < 200; i++) {
          const int data = ((int)c.a1[(i + c.start_index + 1) % 200] - kIntOrigin) * (c.negative_pivot ? -1 : 1);
          if (data > 80) counter++;
          if (counter > 0 && data < 80) break;
        }
        const float frames = counter / 16.6666666667f;
        c.percents[0] = (float)(100 * std::fmax(0, 1 - frames));                          // no turn
        c.percents[1] = (float)(100 * std::fmax(0, std::fmin(2 - frames, frames)));      // empty pivot
        c.percents[2] = (float)(100 * std::fmax(0, std::fmin(1, frames - 1)));           // dashback
        c.done = true;
        finished = true;
      }
      c.end_index = (c.end_index + 1) % 200;
      return finished;
    }
    case Mode::Trigger: {
      // CM_TRIG: starts at an analog 43 or a digital press
      const uint8_t analog = c.c_stick ? m.ra : m.la;
      const bool digital = c.c_stick ? m.r : m.l;
      c.a1[c.end_index] = analog;
      c.a1_unfilt[c.end_index] = c.c_stick ? m.ra_raw : m.la_raw;
      c.buttons[c.end_index] = digital ? 1 : 0;
      if (!c.begin && (analog >= 43 || digital)) { c.begin = true; c.start_index = (c.end_index + 150) % 200; }
      bool finished = false;
      if (c.begin && c.start_index == c.end_index) {
        int counter = 0;
        for (int i = 0; i < 200; i++) {
          const int idx = (i + c.start_index + 1) % 200;
          if (c.buttons[idx]) break;   // digital on the same ms as analog or before: good
          if (c.a1[idx] >= 43) counter++;
        }
        const float frames = counter / 16.6666666667f;
        c.percents[0] = (float)(100 * std::fmax(0, 1 - frames));                          // digital powershield
        c.percents[1] = (float)(100 * std::fmax(0, std::fmin(2 - frames, frames)));      // ADT powershield
        c.percents[2] = (float)(100 * std::fmax(0, std::fmin(1, frames - 1)));           // no powershield
        c.done = true;
        finished = true;
      }
      c.end_index = (c.end_index + 1) % 200;
      return finished;
    }
    case Mode::Press: {
      // CM_PRESS: 200 ms from the first button or stick / trigger past its threshold
      const auto marks = [&] {
        return (uint8_t)((std::abs((int)m.ax - 127) >= c.stick_thresh) << 0 | (std::abs((int)m.ay - 127) >= c.stick_thresh) << 1 |
                         (std::abs((int)m.cx - 127) >= c.stick_thresh) << 2 | (std::abs((int)m.cy - 127) >= c.stick_thresh) << 3 |
                         (m.ra >= c.trigger_thresh) << 4 | (m.la >= c.trigger_thresh) << 5);
      };
      if (!c.begin && (m.a || m.b || m.x || m.y || m.z || m.l || m.r || marks())) c.begin = true;
      if (c.begin) {
        c.buttons[c.end_index] = (uint8_t)(abxyszrl(m) & ~0x10);   // the firmware leaves Start out here
        c.analog_marks[c.end_index] = marks();
        c.end_index++;
        if (c.end_index >= 200) { c.done = true; return true; }
      }
      return false;
    }
    default: return false;
  }
}

int ordered(const Capture& c, const uint8_t* src, uint8_t* out) {
  if (c.mode == Mode::StickMap) {
    for (int i = 0; i < 100; i++) out[i] = src[(c.start_index + i) % 100];   // the last sample before the start, then the recording
    return 100;
  }
  if (c.mode == Mode::Press) { std::memcpy(out, src, 200); return 200; }
  for (int i = 0; i < 200; i++) out[i] = src[(c.start_index + 1 + i) % 200];
  return 200;
}

}  // namespace host::phobscope
