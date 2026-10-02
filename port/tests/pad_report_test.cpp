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

// Trigger value per family (Dolphin's L-Analog range): Full leaves the trigger alone; a number caps
// the analog value and drops the click, so a digital or hair trigger gives a light shield.
void check_trigger_cap() {
  const uint16_t kClick = 0x0040;
  uint8_t v = 255; uint16_t b = kClick | 0x0100;
  host::apply_trigger_cap(255, v, b, kClick);                 // default: untouched
  CHECK(v == 255 && b == (kClick | 0x0100));
  host::apply_trigger_cap(100, v, b, kClick);                 // digital press or hair trigger at 255
  CHECK(v == 100 && b == 0x0100);                             // light shield, no click, other buttons kept
  v = 60; b = 0;
  host::apply_trigger_cap(100, v, b, kClick);                 // a half press below the cap passes through
  CHECK(v == 60 && b == 0);
  v = 0; b = 0;
  host::apply_trigger_cap(100, v, b, kClick);                 // released stays released
  CHECK(v == 0 && b == 0);
  v = 0; b = kClick;
  host::apply_trigger_cap(43, v, b, kClick);                  // a button bound to L, trigger at rest
  CHECK(v == 255 && b == kClick);                             // stays a full press (L+R+A+Start)
}

// Per-binding levels: 0, the default, is exactly the behaviour from before they existed; a level is
// honoured; and a bumper bound to L under a low trigger value is still a full press.
void check_binding_levels() {
  const int L = (int)host::BindAction::L, R = (int)host::BindAction::R, Z = (int)host::BindAction::Z;
  const uint16_t kL = host::kActionPadBit[L], kZ = host::kActionPadBit[Z];
  // Every table starts with no levels.
  for (int i = 0; i < (int)host::BindAction::Count; ++i)
    CHECK(!host::default_key_bindings().level[i] && !host::default_pad_bindings()[0].level[i] &&
          !host::default_gc_bindings()[0].level[i] && !host::default_swpro_bindings()[0].level[i] &&
          !host::default_hid_bindings()[0].level[i] && !host::vjoy_b0xx_bindings().level[i]);

  // An Xbox trigger as a binding source. Default: the same answer as the old combined button word,
  // reserved bits in the pad's own word included.
  const uint16_t masks[] = {host::kXInputBindLT, host::kXInputBindRT, XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_A, 0};
  const uint16_t held = XINPUT_GAMEPAD_LEFT_SHOULDER | host::kXInputBindLT;
  for (int left : {0, 100, 200, 201, 255}) for (int right : {0, 140, 141, 255}) for (uint16_t mask : masks) {
    const uint16_t old_buttons = host::xinput_binding_buttons(held, (uint8_t)left, (uint8_t)right, 200, 140);
    CHECK(host::xinput_binding_pressed(mask, 0, held, (uint8_t)left, (uint8_t)right, 200, 140) == ((old_buttons & mask) != 0));
  }
  // Its own level: pressed above it, whatever the family's press point is.
  CHECK(!host::xinput_binding_pressed(host::kXInputBindLT, 60, 0, 60, 0, 200, 200));
  CHECK(host::xinput_binding_pressed(host::kXInputBindLT, 60, 0, 61, 0, 200, 200));
  CHECK(!host::xinput_binding_pressed(host::kXInputBindRT, 250, 0, 255, 240, 200, 200));
  CHECK(host::xinput_binding_pressed(host::kXInputBindRT, 250, 0, 0, 251, 200, 200));
  CHECK(host::xinput_binding_pressed(XINPUT_GAMEPAD_A, 60, XINPUT_GAMEPAD_A, 0, 0, 200, 200));   // a button has no press point

  // A PlayStation trigger as a binding source. Default: the reader's own L2/R2 bit.
  CHECK(host::ds4_binding_pressed(host::DS4_R2, 0, host::DS4_R2, 0, 40));
  CHECK(!host::ds4_binding_pressed(host::DS4_R2, 0, 0, 0, 0));
  CHECK(!host::ds4_binding_pressed(host::DS4_R2, 0, host::DS4_L2, 255, 0));
  CHECK(!host::ds4_binding_pressed(host::DS4_R2, 120, host::DS4_R2, 0, 120));
  CHECK(host::ds4_binding_pressed(host::DS4_R2, 120, host::DS4_R2, 0, 121));
  CHECK(host::ds4_binding_pressed(host::DS4_L2, 120, host::DS4_L2, 200, 0));
  CHECK(host::ds4_binding_pressed(host::DS4_CROSS, 120, host::DS4_CROSS, 0, 0));
  CHECK(!host::ds4_binding_pressed(host::DS4_CROSS, 120, host::DS4_L2, 255, 0));

  // A button bound to L or R. Default (and 255): the button, as before.
  host::PadState pad{};
  host::apply_bound_press(L, 0, pad);
  CHECK(pad.button == kL && pad.trig_l == 0 && pad.trig_r == 0);
  pad = {}; host::apply_bound_press(L, 255, pad);
  CHECK(pad.button == kL && pad.trig_l == 0);
  // Its own depth: the trigger pressed that far, no click, the other trigger untouched.
  pad = {}; host::apply_bound_press(L, 100, pad);
  CHECK(pad.button == 0 && pad.trig_l == 100 && pad.trig_r == 0);
  pad = {}; pad.trig_r = 180; host::apply_bound_press(R, 100, pad);   // never below the trigger's own travel
  CHECK(pad.button == 0 && pad.trig_r == 180 && pad.trig_l == 0);
  pad = {}; host::apply_bound_press(Z, 100, pad);                     // only L and R have a depth
  CHECK(pad.button == kZ && pad.trig_l == 0 && pad.trig_r == 0);

  // The 0.8.67 rule: a bumper bound to L, trigger at rest, low trigger value: a full press.
  pad = {}; host::apply_bound_press(L, 0, pad);
  host::apply_trigger_cap(43, pad.trig_l, pad.button, kL);
  CHECK(pad.trig_l == 255 && pad.button == kL);
  // Given a depth of its own, the same bumper is a light press and the trigger value bounds it.
  pad = {}; host::apply_bound_press(L, 100, pad);
  host::apply_trigger_cap(60, pad.trig_l, pad.button, kL);
  CHECK(pad.trig_l == 60 && pad.button == 0);
  pad = {}; host::apply_bound_press(L, 50, pad);
  host::apply_trigger_cap(255, pad.trig_l, pad.button, kL);
  CHECK(pad.trig_l == 50 && pad.button == 0);
}

int main() {
  check_trigger_cap();
  check_binding_levels();
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
