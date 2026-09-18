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

// What to call the device in a slot, for the Controls tab. Empty when the slot is unused.
std::string hidpad_name(int slot);

// Live axis values as the device reports them, before anything is done to them. A cheap gamepad can
// describe itself wrongly in its own descriptor, and the only way anyone finds that out is by
// watching the raw numbers while moving the stick, so the panel shows them.
struct HidPadAxes { int32_t value[8]; const char* name[8]; int count; };
HidPadAxes hidpad_axes(int slot);

}  // namespace host
