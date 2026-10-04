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

  // A GameCube trigger as a binding source. Default: the controller's own click, as before.
  const uint16_t kR = host::kActionPadBit[R], kA = host::kActionPadBit[(int)host::BindAction::A];
  for (int left : {0, 100, 255}) for (uint16_t buttons : {(uint16_t)0, kL, kA, (uint16_t)(kL | kA)}) for (uint16_t mask : {kL, kR, kA, (uint16_t)0})
    CHECK(host::gc_binding_pressed(mask, 0, buttons, (uint8_t)left, 0) == ((buttons & mask) != 0));
  // Its own level: pressed above it and not at or below it, with or without the click.
  CHECK(!host::gc_binding_pressed(kL, 128, 0, 128, 0));
  CHECK(host::gc_binding_pressed(kL, 128, 0, 129, 0));
  CHECK(!host::gc_binding_pressed(kL, 128, kL, 100, 0));
  CHECK(!host::gc_binding_pressed(kR, 128, 0, 255, 128));           // the other trigger does not count
  CHECK(host::gc_binding_pressed(kR, 128, 0, 0, 129));
  CHECK(host::gc_binding_pressed(kA, 128, kA, 0, 0));               // a button has no press point
  CHECK(!host::gc_binding_pressed(kA, 128, kL, 255, 255));

  // The travel of one L analog / R analog binding: a trigger passes through, a button presses its
  // depth (the default one with no level), never below a trigger's own travel on the same binding.
  const uint32_t LT = host::kXInputBindLT, RT = host::kXInputBindRT, LB = XINPUT_GAMEPAD_LEFT_SHOULDER;
  CHECK(host::analog_binding_travel(LT, 0, 0, LT, RT, 77, 200) == 77);
  CHECK(host::analog_binding_travel(RT, 0, 0, LT, RT, 77, 200) == 200);            // either trigger can feed either side
  CHECK(host::analog_binding_travel(0, 0, LB, LT, RT, 255, 255) == 0);             // unbound: nothing
  CHECK(host::analog_binding_travel(LB, 80, LB, LT, RT, 255, 255) == 80);
  CHECK(host::analog_binding_travel(LB, 80, 0, LT, RT, 255, 255) == 0);
  CHECK(host::analog_binding_travel(LB, 0, LB, LT, RT, 0, 0) == host::kDefaultAnalogDepth);
  CHECK(host::analog_binding_travel(LB | LT, 100, LB, LT, RT, 180, 0) == 180);
  CHECK(host::analog_binding_travel(LB | LT, 100, LB, LT, RT, 40, 0) == 100);
  CHECK(host::analog_binding_travel(LT, 0, LT, LT, RT, 0, 0) == 0);                // a stray trigger bit is not a button

  // The 0.8.67 rule: a bumper bound to L, trigger at rest, low trigger value: a full press.
  host::PadBindings bind = host::default_pad_bindings()[0];
  host::xinput_bind(bind, L, XINPUT_GAMEPAD_LEFT_SHOULDER);
  host::PadState pad{};
  host::xinput_apply_bindings(bind, XINPUT_GAMEPAD_LEFT_SHOULDER, 0, 0, 200, 200, pad);
  CHECK(pad.button == kL && pad.trig_l == 0);
  host::apply_trigger_cap(43, pad.trig_l, pad.button, kL);
  CHECK(pad.trig_l == 255 && pad.button == kL);
  // On L analog instead, the same bumper is a light press and the trigger value bounds it.
  bind = host::default_pad_bindings()[0];
  host::xinput_bind(bind, (int)host::BindAction::LAnalog, XINPUT_GAMEPAD_LEFT_SHOULDER);
  bind.level[(size_t)host::BindAction::LAnalog] = 100;
  pad = {}; host::xinput_apply_bindings(bind, XINPUT_GAMEPAD_LEFT_SHOULDER, 0, 0, 200, 200, pad);
  host::apply_trigger_cap(60, pad.trig_l, pad.button, kL);
  CHECK(pad.trig_l == 60 && pad.button == 0);
  (void)kZ;
}

// ---- L and R split into the click and the travel ----
bool same_pad(const host::PadState& a, const host::PadState& b) {
  return a.button == b.button && a.trig_l == b.trig_l && a.trig_r == b.trig_r;
}
constexpr int kOldActions = (int)host::BindAction::LAnalog;   // the actions a table had before the split

// An Xbox pad as it was read before the split: the travel fixed to the triggers unless another
// action took one, a button on L or R with a level pressing that deep, and a click wherever the
// travel passed the family's point. `bind` is an old table (L analog / R analog ignored).
host::PadState old_xinput(const host::PadBindings& bind, uint16_t buttons, uint8_t left, uint8_t right, int click_l, int click_r) {
  const int L = (int)host::BindAction::L, R = (int)host::BindAction::R;
  host::PadState pad{};
  pad.trig_l = left; pad.trig_r = right;
  bool left_taken = false, right_taken = false;
  for (int i = 0; i < kOldActions; ++i) {
    if (i != L && (bind.mask[i] & host::kXInputBindLT)) left_taken = true;
    if (i != R && (bind.mask[i] & host::kXInputBindRT)) right_taken = true;
  }
  if (left_taken && !(bind.mask[L] & host::kXInputBindLT)) pad.trig_l = 0;
  if (right_taken && !(bind.mask[R] & host::kXInputBindRT)) pad.trig_r = 0;
  for (int i = 0; i < kOldActions; ++i) {
    if (!host::xinput_binding_pressed(bind.mask[i], bind.level[i], buttons, left, right, click_l, click_r)) continue;
    const int depth = (bind.mask[i] & (host::kXInputBindLT | host::kXInputBindRT)) ? 0 : bind.level[i];
    if ((i == L || i == R) && depth > 0 && depth < 255) {
      uint8_t& trigger = i == L ? pad.trig_l : pad.trig_r;
      if (trigger < depth) trigger = (uint8_t)depth;
    } else pad.button |= host::kActionPadBit[i];
  }
  if (pad.trig_l > click_l) pad.button |= host::kActionPadBit[L];
  if (pad.trig_r > click_r) pad.button |= host::kActionPadBit[R];
  return pad;
}
// The table an Xbox pad had before the split: today's defaults without the four trigger bindings.
host::PadBindings old_xinput_defaults() {
  host::PadBindings b = host::default_pad_bindings()[0];
  for (host::BindAction a : {host::BindAction::L, host::BindAction::R, host::BindAction::LAnalog, host::BindAction::RAnalog}) b.mask[(size_t)a] = 0;
  return b;
}
// An old table carried over plays exactly as it did, over a sweep of both triggers and some buttons.
void check_xinput_carried_over(host::PadBindings old_table, const char* name) {
  host::PadBindings now = old_table;
  host::migrate_trigger_bindings(now.mask, now.level, host::kXInputBindLT, host::kXInputBindRT, true);
  const uint16_t held[] = {0, XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER,
                           (uint16_t)(XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_A), XINPUT_GAMEPAD_START};
  int bad = 0;
  for (int click : {200, 120}) for (int left = 0; left < 256; left += 3) for (int right = 0; right < 256; right += 15) for (uint16_t buttons : held) {
    host::PadState got{};
    host::xinput_apply_bindings(now, buttons, (uint8_t)left, (uint8_t)right, click, click, got);
    if (!same_pad(got, old_xinput(old_table, buttons, (uint8_t)left, (uint8_t)right, click, click))) ++bad;
  }
  if (bad) std::printf("FAIL carried-over Xbox table (%s): %d inputs differ\n", name, bad), ++g_failures;
}

void check_trigger_split() {
  using A = host::BindAction;
  const int L = (int)A::L, R = (int)A::R, Z = (int)A::Z, LA = (int)A::LAnalog, RA = (int)A::RAnalog;
  const uint16_t kL = host::kActionPadBit[L], kR = host::kActionPadBit[R], kZ = host::kActionPadBit[Z], kA = host::kActionPadBit[(int)A::A];
  const uint16_t LT = host::kXInputBindLT, RT = host::kXInputBindRT, LB = XINPUT_GAMEPAD_LEFT_SHOULDER;

  // ---- defaults give the game the pad they always did ----
  // Xbox: today's default table against the old reading of the old default table.
  {
    const host::PadBindings now = host::default_pad_bindings()[0], old_table = old_xinput_defaults();
    int bad = 0;
    for (int click : {200, 60, 254}) for (int left = 0; left < 256; ++left) for (int right = 0; right < 256; right += 5)
      for (uint16_t buttons : {(uint16_t)0, (uint16_t)XINPUT_GAMEPAD_A, (uint16_t)(XINPUT_GAMEPAD_RIGHT_SHOULDER | XINPUT_GAMEPAD_START), (uint16_t)0x0C00}) {
        host::PadState got{};
        host::xinput_apply_bindings(now, buttons, (uint8_t)left, (uint8_t)right, click, click, got);
        if (!same_pad(got, old_xinput(old_table, buttons, (uint8_t)left, (uint8_t)right, click, click))) ++bad;
      }
    if (bad) std::printf("FAIL Xbox defaults: %d inputs differ from before the split\n", bad), ++g_failures;
  }
  // PlayStation: the old defaults (L1 and R1 the clicks, R2 Z) with the travel now bound to L2 / R2.
  // Before, the travel was simply whatever the reader put in the pad.
  {
    host::PadBindings bind{};
    bind.mask[(int)A::A] = host::DS4_CROSS; bind.mask[L] = host::DS4_L1; bind.mask[R] = host::DS4_R1; bind.mask[Z] = host::DS4_R2;
    host::PadBindings old_table = bind;
    bind.mask[LA] = host::DS4_L2; bind.mask[RA] = host::DS4_R2;
    int bad = 0;
    for (int left = 0; left < 256; ++left) for (int right = 0; right < 256; right += 5)
      for (uint16_t held : {(uint16_t)0, (uint16_t)host::DS4_L1, (uint16_t)(host::DS4_R1 | host::DS4_CROSS)}) {
        // As the reader decodes them: nothing at or below its press point, the L2 / R2 bit above it.
        const uint8_t l2 = left > host::kPlayStationTriggerPress ? (uint8_t)left : 0, r2 = right > host::kPlayStationTriggerPress ? (uint8_t)right : 0;
        const uint16_t buttons = (uint16_t)(held | (l2 ? host::DS4_L2 : 0) | (r2 ? host::DS4_R2 : 0));
        host::PadState want{}; want.trig_l = l2; want.trig_r = r2;
        for (int i = 0; i < kOldActions; ++i)
          if (host::ds4_binding_pressed(old_table.mask[i], 0, buttons, l2, r2)) want.button |= host::kActionPadBit[i];
        host::PadState got{}; got.trig_l = l2; got.trig_r = r2;
        host::ds4_apply_bindings(bind, buttons, got);
        if (!same_pad(got, want)) ++bad;
      }
    if (bad) std::printf("FAIL PlayStation defaults: %d inputs differ from before the split\n", bad), ++g_failures;
  }
  // GameCube adapter: the identity, with a click over a resting trigger bottoming it out.
  {
    const host::GCBindings bind = host::default_gc_bindings()[0];
    int bad = 0;
    for (int left = 0; left < 256; ++left) for (int right = 0; right < 256; right += 5)
      for (uint16_t buttons : {(uint16_t)0, kL, kR, (uint16_t)(kL | kA), kZ, (uint16_t)(kL | kR | kA | 0x1000)}) {
        host::PadState want{}; want.button = buttons; want.trig_l = (uint8_t)left; want.trig_r = (uint8_t)right;
        if ((buttons & kL) && !left) want.trig_l = 255;
        if ((buttons & kR) && !right) want.trig_r = 255;
        host::PadState got{}; got.button = buttons; got.trig_l = (uint8_t)left; got.trig_r = (uint8_t)right;
        host::gc_apply_bindings(bind, got);
        if (!same_pad(got, want)) ++bad;
      }
    if (bad) std::printf("FAIL GameCube adapter defaults: %d inputs differ from before the split\n", bad), ++g_failures;
  }

  // ---- Z on LT: the trigger is Z's alone ----
  host::PadBindings bind = host::default_pad_bindings()[0];
  host::xinput_bind(bind, Z, LT);
  CHECK(bind.mask[Z] == LT && bind.mask[L] == 0 && bind.mask[LA] == 0);        // the defaults let go: "Not bound"
  CHECK(bind.mask[R] == RT && bind.mask[RA] == RT);                            // the other side is untouched
  host::PadState pad{};
  host::xinput_apply_bindings(bind, 0, 255, 90, 200, 200, pad);
  CHECK(pad.button == kZ && pad.trig_l == 0 && pad.trig_r == 90);              // grabs; neither shields nor clicks
  pad = {}; host::xinput_apply_bindings(bind, 0, 150, 0, 200, 200, pad);
  CHECK(pad.button == 0 && pad.trig_l == 0);
  // L analog put back on LT by hand: both stay.
  host::xinput_bind(bind, LA, LT);
  CHECK(bind.mask[Z] == LT && bind.mask[LA] == LT && bind.mask[L] == 0);
  pad = {}; host::xinput_apply_bindings(bind, 0, 220, 0, 200, 200, pad);
  CHECK(pad.button == kZ && pad.trig_l == 220);
  // A trigger given to its own shoulder, or a button given to anything, takes nothing away.
  bind = host::default_pad_bindings()[0];
  host::xinput_bind(bind, L, LT); host::xinput_bind(bind, LA, LT); host::xinput_bind(bind, Z, LB);
  CHECK(bind.mask[L] == LT && bind.mask[LA] == LT && bind.mask[R] == RT && bind.mask[RA] == RT);

  // ---- L on a bumper, L analog on LT: the trigger never clicks, the bumper only clicks ----
  bind = host::default_pad_bindings()[0];
  host::xinput_bind(bind, L, LB);
  CHECK(bind.mask[LA] == LT);
  pad = {}; host::xinput_apply_bindings(bind, 0, 255, 0, 200, 200, pad);
  CHECK(pad.button == 0 && pad.trig_l == 255);
  pad = {}; host::xinput_apply_bindings(bind, LB, 0, 0, 200, 200, pad);
  CHECK(pad.button == kL && pad.trig_l == 0);
  pad = {}; host::xinput_apply_bindings(bind, LB, 70, 255, 200, 200, pad);
  CHECK(pad.button == (kL | kR) && pad.trig_l == 70 && pad.trig_r == 255);     // R is still the default trigger

  // ---- L analog on a button at depth 80: that travel, no click ----
  bind = host::default_pad_bindings()[0];
  host::xinput_bind(bind, LA, LB);
  bind.level[LA] = 80;
  pad = {}; host::xinput_apply_bindings(bind, LB, 0, 0, 200, 200, pad);
  CHECK(pad.button == 0 && pad.trig_l == 80 && pad.trig_r == 0);
  pad = {}; host::xinput_apply_bindings(bind, 0, 0, 0, 200, 200, pad);
  CHECK(pad.button == 0 && pad.trig_l == 0);
  bind.level[LA] = 0;                                                          // no level: the default light press
  pad = {}; host::xinput_apply_bindings(bind, LB, 0, 0, 200, 200, pad);
  CHECK(pad.button == 0 && pad.trig_l == host::kDefaultAnalogDepth);
  // The same on a table of plain buttons (keyboard, Switch Pro, a box), over a box's own trigger axis.
  {
    uint32_t table[(size_t)A::Count] = {};
    table[L] = 1u << 4; table[LA] = 1u << 6;
    uint8_t levels[(size_t)A::Count] = {}; levels[LA] = 80;
    auto run = [&](uint32_t buttons, uint8_t axis_l) {
      host::PadState p{};
      host::button_apply_bindings(p, axis_l, 0, [&](int i) { return (table[i] & buttons) != 0; }, [&](int i) { return (int)levels[i]; });
      return p;
    };
    CHECK(run(1u << 6, 0).trig_l == 80 && run(1u << 6, 0).button == 0);
    CHECK(run(1u << 4, 0).button == kL && run(1u << 4, 0).trig_l == 0);          // the family bottoms a click out afterwards
    CHECK(run(1u << 6, 200).trig_l == 200 && run(0, 33).trig_l == 33);           // the axis passes through
  }

  // ---- tables saved before the split ----
  // The old defaults become today's.
  {
    host::PadBindings moved = old_xinput_defaults();
    host::migrate_trigger_bindings(moved.mask, moved.level, LT, RT, true);
    const host::PadBindings now = host::default_pad_bindings()[0];
    CHECK(std::memcmp(&moved, &now, sizeof now) == 0);
    host::migrate_trigger_bindings(moved.mask, moved.level, LT, RT, true);     // and a second pass changes nothing
    CHECK(std::memcmp(&moved, &now, sizeof now) == 0);
    host::GCBindings gc = host::default_gc_bindings()[0];
    gc.mask[LA] = gc.mask[RA] = 0;
    host::migrate_trigger_bindings(gc.mask, gc.level, kL, kR, false);
    const host::GCBindings gc_now = host::default_gc_bindings()[0];
    CHECK(std::memcmp(&gc, &gc_now, sizeof gc_now) == 0);
  }
  check_xinput_carried_over(old_xinput_defaults(), "defaults");
  // Z on LT with L never touched: the trigger stays Z's alone.
  {
    host::PadBindings old_table = old_xinput_defaults();
    old_table.mask[Z] = LT;
    check_xinput_carried_over(old_table, "Z on LT");
    host::migrate_trigger_bindings(old_table.mask, old_table.level, LT, RT, true);
    CHECK(old_table.mask[L] == 0 && old_table.mask[LA] == 0 && old_table.mask[R] == RT && old_table.mask[RA] == RT);
    // L on LT by hand as well: both stayed, and still do.
    host::PadBindings both = old_xinput_defaults();
    both.mask[Z] = LT; both.mask[L] = LT; both.level[L] = 90;
    check_xinput_carried_over(both, "Z and L on LT");
  }
  // A bumper on L with a level: "press that deep, no click". It moves to L analog with the level and
  // L keeps only the click the trigger itself always gave.
  {
    host::PadBindings old_table = old_xinput_defaults();
    old_table.mask[L] = LB; old_table.level[L] = 100;
    check_xinput_carried_over(old_table, "bumper on L, depth 100");
    host::migrate_trigger_bindings(old_table.mask, old_table.level, LT, RT, true);
    CHECK(old_table.mask[LA] == (LT | LB) && old_table.level[LA] == 100 && old_table.mask[L] == LT && old_table.level[L] == 0);
    CHECK(old_table.mask[R] == RT && old_table.mask[RA] == RT && old_table.level[RA] == 0);
  }
  // A bumper on L with no level (or "full"): it stays L, beside the trigger that also clicked.
  {
    host::PadBindings old_table = old_xinput_defaults();
    old_table.mask[L] = LB; old_table.mask[R] = XINPUT_GAMEPAD_RIGHT_SHOULDER; old_table.level[R] = 255;
    old_table.mask[Z] = XINPUT_GAMEPAD_X;
    check_xinput_carried_over(old_table, "bumpers on L and R");
    host::migrate_trigger_bindings(old_table.mask, old_table.level, LT, RT, true);
    CHECK(old_table.mask[L] == (LB | LT) && old_table.mask[LA] == LT && old_table.level[LA] == 0);
  }
  // A device with no analog trigger of its own to bind (keyboard key codes here): the leveled key
  // moves, the plain one stays, and nothing else is invented.
  {
    host::KeyBindings keys = host::default_key_bindings();
    keys.level[L] = 90;
    host::migrate_trigger_bindings(keys.vk, keys.level, 0, 0, false);
    CHECK(keys.vk[L] == 0 && keys.level[L] == 0 && keys.vk[LA] == 'Q' && keys.level[LA] == 90);
    CHECK(keys.vk[R] == 'W' && keys.vk[RA] == 0 && keys.level[RA] == 0 && keys.vk[Z] == 'E');
  }
  // The adapter: the travel was always the controller's own; a leveled button on L joins it.
  {
    host::GCBindings gc = host::default_gc_bindings()[0];
    gc.mask[LA] = gc.mask[RA] = 0;
    gc.mask[L] = kA; gc.level[L] = 60;
    host::migrate_trigger_bindings(gc.mask, gc.level, kL, kR, false);
    CHECK(gc.mask[L] == 0 && gc.mask[LA] == (kL | kA) && gc.level[LA] == 60 && gc.mask[R] == kR && gc.mask[RA] == kR);
    host::PadState p{}; p.button = kA; p.trig_l = 20;
    host::gc_apply_bindings(gc, p);
    CHECK(p.trig_l == 60 && !(p.button & kL));                                 // as before: that deep, no click
  }
}

int main() {
  check_trigger_cap();
  check_binding_levels();
  check_trigger_split();
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
