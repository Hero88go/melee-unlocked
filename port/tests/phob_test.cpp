// PhobGCC port: calibration hits the firmware's notch targets, filters behave, trigger modes match.
// SPDX-License-Identifier: GPL-3.0-only
#include "phob.h"
#include <cmath>
#include <cstdio>

namespace {
int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)
using namespace host::phob;
constexpr float kPi = 3.14159265358979323846f;
constexpr uint16_t PAD_L = 0x0040, PAD_R = 0x0020, PAD_A = 0x0100, PAD_B = 0x0200, PAD_START = 0x1000, PAD_UP = 0x0008;

// A stock stick with a slightly squashed, off-centre gate, as 0..1 readings.
void stock_reading(int notch, bool centre, float& x, float& y) {
  if (centre) { x = 0.51f; y = 0.49f; return; }
  const float a = kPi * notch / 8.0f;
  const bool firefox = notch % 2 == 1;
  const float r = firefox ? 0.0f : (notch % 4 == 2 ? 0.34f : 0.37f);   // no modder notches, rounder diagonals
  x = 0.51f + r * 1.05f * std::cos(a);
  y = 0.49f + r * 0.95f * std::sin(a);
}

void calibrate(StickCal& saved) {
  CalSession c;
  cal_begin(c, ASTICK, saved);
  bool done = false;
  for (int guard = 0; guard < 100 && !done; ++guard) {
    float x = 0.51f, y = 0.49f;
    if (c.step < kCalPoints) { const CalPrompt p = cal_prompt(c.step); stock_reading(p.notch, p.centre, x, y); }
    done = cal_advance(c, x, y, saved);
  }
  CHECK(done);
  CHECK(saved.measured);
}

HardwareInput at(float x, float y) {
  HardwareInput in;
  in.ax = (uint8_t)std::lround(x * 255); in.ay = (uint8_t)std::lround(y * 255);
  return in;
}
}  // namespace

int main() {
  // Off by default and an uncalibrated stick at rest reads zero.
  Settings s;
  CHECK(!s.enabled);
  Prepared p;
  prepare(s, p);
  CHECK(!p.a.calibrated());
  PortState st;
  HardwareInput rest;
  Output o = step(p, st, rest, 5);
  CHECK(o.stick_x == 0 && o.stick_y == 0);

  // Calibration: every cardinal and diagonal of the measured gate lands on the firmware's target.
  calibrate(s.a_cal);
  s.x_snapback = s.y_snapback = 0;   // filters off so the step is the mapping alone
  s.cx_smoothing = s.cy_smoothing = 0;
  prepare(s, p);
  CHECK(p.a.calibrated());
  for (int notch = 0; notch < 16; notch += 2) {
    float rx, ry, tx, ty;
    stock_reading(notch, false, rx, ry);
    calc_stick_values(kPi * notch / 8.0f, tx, ty);
    PortState fresh;
    const Output out = step(p, fresh, at(rx, ry), 20);
    // 8-bit input: within two units of the ideal notch, and cardinals snap exactly to the axis.
    CHECK(std::abs(out.stick_x - (int)std::lround(tx)) <= 2);
    CHECK(std::abs(out.stick_y - (int)std::lround(ty)) <= 2);
    if (notch % 4 == 0) CHECK(out.stick_x == 0 || out.stick_y == 0);
  }
  { PortState fresh; const Output out = step(p, fresh, at(0.51f, 0.49f), 20); CHECK(out.stick_x == 0 && out.stick_y == 0); }

  // Smart snapback: a release from the rim settles at centre without a large overshoot past it.
  {
    Settings f = s;
    f.x_snapback = f.y_snapback = kSnapbackDefault;
    Prepared pf;
    prepare(f, pf);
    PortState sf, sraw;
    float rx, ry;
    stock_reading(0, false, rx, ry);
    step(pf, sf, at(rx, ry), 50);
    // Simulated stock snapback: the reading swings past centre for a few ms.
    const float trace[] = {0.70f, 0.55f, 0.40f, 0.33f, 0.36f, 0.45f, 0.52f, 0.51f, 0.51f, 0.51f};
    int worst_filtered = 0, worst_raw = 0;
    for (float tx : trace) {
      const Output a = step(pf, sf, at(tx, 0.49f), 1);
      if (a.stick_x < worst_filtered) worst_filtered = a.stick_x;
      Prepared pr; Settings r = s; prepare(r, pr);
      const Output b = step(pr, sraw, at(tx, 0.49f), 1);
      if (b.stick_x < worst_raw) worst_raw = b.stick_x;
    }
    CHECK(worst_filtered > worst_raw);
    const Output settled = step(pf, sf, at(0.51f, 0.49f), 200);
    CHECK(settled.stick_x == 0);
  }

  // Trigger modes, the firmware's processButtons switch.
  auto trig = [&](int mode, uint8_t travel, bool click, int offset, uint8_t& la, bool& l) {
    Settings t; t.l_mode = mode; t.l_offset = offset;
    Prepared pt; prepare(t, pt); PortState stt;
    HardwareInput in; in.la = travel; if (click) in.buttons = PAD_L;
    const Output out = step(pt, stt, in, 1);
    la = out.trig_l; l = (out.buttons & PAD_L) != 0;
  };
  uint8_t la; bool l;
  trig(0, 120, false, 49, la, l); CHECK(la == 120 && !l);
  trig(1, 120, true, 49, la, l);  CHECK(la == 0 && l);
  trig(2, 200, true, 49, la, l);  CHECK(la == 200 && !l);
  trig(3, 120, false, 80, la, l); CHECK(la == 80);
  trig(3, 200, true, 80, la, l);  CHECK(la == 255 && l);
  trig(4, 0, true, 90, la, l);    CHECK(la == 90 && !l);
  trig(5, 0, true, 90, la, l);    CHECK(la == 90 && l);
  trig(7, 200, true, 80, la, l);  CHECK(la == 80 && !l);
  trig(8, 40, false, 227, la, l); CHECK(la == 100);

  // LRAS always gets through, whatever the trigger modes.
  {
    Settings t; t.l_mode = 1; t.r_mode = 4;
    Prepared pt; prepare(t, pt); PortState stt;
    HardwareInput in; in.buttons = PAD_L | PAD_R | PAD_A | PAD_START;
    const Output out = step(pt, stt, in, 1);
    CHECK((out.buttons & (PAD_L | PAD_R | PAD_A | PAD_START)) == (PAD_L | PAD_R | PAD_A | PAD_START));
    CHECK(out.trig_l == 255 && out.trig_r == 255);
  }

  // Tourney mode 3: Start must be held for a second.
  {
    Settings t; t.tourney = 3;
    Prepared pt; prepare(t, pt); PortState stt;
    HardwareInput in; in.buttons = PAD_START;
    CHECK(!(step(pt, stt, in, 500).buttons & PAD_START));
    CHECK(step(pt, stt, in, 600).buttons & PAD_START);
  }
  // Tourney mode 2: taunt disabled.
  {
    Settings t; t.tourney = 2;
    Prepared pt; prepare(t, pt); PortState stt;
    HardwareInput in; in.buttons = PAD_UP;
    CHECK(!(step(pt, stt, in, 1).buttons & PAD_UP));
  }
  // Remap: swap A and B.
  {
    Settings t; t.remap[A_REMAP] = 1 << B_REMAP; t.remap[B_REMAP] = 1 << A_REMAP;
    Prepared pt; prepare(t, pt); PortState stt;
    HardwareInput in; in.buttons = PAD_A;
    const Output out = step(pt, stt, in, 1);
    CHECK((out.buttons & PAD_B) && !(out.buttons & PAD_A));
  }
  // Notch fix keeps the measurement, and a turned notch stays within the firmware's limits.
  {
    CalSession c;
    CHECK(cal_begin_notch_fix(c, ASTICK, s.a_cal));
    CHECK(c.step >= kCalPoints);
    const int idx = cal_adjust_notch(c.step);
    const float before = c.notch_angles[idx];
    for (int i = 0; i < 100000; ++i) cal_adjust(c, false, true, false, 1.0f);
    CHECK(c.notch_angles[idx] > before);
    CHECK(c.notch_angles[idx] - before < kPi / 8.0f);
  }
  // Resets.
  {
    Settings t = s; t.l_mode = 5; t.x_snapback = -3;
    soft_reset(t); CHECK(t.l_mode == 0 && t.x_snapback == kSnapbackDefault && t.a_cal.measured);
    hard_reset(t); CHECK(!t.a_cal.measured);
  }
  // Text round trip keeps every setting and the calibration.
  {
    Settings t = s; t.enabled = true; t.l_mode = 3; t.r_offset = 120; t.ay_waveshaping = -7; t.remap[X_REMAP] = 1 << Z_REMAP;
    Settings u; parse(serialize(t), u);
    CHECK(u.enabled && u.l_mode == 3 && u.r_offset == 120 && u.ay_waveshaping == -7 && u.remap[X_REMAP] == (1 << Z_REMAP));
    CHECK(u.a_cal.measured && !u.c_cal.measured);
    Prepared pa, pb; prepare(t, pa); prepare(u, pb);
    PortState sa, sb; float rx, ry; stock_reading(2, false, rx, ry);
    const Output oa = step(pa, sa, at(rx, ry), 30), ob = step(pb, sb, at(rx, ry), 30);
    CHECK(oa.stick_x == ob.stick_x && oa.stick_y == ob.stick_y);
    Settings bad; parse("l_mode 99\nx_snapback -50\n", bad); CHECK(bad.l_mode == 8 && bad.x_snapback == -10);
  }
  float mx, my;
  melee_coords(80, 0, mx, my); CHECK(mx == 1.0f && my == 0.0f);
  melee_coords(100, 100, mx, my); CHECK(std::fabs(mx - 0.7f) < 0.02f);

  if (g_failures) std::printf("%d failure(s)\n", g_failures);
  else std::printf("phob: all checks passed\n");
  return g_failures ? 1 : 0;
}
