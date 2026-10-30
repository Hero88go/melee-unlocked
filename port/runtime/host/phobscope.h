// PhobScope's recordings and Melee measurements, from PhobGCC's rp2040 main loop (the CaptureMode
// cases of main.cpp): the same triggers, the same windows and the same formulas. PhobGCC
// (https://github.com/PhobGCC/PhobGCC-SW, GPL-3.0) is by Phobos132, FrostSSBM, CarVac, NiceMitch
// and the PhobGCC contributors.
//
// The firmware records one sample per 1 ms loop. Here samples come from the adapter's reports,
// resampled to 1 ms (a report holds until the next one), so the windows and frame counts match.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>

namespace host::phobscope {

// The firmware's CaptureMode, the ones that measure something.
enum class Mode : uint8_t { None, Reaction, StickMap, Snapback, Dashback, Pivot, Trigger, Press };

// One millisecond of the controller as the game would get it. Sticks and triggers are the output
// bytes (stick origin 127); the unfiltered values are the firmware's "unfiltered" readings, the
// stick through calibration but before the filters.
struct Ms {
  uint8_t ax = 127, ay = 127, cx = 127, cy = 127, la = 0, ra = 0;
  float ax_unfilt = 0, ay_unfilt = 0, cx_unfilt = 0, cy_unfilt = 0;
  uint8_t la_raw = 0, ra_raw = 0;   // the trigger before its trigger mode (the firmware's hardware.La)
  bool a = false, b = false, x = false, y = false, start = false, z = false, l = false, r = false;
  bool du = false, dd = false, dl = false, dr = false;
};

struct Capture {
  Mode mode = Mode::None;
  bool c_stick = false;     // captureStick == CSTICK (for Trigger: the R trigger)
  bool y_axis = false;      // whichAxis == YAXIS
  // The firmware's stickThresh and triggerThresh: StickMap starts at 10; Button Timing defaults to
  // 23 / 255 (10..100 / 10..255); the Reaction Time Test to 23 / 49 (10..100 / 10..200).
  int stick_thresh = 10;
  int trigger_thresh = 255;
  bool begin = false, triggered = false, done = false;
  int start_index = 0, end_index = 0;
  uint8_t a1[200] = {}, a2[200] = {}, a1_unfilt[200] = {}, a2_unfilt[200] = {};
  uint8_t buttons[200] = {};   // abxyszrl: A B X Y S Z L R as bits 0..7 (Trigger: the digital press)
  uint8_t analog_marks[200] = {};   // Press: Ax Ay Cx Cy R L past their thresholds, bits 0..5
  float percents[3] = {};
  uint32_t delay_ms = 0;    // Reaction
  // Pivot's direction, the firmware's static negativePivot.
  bool negative_pivot = false;
  int x0 = 0, y0 = 0;
};

// Starts a recording (the firmware's prep before each mode: begin, triggered, done and the indexes
// cleared). Settings such as stick, axis and thresholds are kept.
void start(Capture& c, Mode mode);
// Feeds one millisecond. Returns true on the millisecond the recording finishes.
bool feed(Capture& c, const Ms& m);

// The samples of a finished recording in order, oldest first: 200 for the single-axis modes, 100
// for StickMap. Returns how many were written.
int ordered(const Capture& c, const uint8_t* src, uint8_t* out);

}  // namespace host::phobscope
