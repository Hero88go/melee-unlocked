// Box controller Melee coordinates (box_melee.h) and box mode on a binding table (apply_box_actions):
// every value checked against the published B0XX Melee coordinates, in the game's units (80 = full).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "host.h"
#include "input_bindings.h"
#include <cstdio>

namespace {
int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

using host::box::Buttons;
using host::box::Result;
using host::box::State;

// One poll from a fresh state: the opposing-button rule needs history only where a test says so.
Result once(Buttons b) { State s; return host::box::melee(b, s); }
bool stick(const Result& r, int x, int y) { return r.stick && r.stick_x == x && r.stick_y == y; }
bool cstick(const Result& r, int x, int y) { return r.c && r.c_x == x && r.c_y == y; }

void coordinates() {
  Buttons b;
  b.right = true;                                       CHECK(stick(once(b), 80, 0));
  b.mod_x = true;                                       CHECK(stick(once(b), 53, 0));    // 0.6625 walk
  b.mod_x = false; b.mod_y = true;                      CHECK(stick(once(b), 27, 0));    // 0.3375 slow walk
  b = {}; b.up = true; b.mod_x = true;                  CHECK(stick(once(b), 0, 43));    // 0.5375
  b.mod_x = false; b.mod_y = true;                      CHECK(stick(once(b), 0, 59));    // 0.7375
  b = {}; b.right = true; b.up = true;                  CHECK(stick(once(b), 56, 56));   // 0.7 0.7
  b = {}; b.right = true; b.down = true; b.shield = true; CHECK(stick(once(b), 56, -55)); // shield drop 0.7 0.6875
  b.mod_x = true;                                       CHECK(stick(once(b), 51, -30));  // long wavedash 0.6375 0.375
  b.mod_x = false; b.mod_y = true;                      CHECK(stick(once(b), 40, -68));  // short wavedash 0.5 0.85
  b.down = false; b.up = true;                          CHECK(stick(once(b), 38, 70));   // 0.475 0.875
  // Up-B angles: a modifier on a diagonal with no shield, the C buttons picking the angle.
  b = {}; b.right = true; b.up = true; b.mod_x = true;  CHECK(stick(once(b), 59, 25));   // 0.7375 0.3125
  b.c_down = true;                                      CHECK(stick(once(b), 56, 29));
  b.c_down = false; b.c_left = true;                    CHECK(stick(once(b), 63, 39));
  b.c_left = false; b.c_up = true;                      CHECK(stick(once(b), 56, 41));
  b.c_up = false; b.c_right = true;                     CHECK(stick(once(b), 49, 42));
  b.c_right = false; b.b = true;                        CHECK(stick(once(b), 73, 31));   // extended 0.9125 0.3875
  b = {}; b.left = true; b.up = true; b.mod_y = true;   CHECK(stick(once(b), -25, 59));
  b.c_right = true; b.b = true;                         CHECK(stick(once(b), -47, 57));
  // C-stick: alone full, diagonal 0.525 0.85, Mod X with a side and the stick's up: angled forward smash.
  b = {}; b.c_left = true;                              CHECK(cstick(once(b), -80, 0));
  b.c_up = true;                                        CHECK(cstick(once(b), -42, 68));
  b = {}; b.c_right = true; b.mod_x = true; b.up = true; CHECK(cstick(once(b), 68, 42));
  // Mod X + Mod Y: the C buttons are the D-pad and the C-stick rests.
  b = {}; b.mod_x = b.mod_y = true; b.c_up = true;
  { const Result r = once(b); CHECK(r.dpad_up && !r.dpad_down && cstick(r, 0, 0)); }
  // Mod Y with B on a cardinal: not slowed (full speed turnaround B).
  b = {}; b.left = true; b.mod_y = true; b.b = true;    CHECK(stick(once(b), -80, 0));
}

void opposing_directions() {
  State s; Buttons b;
  b.left = true;                 CHECK(host::box::melee(b, s).stick_x == -80);
  b.right = true;                CHECK(host::box::melee(b, s).stick_x == 80);    // the newer press wins
  b.right = false;               CHECK(host::box::melee(b, s).stick_x == 0);     // the beaten one stays dead
  b.left = false;                CHECK(host::box::melee(b, s).stick_x == 0);
  b.left = true;                 CHECK(host::box::melee(b, s).stick_x == -80);   // until let go and pressed again
}

void binding_table() {
  using host::BindAction;
  auto bit = [](BindAction a) { return 1u << (int)a; };
  host::PadState p{};
  State s;
  // Not in box mode: nothing is touched.
  p.stick_x = 5;
  CHECK(!host::apply_box_actions(false, bit(BindAction::SRight) | bit(BindAction::ModX), p, s) && p.stick_x == 5);
  // In box mode: Mod X walk, the shield buttons press R, Mod X + Mod Y + C-Left is D-pad Left.
  p = {};
  CHECK(host::apply_box_actions(true, bit(BindAction::SRight) | bit(BindAction::ModX), p, s) && p.stick_x == 53 && p.stick_y == 0);
  p = {}; host::apply_box_actions(true, bit(BindAction::LightShield), p, s); CHECK(p.trig_r == 49);
  p = {}; host::apply_box_actions(true, bit(BindAction::MidShield), p, s);   CHECK(p.trig_r == 94);
  p = {}; host::apply_box_actions(true, bit(BindAction::ModX) | bit(BindAction::ModY) | bit(BindAction::CLeft), p, s);
  CHECK((p.button & host::kActionPadBit[(size_t)BindAction::DLeft]) && p.sub_x == 0 && p.sub_y == 0);
  // A shield button on a down diagonal is the shield drop angle.
  State fresh; p = {};
  host::apply_box_actions(true, bit(BindAction::SRight) | bit(BindAction::SDown) | bit(BindAction::LightShield), p, fresh);
  CHECK(p.stick_x == 56 && p.stick_y == -55 && p.trig_r == 49);
  // The box buttons are appended: every older action keeps its number.
  CHECK((int)BindAction::LAnalog == 20 && (int)BindAction::RAnalog == 21 && (int)BindAction::ModX == 22);
  CHECK((int)BindAction::Count <= 32);   // actions travel as bits of a uint32_t
}
}  // namespace

int main() {
  coordinates();
  opposing_directions();
  binding_table();
  if (g_failures) { std::printf("%d box controller check(s) failed\n", g_failures); return 1; }
  std::printf("all box controller checks passed\n");
  return 0;
}
