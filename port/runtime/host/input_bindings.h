// Remappable keyboard/XInput -> GameCube action bindings, and which physical
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

extern KeyBindings g_key_bindings;
extern std::array<PadBindings, 4> g_pad_bindings;
extern std::array<GCBindings, 4> g_gc_bindings;

// ---- port assignment: which physical device feeds each in-game port ----
enum class DeviceKind : uint8_t { None, Keyboard, XInputPad, GCAdapter };

struct PortSource {
  DeviceKind kind = DeviceKind::None;
  int index = 0;   // XInput pad index (0-3) or GC adapter physical port (0-3); unused for Keyboard/None
};

// Default: keyboard -> port 1, Xbox pad 0 -> port 2, GC adapter port 0 -> port 3, port 4 unassigned.
inline std::array<PortSource, 4> default_port_sources() {
  return {{
    { DeviceKind::Keyboard, 0 },
    { DeviceKind::XInputPad, 0 },
    { DeviceKind::GCAdapter, 0 },
    { DeviceKind::None, 0 },
  }};
}

extern std::array<PortSource, 4> g_port_sources;

// ---- rebind capture ----
enum class CaptureDevice : uint8_t { None, Keyboard, XInputPad, GCAdapter };

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
  uint16_t gc_actions[4]{};
  bool xinput_connected[4]{};
  uint32_t gc_mask = 0;
};
void input_debug_snapshot(InputDebugSnapshot& snapshot);

}  // namespace host