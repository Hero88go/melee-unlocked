// PhobGCC settings for any GameCube controller on the adapter.
//
// A PhobGCC is a replacement controller board whose firmware calibrates the sticks, filters
// snapback and reshapes the triggers before the console ever sees them. This is that firmware's
// processing, ported to run here on the adapter's reports, so a stock controller gets the same
// settings, with the same names, ranges, defaults and menus a Phob owner knows.
//
// Ported from PhobGCC (https://github.com/PhobGCC/PhobGCC-SW, GPL-3.0), by Phobos132, FrostSSBM,
// CarVac, NiceMitch and the PhobGCC contributors (see NOTICE and the README credits):
//  - stick.h: calcStickValues, angleOnSphere, stripCalPoints, computeStickAngles, cleanNotches,
//    notchRemap, transformCalPoints, cleanCalPoints, legalizeNotch(es), adjustNotch,
//    linearizeCal, notchCalibrate
//  - filter.h: velDampFromSnapback, recomputeGains, runKalman, calcWaveshapeMult, the waveshaping
//    and the smoothing low-pass from readSticks
//  - phobGCC.h: the defaults and limits of ControlConfig and FilterGains, applyRemaps, the
//    trigger modes, tourney lockouts and LRAS of processButtons, applyCalFromPoints,
//    calibrationAdvance, calibrationUndo, calibrationSkipMeasurement, and readSticks' order,
//    clamp and output hysteresis
// The 3rd order fit (the firmware uses Rowan Easter-Robinson's public-domain CurveFitting) is a
// plain least-squares fit here, which gives the same coefficients.
//
// What is different, because the hardware is:
//  - A Phob reads its Hall-effect sensors at 12 bits. Here the "raw" reading of a stick is the
//    stock controller's own 8-bit value as the adapter reports it, over 255. Calibration works on
//    it the same way (the firmware also starts from a 0..1 reading), with 8-bit steps.
//  - A Phob runs its loop at a fixed 1 kHz. The same loop runs here on a 1 ms clock fed by the
//    adapter: one step per report from a 1000 Hz adapter (an overclocked or lossless one), and the
//    held report stepped once per millisecond from a slower one, so every gain is the firmware's.
//  - A stick with no calibration passes the controller's reading through (relative to the
//    adapter's neutral point) where the firmware would output its raw sensor value. Filters,
//    cardinal snapping and radius scaling still apply to it.
//  - Rumble strength, auto-initialize and PhobVision's display offset have nothing to act on
//    here (the adapter's rumble is on or off), so they are not offered.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <string>

namespace host::phob {

constexpr int kNotches = 16;
constexpr int kCalPoints = kNotches * 2;           // a centre reading before every notch reading
constexpr int kAdjNotches = 12;                    // the diagonals and the 8 modder notches
constexpr int kCalSteps = kCalPoints + kAdjNotches;

enum NotchStatus : uint8_t { TERT_INACTIVE, TERT_ACTIVE, SECONDARY, CARDINAL };
enum Stick : uint8_t { ASTICK, CSTICK };

// The firmware's limits and defaults (ControlConfig in phobGCC.h).
constexpr int kTriggerModeMin = 0, kTriggerModeMax = 8;      // shown to the player as 1..9
constexpr int kTriggerOffsetMin = 49, kTriggerOffsetMax = 227, kTriggerOffsetDefault = 49;
constexpr int kSnapbackMin = -10, kSnapbackMax = 10, kSnapbackDefault = 4;
constexpr int kSmoothingMin = 0, kSmoothingMax = 18;
constexpr int kCSmoothingDefault = 5;                        // "C-Stick Snapback" in the menus
constexpr int kWaveshapingMin = -24, kWaveshapingMax = 24;
constexpr int kCardinalMin = -2, kCardinalMax = 6, kCardinalDefault = 6;
constexpr int kScalerMin = 90, kScalerMax = 110, kScalerDefault = 100;
constexpr int kTourneyMin = 0, kTourneyMax = 5;

// Remap bits, the firmware's RemapConfig order. remap[output] = the hardware buttons feeding it.
enum Remap : uint8_t { A_REMAP, B_REMAP, D_REMAP, L_REMAP, R_REMAP, X_REMAP, Y_REMAP, Z_REMAP, REMAP_COUNT };

// One stick's calibration as the firmware stores it: the 32 raw readings and the 16 notch angles.
struct StickCal {
  bool measured = false;
  float points_x[kCalPoints] = {};
  float points_y[kCalPoints] = {};
  float notch_angles[kNotches];
  StickCal();
};

// Every setting of the Phob menus that acts on a controller here.
struct Settings {
  bool enabled = false;
  uint8_t remap[REMAP_COUNT] = {1 << A_REMAP, 1 << B_REMAP, 1 << D_REMAP, 1 << L_REMAP,
                                1 << R_REMAP, 1 << X_REMAP, 1 << Y_REMAP, 1 << Z_REMAP};
  int l_mode = 0, r_mode = 0;
  int l_offset = kTriggerOffsetDefault, r_offset = kTriggerOffsetDefault;
  int x_snapback = kSnapbackDefault, y_snapback = kSnapbackDefault;
  int ax_waveshaping = 0, ay_waveshaping = 0, cx_waveshaping = 0, cy_waveshaping = 0;
  int ax_smoothing = 0, ay_smoothing = 0;
  int cx_smoothing = kCSmoothingDefault, cy_smoothing = kCSmoothingDefault;
  int a_cardinal = kCardinalDefault, c_cardinal = kCardinalDefault;
  int a_scaler = kScalerDefault, c_scaler = kScalerDefault;
  int tourney = 0;
  StickCal a_cal, c_cal;
};
// The firmware's Soft Reset (everything but the stick calibration) and Hard Reset (everything).
void soft_reset(Settings& s);
void hard_reset(Settings& s);
// Text form, one "key value" per line, for the settings file and for sharing a setup. parse keeps
// what the text does not mention as it was.
std::string serialize(const Settings& s);
void parse(const std::string& text, Settings& s);
// Brings every value into the firmware's range (a settings file written by hand, say).
void clamp(Settings& s);

struct StickParams {
  float fit_x[4] = {}, fit_y[4] = {};   // linearization, highest order first
  float affine[kNotches][4] = {};
  float boundary[kNotches] = {};
  bool calibrated() const { return !(fit_x[0] == 0 && fit_y[0] == 0); }
};
// FilterGains after recomputeGains, for the firmware's fixed 1 ms step.
struct Gains {
  float max_stick, x_vel_decay, y_vel_decay, x_vel_pos, y_vel_pos, x_vel_damp, y_vel_damp;
  float vel_thresh, accel_thresh, ax_smooth, ay_smooth, cx_smooth, cy_smooth;
};
// What a Settings needs computed once (when it changes), not on every step.
struct Prepared {
  Settings s;
  StickParams a, c;
  Gains g;
};
void prepare(const Settings& s, Prepared& out);

// The calibration maths, as the firmware has it.
float linearize(float point, const float coeffs[4]);
void calc_stick_values(float angle, float& x, float& y);
void angle_on_sphere(float x, float y, float& angle);
void params_from_cal(const float points_x[kCalPoints], const float points_y[kCalPoints], const float notch_angles[kNotches],
                     NotchStatus status[kNotches], StickParams& out);
// notchRemap: `cal_step` -1 is normal play (cardinal snapping and the small centre snap apply).
void notch_remap(float x_in, float y_in, float& x_out, float& y_out, const StickParams& p, int cal_step,
                 const Settings& s, Stick which);

// One controller's filter and button state, the firmware's statics.
struct PortState {
  bool primed = false;
  float k_xpos = 0, k_ypos = 0, k_xvel = 0, k_yvel = 0, k_xvel_filt = 0, k_yvel_filt = 0;
  float x_pos_filt = 0, y_pos_filt = 0;
  struct Wave { float old_x = 0, old_y = 0, old_vx = 0, old_vy = 0, out_x = 0, out_y = 0; } wave_a, wave_c;
  float smooth_ax = 0, smooth_ay = 0, smooth_cx = 0, smooth_cy = 0;
  uint8_t ax = 127, ay = 127, cx = 127, cy = 127;   // the output bytes (the firmware's btn), origin 127
  int start_lockout = 1000, du_lockout = 1000;
  // The unfiltered output, for the Phob menus' input viewer and scope.
  float ax_unfilt = 0, ay_unfilt = 0, cx_unfilt = 0, cy_unfilt = 0;
};

// A controller as the adapter reports it. `buttons` uses the game's pad bits.
struct HardwareInput {
  uint16_t buttons = 0;
  uint8_t ax = 128, ay = 128, cx = 128, cy = 128;   // the raw bytes
  uint8_t ax0 = 128, ay0 = 128, cx0 = 128, cy0 = 128;   // the adapter's neutral point (uncalibrated sticks)
  uint8_t la = 0, ra = 0;   // trigger travel, already relative to the trigger's rest
};
struct Output {
  uint16_t buttons = 0;
  int8_t stick_x = 0, stick_y = 0, sub_x = 0, sub_y = 0;
  uint8_t trig_l = 0, trig_r = 0;
};

// Runs the firmware's loop `steps` times (one per millisecond since the last report, at least
// one) on the held input and returns what the controller would send.
Output step(const Prepared& p, PortState& st, const HardwareInput& in, int steps);

// ---- calibration: the firmware's stick calibration and notch fix, step by step ----
// Steps 0..31 measure (even: the stick at rest, odd: the stick held in a notch, in the firmware's
// order), steps 32..43 adjust notches, -1 is done.
struct CalSession {
  Stick which = ASTICK;
  int step = -1;
  int scaler = kScalerDefault;   // the stick's radius scaling, which the firmware's notch measurement includes
  float temp_x[kCalPoints] = {}, temp_y[kCalPoints] = {};
  float notch_angles[kNotches] = {};
  float saved_angles[kNotches] = {};   // the angles kept from the last calibration (getNotchAnglesSetting)
  float measured_angles[kNotches] = {};
  NotchStatus status[kNotches] = {};
  StickParams params;   // what the stick does right now, updated while notches are adjusted
};
// Stick Calibration (measures from the start) and Notch Fix (keeps the last measurement and goes
// straight to adjusting notches; needs a stick that was calibrated once).
void cal_begin(CalSession& c, Stick which, const StickCal& saved, int scaler = kScalerDefault);
bool cal_begin_notch_fix(CalSession& c, Stick which, const StickCal& saved, int scaler = kScalerDefault);
// A/L/R: records the reading (`raw_*` 0..1, an average over a moment) or moves to the next notch.
// Returns true when calibration finished, and then `saved` holds the result to keep.
bool cal_advance(CalSession& c, float raw_x, float raw_y, StickCal& saved);
// Z: one step back.
void cal_undo(CalSession& c);
// Held X / Y turn the notch's output clockwise / counter-clockwise; B puts it back where it was
// measured. `dt_ms` is how long they were held.
void cal_adjust(CalSession& c, bool cw, bool ccw, bool reset, float dt_ms);
// What a measurement step asks for: the notch index (0 = right, counter-clockwise, 16 of them),
// whether the stick should be at rest, and whether the notch is one a stock gate may not have.
struct CalPrompt { int notch; bool centre; bool optional; };
CalPrompt cal_prompt(int step);
// The notch being adjusted during steps 32..43.
int cal_adjust_notch(int step);

// A stick coordinate pair as the Phob menus show it in Melee units (clamped to the unit circle).
void melee_coords(int x, int y, float& mx, float& my);

}  // namespace host::phob
