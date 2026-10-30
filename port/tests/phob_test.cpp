// PhobGCC port: calibration hits the firmware's notch targets, filters behave, trigger modes match.
// SPDX-License-Identifier: GPL-3.0-only
#include "phob.h"
#include "phobscope.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

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


  // ---- more checks ----
  // Uncalibrated: the reading passes through unfiltered and immediately, as the firmware outputs raw.
  {
    Settings t; t.enabled = true; Prepared pt; prepare(t, pt); PortState stt;
    HardwareInput in; in.ax = 200; in.ay = 60; in.ax0 = 128; in.ay0 = 130;
    Output o1 = step(pt, stt, in, 1);
    CHECK(o1.stick_x == 72 && o1.stick_y == -70);
    in.ax = 128; in.ay = 130;
    o1 = step(pt, stt, in, 1);
    CHECK(o1.stick_x == 0 && o1.stick_y == 0);
  }
  // Trigger readings under 3 are 0 (readLa).
  { uint8_t a; bool b; trig(0, 2, false, 49, a, b); CHECK(a == 0); trig(0, 3, false, 49, a, b); CHECK(a == 3); }
  // L remapped away: its analog is shut off; R remapped onto L's button: R digital only.
  {
    Settings t; t.remap[L_REMAP] = 1 << Z_REMAP; Prepared pt; prepare(t, pt); PortState stt;
    HardwareInput in; in.la = 150; in.ra = 150;
    const Output out = step(pt, stt, in, 1);
    CHECK(out.trig_l == 0 && out.trig_r == 150);
  }
  // Tourney modes 1 (taunt delayed), 4 (both delayed), 5 (start delayed, taunt disabled).
  {
    Settings t; t.tourney = 1; Prepared pt; prepare(t, pt); PortState stt;
    HardwareInput in; in.buttons = PAD_UP;
    CHECK(!(step(pt, stt, in, 999).buttons & PAD_UP));
    CHECK(step(pt, stt, in, 2).buttons & PAD_UP);
    t.tourney = 4; prepare(t, pt); PortState s4; in.buttons = PAD_START;
    CHECK(!(step(pt, s4, in, 10).buttons & PAD_START));
    t.tourney = 5; prepare(t, pt); PortState s5; in.buttons = PAD_UP;
    CHECK(!(step(pt, s5, in, 2000).buttons & PAD_UP));
    in.buttons = PAD_START | PAD_UP;   // firmware: Start + D-up together give D-up, no Start
    const Output both = step(pt, s5, in, 1);
    CHECK((both.buttons & PAD_UP) && !(both.buttons & PAD_START));
  }
  // Multi-millisecond steps equal one-millisecond steps.
  {
    Settings f = s; f.x_snapback = 6; f.ax_waveshaping = 9; f.ax_smoothing = 4;
    Prepared pf; prepare(f, pf);
    PortState a, b; float rx, ry; stock_reading(4, false, rx, ry);
    Output oa{}, ob{};
    oa = step(pf, a, at(rx, ry), 7);
    for (int i = 0; i < 7; i++) ob = step(pf, b, at(rx, ry), 1);
    CHECK(oa.stick_x == ob.stick_x && oa.stick_y == ob.stick_y && a.smooth_ax == b.smooth_ax);
  }
  // Many random stock sticks: calibration always finishes, hits every cardinal and diagonal, and
  // the stick has no holes or jumps (neighbouring readings never land far apart).
  {
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> j(-0.03f, 0.03f), u(0, 1);
    int worst_jump = 0, worst_notch = 0, skipped_ok = 0;
    for (int trial = 0; trial < 200; trial++) {
      const float ox = 0.5f + j(rng), oy = 0.5f + j(rng), sx = 0.30f + u(rng) * 0.1f, sy = 0.30f + u(rng) * 0.1f;
      const bool modder = u(rng) < 0.3f;
      StickCal cal;
      CalSession c; cal_begin(c, ASTICK, cal);
      bool fin = false; int adjust_steps = 0;
      for (int g = 0; g < 100 && !fin; g++) {
        float x = ox, y = oy;
        if (c.step >= kCalPoints) adjust_steps++;
        if (c.step < kCalPoints) {
          const CalPrompt pr = cal_prompt(c.step);
          if (!pr.centre && (!pr.optional || modder)) {
            const float a = kPi * pr.notch / 8;
            const float r = pr.notch % 4 == 2 ? 0.93f : 1.0f;
            x = ox + sx * r * std::cos(a); y = oy + sy * r * std::sin(a);
          }
        }
        fin = cal_advance(c, x + j(rng) * 0.02f, y + j(rng) * 0.02f, cal);
      }
      CHECK(fin);
      // Without modder notches only the 4 diagonals are adjusted; with them all 12.
      if (adjust_steps == (modder ? 12 : 4)) skipped_ok++;
      Settings t; t.a_cal = cal; t.x_snapback = t.y_snapback = 0; t.cx_smoothing = t.cy_smoothing = 0;
      Prepared pt; prepare(t, pt);
      for (int n = 0; n < 16; n += 2) {
        const float a = kPi * n / 8, r = n % 4 == 2 ? 0.93f : 1.0f;
        float tx, ty; calc_stick_values(a, tx, ty);
        PortState fresh;
        const Output o2 = step(pt, fresh, at(ox + sx * r * std::cos(a), oy + sy * r * std::sin(a)), 30);
        worst_notch = std::max(worst_notch, std::max(std::abs(o2.stick_x - (int)std::lround(tx)), std::abs(o2.stick_y - (int)std::lround(ty))));
      }
      int prev_x = 0, prev_y = 0; bool have = false;
      for (int k = 0; k < 720; k++) {   // a slow roll around the rim
        const float a = kPi * k / 360;
        PortState fresh;
        const Output o3 = step(pt, fresh, at(ox + sx * 0.97f * std::cos(a), oy + sy * 0.97f * std::sin(a)), 20);
        if (have) worst_jump = std::max(worst_jump, std::abs(o3.stick_x - prev_x) + std::abs(o3.stick_y - prev_y));
        prev_x = o3.stick_x; prev_y = o3.stick_y; have = true;
      }
    }
    std::printf("random sticks: worst notch error %d, worst neighbour jump %d, notch skipping right %d/200\n", worst_notch, worst_jump, skipped_ok);
    CHECK(worst_notch <= 3);
    CHECK(worst_jump <= 12);   // cardinal snapping moves up to 6 units at once on each side of an axis
    CHECK(skipped_ok == 200);
  }
  // Notch limits: a diagonal cannot be turned past the firmware's 30% stretch.
  {
    CalSession c; CHECK(cal_begin_notch_fix(c, ASTICK, s.a_cal));
    const int idx = cal_adjust_notch(c.step);   // the first adjust step is the up-right diagonal
    CHECK(idx == 2);
    for (int i = 0; i < 200000; ++i) cal_adjust(c, false, true, false, 1.0f);
    const float hi = c.notch_angles[idx];
    for (int i = 0; i < 400000; ++i) cal_adjust(c, true, false, false, 1.0f);
    const float lo = c.notch_angles[idx];
    CHECK(hi - c.measured_angles[idx] <= 0.3f * (kPi / 4) + 1e-3f);
    CHECK(c.measured_angles[idx] - lo <= 0.3f * (kPi / 4) + 1e-3f);
    cal_adjust(c, false, false, true, 1.0f);   // B: back to measured
    CHECK(c.notch_angles[idx] == c.measured_angles[idx]);
  }
  // Undo: back from a notch goes to the centre reading before the previous notch (calibrationUndo).
  {
    StickCal cal; CalSession c; cal_begin(c, ASTICK, cal);
    for (int i = 0; i < 5; i++) cal_advance(c, 0.5f, 0.5f, cal);
    CHECK(c.step == 5); cal_undo(c); CHECK(c.step == 2);
    cal_undo(c); CHECK(c.step == 0); cal_undo(c); CHECK(c.step == 0);
  }
  // Hysteresis: a reading wobbling under the firmware's 0.3 band does not flicker the output.
  {
    Settings t = s; t.x_snapback = t.y_snapback = 0; Prepared pt; prepare(t, pt); PortState stt;
    float rx, ry; stock_reading(0, false, rx, ry);
    const Output base = step(pt, stt, at(rx * 0.8f + 0.1f, ry), 30);
    int changes = 0; int last = base.stick_x;
    for (int i = 0; i < 200; i++) { const Output o4 = step(pt, stt, at(rx * 0.8f + 0.1f, ry), 1); if (o4.stick_x != last) changes++; last = o4.stick_x; }
    CHECK(changes == 0);
  }
  // A damaged settings file cannot produce NaN calibration.
  { Settings bad; parse("a_points_x nan nan\n", bad); CHECK(!bad.a_cal.measured); }

  // Ice Climbers desync (Altimor's isDesyncCoordinate): axes and centre are safe; x = 64 (0.8)
  // stays 0.8 for Nana (64/80*127 truncates to 101, 101/127 = 0.795) and so desyncs.
  CHECK(!ic_desync_coordinate(80, 0) && !ic_desync_coordinate(0, 80) && !ic_desync_coordinate(0, -80));
  CHECK(ic_desync_coordinate(64, 30));
  // ---- PhobScope measurements (phobscope.h), against values worked out from the firmware's formulas ----
  {
    using namespace host::phobscope;
    auto stick = [](int x, float unf) { Ms m; m.ax = (uint8_t)(127 + x); m.ax_unfilt = unf; return m; };
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.6f; };
    // Dashback: straight from 0 to 100 is 100% vanilla.
    { Capture c; start(c, Mode::Dashback); bool fin = false;
      for (int i = 0; i < 20; i++) fin |= feed(c, stick(0, 0));
      for (int i = 0; i < 300 && !fin; i++) fin |= feed(c, stick(100, 100));
      CHECK(fin && near(c.percents[0], 100) && near(c.percents[1], 100)); }
    // 8 ms in the tilt zone (30): vanilla (1 - 8/16.67) = 52%.
    { Capture c; start(c, Mode::Dashback); bool fin = false;
      for (int i = 0; i < 20; i++) fin |= feed(c, stick(0, 0));
      for (int i = 0; i < 8; i++) fin |= feed(c, stick(30, 30));
      for (int i = 0; i < 300 && !fin; i++) fin |= feed(c, stick(100, 100));
      CHECK(fin && near(c.percents[0], 52.0f) && c.percents[1] >= c.percents[0]); }
    // Snapback: a clean release from 100 never reads negative.
    { Capture c; start(c, Mode::Snapback); bool fin = false;
      for (int i = 0; i < 30; i++) fin |= feed(c, stick(100, 100));
      for (int i = 0; i < 300 && !fin; i++) fin |= feed(c, stick(0, 0));
      CHECK(fin && near(c.percents[0], 100)); }
    // Snapback past centre to -40 for 34 ms: every 60 Hz phase sees it, 0% positive.
    { Capture c; start(c, Mode::Snapback); bool fin = false;
      for (int i = 0; i < 30; i++) fin |= feed(c, stick(100, 100));
      for (int i = 0; i < 34; i++) fin |= feed(c, stick(-40, -40));
      for (int i = 0; i < 300 && !fin; i++) fin |= feed(c, stick(0, 0));
      CHECK(fin && near(c.percents[0], 0)); }
    // Pivot: 10 ms past 80 the other way = 0.6 frames: no turn 40, empty pivot 60, dashback 0.
    { Capture c; start(c, Mode::Pivot); bool fin = false;
      for (int i = 0; i < 30; i++) fin |= feed(c, stick(100, 100));
      for (int i = 0; i < 10; i++) fin |= feed(c, stick(-100, -100));
      for (int i = 0; i < 300 && !fin; i++) fin |= feed(c, stick(0, 0));
      CHECK(fin && near(c.percents[0], 40) && near(c.percents[1], 60) && near(c.percents[2], 0)); }
    // Trigger: analog at 60 for 25 ms before the click = 1.5 frames: D 0, ADT 50, none 50.
    { Capture c; start(c, Mode::Trigger); bool fin = false;
      Ms m; m.la = 60;
      for (int i = 0; i < 25; i++) fin |= feed(c, m);
      m.l = true; m.la = 255;
      for (int i = 0; i < 300 && !fin; i++) fin |= feed(c, m);
      CHECK(fin && near(c.percents[0], 0) && near(c.percents[1], 50) && near(c.percents[2], 50)); }
    // Click with no analog first: 100% digital powershield.
    { Capture c; start(c, Mode::Trigger); bool fin = false;
      Ms m; m.l = true; m.la = 255;
      for (int i = 0; i < 300 && !fin; i++) fin |= feed(c, m);
      CHECK(fin && near(c.percents[0], 100)); }
    // Reaction: counts milliseconds until a press (Start does not count).
    { Capture c; c.stick_thresh = 23; c.trigger_thresh = 49; start(c, Mode::Reaction);
      Ms idle; Ms st; st.start = true; Ms a; a.a = true;
      for (int i = 0; i < 200; i++) feed(c, i % 2 ? st : idle);
      CHECK(!c.done);
      CHECK(feed(c, a) && c.delay_ms == 201); }
    // Stickmap: 100 samples starting once the stick moves more than 10.
    { Capture c; c.stick_thresh = 10; start(c, Mode::StickMap); bool fin = false;
      for (int i = 0; i < 50; i++) fin |= feed(c, stick(0, 0));
      for (int i = 0; i < 200 && !fin; i++) fin |= feed(c, stick(i + 1, (float)(i + 1)));
      uint8_t x[100]; ordered(c, c.a1, x);
      CHECK(fin && x[1] - 127 >= 11 && x[1] - 127 <= 12 && x[99] > x[1]); }
    // Button timing: 200 ms from the first press; marks a stick past its threshold.
    { Capture c; c.stick_thresh = 23; c.trigger_thresh = 255; start(c, Mode::Press); bool fin = false;
      Ms idle; for (int i = 0; i < 40; i++) fin |= feed(c, idle);
      Ms jump; jump.x = true; fin |= feed(c, jump);
      for (int i = 0; i < 5; i++) fin |= feed(c, idle);
      Ms dash = stick(70, 70);
      for (int i = 0; i < 300 && !fin; i++) fin |= feed(c, dash);
      CHECK(fin && (c.buttons[0] & 4) && !(c.buttons[1] & 4) && (c.analog_marks[6] & 1) && !(c.analog_marks[5] & 1)); }
  }

  if (g_failures) std::printf("%d failure(s)\n", g_failures);
  else std::printf("phob: all checks passed\n");
  return g_failures ? 1 : 0;
}
