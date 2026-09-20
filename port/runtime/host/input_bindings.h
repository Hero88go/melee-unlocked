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
  A, B, X, Y, Z, Start, L, R, DUp, DDown, DLeft, DRight,
  // C-stick directions, for devices that have them as buttons (keyboard, box controllers). A pad
  // with an analog C-stick keeps using it; a bound direction pushes the C-stick all the way.
  CUp, CDown, CLeft, CRight,
  // Control stick directions, the same idea. These were the one thing on the keyboard that could
  // not be rebound: the stick was hard wired to the arrow keys while every other key was a
  // setting, so anyone who did not want their right hand on the arrows was stuck. Left unbound on
  // a pad, whose analog stick feeds these directly.
  SUp, SDown, SLeft, SRight, Count
};
inline constexpr bool is_cstick_action(int i) { return i >= (int)BindAction::CUp && i <= (int)BindAction::CRight; }
inline constexpr bool is_stick_action(int i) { return i >= (int)BindAction::SUp && i <= (int)BindAction::SRight; }

struct KeyBindings { int vk[(size_t)BindAction::Count]; };
struct PadBindings { unsigned short mask[(size_t)BindAction::Count]; };  // 0 = unbound
struct GCBindings { unsigned short mask[(size_t)BindAction::Count]; };  // GC adapter raw button mask, same layout as kActionPadBit
// Generic HID gamepads (B0XX, Frame1, vJoy, third-party pads). Thirty-two bits rather than sixteen
// because a box controller really does have more than sixteen buttons, and the numbering is the
// device's own: bit N is HID button N+1, whatever that button happens to be labelled.
struct HidBindings { uint32_t mask[(size_t)BindAction::Count]; };

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
  0x0100, 0x0200, 0x0400, 0x0800, 0x0010, 0x1000, 0x0040, 0x0020, 0x0008, 0x0004, 0x0001, 0x0002,
  0, 0, 0, 0,  // C-stick directions are not buttons (see apply_cstick_actions)
  0, 0, 0, 0   // control stick directions likewise (see apply_stick_actions)
};

// Pushes the C-stick for bound C-stick directions in `actions` (BindAction bit indices).
inline void apply_cstick_actions(uint32_t actions, int8_t& sub_x, int8_t& sub_y) {
  const bool up = actions & (1u << (int)BindAction::CUp), down = actions & (1u << (int)BindAction::CDown);
  const bool left = actions & (1u << (int)BindAction::CLeft), right = actions & (1u << (int)BindAction::CRight);
  if (up != down) sub_y = up ? 127 : -127;
  if (left != right) sub_x = right ? 127 : -127;
}

// The same for the control stick. Returns false when nothing is bound or held, so a caller with an
// analog stick of its own can leave it alone rather than have it zeroed by an unbound keyboard.
inline bool apply_stick_actions(uint32_t actions, int8_t& stick_x, int8_t& stick_y) {
  const bool up = actions & (1u << (int)BindAction::SUp), down = actions & (1u << (int)BindAction::SDown);
  const bool left = actions & (1u << (int)BindAction::SLeft), right = actions & (1u << (int)BindAction::SRight);
  if (!up && !down && !left && !right) return false;
  if (up != down) stick_y = up ? 127 : -127;
  if (left != right) stick_x = right ? 127 : -127;
  return true;
}

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
  k.vk[(size_t)BindAction::CUp]    = 'I';
  k.vk[(size_t)BindAction::CDown]  = 'K';
  k.vk[(size_t)BindAction::CLeft]  = 'J';
  k.vk[(size_t)BindAction::CRight] = 'L';
  // The arrow keys the control stick was hard wired to, now as ordinary defaults that can be
  // rebound like everything else. Anyone happy with the arrows keeps them and notices nothing.
  k.vk[(size_t)BindAction::SUp]    = VK_UP;
  k.vk[(size_t)BindAction::SDown]  = VK_DOWN;
  k.vk[(size_t)BindAction::SLeft]  = VK_LEFT;
  k.vk[(size_t)BindAction::SRight] = VK_RIGHT;
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

// There is no standard button order across HID gamepads, so this is a starting point rather than a
// correct mapping: buttons 1-4 as the face buttons, 5 and 6 as the shoulders, 8 as grab and 10 as
// Start, which is the order most pads and most vJoy feeder configurations report. A device that
// differs gets rebound, which is why the Controls tab shows the buttons and raw axes live.
inline std::array<HidBindings, 4> default_hid_bindings() {
  std::array<HidBindings, 4> pads{};
  for (auto& p : pads) {
    p.mask[(size_t)BindAction::B]      = 1u << 0;   // HID button 1
    p.mask[(size_t)BindAction::A]      = 1u << 1;
    p.mask[(size_t)BindAction::X]      = 1u << 2;
    p.mask[(size_t)BindAction::Y]      = 1u << 3;
    p.mask[(size_t)BindAction::L]      = 1u << 4;
    p.mask[(size_t)BindAction::R]      = 1u << 5;
    p.mask[(size_t)BindAction::Z]      = 1u << 7;
    p.mask[(size_t)BindAction::Start]  = 1u << 9;
  }
  return pads;
}

// The hat switch of a HID pad, as four buttons above the device's own: many box controllers report
// their D-pad there (HID button numbers 29 to 32 are free on every device seen so far).
enum : uint32_t { HID_HAT_UP = 1u << 28, HID_HAT_RIGHT = 1u << 29, HID_HAT_DOWN = 1u << 30, HID_HAT_LEFT = 1u << 31 };

// Box controller layouts, from the Dolphin profiles their firmware or feeder ships (Dolphin numbers
// DInput buttons from 0, which is bit N here).
// B0XX-layout boxes on HayBox firmware in DInput mode (Arduino based: B0XX R1-R3, LBX), from
// HayBox_DInput.ini. Pico-based HayBox boxes default to XInput and need nothing.
inline HidBindings haybox_dinput_bindings() {
  HidBindings b{};
  b.mask[(size_t)BindAction::A] = 1u << 1;  b.mask[(size_t)BindAction::B] = 1u << 0;
  b.mask[(size_t)BindAction::X] = 1u << 3;  b.mask[(size_t)BindAction::Y] = 1u << 2;
  b.mask[(size_t)BindAction::Z] = 1u << 4;  b.mask[(size_t)BindAction::Start] = 1u << 9;
  b.mask[(size_t)BindAction::L] = 1u << 7;  b.mask[(size_t)BindAction::R] = 1u << 5;
  b.mask[(size_t)BindAction::DUp] = HID_HAT_UP;     b.mask[(size_t)BindAction::DDown] = HID_HAT_DOWN;
  b.mask[(size_t)BindAction::DLeft] = HID_HAT_LEFT; b.mask[(size_t)BindAction::DRight] = HID_HAT_RIGHT;
  return b;
}
// vJoy fed as a B0XX (the b0xx-ahk keyboard setup and others that use its profile), from
// b0xx-keyboard.ini.
inline HidBindings vjoy_b0xx_bindings() {
  HidBindings b{};
  b.mask[(size_t)BindAction::L] = 1u << 0;  b.mask[(size_t)BindAction::Y] = 1u << 1;
  b.mask[(size_t)BindAction::R] = 1u << 2;  b.mask[(size_t)BindAction::B] = 1u << 3;
  b.mask[(size_t)BindAction::A] = 1u << 4;  b.mask[(size_t)BindAction::X] = 1u << 5;
  b.mask[(size_t)BindAction::Z] = 1u << 6;  b.mask[(size_t)BindAction::Start] = 1u << 7;
  b.mask[(size_t)BindAction::DUp] = 1u << 8;    b.mask[(size_t)BindAction::DLeft] = 1u << 9;
  b.mask[(size_t)BindAction::DDown] = 1u << 10; b.mask[(size_t)BindAction::DRight] = 1u << 11;
  return b;
}

// Stick deadzones per controller family, in the game's units (a full push is 127). Zero, the
// default, passes the stick through untouched. Inside the deadzone the stick reads as centred;
// outside it is left exactly as the device sent it, so no angle a box or a notched pad produces
// is moved.
enum class PadFamily : uint8_t { GameCube, Xbox, PlayStation, Switch, Box, Count };
struct Deadzone { int main = 0, c = 0; };
extern std::array<Deadzone, (size_t)PadFamily::Count> g_deadzones;
inline void apply_deadzone(const Deadzone& dz, int8_t& x, int8_t& y, bool c) {
  const int r = c ? dz.c : dz.main;
  if (r > 0 && (int)x * x + (int)y * y < r * r) { x = 0; y = 0; }
}

extern KeyBindings g_key_bindings;
extern std::array<PadBindings, 4> g_pad_bindings;
extern std::array<GCBindings, 4> g_gc_bindings;
extern std::array<PadBindings, 4> g_ds4_bindings;
extern std::array<PadBindings, 4> g_swpro_bindings;
extern std::array<HidBindings, 4> g_hid_bindings;

// ---- port assignment: which physical device feeds each in-game port ----
// Appended to, never reordered: the settings file stores a port's source as the index of the
// combo-box entry built from this in pc_settings.cpp.
enum class DeviceKind : uint8_t { None, Keyboard, XInputPad, DS4Pad, GCAdapter, SwitchPro, HidPad };

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
// Which box controller a port was given, by name. A HID pad's slot number is only the order Windows
// happened to list the devices in this time, which can change between launches (a vJoy device and a
// GRAM, say), so a port keeps following its named device to whatever slot it lands in. Empty for
// anything but a HID pad. Saved with spaces as underscores, since a settings value is one word.
extern std::array<std::string, 4> g_port_device_names;
// The device that actually fed each port on the last poll (window.cpp): its source, or the first
// spare pad for a port left on the keyboard.
extern std::array<PortSource, 4> g_port_feeding;
inline std::string port_device_key(std::string name) {
  for (char& c : name) if (c == ' ') c = '_';
  return name;
}

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
enum class CaptureDevice : uint8_t { None, Keyboard, XInputPad, DS4Pad, GCAdapter, SwitchPro, HidPad };

// Starts listening. Call once when the settings UI enters "press a button" mode.
// Starts listening for the next press on ONE device: the kind and index of the tab being rebound.
// It used to listen to everything at once, and a press seen on a different device than the tab
// restarted the capture, which re-recorded every baseline with the button still held down. A box
// controller that also shows up as an XInput pad (or sits beside one) fired XInput first on every
// press, so the press on the device actually being rebound was swallowed into the new baseline and
// the rebind waited forever. Escape always cancels, whatever is being rebound.
void input_begin_capture(CaptureDevice want, int want_index);
void input_begin_capture();
// Call every frame while waiting. Returns true once something new was pressed
// (or Escape was pressed to cancel; in that case device == None).
// On success: device/value/device_index identify what was pressed (value = VK code for
// Keyboard, XInput button bit for XInputPad, GC adapter button bit for GCAdapter).
bool input_poll_capture(CaptureDevice& device, int& value, int& device_index);
// Optional: abandon a capture early (e.g. UI closed mid-capture).
void input_cancel_capture();

struct InputDebugSnapshot {
  PadState ports[4]{};
  uint32_t keyboard_actions = 0;
  uint32_t xinput_actions[4]{};
  uint32_t ds4_actions[4]{};
  uint32_t gc_actions[4]{};
  uint32_t swpro_actions[4]{};
  uint32_t hid_actions[4]{};
  bool xinput_connected[4]{};
  bool ds4_connected[4]{};
  bool swpro_connected[4]{};
  bool hid_connected[4]{};
  uint32_t gc_mask = 0;
  // Each device's own sticks and buttons, whether or not it plays as a port (settings picture).
  PadState keyboard_pad{};
  PadState xinput_pad[4]{}, ds4_pad[4]{}, gc_pad[4]{}, swpro_pad[4]{}, hid_pad[4]{};
  uint16_t swpro_buttons[4]{};   // raw SWPRO_* bits, for the Switch Pro picture
};
void input_debug_snapshot(InputDebugSnapshot& snapshot);

}  // namespace host
