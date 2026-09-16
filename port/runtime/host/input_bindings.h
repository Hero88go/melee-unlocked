// Remappable keyboard/XInput/DS4 -> GameCube action bindings, and which physical
// device feeds each of the 4 in-game controller ports.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
#include <xinput.h>
#include <cstdint>
#include <array>

namespace host {

enum class BindAction : uint8_t {
  A, B, X, Y, Z, Start, L, R, DUp, DDown, DLeft, DRight, Count
};

struct KeyBindings { int vk[(size_t)BindAction::Count]; };
struct PadBindings { unsigned short mask[(size_t)BindAction::Count]; };  // 0 = unbound
struct GCBindings { unsigned short mask[(size_t)BindAction::Count]; };  // GC adapter raw button mask, same layout as kActionPadBit

// Native DualShock 4 HID button masks. These are independent of XInput and are
// populated from the controller's USB/Bluetooth Raw Input report.
enum : uint16_t {
  DS4_DPAD_UP = 1u << 0, DS4_DPAD_DOWN = 1u << 1, DS4_DPAD_LEFT = 1u << 2, DS4_DPAD_RIGHT = 1u << 3,
  DS4_SQUARE = 1u << 4, DS4_CROSS = 1u << 5, DS4_CIRCLE = 1u << 6, DS4_TRIANGLE = 1u << 7,
  DS4_L1 = 1u << 8, DS4_R1 = 1u << 9, DS4_L2 = 1u << 10, DS4_R2 = 1u << 11,
  DS4_SHARE = 1u << 12, DS4_OPTIONS = 1u << 13, DS4_L3 = 1u << 14, DS4_R3 = 1u << 15,
};

// EXPERIMENTAL. Native Nintendo Switch Pro Controller HID button masks, decoded from the
// controller's own 0x30 (or 0x3F) report by switch_pro.cpp. Home and Capture are left out: there is
// no room in a 16 bit mask and neither is useful in a match.
enum : uint16_t {
  SWPRO_DPAD_UP = 1u << 0, SWPRO_DPAD_DOWN = 1u << 1, SWPRO_DPAD_LEFT = 1u << 2, SWPRO_DPAD_RIGHT = 1u << 3,
  SWPRO_B = 1u << 4, SWPRO_A = 1u << 5, SWPRO_Y = 1u << 6, SWPRO_X = 1u << 7,
  SWPRO_L = 1u << 8, SWPRO_R = 1u << 9, SWPRO_ZL = 1u << 10, SWPRO_ZR = 1u << 11,
  SWPRO_MINUS = 1u << 12, SWPRO_PLUS = 1u << 13, SWPRO_L3 = 1u << 14, SWPRO_R3 = 1u << 15,
};

// The window's WM_INPUT handler passes on every HID report that was not a DualShock's; this returns
// true when the report belonged to a Switch controller. `count` is Raw Input's report batch count.
bool switchpro_raw_input(void* device, const uint8_t* report, size_t size, size_t count);

inline constexpr uint16_t kActionPadBit[(size_t)BindAction::Count] = {
  0x0100, 0x0200, 0x0400, 0x0800, 0x0010, 0x1000, 0x0040, 0x0020, 0x0008, 0x0004, 0x0001, 0x0002
};

inline KeyBindings default_key_bindings() {
  KeyBindings k{};
  k.vk[(size_t)BindAction::A]      = 'Z';
  k.vk[(size_t)BindAction::B]      = 'X';
  k.vk[(size_t)BindAction::X]      = 'C';
  k.vk[(size_t)BindAction::Y]      = 'V';
  k.vk[(size_t)BindAction::Start]  = VK_RETURN;
  k.vk[(size_t)BindAction::L]      = 'Q';
  k.vk[(size_t)BindAction::R]      = 'W';
  k.vk[(size_t)BindAction::Z]      = 'E';
  k.vk[(size_t)BindAction::DUp]    = 'T';
  k.vk[(size_t)BindAction::DDown]  = 'G';
  k.vk[(size_t)BindAction::DLeft]  = 'F';
  k.vk[(size_t)BindAction::DRight] = 'H';
  return k;
}

inline std::array<PadBindings, 4> default_pad_bindings() {
  std::array<PadBindings, 4> pads{};
  for (auto& p : pads) {
    p.mask[(size_t)BindAction::A]      = XINPUT_GAMEPAD_A;
    p.mask[(size_t)BindAction::B]      = XINPUT_GAMEPAD_B;
    p.mask[(size_t)BindAction::X]      = XINPUT_GAMEPAD_X;
    p.mask[(size_t)BindAction::Y]      = XINPUT_GAMEPAD_Y;
    p.mask[(size_t)BindAction::Start]  = XINPUT_GAMEPAD_START;
    p.mask[(size_t)BindAction::Z]      = XINPUT_GAMEPAD_RIGHT_SHOULDER;
    p.mask[(size_t)BindAction::DUp]    = XINPUT_GAMEPAD_DPAD_UP;
    p.mask[(size_t)BindAction::DDown]  = XINPUT_GAMEPAD_DPAD_DOWN;
    p.mask[(size_t)BindAction::DLeft]  = XINPUT_GAMEPAD_DPAD_LEFT;
    p.mask[(size_t)BindAction::DRight] = XINPUT_GAMEPAD_DPAD_RIGHT;
  }
  return pads;
}

inline std::array<GCBindings, 4> default_gc_bindings() {
  std::array<GCBindings, 4> gc{};
  for (auto& g : gc) {
    g.mask[(size_t)BindAction::A]      = kActionPadBit[(size_t)BindAction::A];
    g.mask[(size_t)BindAction::B]      = kActionPadBit[(size_t)BindAction::B];
    g.mask[(size_t)BindAction::X]      = kActionPadBit[(size_t)BindAction::X];
    g.mask[(size_t)BindAction::Y]      = kActionPadBit[(size_t)BindAction::Y];
    g.mask[(size_t)BindAction::Z]      = kActionPadBit[(size_t)BindAction::Z];
    g.mask[(size_t)BindAction::Start]  = kActionPadBit[(size_t)BindAction::Start];
    g.mask[(size_t)BindAction::L]      = kActionPadBit[(size_t)BindAction::L];
    g.mask[(size_t)BindAction::R]      = kActionPadBit[(size_t)BindAction::R];
    g.mask[(size_t)BindAction::DUp]    = kActionPadBit[(size_t)BindAction::DUp];
    g.mask[(size_t)BindAction::DDown]  = kActionPadBit[(size_t)BindAction::DDown];
    g.mask[(size_t)BindAction::DLeft]  = kActionPadBit[(size_t)BindAction::DLeft];
    g.mask[(size_t)BindAction::DRight] = kActionPadBit[(size_t)BindAction::DRight];
  }
  return gc;
}

// Switch face buttons sit where a Melee player's thumb expects the GameCube ones, not where their
// letters say: B is the low button under the thumb, which is the GameCube A, so the pair is
// swapped. ZR is the natural grab button, and the L/R shoulders stay L/R (they are digital on this
// pad, so a press reports a fully pressed analog trigger, which is what Melee shields from). ZL is
// left unbound on purpose: the GameCube has no fourth shoulder, and it is free to rebind.
inline std::array<PadBindings, 4> default_swpro_bindings() {
  std::array<PadBindings, 4> pads{};
  for (auto& p : pads) {
    p.mask[(size_t)BindAction::A]      = SWPRO_B;
    p.mask[(size_t)BindAction::B]      = SWPRO_A;
    p.mask[(size_t)BindAction::X]      = SWPRO_X;
    p.mask[(size_t)BindAction::Y]      = SWPRO_Y;
    p.mask[(size_t)BindAction::Z]      = SWPRO_ZR;
    p.mask[(size_t)BindAction::Start]  = SWPRO_PLUS;
    p.mask[(size_t)BindAction::L]      = SWPRO_L;
    p.mask[(size_t)BindAction::R]      = SWPRO_R;
    p.mask[(size_t)BindAction::DUp]    = SWPRO_DPAD_UP;
    p.mask[(size_t)BindAction::DDown]  = SWPRO_DPAD_DOWN;
    p.mask[(size_t)BindAction::DLeft]  = SWPRO_DPAD_LEFT;
    p.mask[(size_t)BindAction::DRight] = SWPRO_DPAD_RIGHT;
  }
  return pads;
}

extern KeyBindings g_key_bindings;
extern std::array<PadBindings, 4> g_pad_bindings;
extern std::array<GCBindings, 4> g_gc_bindings;
extern std::array<PadBindings, 4> g_ds4_bindings;
extern std::array<PadBindings, 4> g_swpro_bindings;

// ---- port assignment: which physical device feeds each in-game port ----
// Appended to, never reordered: the settings file stores a port's source as the index of the
// combo-box entry built from this in pc_settings.cpp.
enum class DeviceKind : uint8_t { None, Keyboard, XInputPad, DS4Pad, GCAdapter, SwitchPro };

struct PortSource {
  DeviceKind kind = DeviceKind::None;
  int index = 0;   // physical controller index; unused for Keyboard/None
};

// Default: GameCube adapter port N drives game port N, as it did through 0.1.7. Port 1 falls back to
// the keyboard and the first unrouted pad when adapter port 1 is empty, so a keyboard-only or
// pad-only player is still player 1.
//
// The previous default (keyboard -> port 1, Xbox pad -> port 2, adapter port 1 -> port 3) silently
// made adapter users player 3 and pad users player 2: their controller was read but drove a port
// nobody was playing, so it looked like it "never becomes active" no matter which socket they used.
inline std::array<PortSource, 4> default_port_sources() {
  return {{
    { DeviceKind::GCAdapter, 0 },
    { DeviceKind::GCAdapter, 1 },
    { DeviceKind::GCAdapter, 2 },
    { DeviceKind::GCAdapter, 3 },
  }};
}

extern std::array<PortSource, 4> g_port_sources;

// ---- rebind capture ----
enum class CaptureDevice : uint8_t { None, Keyboard, XInputPad, DS4Pad, GCAdapter, SwitchPro };

// Starts listening. Call once when the settings UI enters "press a button" mode.
void input_begin_capture();
// Call every frame while waiting. Returns true once something new was pressed
// (or Escape was pressed to cancel — in that case device == None).
// On success: device/value/device_index identify what was pressed (value = VK code for
// Keyboard, XInput button bit for XInputPad, GC adapter button bit for GCAdapter).
bool input_poll_capture(CaptureDevice& device, int& value, int& device_index);
// Optional: abandon a capture early (e.g. UI closed mid-capture).
void input_cancel_capture();

struct InputDebugSnapshot {
  PadState ports[4]{};
  uint16_t keyboard_actions = 0;
  uint16_t xinput_actions[4]{};
  uint16_t ds4_actions[4]{};
  uint16_t gc_actions[4]{};
  uint16_t swpro_actions[4]{};
  bool xinput_connected[4]{};
  bool ds4_connected[4]{};
  bool swpro_connected[4]{};
  uint32_t gc_mask = 0;
};
void input_debug_snapshot(InputDebugSnapshot& snapshot);

}  // namespace host