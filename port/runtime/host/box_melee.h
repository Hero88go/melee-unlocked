// Box controller (B0XX layout) Melee coordinates, for a control stick and a C-stick made of buttons.
//
// A keyboard, or a box in DInput or keyboard mode, sends plain buttons: Left, Right, Down, Up, the
// four C buttons, Mod X, Mod Y, B, the shoulders and the light and mid shield buttons. On a box in
// its normal mode the firmware turns those into stick positions; here the PC does the same, with the
// coordinates the B0XX publishes for Melee (HayBox's Melee mode sends the same ones). They are in
// the game's own units: 80 is a full push and the game reads value / 80. Its stick clamp is a circle
// of radius 80, so every coordinate below reaches the game exactly as written (the one exception is
// noted where it happens, and the game clamps it the same way when a box sends it).
//
// Written for this project from the published coordinate values; no firmware code is used.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>

namespace host::box {

// A coordinate as two magnitudes; the directions held give the signs.
struct Coord { int8_t x, y; };

// Two opposing buttons (Left and Right, Down and Up, and the same pairs of C buttons). Held together,
// the one pressed last wins, and the one it beat stays dead until it is let go, even when the newer
// one is released first: "second input priority without reactivation", the rule the B0XX and
// HayBox's Melee mode use on all four pairs.
struct Opposing {
  bool had_neg = false, had_pos = false;   // held on the previous poll
  int8_t dead = 0;                         // -1 or +1: the side that lost, ignored until released
  int8_t out = 0;
  int8_t update(bool neg, bool pos) {
    if (!neg && dead < 0) dead = 0;
    if (!pos && dead > 0) dead = 0;
    const bool new_neg = neg && !had_neg, new_pos = pos && !had_pos;
    had_neg = neg; had_pos = pos;
    if (neg && pos) {
      if (new_neg && !new_pos) { out = -1; dead = 1; }
      else if (new_pos && !new_neg) { out = 1; dead = -1; }
      else if (new_neg && new_pos) out = 0;   // both on the same poll: neither, until one is let go
      // otherwise both are still held and the winner stays
    } else if (neg) {
      out = dead < 0 ? 0 : -1;
    } else if (pos) {
      out = dead > 0 ? 0 : 1;
    } else {
      out = 0;
    }
    return out;
  }
};

struct State { Opposing x, y, c_x, c_y; };

struct Buttons {
  bool left = false, right = false, down = false, up = false;
  bool c_left = false, c_right = false, c_down = false, c_up = false;
  bool mod_x = false, mod_y = false;
  bool shield = false;   // L, R, light shield or mid shield: the air dodge and shield drop angles
  bool b = false;        // B: the extended up-B angles, and Mod Y's full speed turnaround B
};

struct Result {
  bool stick = false;    // a control stick button is held: the stick below replaces the device's own
  int8_t stick_x = 0, stick_y = 0;
  bool c = false;        // a C button is held, or the D-pad layer is on: the C-stick below replaces it
  int8_t c_x = 0, c_y = 0;
  bool dpad_up = false, dpad_down = false, dpad_left = false, dpad_right = false;
};

inline constexpr int kFull = 80;
inline constexpr Coord kDiagonal{56, 56};             // 0.7000 0.7000
inline constexpr Coord kShieldDownDiagonal{56, 55};   // 0.7000 0.6875: shield drop, a 44.5 degree wavedash
inline constexpr Coord kModX{53, 43};                 // horizontal 0.6625 (walk), vertical 0.5375
inline constexpr Coord kModXShield{51, 30};           // 0.6375 0.3750: the long wavedash
inline constexpr Coord kModY{27, 59};                 // horizontal 0.3375 (slow walk), vertical 0.7375
inline constexpr Coord kModYShieldUp{38, 70};         // 0.4750 0.8750
inline constexpr Coord kModYShieldDown{40, 68};       // 0.5000 0.8500: the short wavedash
// A modifier on a diagonal with no shield button: the up-B angles. Column 0 is the modifier alone,
// then C-Down, C-Left, C-Up and C-Right held with it; row 1 is the same with B held (the extended
// angles, further from the centre). Mod X runs 23 to 41 degrees above the horizontal, Mod Y 67 to 49.
inline constexpr Coord kModXDiagonal[2][5] = {
    {{59, 25}, {56, 29}, {63, 39}, {56, 41}, {49, 42}},
    {{73, 31}, {70, 36}, {68, 42}, {59, 43}, {51, 43}}};
inline constexpr Coord kModYDiagonal[2][5] = {
    {{25, 59}, {29, 56}, {39, 63}, {41, 56}, {51, 61}},
    {{31, 73}, {36, 70}, {42, 68}, {47, 64}, {47, 57}}};
inline constexpr Coord kCDiagonal{42, 68};            // 0.5250 0.8500: ASDI slide off
inline constexpr Coord kCModXSide{68, 42};            // 0.8500 0.5250: angled forward smash
// Trigger values (out of 255; the game's shield range ends at 140) for the two shield buttons.
inline constexpr uint8_t kLightShield = 49, kMidShield = 94;

// One poll. `state` carries the opposing-button rule from poll to poll, so each device (and each
// reader of a device) keeps its own.
inline Result melee(const Buttons& in, State& state) {
  Result r;
  const int x = state.x.update(in.left, in.right), y = state.y.update(in.down, in.up);
  const int cx = state.c_x.update(in.c_left, in.c_right), cy = state.c_y.update(in.c_down, in.c_up);
  r.stick = in.left || in.right || in.down || in.up;
  const bool dpad_layer = in.mod_x && in.mod_y;
  r.c = dpad_layer || in.c_left || in.c_right || in.c_down || in.c_up;

  // The C button that picks an up-B angle. With two held (one of each pair) the later one in this
  // order wins, as on the box: Down, Left, Up, Right.
  int angle = 0;
  if (cy < 0) angle = 1;
  if (cx < 0) angle = 2;
  if (cy > 0) angle = 3;
  if (cx > 0) angle = 4;
  const bool diagonal = x && y;
  Coord m{0, 0};
  if (in.mod_y) {   // Mod Y decides when both are held (Mod X + Mod Y is the D-pad layer)
    if (diagonal) m = in.shield ? (y > 0 ? kModYShieldUp : kModYShieldDown) : kModYDiagonal[in.b ? 1 : 0][angle];
    else m = kModY;   // only the axis being held counts
    // B with Mod Y: the stick is not slowed, so a turnaround neutral B gains no reach. The up-B angles
    // (a diagonal with no shield) keep their own values. With a shield on a diagonal this gives
    // (80, 70 or 68), outside the game's circle: the game clamps it as it does from a box.
    if (in.b && !(diagonal && !in.shield)) m.x = kFull;
  } else if (in.mod_x) {
    if (diagonal) m = in.shield ? kModXShield : kModXDiagonal[in.b ? 1 : 0][angle];
    else m = kModX;
  } else if (diagonal) {
    m = in.shield && y < 0 ? kShieldDownDiagonal : kDiagonal;
  } else {
    m = {kFull, kFull};
  }
  // Left and Right held together with no vertical: full speed sideways whatever the modifiers
  // say, for the furthest ledge dash.
  if (in.left && in.right && !y) m.x = kFull;
  r.stick_x = (int8_t)(x * m.x);
  r.stick_y = (int8_t)(y * m.y);

  if (dpad_layer) {   // the C buttons are the D-pad and the C-stick rests
    r.dpad_up = cy > 0; r.dpad_down = cy < 0; r.dpad_left = cx < 0; r.dpad_right = cx > 0;
  } else if (cx && cy) {
    r.c_x = (int8_t)(cx * kCDiagonal.x); r.c_y = (int8_t)(cy * kCDiagonal.y);
  } else if (cx && in.mod_x) {   // tilted by the control stick's up or down
    r.c_x = (int8_t)(cx * kCModXSide.x); r.c_y = (int8_t)(y * kCModXSide.y);
  } else {
    r.c_x = (int8_t)(cx * kFull); r.c_y = (int8_t)(cy * kFull);
  }
  return r;
}

}  // namespace host::box
