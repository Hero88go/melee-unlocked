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
// A stick axis as the reader turns it into the pad's signed value (floored, no deadzone), for tests.
int8_t hid_axis_signed(long logical_min, long logical_max, long raw, bool flip = false);

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

// What to call the device in a slot, for the Controls tab. Empty when the slot is unused.
std::string hidpad_name(int slot);

// Live axis values as the device reports them, before anything is done to them. A cheap gamepad can
// describe itself wrongly in its own descriptor, and the only way anyone finds that out is by
// watching the raw numbers while moving the stick, so the panel shows them.
struct HidPadAxes { int32_t value[8]; const char* name[8]; int count; };
HidPadAxes hidpad_axes(int slot);

}  // namespace host
