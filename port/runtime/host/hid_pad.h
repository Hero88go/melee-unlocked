// Generic USB/Bluetooth HID gamepads: B0XX, Frame1, vJoy and anything else that presents itself as
// an ordinary HID gamepad or joystick.
//
// The other pad paths in this port are written against a layout somebody worked out in advance: the
// DS4 reader knows axes start at byte 1, the Switch Pro reader knows report 0x30. A box controller
// cannot be read that way, because there is no single layout. What there is instead is the report
// descriptor the device publishes about itself, which Windows will parse for us through HidP. So
// this path discovers where the axes and buttons are at runtime rather than being told.
//
// Box controllers need no special maths from us. A B0XX or a Frame1 computes its own stick
// coordinates in firmware and reports them as ordinary analog axes, exactly as a real stick would,
// and a vJoy feeder does the same from the PC side. What they need is to be seen at all, and to have
// their axes left alone once they are: a deadzone applied to a box is what turns an exact wavedash
// angle into a wrong one, so there is none here.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <string>

namespace host {

struct PadState;

constexpr int kHidPadSlots = 4;

// Fills the slots that are sending input, returns their mask, and hands back the raw button bits for
// the binding table (bit N = HID button N+1, the numbering the device itself uses).
uint32_t hidpad_poll(PadState out[kHidPadSlots], uint32_t buttons[kHidPadSlots]);

// One WM_INPUT report. Returns true when this device is a generic pad and the report was consumed,
// so the caller can stop looking. Devices that already have a dedicated reader must be offered to
// that reader first.
bool hidpad_raw_input(void* device, const uint8_t* report, uint32_t size, uint32_t count);
// Windows said the device is gone (WM_INPUT_DEVICE_CHANGE, GIDC_REMOVAL): its slot is emptied, so
// its last report is no longer read as held input. A device plugged in again starts over.
void hidpad_device_removed(void* device);

// Sony pads have a reader of their own (window.cpp). DualSense is one of them: through the generic
// path its right stick lands on Z/Rz and its triggers on Rx/Ry, which a generic reader takes the other
// way round, so the C-stick pressed the shoulders and the d-pad (a hat switch) did nothing.
inline bool playstation_pad(unsigned long vendor, unsigned long product) {
  if (vendor != 0x054C) return false;
  return product == 0x05C4 || product == 0x09CC || product == 0x0BA0   // DualShock 4 v1, v2, wireless adapter
      || product == 0x0CE6 || product == 0x0DF2;                       // DualSense, DualSense Edge
}
inline bool dualsense_pad(unsigned long vendor, unsigned long product) {
  return vendor == 0x054C && (product == 0x0CE6 || product == 0x0DF2);
}

// How an analog axis used as a trigger is read. The rest position is decided from the first report:
// at the minimum (a real trigger), at the maximum (reversed), or in the middle (an axis nothing drives,
// read as a half-axis the way Dolphin's Z+ binding reads it).
constexpr int8_t kTriggerRestMin = 0, kTriggerRestCentre = 1, kTriggerRestMax = 2;
int8_t hid_trigger_rest(long logical_min, long logical_max, long raw);
uint8_t hid_trigger_value(long logical_min, long logical_max, long raw, int8_t rest);
// The trigger's origin taken off, as the console's pad library does when it reads a controller (the
// game's own clamp takes nothing off): the lowest value seen, at most 64 (a trigger held when the pad
// appeared), starting from the first. `origin` is the pad's own, kTriggerOriginUnknown at first.
constexpr uint8_t kTriggerOriginUnknown = 255, kTriggerOriginMax = 64;
inline uint8_t hid_trigger_from_origin(uint8_t value, uint8_t* origin) {
  if (*origin == kTriggerOriginUnknown) *origin = value < kTriggerOriginMax ? value : kTriggerOriginMax;
  else if (value < *origin) *origin = value;
  return value > *origin ? (uint8_t)(value - *origin) : 0;
}
// A stick axis as the reader turns it into the pad's signed value (floored, no deadzone), for tests.
int8_t hid_axis_signed(long logical_min, long logical_max, long raw, bool flip = false);

// How a stick field's raw bits carry its value. As declared (the default), or, for a device whose
// descriptor and data disagree, the field's bits as two's complement or as offset binary around the
// field's middle. A B0XX r2 on firmware v3 read in the bottom-right corner at rest on both sticks and
// could not bind a stick direction (player report 11-21): its range is not what its data is. Decided
// per axis from the first report, where a box's stick is at rest: the declared reading unless it is far
// from the centre and another reading is near it. Frame1 has its own (frame1_stick_encoding).
constexpr int8_t kStickDeclared = 0, kStickTwos = 1, kStickOffset = 2;
int8_t hid_stick_value(long logical_min, long logical_max, int bits, unsigned long raw, int8_t encoding, bool flip = false);
int8_t hid_stick_encoding(long logical_min, long logical_max, int bits, unsigned long rest);

// HayBox in its DInput mode, the firmware of the Frame1 and many other box controllers, describes six
// 16-bit axes (X, Y, Z, Rz, Rx, Ry), a hat and buttons 1-16, and publishes no name of its own (on a
// Pico it is just "Pico"), so the descriptor is how it is recognised. Its C-stick is Rx/Ry, its
// triggers are Z (left) and Rz (right), and its buttons follow HayBox_DInput.ini. The generic reader
// took Z as the right trigger and never read Rz, so its L pressed both shields and R none.
// `axes` is in the reader's order: X, Y, Z, Rx, Ry, Rz, Slider, Hat.
struct HidAxisRange { bool present = false; long logical_min = 0, logical_max = 0; };
inline bool haybox_dinput_shape(const HidAxisRange (&axes)[8], unsigned button_min, unsigned button_max) {
  for (int i = 0; i < 6; ++i)
    if (!axes[i].present || axes[i].logical_min != 0 || axes[i].logical_max != 0xFFFF) return false;
  return !axes[6].present && axes[7].present && button_min == 1 && button_max == 16;
}
// Whether the device in a slot has that shape.
bool hidpad_haybox_dinput(int slot);
// A B0XX on its own firmware 4.x (hid_pad.cpp: Arduino Leonardo, signed axes, buttons 1-20).
bool hidpad_b0xx_firmware4(int slot);

// A Frame1 on its own firmware in DInput mode (Mod Y held on plug-in) names itself "Frame1 Dinput".
// It is not HayBox: its maker's public pico-rectangle firmware ("HID with triggers", which it grew
// from) publishes X and Y for the control stick, Z and Rx for the C-stick, two Sliders for the
// analog triggers, 16-bit each, and buttons 1-16 in the order A B X Y Start, -, -, -, D-pad left
// right down up, Z R L. Each stick field is the GameCube byte moved into the top of the word
// (((v - 128) << 8) + 127, Y negated), so it rests near 0, not mid-range. The generic reader scales
// from the middle of the declared range, and a player's Frame1 read as full left and full up at
// rest (log: stick -127,127 c-stick -127,0): stuck in the top-left corner.
inline bool frame1_dinput_name(std::string lower) {
  for (char& ch : lower) ch = (char)((ch >= 'A' && ch <= 'Z') ? ch - 'A' + 'a' : ch);
  return lower.rfind("frame1", 0) == 0 || lower.find("pico-rectangle") != std::string::npos;
}
// The two ways a 16-bit field can carry that byte: two's complement around 0 (pico-rectangle) or
// offset binary around the middle (HayBox and most pads). Decided per axis from the first report,
// where a box's stick is at rest, by whichever reads nearer the centre.
constexpr int8_t kFrame1Twos = 0, kFrame1Offset = 1;
inline int frame1_field(unsigned long raw, int bits, int8_t encoding) {
  if (bits < 8 || bits > 16) bits = 16;
  const unsigned long mask = (1ul << bits) - 1, u = raw & mask;
  const int shift = bits - 8;
  if (encoding == kFrame1Offset) return (int)(u >> shift) - 128;
  const long s = u >= (1ul << (bits - 1)) ? (long)u - (long)(1ul << bits) : (long)u;
  return (int)(s >> shift);   // arithmetic: floors, so -20353 is -80 as the firmware meant
}
inline int8_t frame1_stick_encoding(unsigned long rest, int bits) {
  const int twos = frame1_field(rest, bits, kFrame1Twos), offset = frame1_field(rest, bits, kFrame1Offset);
  return (twos < 0 ? -twos : twos) <= (offset < 0 ? -offset : offset) ? kFrame1Twos : kFrame1Offset;
}
// A stick axis as the pad's signed value; flip for Y, which the firmware sends down-positive.
inline int8_t frame1_stick(unsigned long raw, int bits, int8_t encoding, bool flip = false) {
  int v = frame1_field(raw, bits, encoding);
  if (flip) v = -v;
  return (int8_t)(v > 127 ? 127 : v < -128 ? -128 : v);
}
// The analog triggers: pico-rectangle sends ((l - 128) << 8) + 128, other builds l << 8. A trigger
// rests low, so the encoding that reads lower on the first report is the one in use.
inline uint8_t frame1_trigger(unsigned long raw, int bits, int8_t encoding) {
  const int v = encoding == kFrame1Twos ? frame1_field(raw, bits, kFrame1Twos) + 128
                                        : frame1_field(raw, bits, kFrame1Offset) + 128;
  return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}
inline int8_t frame1_trigger_encoding(unsigned long rest, int bits) {
  return frame1_trigger(rest, bits, kFrame1Twos) <= frame1_trigger(rest, bits, kFrame1Offset) ? kFrame1Twos : kFrame1Offset;
}
// Whether the device in a slot is read as a Frame1 in DInput mode.
bool hidpad_frame1_dinput(int slot);

// What to call the device in a slot, for the Controls tab. Empty when the slot is unused.
std::string hidpad_name(int slot);

// Live axis values as the device reports them, before anything is done to them. A cheap gamepad can
// describe itself wrongly in its own descriptor, and the only way anyone finds that out is by
// watching the raw numbers while moving the stick, so the panel shows them.
struct HidPadAxes { int32_t value[8]; const char* name[8]; int count; };
HidPadAxes hidpad_axes(int slot);

// Choosing an axis by moving it (g_hid_axis_role). `base` and `now` are every axis as a stick value
// (-128..127 over its range), `present` a bit per axis. The axis that moved furthest, when that is at
// least kHidAxisMoved (a light shield button moves a trigger axis about a fifth of its range, and a
// resting stick's jitter is far below that), or -1. The hat (index 7) is never an answer.
constexpr int kHidAxisMoved = 32;
inline int hid_moved_axis(const int8_t base[8], const int8_t now[8], unsigned present) {
  int best = -1, most = 0;   // not "far": windows.h defines that away
  for (int i = 0; i < 7; ++i) {
    if (!((present >> i) & 1u)) continue;
    const int d = (int)now[i] - (int)base[i], a = d < 0 ? -d : d;
    if (a > most) { most = a; best = i; }
  }
  return most >= kHidAxisMoved ? best : -1;
}
// Which axis a role reads: the player's choice when that axis exists, otherwise the reader's own
// (`automatic`, -1 for none) unless the player gave that axis to another role.
inline int hid_role_axis(const uint8_t roles[6], int role, int automatic, unsigned present) {
  const uint8_t chosen = roles[role];
  if (chosen < 7 && ((present >> chosen) & 1u)) return chosen;
  for (int r = 0; r < 6; ++r)
    if (r != role && automatic >= 0 && roles[r] == automatic) return -1;
  return automatic;
}
// Remembers where every axis of the pad is now; the poll then answers with the axis moved since
// (hid_moved_axis), -1 while none has. Not offered for a Frame1, which has a reader of its own.
void hidpad_axis_capture_begin(int slot);
int hidpad_axis_capture_poll(int slot);
// Gives `axis` to `role` (HID_ROLE_*) for this pad, taking it from any role that had it. The pad is
// expected to be held where the capture found it: right or up for a stick, which sets or clears that
// direction's invert switch so the push reads the way it was made; a trigger rests where the capture began.
void hidpad_assign_axis(int slot, int role, int axis);
const char* hidpad_axis_name(int slot, int axis);

}  // namespace host
