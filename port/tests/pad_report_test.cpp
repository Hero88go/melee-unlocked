// Byte-level checks for the controller readers that cannot be tried without the hardware: the Sony
// report layouts (DualShock 4 and DualSense, USB and Bluetooth) and the HID trigger rest detection.
// Each report is built the way the pad sends it, with one field set, and the decoded result checked.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "host.h"
#include "hid_pad.h"
#include "input_bindings.h"
#include "playstation_pad.h"
#include <cstdio>
#include <cstring>
#include <vector>

// hid_pad.cpp logs when a device appears; nothing appears here.
namespace host { void log(const char*, ...) {} }

namespace {
int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

// A report with the pad at rest: sticks centred, hat released (8), no buttons, triggers up.
enum class Kind { Ds4Usb, Ds4Bt, DualSenseUsb, DualSenseBtFull, DualSenseBtShort };

struct Built { std::vector<uint8_t> bytes; size_t data; bool dualsense; size_t sticks, buttons, trig_l, trig_r; };

Built rest(Kind kind) {
  Built b{};
  switch (kind) {
    case Kind::Ds4Usb:          b.bytes.assign(64, 0); b.bytes[0] = 0x01; b.data = 1; b.dualsense = false; break;
    case Kind::Ds4Bt:           b.bytes.assign(78, 0); b.bytes[0] = 0x11; b.bytes[1] = 0xC0; b.data = 3; b.dualsense = false; break;
    case Kind::DualSenseUsb:    b.bytes.assign(64, 0); b.bytes[0] = 0x01; b.data = 1; b.dualsense = true; break;
    case Kind::DualSenseBtFull: b.bytes.assign(78, 0); b.bytes[0] = 0x31; b.bytes[1] = 0x10; b.data = 2; b.dualsense = true; break;
    case Kind::DualSenseBtShort:b.bytes.assign(10, 0); b.bytes[0] = 0x01; b.data = 1; b.dualsense = true; break;
  }
  const bool full_dualsense = kind == Kind::DualSenseUsb || kind == Kind::DualSenseBtFull;
  b.sticks = b.data;
  b.buttons = b.data + (full_dualsense ? 7 : 4);
  b.trig_l = b.data + (full_dualsense ? 4 : 7);
  b.trig_r = b.data + (full_dualsense ? 5 : 8);
  for (int i = 0; i < 4; ++i) b.bytes[b.sticks + i] = 0x80;
  b.bytes[b.buttons] = 0x08;   // hat released
  return b;
}

bool decode(const Built& b, host::PadState& pad, uint16_t& buttons) {
  return host::playstation_parse(b.bytes.data(), b.bytes.size(), b.dualsense, pad, buttons);
}

void check_layout(Kind kind, const char* name) {
  host::PadState pad{};
  uint16_t buttons = 0xFFFF;
  Built b = rest(kind);
  CHECK(decode(b, pad, buttons));
  if (buttons != 0 || pad.stick_x || pad.stick_y || pad.sub_x || pad.sub_y || pad.trig_l || pad.trig_r)
    std::printf("FAIL %s at rest: buttons %04X stick %d,%d c %d,%d trig %u/%u\n", name, buttons, pad.stick_x, pad.stick_y,
                pad.sub_x, pad.sub_y, pad.trig_l, pad.trig_r), ++g_failures;

  // Each stick axis on its own, so a layout that is off by one byte cannot pass by symmetry.
  b = rest(kind); b.bytes[b.sticks + 0] = 0xFF; decode(b, pad, buttons);
  CHECK(pad.stick_x == 127 && pad.stick_y == 0 && pad.sub_x == 0 && pad.sub_y == 0);
  b = rest(kind); b.bytes[b.sticks + 1] = 0x00; decode(b, pad, buttons);   // Y up
  CHECK(pad.stick_y == 127 && pad.stick_x == 0 && pad.sub_x == 0);
  b = rest(kind); b.bytes[b.sticks + 2] = 0x00; decode(b, pad, buttons);   // C-stick left
  CHECK(pad.sub_x == -128 && pad.stick_x == 0 && pad.trig_l == 0 && pad.trig_r == 0);
  b = rest(kind); b.bytes[b.sticks + 3] = 0xFF; decode(b, pad, buttons);   // C-stick down
  CHECK(pad.sub_y == -127 && pad.trig_l == 0 && pad.trig_r == 0);

  // Triggers are analog and never move a stick.
  b = rest(kind); b.bytes[b.trig_l] = 0xFF; decode(b, pad, buttons);
  CHECK(pad.trig_l == 255 && pad.trig_r == 0 && (buttons & host::DS4_L2) && pad.sub_x == 0 && pad.sub_y == 0);
  b = rest(kind); b.bytes[b.trig_r] = 0xC0; decode(b, pad, buttons);
  CHECK(pad.trig_r == 0xC0 && pad.trig_l == 0 && (buttons & host::DS4_R2) && pad.sub_x == 0 && pad.sub_y == 0);

  // The d-pad is a hat: 0 up, 2 right, 4 down, 6 left, odd values the diagonals.
  struct { uint8_t hat; uint16_t want; } hats[] = {
    {0, host::DS4_DPAD_UP}, {1, host::DS4_DPAD_UP | host::DS4_DPAD_RIGHT}, {2, host::DS4_DPAD_RIGHT},
    {4, host::DS4_DPAD_DOWN}, {6, host::DS4_DPAD_LEFT}, {7, host::DS4_DPAD_UP | host::DS4_DPAD_LEFT}, {8, 0}};
  for (const auto& h : hats) {
    b = rest(kind); b.bytes[b.buttons] = h.hat; decode(b, pad, buttons);
    if (buttons != h.want) std::printf("FAIL %s hat %u: got %04X want %04X\n", name, h.hat, buttons, h.want), ++g_failures;
  }

  // Face buttons share the hat byte; shoulders and system buttons are the byte after it.
  struct { size_t byte; uint8_t bit; uint16_t want; } keys[] = {
    {0, 0x10, host::DS4_SQUARE}, {0, 0x20, host::DS4_CROSS}, {0, 0x40, host::DS4_CIRCLE}, {0, 0x80, host::DS4_TRIANGLE},
    {1, 0x01, host::DS4_L1}, {1, 0x02, host::DS4_R1}, {1, 0x10, host::DS4_SHARE}, {1, 0x20, host::DS4_OPTIONS},
    {1, 0x40, host::DS4_L3}, {1, 0x80, host::DS4_R3}};
  for (const auto& k : keys) {
    b = rest(kind); b.bytes[b.buttons + k.byte] |= k.bit; decode(b, pad, buttons);
    if (buttons != k.want) std::printf("FAIL %s byte +%zu bit %02X: got %04X want %04X\n", name, k.byte, k.bit, buttons, k.want), ++g_failures;
  }
}

void check_triggers() {
  // A real trigger rests at its minimum and reads as a full-range axis.
  CHECK(host::hid_trigger_rest(0, 255, 0) == host::kTriggerRestMin);
  CHECK(host::hid_trigger_value(0, 255, 0, host::kTriggerRestMin) == 0);
  CHECK(host::hid_trigger_value(0, 255, 255, host::kTriggerRestMin) == 255);
  // vJoy's Z with nothing writing it: 16384 of 0..32767. Released there, fully pressed at the top.
  const int8_t vjoy = host::hid_trigger_rest(0, 32767, 16384);
  CHECK(vjoy == host::kTriggerRestCentre);
  CHECK(host::hid_trigger_value(0, 32767, 16384, vjoy) == 0);
  CHECK(host::hid_trigger_value(0, 32767, 32767, vjoy) == 255);
  CHECK(host::hid_trigger_value(0, 32767, 0, vjoy) == 0);                      // the other half does nothing
  CHECK(host::hid_trigger_value(0, 32767, 24576, vjoy) >= 126 && host::hid_trigger_value(0, 32767, 24576, vjoy) <= 129);
  // vJoy's slider at 1 (as in the report): a trigger resting at the bottom.
  CHECK(host::hid_trigger_rest(0, 32767, 1) == host::kTriggerRestMin);
  // An axis that rests at its maximum is read the other way up.
  const int8_t top = host::hid_trigger_rest(0, 1023, 1023);
  CHECK(top == host::kTriggerRestMax);
  CHECK(host::hid_trigger_value(0, 1023, 1023, top) == 0 && host::hid_trigger_value(0, 1023, 0, top) == 255);
}
}  // namespace

int main() {
  check_layout(Kind::Ds4Usb, "DS4 USB");
  check_layout(Kind::Ds4Bt, "DS4 Bluetooth");
  check_layout(Kind::DualSenseUsb, "DualSense USB");
  check_layout(Kind::DualSenseBtFull, "DualSense Bluetooth");
  check_layout(Kind::DualSenseBtShort, "DualSense Bluetooth short");
  check_triggers();
  // A pad that calls itself a DualShock 4 (dualsense = false) but sends a DualSense USB report:
  // Cross must read as Cross, not as the left trigger (a player saw every button as L2).
  {
    uint8_t r[64] = {};
    r[0] = 0x01; r[1] = r[2] = r[3] = r[4] = 0x80;   // sticks centred
    r[5] = 0; r[6] = 0;                             // triggers released
    r[8] = 0x20 | 0x08;                             // Cross held, d-pad released
    host::PadState p{}; uint16_t b = 0;
    CHECK(host::playstation_parse(r, sizeof r, false, p, b));
    CHECK((b & host::DS4_CROSS) && !(b & host::DS4_L2) && p.trig_l == 0);
    // And a real DualShock 4 report with Cross held still reads the same way.
    uint8_t q[64] = {};
    q[0] = 0x01; q[1] = q[2] = q[3] = q[4] = 0x80; q[5] = 0x20 | 0x08; q[8] = 0; q[9] = 0;
    CHECK(host::playstation_parse(q, sizeof q, false, p, b));
    CHECK((b & host::DS4_CROSS) && !(b & host::DS4_L2));
    CHECK(host::playstation_parse(q, sizeof q, true, p, b));   // even if it claimed to be a DualSense
    CHECK((b & host::DS4_CROSS) && !(b & host::DS4_L2));
  }
  // Too short to hold the fields: refused, not read past the end.
  host::PadState pad{}; uint16_t buttons = 0;
  const uint8_t tiny[4] = {0x01, 0x80, 0x80, 0x80};
  CHECK(!host::playstation_parse(tiny, sizeof tiny, false, pad, buttons));
  std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "all pad report checks passed", g_failures, g_failures == 1 ? "" : "s");
  return g_failures ? 1 : 0;
}
