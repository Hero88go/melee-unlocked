// SPDX-License-Identifier: GPL-2.0-or-later
#include "playstation_pad.h"
#include "host.h"
#include "input_bindings.h"
#include <algorithm>

namespace host {
namespace {

// Where the fields of one report sit, counted from the start of the data (after the report ID and
// any Bluetooth header). The DualShock 4, and the DualSense's short Bluetooth report, keep the hat
// and buttons right after the sticks and the triggers after those. The DualSense's full report (USB,
// or Bluetooth once it is switched on) puts its triggers straight after the sticks and its buttons
// after a counter byte.
struct Layout { size_t sticks, buttons, trigger_l, trigger_r; };
constexpr Layout kDualShock{0, 4, 7, 8};
constexpr Layout kDualSense{0, 7, 4, 5};

}  // namespace

bool playstation_parse(const uint8_t* report, size_t size, bool dualsense, PadState& pad, uint16_t& buttons) {
  if (!report || !size) return false;
  // USB reports use ID 0x01 with the data at byte 1. Over Bluetooth a DualShock 4 sends 0x11 and a
  // DualSense 0x31, each with two bytes ahead of the data. A DualSense that has not been switched to
  // its full report sends a short 0x01 in the DualShock 4 layout.
  size_t offset = 0;
  Layout layout = kDualShock;
  if (report[0] == 0x11) offset = 3;
  else if (report[0] == 0x31) { offset = 2; layout = kDualSense; }
  else if (report[0] == 0x01) { offset = 1; if (dualsense && size >= 32) layout = kDualSense; }
  if (size < offset + 9) return false;   // the short Bluetooth report ends exactly at the right trigger
  const uint8_t* d = report + offset;
  // A full-size USB report can be either layout whatever the device calls itself: third-party "PS5"
  // pads and some remappers present as a DualShock 4 but send DualSense reports, and read with the
  // DualShock 4 layout every face button landed on the left trigger's byte ("every button is L2").
  // The d-pad's released code (8) says which it is: it sits at byte 4 in a DualShock 4 report and at
  // byte 7 in a DualSense one. When both or neither read 8 (a d-pad held, or a trigger whose low
  // nibble happens to be 8), the device's own identity decides, as before.
  if (report[0] == 0x01 && size >= 32) {
    const bool ds4_hat = (d[4] & 0x0F) == 8, dualsense_hat = (d[7] & 0x0F) == 8;
    if (ds4_hat && !dualsense_hat) layout = kDualShock;
    else if (dualsense_hat && !ds4_hat) layout = kDualSense;
  }

  uint16_t b = 0;
  const uint8_t hat = d[layout.buttons] & 0x0F;   // 0 = up, clockwise to 7 = up-left, 8 = released
  if (hat == 0 || hat == 1 || hat == 7) b |= DS4_DPAD_UP;
  if (hat == 3 || hat == 4 || hat == 5) b |= DS4_DPAD_DOWN;
  if (hat == 5 || hat == 6 || hat == 7) b |= DS4_DPAD_LEFT;
  if (hat == 1 || hat == 2 || hat == 3) b |= DS4_DPAD_RIGHT;
  // Both layouts keep the face buttons in the high nibble beside the hat, and the shoulders and the
  // system buttons in the byte after it, with the same bits.
  const uint8_t face = d[layout.buttons], shoulder = d[layout.buttons + 1];
  if (face & 0x10) b |= DS4_SQUARE;
  if (face & 0x20) b |= DS4_CROSS;
  if (face & 0x40) b |= DS4_CIRCLE;
  if (face & 0x80) b |= DS4_TRIANGLE;
  if (shoulder & 0x01) b |= DS4_L1;
  if (shoulder & 0x02) b |= DS4_R1;
  if (shoulder & 0x10) b |= DS4_SHARE;
  if (shoulder & 0x20) b |= DS4_OPTIONS;
  if (shoulder & 0x40) b |= DS4_L3;
  if (shoulder & 0x80) b |= DS4_R3;

  // Y is inverted: 128 - raw reaches +128 at full up, which wrapped to -128 in an int8 and turned a
  // full tilt up into a full tilt down. Clamp to the int8 range first.
  auto up_axis = [](uint8_t raw) { return (int8_t)std::min(127, 128 - (int)raw); };
  PadState p{};
  p.err = 0;
  p.stick_x = (int8_t)((int)d[layout.sticks] - 128);
  p.stick_y = up_axis(d[layout.sticks + 1]);
  p.sub_x = (int8_t)((int)d[layout.sticks + 2] - 128);
  p.sub_y = up_axis(d[layout.sticks + 3]);
  if (d[layout.trigger_r] > 30) { b |= DS4_R2; p.trig_r = d[layout.trigger_r]; }
  if (d[layout.trigger_l] > 30) { b |= DS4_L2; p.trig_l = d[layout.trigger_l]; }
  pad = p;
  buttons = b;
  return true;
}

}  // namespace host
