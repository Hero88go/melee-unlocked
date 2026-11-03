// Device-independent binding types shared by input, profiles and the settings overlay.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <cstddef>

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
  LAnalog, RAnalog,
  // A box controller's own buttons, for one that sends plain buttons and leaves the stick maths to
  // the PC (a Frame1 or B0XX in DInput or keyboard mode, or a keyboard set up as one). Binding Mod X
  // or Mod Y on a device turns its button-made stick and C-stick into the box's Melee coordinates
  // (box_melee.h); a device without them keeps the plain full-push directions. The shield buttons
  // are the box's light and mid shield, analog travel on R. Appended, like the rest.
  ModX, ModY, LightShield, MidShield,
  // Dolphin's per-stick Modifier: while held, a stick made of buttons pushes only part of the way
  // (g_stick_modifier_percent, 50 by default), so a keyboard can walk. Appended, like the rest.
  StickMod,
  Count
};
// 0 = unbound. Levels are analog thresholds; 0 uses the device default.
struct KeyBindings { int vk[(std::size_t)BindAction::Count]; uint8_t level[(std::size_t)BindAction::Count]; };
struct PadBindings { unsigned short mask[(std::size_t)BindAction::Count]; uint8_t level[(std::size_t)BindAction::Count]; };  // 0 = unbound
struct GCBindings { unsigned short mask[(std::size_t)BindAction::Count]; uint8_t level[(std::size_t)BindAction::Count]; };  // GC adapter raw button mask, same layout as kActionPadBit
// Generic HID pads use 32 button bits; bit N is the device's own button N+1.
struct HidBindings { uint32_t mask[(std::size_t)BindAction::Count]; uint8_t level[(std::size_t)BindAction::Count]; };

enum class CaptureDevice : uint8_t { None, Keyboard, XInputPad, DS4Pad, GCAdapter, SwitchPro, HidPad };

extern KeyBindings g_key_bindings;
extern std::array<PadBindings, 4> g_pad_bindings;
extern std::array<GCBindings, 4> g_gc_bindings;
extern std::array<PadBindings, 4> g_ds4_bindings;
extern std::array<PadBindings, 4> g_swpro_bindings;
extern std::array<HidBindings, 4> g_hid_bindings;

}  // namespace host
