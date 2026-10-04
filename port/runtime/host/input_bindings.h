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
  SUp, SDown, SLeft, SRight,
  // A GameCube trigger is two inputs: the analog travel (light shield) and the click at its end
  // (full shield, tech, air dodge). L and R above are the click alone; these are the travel alone.
  // Appended, so saved profiles and every table indexed by this enum keep their meaning.
  LAnalog, RAnalog, Count
};
inline constexpr bool is_cstick_action(int i) { return i >= (int)BindAction::CUp && i <= (int)BindAction::CRight; }
inline constexpr bool is_analog_action(int i) { return i == (int)BindAction::LAnalog || i == (int)BindAction::RAnalog; }
inline constexpr bool is_stick_action(int i) { return i >= (int)BindAction::SUp && i <= (int)BindAction::SRight; }

// Every table carries a `level` per binding beside the binding itself: its own analog threshold,
// 0 = default (see "per-binding analog level" below).
struct KeyBindings { int vk[(size_t)BindAction::Count]; uint8_t level[(size_t)BindAction::Count]; };
struct PadBindings { unsigned short mask[(size_t)BindAction::Count]; uint8_t level[(size_t)BindAction::Count]; };  // 0 = unbound
// XInput reserves these wButtons bits. They identify analog triggers in binding
// capture and profiles; hardware button bits are masked before they are added.
inline constexpr uint16_t kXInputBindLT = 0x0400, kXInputBindRT = 0x0800;
inline uint16_t xinput_binding_buttons(uint16_t buttons, uint8_t left, uint8_t right,
                                      int left_threshold, int right_threshold) {
  buttons &= (uint16_t)~(kXInputBindLT | kXInputBindRT);
  if (left > left_threshold) buttons |= kXInputBindLT;
  if (right > right_threshold) buttons |= kXInputBindRT;
  return buttons;
}
struct GCBindings { unsigned short mask[(size_t)BindAction::Count]; uint8_t level[(size_t)BindAction::Count]; };  // GC adapter raw button mask, same layout as kActionPadBit
// Generic HID gamepads (B0XX, Frame1, vJoy, third-party pads). Thirty-two bits rather than sixteen
// because a box controller really does have more than sixteen buttons, and the numbering is the
// device's own: bit N is HID button N+1, whatever that button happens to be labelled.
struct HidBindings { uint32_t mask[(size_t)BindAction::Count]; uint8_t level[(size_t)BindAction::Count]; };

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
  0, 0, 0, 0,  // control stick directions likewise (see apply_stick_actions)
  0, 0         // the trigger travel is a value, not a button (see apply_actions)
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
    // The triggers, as they always behaved: the travel is the light shield, and the click comes at
    // the family's full-press point (Deadzone::click_l / click_r, which a binding with no level uses).
    p.mask[(size_t)BindAction::LAnalog] = kXInputBindLT;
    p.mask[(size_t)BindAction::RAnalog] = kXInputBindRT;
    p.mask[(size_t)BindAction::L]      = kXInputBindLT;
    p.mask[(size_t)BindAction::R]      = kXInputBindRT;
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
    // As a source of a travel binding, the L and R bits mean that trigger's own analog travel.
    g.mask[(size_t)BindAction::LAnalog] = kActionPadBit[(size_t)BindAction::L];
    g.mask[(size_t)BindAction::RAnalog] = kActionPadBit[(size_t)BindAction::R];
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
//
// Trigger values per family: 255, the default, leaves a trigger exactly as it is. Below 255 the
// trigger is analog only, the way Dolphin's L-Analog / R-Analog range works: its value is capped
// there and the full-press click is not sent. A digital or hair trigger then gives a light shield
// (Melee shields lightly from 43, hardest at 140; only the click gives a full shield).
enum class PadFamily : uint8_t { GameCube, Xbox, PlayStation, Switch, Box, Count };
inline constexpr int kDefaultTriggerClick = 200;
struct Deadzone {
  int main = 0, c = 0, trig_l = 255, trig_r = 255;
  int click_l = kDefaultTriggerClick, click_r = kDefaultTriggerClick;
};
inline void apply_trigger_click(int threshold, uint8_t value, uint16_t& button, uint16_t click) {
  if (value > threshold) button |= click;
}
inline void apply_trigger_cap(int cap, uint8_t& value, uint16_t& button, uint16_t click) {
  if (cap >= 255) return;
  // The full press arrived while the trigger itself is at rest: it comes from a button bound to L or
  // R, not from the trigger's travel. That button stays a full press (full shield, L+R+A+Start).
  if ((button & click) && value < 43) { value = 255; return; }
  if (value > cap) value = (uint8_t)(cap < 0 ? 0 : cap);
  button &= (uint16_t)~click;
}

// ---- per-binding analog level ----
// Stored beside each binding (`level` in the tables above). 0, the default, is exactly the
// behaviour from before the level existed. Otherwise, out of 255:
//  - the bound source is analog (an Xbox LT/RT, a PlayStation L2/R2, a GameCube L/R): it counts
//    as pressed above this value, whatever it is bound to;
//  - the source is a button and the action is L analog or R analog, whose output is a travel: how
//    far the button presses the trigger (0 here is kDefaultAnalogDepth). Never a click.
//
// ---- the trigger model ----
// L and R are the click alone: a button source clicks while held, an analog source clicks past its
// press point. L analog and R analog are the travel alone: an analog trigger passes its travel
// through, a button presses a fixed depth. Travel reaches the game only through those two bindings,
// so a trigger bound to Z alone neither shields nor clicks. The defaults are each family's old fixed
// behaviour written out as bindings, so a default profile gives the game the same pad as before.
inline constexpr int kPlayStationTriggerPress = 30;   // where the pad reader itself sets L2/R2 (playstation_pad.cpp)
inline constexpr int kDefaultAnalogDepth = 100;       // a button on L analog / R analog with no level: a light shield
inline constexpr bool is_trigger_action(int i) { return i == (int)BindAction::L || i == (int)BindAction::R; }
// An Xbox binding: `buttons` are the pad's own wButtons, the triggers their raw values. The
// thresholds are the family's, used while the binding has no level of its own.
inline bool xinput_binding_pressed(uint16_t mask, int level, uint16_t buttons, uint8_t left, uint8_t right,
                                   int left_threshold, int right_threshold) {
  if ((mask & kXInputBindLT) && left > (level ? level : left_threshold)) return true;
  if ((mask & kXInputBindRT) && right > (level ? level : right_threshold)) return true;
  return (buttons & mask & (uint16_t)~(kXInputBindLT | kXInputBindRT)) != 0;
}
// The player gives an Xbox input to an action. A trigger given to anything but its own shoulder (Z
// on LT) becomes that action's alone: the shoulder bindings still sitting on it, as the defaults do,
// let go and read "Not bound", or one pull would grab and then shield. The yield happens here, at
// the moment of binding, so the table always says exactly what the pad does; binding L analog (or
// L) to LT again afterwards keeps both, since that is then the player's own choice.
inline void xinput_bind(PadBindings& bind, int action, uint16_t source) {
  bind.mask[action] = source;
  const int own[2][2] = {{(int)BindAction::L, (int)BindAction::LAnalog}, {(int)BindAction::R, (int)BindAction::RAnalog}};
  const uint16_t trigger[2] = {kXInputBindLT, kXInputBindRT};
  for (int side = 0; side < 2; ++side) {
    if (source != trigger[side] || action == own[side][0] || action == own[side][1]) continue;
    for (int a : own[side]) if (bind.mask[a] == trigger[side]) bind.mask[a] = 0;
  }
}
// A PlayStation binding: `buttons` as the pad reader decoded them (L2/R2 set above its own press
// point, and the trigger values zero below it, so a level under that point acts as that point).
inline bool ds4_binding_pressed(uint16_t mask, int level, uint16_t buttons, uint8_t left, uint8_t right) {
  if (!level) return (buttons & mask) != 0;
  if ((mask & DS4_L2) && left > level) return true;
  if ((mask & DS4_R2) && right > level) return true;
  return (buttons & mask & (uint16_t)~(DS4_L2 | DS4_R2)) != 0;
}
// A GameCube adapter binding: `buttons` are the controller's own, where L and R are the physical
// clicks at the end of the trigger's travel. With no level a trigger bound to something is that
// click, as before; with one it is the trigger's travel, so it can press without bottoming out.
inline bool gc_binding_pressed(uint16_t mask, int level, uint16_t buttons, uint8_t left, uint8_t right) {
  const uint16_t click_l = kActionPadBit[(size_t)BindAction::L], click_r = kActionPadBit[(size_t)BindAction::R];
  if (!level) return (buttons & mask) != 0;
  if ((mask & click_l) && left > level) return true;
  if ((mask & click_r) && right > level) return true;
  return (buttons & mask & (uint16_t)~(click_l | click_r)) != 0;
}
// The travel an L analog / R analog binding gives. `analog_l` / `analog_r` are the bits of `mask`
// that name the device's analog triggers (either may feed either side), `left` / `right` their
// travel as read; any other bit is a button of `buttons`, which presses `level` deep while held.
inline uint8_t analog_binding_travel(uint32_t mask, int level, uint32_t buttons, uint32_t analog_l, uint32_t analog_r,
                                     uint8_t left, uint8_t right) {
  int travel = 0;
  if (mask & analog_l) travel = left;
  if ((mask & analog_r) && right > travel) travel = right;
  if (mask & buttons & ~(analog_l | analog_r)) {
    const int depth = level ? level : kDefaultAnalogDepth;
    if (depth > travel) travel = depth;
  }
  return (uint8_t)travel;
}
// Every family's binding table the same way: `pressed(i)` says whether action i's binding is down,
// `travel(i)` what an L analog / R analog binding gives (those two are asked nothing else).
// Returns the actions as BindAction bits for the settings panel.
template <class Pressed, class Travel> uint32_t apply_actions(PadState& pad, Pressed pressed, Travel travel) {
  uint32_t actions = 0;
  pad.trig_l = travel((int)BindAction::LAnalog);
  pad.trig_r = travel((int)BindAction::RAnalog);
  if (pad.trig_l) actions |= 1u << (int)BindAction::LAnalog;
  if (pad.trig_r) actions |= 1u << (int)BindAction::RAnalog;
  for (int i = 0; i < (int)BindAction::Count; ++i) {
    if (is_analog_action(i) || !pressed(i)) continue;
    actions |= (uint32_t)(1u << i);
    pad.button |= kActionPadBit[i];
  }
  apply_cstick_actions(actions, pad.sub_x, pad.sub_y);
  return actions;
}
// An Xbox pad: `buttons` its own wButtons, `left` / `right` the raw triggers, the click points the
// family's. Writes the buttons and both trigger values of `pad`.
inline uint32_t xinput_apply_bindings(const PadBindings& bind, uint16_t buttons, uint8_t left, uint8_t right,
                                      int click_l, int click_r, PadState& pad) {
  return apply_actions(pad,
      [&](int i) { return xinput_binding_pressed(bind.mask[i], bind.level[i], buttons, left, right, click_l, click_r); },
      [&](int i) { return analog_binding_travel(bind.mask[i], bind.level[i], buttons, kXInputBindLT, kXInputBindRT, left, right); });
}
// A PlayStation pad: `buttons` and the trigger values of `pad` as the reader decoded them.
inline uint32_t ds4_apply_bindings(const PadBindings& bind, uint16_t buttons, PadState& pad) {
  const uint8_t l2 = pad.trig_l, r2 = pad.trig_r;   // as read, before the bindings decide the travel
  return apply_actions(pad,
      [&](int i) { return ds4_binding_pressed(bind.mask[i], bind.level[i], buttons, l2, r2); },
      [&](int i) { return analog_binding_travel(bind.mask[i], bind.level[i], buttons, DS4_L2, DS4_R2, l2, r2); });
}
// A GameCube adapter pad: `pad` as the adapter reported it, remapped in place (the default table is
// the identity). A click arriving with its trigger at rest (from another button) bottoms the
// trigger out, as before, since the game shields from the analog value.
inline uint32_t gc_apply_bindings(const GCBindings& bind, PadState& pad) {
  const uint16_t click_l = kActionPadBit[(size_t)BindAction::L], click_r = kActionPadBit[(size_t)BindAction::R];
  const uint16_t raw = pad.button;
  const uint8_t left = pad.trig_l, right = pad.trig_r;
  pad.button = 0;
  const uint32_t actions = apply_actions(pad,
      [&](int i) { return gc_binding_pressed(bind.mask[i], bind.level[i], raw, left, right); },
      [&](int i) { return analog_binding_travel(bind.mask[i], bind.level[i], raw, click_l, click_r, left, right); });
  if ((pad.button & click_l) && !pad.trig_l) pad.trig_l = 255;
  if ((pad.button & click_r) && !pad.trig_r) pad.trig_r = 255;
  return actions;
}
// A table of plain buttons (keyboard, Switch Pro, a box): `held(i)` says whether binding i is down.
// `axis_l` / `axis_r` are a box's own trigger axes, which have no source bit of their own and so
// always pass through; zero elsewhere.
template <class Held, class Level> uint32_t button_apply_bindings(PadState& pad, uint8_t axis_l, uint8_t axis_r, Held held, Level level) {
  return apply_actions(pad, held, [&](int i) {
    const int axis = i == (int)BindAction::LAnalog ? axis_l : axis_r;
    const int depth = held(i) ? (level(i) ? level(i) : kDefaultAnalogDepth) : 0;
    return (uint8_t)(depth > axis ? depth : axis);
  });
}

// ---- tables saved before L and R were split ----
// One binding per trigger then meant all of this at once, and each piece becomes its own binding:
//  - the travel was fixed to the device's own trigger (`analog_l` / `analog_r`, 0 on a device with
//    none): that is L analog / R analog now. On an Xbox pad a trigger bound to another action, with
//    L not on it by hand, gave no travel: there it stays unbound;
//  - a button bound to L with a level 1..254 pressed the trigger that deep and never clicked: it
//    moves to L analog with that level, and L is left unbound;
//  - anything else bound to L was the click and stays L;
//  - an Xbox trigger with travel also clicked at the family's full-press point whatever L was bound
//    to: the trigger joins L's binding beside the button.
// `mask` and `level` are one table's arrays (key codes work the same way: one source per binding).
template <class M> void migrate_trigger_bindings(M* mask, uint8_t* level, uint32_t analog_l, uint32_t analog_r, bool xinput) {
  const int click[2] = {(int)BindAction::L, (int)BindAction::R};
  const int analog[2] = {(int)BindAction::LAnalog, (int)BindAction::RAnalog};
  const uint32_t own[2] = {analog_l, analog_r};
  uint32_t travel[2];
  for (int side = 0; side < 2; ++side) {   // both decided from the old table, before either is rewritten
    travel[side] = own[side];
    if (!xinput || ((uint32_t)mask[click[side]] & own[side])) continue;
    for (int i = 0; i < (int)BindAction::LAnalog; ++i)
      if (i != click[side] && ((uint32_t)mask[i] & own[side])) travel[side] = 0;
  }
  for (int side = 0; side < 2; ++side) {
    const int c = click[side], a = analog[side];
    const uint32_t old = (uint32_t)mask[c];
    uint32_t now_click = old, now_analog = travel[side];
    int analog_level = 0;
    if (old && !(old & (analog_l | analog_r)) && level[c] > 0 && level[c] < 255) {
      now_analog |= old; analog_level = level[c];
      now_click = 0; level[c] = 0;
    }
    if (xinput && travel[side] && !(now_click & own[side])) {
      if (!(now_click & (analog_l | analog_r))) level[c] = 0;   // a button's 255 meant "full": the trigger keeps the family's point
      now_click |= own[side];
    }
    mask[c] = (M)now_click; mask[a] = (M)now_analog; level[a] = (uint8_t)analog_level;
  }
}
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

// Connection facts, with no polling or device ownership. Gameplay, the settings
// window and the input preview use this same configured-route fallback policy.
struct InputDeviceFacts {
  bool xinput[4]{}, ds4[4]{}, gc[4]{}, switch_pro[4]{}, hid[4]{};
};
inline bool input_device_connected(PortSource source, const InputDeviceFacts& facts) {
  if (source.kind == DeviceKind::Keyboard) return true;
  if (source.index < 0 || source.index >= 4) return false;
  switch (source.kind) {
    case DeviceKind::XInputPad: return facts.xinput[source.index];
    case DeviceKind::DS4Pad: return facts.ds4[source.index];
    case DeviceKind::GCAdapter: return facts.gc[source.index];
    case DeviceKind::SwitchPro: return facts.switch_pro[source.index];
    case DeviceKind::HidPad: return facts.hid[source.index];
    default: return false;
  }
}
inline PortSource effective_port_source(const std::array<PortSource, 4>& sources,
                                       const InputDeviceFacts& facts, int port) {
  if (port < 0 || port >= 4) return {};
  const PortSource source = sources[port];
  if (source.kind != DeviceKind::Keyboard &&
      !(port == 0 && source.kind == DeviceKind::GCAdapter && !input_device_connected(source, facts)))
    return source;
  constexpr DeviceKind order[] = {DeviceKind::GCAdapter, DeviceKind::XInputPad,
                                 DeviceKind::DS4Pad, DeviceKind::SwitchPro, DeviceKind::HidPad};
  for (DeviceKind kind : order) for (int index = 0; index < 4; ++index) {
    const PortSource candidate{kind, index};
    if (!input_device_connected(candidate, facts)) continue;
    bool routed = false;
    for (const PortSource assigned : sources)
      if (assigned.kind == kind && assigned.index == index) { routed = true; break; }
    if (!routed) return candidate;
  }
  return {DeviceKind::Keyboard, 0};
}

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
  PortSource feeding[4]{};
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
inline InputDeviceFacts input_device_facts(const InputDebugSnapshot& snapshot) {
  InputDeviceFacts facts;
  for (int index = 0; index < 4; ++index) {
    facts.xinput[index] = snapshot.xinput_connected[index];
    facts.ds4[index] = snapshot.ds4_connected[index];
    facts.gc[index] = (snapshot.gc_mask & (1u << index)) != 0;
    facts.switch_pro[index] = snapshot.swpro_connected[index];
    facts.hid[index] = snapshot.hid_connected[index];
  }
  return facts;
}
void input_debug_snapshot(InputDebugSnapshot& snapshot);

}  // namespace host
