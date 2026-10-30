// The PhobGCC menu, full screen in the game's gold Main Menu look (practice_menu's bars, cursor ring,
// title, description box, button glyphs and the game's own font), driven by the controller.
//
// The screens, their order, their names and their help text follow PhobGCC's PhobVision menu
// (rp2040 menu.h, menuStrings.h), so a Phob owner finds every setting where they expect it. PhobGCC
// (https://github.com/PhobGCC/PhobGCC-SW, GPL-3.0) is by Phobos132, FrostSSBM, CarVac, NiceMitch
// and the PhobGCC contributors. PhobScope's stickmaps are by venus23gg, made with Altimor's Melee
// Stick Coordinate Visualizer; the continuous scope, gate visualizer and export follow GCC Test
// Suite by greenwave-1 (no code from it is used).
//
// Buttons: the D-pad or stick moves, left and right change a value, A picks, B goes back, Start
// closes (and records on the PhobScope screens, as in PhobScope). Calibration and remapping read
// the controller being set up with PhobVision's buttons: A/L/R next, Z back, X/Y turn a notch, B puts
// it back, Start cancels.
// SPDX-License-Identifier: GPL-3.0-only
#include "phob_menu.h"
#include "host.h"
#include "phob.h"
#include "phobscope.h"
#include "phob_stickmaps.h"
#include "practice_menu.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace gx {
namespace {
using namespace host::phob;
using host::phobscope::Mode;
namespace pm = practice_menu;

constexpr uint16_t PAD_LEFT = 0x0001, PAD_RIGHT = 0x0002, PAD_DOWN = 0x0004, PAD_UP = 0x0008, PAD_Z = 0x0010,
                   PAD_R = 0x0020, PAD_L = 0x0040, PAD_A = 0x0100, PAD_B = 0x0200, PAD_X = 0x0400,
                   PAD_Y = 0x0800, PAD_START = 0x1000;

// ---- the screens (PhobVision's tree) ----
enum Screen {
  MAIN, STICK, CALIBRATE, ASTICKCAL, ANOTCHFIX, CSTICKCAL, CNOTCHFIX, FILTER, ASNAPBACK, AWAVE, ASMOOTH,
  CSNAPBACK, CWAVE, GEOM, CARDINALS, RADIUS, SETTINGS, SET_OVER, REMAP, TRIGGER, LTRIGGER, RTRIGGER,
  TOURNEY, RESET, SCOPE, INPUTVIEW, XYSCOPE, TIMESCOPE, PRESSTIME, REACTION, CONTSCOPE, GATE, EXPORT,
  PHOBCHECK, SETUPS, SCREEN_COUNT
};
struct Node { Screen parent; const char* name; const char* about; };
const Node& node(Screen s) {
  static const Node nodes[SCREEN_COUNT] = {
      {MAIN, "PhobGCC", ""},
      {MAIN, "Stick Configuration", "Calibration, notches, the snapback filter and the stick's shape."},
      {STICK, "Stick Calibration", "PhobGCC's own calibration, and Notch Fix to move notches without measuring again."},
      {CALIBRATE, "Calibrate Left Stick", ""},
      {CALIBRATE, "Left Stick Notch Fix", ""},
      {CALIBRATE, "Calibrate C-Stick", ""},
      {CALIBRATE, "C-Stick Notch Fix", ""},
      {STICK, "Stick Filter Settings", "Smart snapback, waveshaping and smoothing, as PhobGCC filters its sticks."},
      {FILTER, "Left Stick Smart Snapback", "Set so that snapback < 23 for Melee. 0 disables the filter. >0 shortens rise time. <0 doesn't shorten rise time. Greater magnitude makes the stick return to center slower."},
      {FILTER, "Left Stick Waveshaping", "This makes the stick stop during fast inputs. Greater magnitudes reduce the speed threshold. <0 lets you mimic a heavy pode potentiometer. It affects pivots, DBOOC, dashback, and more."},
      {FILTER, "Left Stick Smoothing", "This averages the stick position with the past. It can be used to make one axis lag the other. Use X=5 to make down-and-in ledgedashes work."},
      {FILTER, "C-Stick Snapback", "This averages the stick position with the past. Make these 5 or 6 to reduce snapback. Make one axis higher to make it less responsive."},
      {FILTER, "C-Stick Waveshaping", "This makes the stick stop during fast inputs. This can help C-Stick ledge drop consistency."},
      {STICK, "Stick Geometry Settings", "Cardinal snapping and how far the stick reaches."},
      {GEOM, "Cardinal Snapping", "This sets how far the stick snaps to the cardinal axes. 0 disables snapping, and -2 disables 1.0 cardinals even on UCF 0.84+. -1 gives 1.0 on 0.84 except for Zelda teleport."},
      {GEOM, "Stick Radius Scaling", "This sets how far from the center the stick can reach. A normal GCC can get to roughly 100-103. Melee only sees <=80. Calibrate again after changing it."},
      {MAIN, "Settings", "Button remapping, trigger modes, tourney lockouts and resets."},
      {SETTINGS, "Settings Overview", "Everything set on this controller, at a glance."},
      {SETTINGS, "Button Remapping", "You may remap A, B, Du, L, R, X, Y, or Z. When L or R are swapped, they are digital only. Use Trigger Mode 5 to get an analog trigger value on a digital button for lightshielding."},
      {SETTINGS, "Trigger Configuration", "The L and R trigger modes and their offsets."},
      {TRIGGER, "Left Trigger Configuration", "This changes the behavior of the L trigger."},
      {TRIGGER, "Right Trigger Configuration", "This changes the behavior of the R trigger."},
      {SETTINGS, "Tourney Button Lockouts", "This prevents misinputs during tournaments by delaying or disabling the start button, which pauses the match, and/or dpad-up, which taunts. You must hold delayed buttons for 1 second."},
      {SETTINGS, "Reset Settings", "Soft Reset keeps stick calibration. Hard Reset clears everything, calibration included."},
      {MAIN, "PhobScope", "Records your stick and triggers at the adapter's rate and measures them the way PhobGCC does."},
      {SCOPE, "Input Viewer", "Everything the controller sends, before and after the settings."},
      {SCOPE, "Stickmap Plots", "100 ms of stick motion over Melee's stickmaps. Recording starts once the stick moves."},
      {SCOPE, "Value vs Time Plots", "200 ms of one input, and the chance of snapback, dashback, pivots or a powershield."},
      {SCOPE, "Button Timing Viewer", "200 ms from your first press: when each button and stick went down, in frames."},
      {SCOPE, "Reaction Time Test", "Wait for the square, then press anything."},
      {SCOPE, "Continuous Oscilloscope", "One input, continuously, before and after the settings."},
      {SCOPE, "Gate Visualizer", "Roll the stick slowly around the gate; the outline is the farthest it reached in each direction."},
      {SCOPE, "Export Data", "The last 4096 reports of this socket as CSV beside your settings."},
      {MAIN, "Phob Check", "What this controller sends, checked against Melee: the latest PhobScope results and the notch you hold."},
      {MAIN, "Setups", "Save these settings as a setup, switch between setups, or go back to the defaults. A setup works on a PhobGCC and on a stock controller alike."},
  };
  return nodes[s];
}

// ---- per socket state ----
struct MenuState {
  Screen screen = MAIN;
  std::vector<int> cursor = std::vector<int>(SCREEN_COUNT, 0), top = std::vector<int>(SCREEN_COUNT, 0);
  bool loaded = false;
  Settings s;
  CalSession cal;
  bool calibrating = false;
  uint16_t last_buttons = 0xFFFF;   // the socket's own buttons (calibration, remap, reaction)
  std::chrono::steady_clock::time_point last_frame;
  int remap_step = -1;
  int reset_pending = -1;           // 0 soft, 1 hard: waiting for the second A
  bool frozen = false;
  std::vector<host::GCScopeSample> frozen_samples;
  int cont_input = 0, cont_window = 1000;
  int gate_stick = 0;
  float gate[64] = {};
  host::phobscope::Capture cap, shown;
  bool have_shown = false;
  bool auto_repeat = true;
  std::chrono::steady_clock::time_point repeat_at{};
  host::GCScopeSample prev{};
  bool have_prev = false;
  uint32_t next_tick_us = 0;
  int stickmap = 0;
  int view_index = 0;
  int time_input = 0, time_measure = 0;
  bool reaction_waiting = false;
  std::chrono::steady_clock::time_point reaction_at;
  float last_snapback = -1, last_db_van = -1, last_db_ucf = -1, last_pivot[3] = {-1, -1, -1}, last_trig[3] = {-1, -1, -1};
  bool setup_cal = false;
  std::string active_setup;         // the setup last loaded or saved on this socket
  int setup_pick = 0;                // the setup Save Over and Delete act on
  int setup_delete_pending = -1;     // the setup waiting for the second A to be deleted
  std::string status;
  double status_until = 0;
};
MenuState g_menu[4];
bool g_open = false;
int g_port = 0;
double g_changed_at = 0;
PhobNativeIn g_in;
bool g_socket_armed = false;   // the socket's own controller was let go since the menu opened

int latest(int port, std::vector<host::GCScopeSample>& out, int n) {
  out.resize((size_t)n);
  const int got = host::gcadapter_scope(port, out.data(), n);
  out.resize((size_t)got);
  return got;
}

void say(MenuState& m, const std::string& s) { m.status = s; m.status_until = g_in.time + 4.0; }

// ---- PhobScope recording ----
host::phobscope::Ms to_ms(const host::GCScopeSample& s) {
  host::phobscope::Ms m;
  auto out = [](int8_t v) { return (uint8_t)(127 + v); };
  m.ax = out(s.ax); m.ay = out(s.ay); m.cx = out(s.cx); m.cy = out(s.cy);
  m.ax_unfilt = s.raw_ax; m.ay_unfilt = s.raw_ay; m.cx_unfilt = s.raw_cx; m.cy_unfilt = s.raw_cy;
  m.la = s.l; m.ra = s.r; m.la_raw = s.raw_l; m.ra_raw = s.raw_r;
  const uint16_t b = s.buttons;
  m.a = b & PAD_A; m.b = b & PAD_B; m.x = b & PAD_X; m.y = b & PAD_Y; m.start = b & PAD_START; m.z = b & PAD_Z;
  m.l = b & PAD_L; m.r = b & PAD_R; m.du = b & PAD_UP; m.dd = b & PAD_DOWN; m.dl = b & PAD_LEFT; m.dr = b & PAD_RIGHT;
  return m;
}
void begin_capture(int port, MenuState& m, Mode mode) {
  host::phobscope::start(m.cap, mode);
  std::vector<host::GCScopeSample> v;
  m.have_prev = latest(port, v, 1) == 1;
  if (m.have_prev) { m.prev = v.back(); m.next_tick_us = m.prev.time_us + 1000; }
}
// A fixed 1 ms clock: every tick takes the report in force at that moment. Reports arrive with USB
// jitter (and sometimes in bursts); counting each gap as whole milliseconds on its own used to
// stretch short events (9 ms of tilt recorded as 12). Returns true when the recording finished.
bool feed_capture(int port, MenuState& m) {
  if (m.cap.mode == Mode::None || m.cap.done) return false;
  std::vector<host::GCScopeSample> v;
  latest(port, v, 4096);
  if (!m.have_prev) {
    if (v.empty()) return false;
    m.prev = v.back(); m.have_prev = true; m.next_tick_us = m.prev.time_us + 1000;
    return false;
  }
  for (const host::GCScopeSample& s : v) {
    if ((int32_t)(s.time_us - m.prev.time_us) <= 0) continue;
    int ticks = 0;
    while ((int32_t)(s.time_us - m.next_tick_us) >= 0 && ticks < 50) {
      if (host::phobscope::feed(m.cap, to_ms(m.prev))) { m.prev = s; m.next_tick_us = s.time_us + 1000; return true; }
      m.next_tick_us += 1000;
      ++ticks;
    }
    if (ticks == 50) m.next_tick_us = s.time_us + 1000;   // a long gap (adapter unplugged): start the clock again
    m.prev = s;
  }
  return false;
}
Mode time_mode(const MenuState& m) {
  return m.time_input >= 4 ? Mode::Trigger : m.time_measure == 0 ? Mode::Snapback : m.time_measure == 1 ? Mode::Dashback : Mode::Pivot;
}

// Phob's stickmap images, decoded once into runs of one grey.
struct Run { int16_t row, col, len; uint8_t grey; };
const std::vector<Run>& stickmap_runs(int map) {
  static std::vector<Run> cache[phob_stickmaps::kMapCount];
  static bool made[phob_stickmaps::kMapCount] = {};
  std::vector<Run>& runs = cache[map];
  if (made[map]) return runs;
  made[map] = true;
  const auto& mp = phob_stickmaps::kMaps[map];
  const int w = (mp.image[0] << 8) | mp.image[1], h = (mp.image[2] << 8) | mp.image[3];
  int byte = 4, left = (mp.image[byte] >> 3) + 1, color = mp.palette[mp.image[byte] & 7];
  for (int row = 0; row < h; row++) {
    int col = 0;
    while (col < w) {
      const int take = std::min(left, w - col);
      if (color >= 5) runs.push_back({(int16_t)row, (int16_t)col, (int16_t)take, (uint8_t)((color - 5) * 255 / 10)});
      col += take;
      left -= take;
      if (left == 0) { byte++; left = (mp.image[byte] >> 3) + 1; color = mp.palette[mp.image[byte] & 7]; }
    }
  }
  return runs;
}

// ---- rows ----
enum class Kind { Page, Value, Action, Info };
struct Row {
  Kind kind = Kind::Info;
  std::string label, value, about;
  Screen target = MAIN;
  int* v = nullptr;
  int lo = 0, hi = 0, step = 1;
  std::function<std::string(int)> show;   // a value's text (else the number)
  std::function<void()> act;              // an action, or what a change must also do
  bool enabled = true;
};
Row page(Screen s, std::string value = "") { Row r; r.kind = Kind::Page; r.label = node(s).name; r.target = s; r.value = value; r.about = node(s).about; return r; }
Row value(const char* label, int* v, int lo, int hi, const char* about, std::function<std::string(int)> show = nullptr) {
  Row r; r.kind = Kind::Value; r.label = label; r.v = v; r.lo = lo; r.hi = hi; r.about = about; r.show = show; return r;
}
Row action(const std::string& label, std::string value, const std::string& about, std::function<void()> act, bool enabled = true) {
  Row r; r.kind = Kind::Action; r.label = label; r.value = value; r.about = about; r.act = act; r.enabled = enabled; return r;
}
Row info(const std::string& label, std::string value, const std::string& about) { Row r; r.label = label; r.value = value; r.about = about; return r; }
std::string value_text(const Row& r) {
  if (r.kind != Kind::Value) return r.value;
  return r.show ? r.show(*r.v) : std::to_string(*r.v);
}
std::string pct(float v) { if (v < 0) return "-"; char b[16]; std::snprintf(b, sizeof b, "%.0f%%", v); return b; }

const char* trigger_mode_short(int mode) {
  static const char* n[9] = {"Default", "Digital Only", "Analog Only", "Trigger Plug", "Digital => Analog", "Analog + Digital",
                             "Scaled Analog", "Plug, No Digital", "Plug, High Gain"};
  return n[std::clamp(mode, 0, 8)];
}
const char* trigger_mode_text(int mode) {
  switch (mode) {
    case 0: return "This is the default trigger behavior.";
    case 1: return "This disables analog triggers (lightshield).";
    case 2: return "This disables the hard press but preserves LRAS. In Melee you cannot airdodge or tech with this.";
    case 3: return "This caps the maximum analog value at the offset. In Melee, you get a large lightshield for much of the slider travel, then a hardshield.";
    case 4: return "This outputs an analog value (the offset) when hard pressed, without the digital press.";
    case 5: return "This outputs both an analog value and a digital output when hard pressed.";
    case 6: return "This increases the sensitivity of the analog by a multiplier that changes with the offset.";
    case 7: return "Trigger plug emulation without the digital press.";
    default: return "Trigger plug emulation, but has an analog multiplier of 2.5x, enabling lightshield with short travel triggers.";
  }
}
const char* kTourney[6] = {"Off", "Taunt Delayed", "Taunt Disabled", "Start Delayed", "Start and Taunt Delayed", "Start Delayed, Taunt Disabled"};
const char* kRemapNames[REMAP_COUNT] = {"A", "B", "D-pad Up", "L", "R", "X", "Y", "Z"};
const uint16_t kRemapBits[REMAP_COUNT] = {PAD_A, PAD_B, PAD_UP, PAD_L, PAD_R, PAD_X, PAD_Y, PAD_Z};

std::vector<Row> rows_for(int port, MenuState& m) {
  Settings& s = m.s;
  std::vector<Row> rows;
  const auto signed_val = [](int v) { return v > 0 ? "+" + std::to_string(v) : std::to_string(v); };
  switch (m.screen) {
    case MAIN: {
      static int kind;
      kind = s.phob_hardware ? 1 : 0;
      Row c = value("Controller", &kind, 0, 1, "Stock controller: PhobGCC's settings run here. PhobGCC: your controller already runs them, so only PhobScope and Phob Check are offered.",
                    [](int v) { return std::string(v ? "PhobGCC" : "Stock"); });
      c.act = [&s]() { s.phob_hardware = kind == 1; if (s.phob_hardware) s.enabled = false; };
      rows.push_back(c);
      if (!s.phob_hardware) {
        static int on;
        on = s.enabled ? 1 : 0;
        Row e = value("PhobGCC Settings", &on, 0, 1, "Turns everything below on for this socket. If this controller is a PhobGCC, choose PhobGCC above instead: these settings on top of its firmware would apply everything twice.",
                      [](int v) { return std::string(v ? "On" : "Off"); });
        e.act = [&s]() { s.enabled = on == 1; };
        rows.push_back(e);
      }
      // A PhobGCC runs these in its firmware: here they are what it is set to, kept so a setup can
      // be saved and carried to a stock controller (where they run). Nothing is applied twice.
      rows.push_back(page(STICK));
      rows.push_back(page(SETTINGS));
      rows.push_back(page(SCOPE));
      rows.push_back(page(PHOBCHECK));
      rows.push_back(page(SETUPS, m.active_setup));
      static int sock;
      sock = port;
      Row p = value("Socket", &sock, 0, 3, "The adapter socket this menu sets up.", [](int v) { return std::to_string(v + 1); });
      p.act = []() { g_port = sock; g_changed_at = g_in.time; };
      rows.push_back(p);
      break;
    }
    // A PhobGCC calibrates itself: only the settings a setup keeps are offered for it.
    case STICK: rows = s.phob_hardware ? std::vector<Row>{page(FILTER), page(GEOM)} : std::vector<Row>{page(CALIBRATE), page(FILTER), page(GEOM)}; break;
    case CALIBRATE: {
      const auto begin = [&m](Screen sc, Stick which, bool fix) {
        return [&m, sc, which, fix]() {
          StickCal& saved = which == ASTICK ? m.s.a_cal : m.s.c_cal;
          const int scaler = which == ASTICK ? m.s.a_scaler : m.s.c_scaler;
          if (fix) {
            if (!cal_begin_notch_fix(m.cal, which, saved, scaler)) { say(m, "Calibrate this stick once first."); return; }
          } else {
            cal_begin(m.cal, which, saved, scaler);
          }
          m.calibrating = true;
          m.last_buttons = 0xFFFF;
          m.screen = sc;
          g_changed_at = g_in.time;
        };
      };
      rows.push_back(action("Calibrate Left Stick", s.a_cal.measured ? "Done" : "", "The stick at rest, then each notch, 16 times around the gate; then turn each diagonal and modder notch to the angle you want.", begin(ASTICKCAL, ASTICK, false)));
      rows.push_back(action("Left Stick Notch Fix", "", "Keeps your last measurement and goes straight to adjusting the notches.", begin(ANOTCHFIX, ASTICK, true), s.a_cal.measured));
      rows.push_back(action("Calibrate C-Stick", s.c_cal.measured ? "Done" : "", "The C-stick the same way.", begin(CSTICKCAL, CSTICK, false)));
      rows.push_back(action("C-Stick Notch Fix", "", "Keeps the C-stick's last measurement and goes straight to adjusting the notches.", begin(CNOTCHFIX, CSTICK, true), s.c_cal.measured));
      rows.push_back(action("Clear Calibration", "", "Both sticks go back to their plain readings.", [&s, &m]() { s.a_cal = StickCal{}; s.c_cal = StickCal{}; say(m, "Calibration cleared."); },
                            s.a_cal.measured || s.c_cal.measured));
      break;
    }
    case FILTER: rows = {page(ASNAPBACK, signed_val(s.x_snapback) + " / " + signed_val(s.y_snapback)), page(AWAVE, signed_val(s.ax_waveshaping) + " / " + signed_val(s.ay_waveshaping)),
                         page(ASMOOTH, std::to_string(s.ax_smoothing) + " / " + std::to_string(s.ay_smoothing)), page(CSNAPBACK, std::to_string(s.cx_smoothing) + " / " + std::to_string(s.cy_smoothing)),
                         page(CWAVE, signed_val(s.cx_waveshaping) + " / " + signed_val(s.cy_waveshaping))}; break;
    case ASNAPBACK: rows = {value("Left Stick X", &s.x_snapback, kSnapbackMin, kSnapbackMax, node(ASNAPBACK).about, signed_val),
                            value("Left Stick Y", &s.y_snapback, kSnapbackMin, kSnapbackMax, node(ASNAPBACK).about, signed_val)}; break;
    case AWAVE: rows = {value("Left Stick X", &s.ax_waveshaping, kWaveshapingMin, kWaveshapingMax, node(AWAVE).about, signed_val),
                        value("Left Stick Y", &s.ay_waveshaping, kWaveshapingMin, kWaveshapingMax, node(AWAVE).about, signed_val)}; break;
    case ASMOOTH: rows = {value("Left Stick X", &s.ax_smoothing, kSmoothingMin, kSmoothingMax, node(ASMOOTH).about),
                          value("Left Stick Y", &s.ay_smoothing, kSmoothingMin, kSmoothingMax, node(ASMOOTH).about)}; break;
    case CSNAPBACK: rows = {value("C-Stick X", &s.cx_smoothing, kSmoothingMin, kSmoothingMax, node(CSNAPBACK).about),
                            value("C-Stick Y", &s.cy_smoothing, kSmoothingMin, kSmoothingMax, node(CSNAPBACK).about)}; break;
    case CWAVE: rows = {value("C-Stick X", &s.cx_waveshaping, kWaveshapingMin, kWaveshapingMax, node(CWAVE).about, signed_val),
                        value("C-Stick Y", &s.cy_waveshaping, kWaveshapingMin, kWaveshapingMax, node(CWAVE).about, signed_val)}; break;
    case GEOM: rows = {page(CARDINALS, std::to_string(s.a_cardinal) + " / " + std::to_string(s.c_cardinal)), page(RADIUS, std::to_string(s.a_scaler) + " / " + std::to_string(s.c_scaler))}; break;
    case CARDINALS: rows = {value("Left Stick", &s.a_cardinal, kCardinalMin, kCardinalMax, node(CARDINALS).about),
                            value("C-Stick", &s.c_cardinal, kCardinalMin, kCardinalMax, node(CARDINALS).about)}; break;
    case RADIUS: rows = {value("Left Stick", &s.a_scaler, kScalerMin, kScalerMax, node(RADIUS).about),
                         value("C-Stick", &s.c_scaler, kScalerMin, kScalerMax, node(RADIUS).about)}; break;
    case SETTINGS: rows = {page(SET_OVER), page(REMAP), page(TRIGGER, std::to_string(s.l_mode + 1) + " / " + std::to_string(s.r_mode + 1)), page(TOURNEY, kTourney[std::clamp(s.tourney, 0, 5)]), page(RESET)}; break;
    case SET_OVER:
      rows = {info("Left Stick", s.a_cal.measured ? "Calibrated" : "Not calibrated", node(SET_OVER).about),
              info("C-Stick", s.c_cal.measured ? "Calibrated" : "Not calibrated", node(SET_OVER).about),
              info("Snapback", signed_val(s.x_snapback) + " / " + signed_val(s.y_snapback), node(ASNAPBACK).about),
              info("Waveshaping", signed_val(s.ax_waveshaping) + " / " + signed_val(s.ay_waveshaping), node(AWAVE).about),
              info("Smoothing", std::to_string(s.ax_smoothing) + " / " + std::to_string(s.ay_smoothing), node(ASMOOTH).about),
              info("C-Stick Snapback", std::to_string(s.cx_smoothing) + " / " + std::to_string(s.cy_smoothing), node(CSNAPBACK).about),
              info("Cardinal Snapping", std::to_string(s.a_cardinal) + " / " + std::to_string(s.c_cardinal), node(CARDINALS).about),
              info("Radius", std::to_string(s.a_scaler) + " / " + std::to_string(s.c_scaler), node(RADIUS).about),
              info("L Trigger", std::to_string(s.l_mode + 1) + "  " + trigger_mode_short(s.l_mode), trigger_mode_text(s.l_mode)),
              info("R Trigger", std::to_string(s.r_mode + 1) + "  " + trigger_mode_short(s.r_mode), trigger_mode_text(s.r_mode)),
              info("Tourney", kTourney[std::clamp(s.tourney, 0, 5)], node(TOURNEY).about)};
      break;
    case REMAP:
      for (int out = 0; out < REMAP_COUNT; out++) {
        std::string from;
        for (int i = 0; i < REMAP_COUNT; i++) if (s.remap[out] & (1 << i)) { if (!from.empty()) from += " + "; from += kRemapNames[i]; }
        rows.push_back(action(kRemapNames[out], m.remap_step == out ? "Press a button" : (from.empty() ? "None" : from),
                              std::string("Press A, then the button that should press ") + kRemapNames[out] + ". Start cancels.",
                              [&m, out]() { m.remap_step = out; m.last_buttons = 0xFFFF; }));
      }
      rows.push_back(action("Reset Remapping", "", "Every button back to itself.", [&s]() { for (int i = 0; i < REMAP_COUNT; i++) s.remap[i] = (uint8_t)(1 << i); }));
      break;
    case TRIGGER: rows = {page(LTRIGGER, std::to_string(s.l_mode + 1) + "  " + trigger_mode_short(s.l_mode)),
                          page(RTRIGGER, std::to_string(s.r_mode + 1) + "  " + trigger_mode_short(s.r_mode))}; break;
    case LTRIGGER: case RTRIGGER: {
      int* mode = m.screen == LTRIGGER ? &s.l_mode : &s.r_mode;
      int* offset = m.screen == LTRIGGER ? &s.l_offset : &s.r_offset;
      rows.push_back(value("Mode", mode, kTriggerModeMin, kTriggerModeMax, trigger_mode_text(*mode),
                           [](int v) { return std::to_string(v + 1) + "  " + trigger_mode_short(v); }));
      Row o = value("Offset", offset, kTriggerOffsetMin, kTriggerOffsetMax, "The value modes 4 to 9 use: the cap, the lightshield, or the multiplier.");
      o.enabled = *mode >= 3;
      rows.push_back(o);
      break;
    }
    case TOURNEY: {
      Row r = value("Lockout", &s.tourney, kTourneyMin, kTourneyMax, node(TOURNEY).about, [](int v) { return std::string(kTourney[std::clamp(v, 0, 5)]); });
      rows.push_back(r);
      break;
    }
    case RESET:
      rows.push_back(action("Soft Reset", m.reset_pending == 0 ? "Press A again" : "", "This resets everything to defaults except for stick calibration, which is untouched.",
                            [&m, &s]() { if (m.reset_pending == 0) { soft_reset(s); m.reset_pending = -1; say(m, "Soft reset done."); } else m.reset_pending = 0; }));
      rows.push_back(action("Hard Reset", m.reset_pending == 1 ? "Press A again" : "", "This resets everything including stick cal. You'll need to perform stick calibration again.",
                            [&m, &s]() { if (m.reset_pending == 1) { hard_reset(s); m.reset_pending = -1; say(m, "Hard reset done."); } else m.reset_pending = 1; }));
      break;
    case SCOPE: rows = {page(INPUTVIEW), page(XYSCOPE), page(TIMESCOPE), page(PRESSTIME), page(REACTION), page(CONTSCOPE), page(GATE), page(EXPORT)}; break;
    case INPUTVIEW: rows = {info("Adapter", std::to_string((int)std::lround(host::gcadapter_poll_rate_hz())) + " Hz", node(INPUTVIEW).about)}; break;
    case XYSCOPE: {
      static const char* maps[7] = {"None", "Deadzones", "Wait Attacks", "Wait Movement", "Crouch", "Left Ledge", "Right Ledge"};
      rows.push_back(value("Stickmap", &m.stickmap, 0, 6, "Melee's stick regions behind the recording: deadzones, wait attacks, wait movement, crouch and the ledges.",
                           [](int v) { return std::string(maps[std::clamp(v, 0, 6)]); }));
      static int which;
      which = m.cap.c_stick ? 1 : 0;
      Row w = value("Stick", &which, 0, 1, "Which stick to record.", [](int v) { return std::string(v ? "C-Stick" : "Left Stick"); });
      w.act = [&m]() { m.cap.c_stick = which == 1; };
      rows.push_back(w);
      rows.push_back(action("Record", m.cap.mode == Mode::StickMap && !m.cap.done ? "Waiting for motion" : "", "Start or A: 100 ms of motion, starting when the stick moves.",
                            [&m, port]() { m.cap.stick_thresh = 10; begin_capture(port, m, Mode::StickMap); m.view_index = 0; }));
      if (m.have_shown && m.shown.mode == Mode::StickMap)
        rows.push_back(value("Highlight Sample", &m.view_index, 0, 99, "Step through the recording; the card shows that sample's coordinates and buttons."));
      break;
    }
    case TIMESCOPE: {
      static const char* inputs[6] = {"Left Stick X", "Left Stick Y", "C-Stick X", "C-Stick Y", "L Trigger", "R Trigger"};
      static const char* measures[3] = {"Snapback", "Dashback", "Pivots"};
      rows.push_back(value("Input", &m.time_input, 0, 5, "Which input to record.", [](int v) { return std::string(inputs[std::clamp(v, 0, 5)]); }));
      Row me = value("Measurement", &m.time_measure, 0, 2, "Snapback: flick from the rim and let go. Dashback: dash from neutral. Pivots: hold a dash one way, then flick the other way.",
                     [](int v) { return std::string(measures[std::clamp(v, 0, 2)]); });
      if (m.time_input >= 4) { me.enabled = false; me.show = [](int) { return std::string("Powershield"); }; }
      rows.push_back(me);
      static int ar;
      ar = m.auto_repeat ? 1 : 0;
      Row a = value("Auto-Repeat", &ar, 0, 1, "Records again by itself a moment after each result.", [](int v) { return std::string(v ? "On" : "Off"); });
      a.act = [&m]() { m.auto_repeat = ar == 1; };
      rows.push_back(a);
      rows.push_back(action("Record", "", "Start or A records 200 ms around your next input.", [&m, port]() { begin_capture(port, m, time_mode(m)); }));
      break;
    }
    case PRESSTIME: {
      rows.push_back(value("Stick", &m.cap.stick_thresh, 10, 100, "How far a stick must move to count as pressed."));
      rows.push_back(value("Trigger", &m.cap.trigger_thresh, 10, 255, "How far a trigger must travel to count as pressed."));
      static int ar;
      ar = m.auto_repeat ? 1 : 0;
      Row a = value("Auto-Repeat", &ar, 0, 1, "Records again by itself a moment after each result.", [](int v) { return std::string(v ? "On" : "Off"); });
      a.act = [&m]() { m.auto_repeat = ar == 1; };
      rows.push_back(a);
      rows.push_back(action("Record", m.cap.mode == Mode::Press && !m.cap.begin ? "Waiting for input" : "",
                            "Press Start to initiate 200ms of recording, starting when you press ABXYLRZ or move a stick.",
                            [&m, port]() { begin_capture(port, m, Mode::Press); }));
      break;
    }
    case REACTION: {
      rows.push_back(value("Stick", &m.cap.stick_thresh, 10, 100, "Set stick & trigger thresholds: how far a stick must move to count."));
      rows.push_back(value("Trigger", &m.cap.trigger_thresh, 10, 200, "How far a trigger must travel to count."));
      rows.push_back(action("Start Test", m.reaction_waiting ? "Wait..." : "", "Press start to begin the reaction test. You can press any button or move any stick.",
                            [&m]() {
                              static std::mt19937 rng{std::random_device{}()};
                              m.reaction_waiting = true;   // firmware: a random 3 to 8 seconds
                              m.reaction_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000 + (int)(rng() % 5000));
                              m.cap.mode = Mode::None;
                              m.last_buttons = 0xFFFF;
                            }));
      break;
    }
    case CONTSCOPE: {
      static const char* inputs[6] = {"Left Stick X", "Left Stick Y", "C-Stick X", "C-Stick Y", "L Trigger", "R Trigger"};
      rows.push_back(value("Input", &m.cont_input, 0, 5, "Which input to draw.", [](int v) { return std::string(inputs[std::clamp(v, 0, 5)]); }));
      Row w = value("Window", &m.cont_window, 100, 4000, "How many reports wide the scope is.", [](int v) { return std::to_string(v) + " ms"; });
      w.step = 100;
      rows.push_back(w);
      rows.push_back(action("Freeze", m.frozen ? "Frozen" : "", "Holds what is on screen; press again to continue.",
                            [&m, port]() { m.frozen = !m.frozen; if (m.frozen) latest(port, m.frozen_samples, m.cont_window); }));
      break;
    }
    case GATE: {
      Row w = value("Stick", &m.gate_stick, 0, 1, "Which stick to trace.", [](int v) { return std::string(v ? "C-Stick" : "Left Stick"); });
      rows.push_back(w);
      rows.push_back(action("Clear", "", "Start a new outline.", [&m]() { std::fill(std::begin(m.gate), std::end(m.gate), 0.0f); }));
      break;
    }
    case EXPORT:
      rows.push_back(action("Export CSV", "", node(EXPORT).about, [&m, port]() {
        std::vector<host::GCScopeSample> v;
        latest(port, v, 4096);
        char file[64];
        std::snprintf(file, sizeof file, "scope-port%d-%lld.csv", port + 1,
                      (long long)std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        const std::string name = host::phob_store_folder() + "/" + file;
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::u8path(host::phob_store_folder()), ec);
        std::ofstream out(std::filesystem::u8path(name));
        out << "time_us,raw_ax,raw_ay,raw_cx,raw_cy,raw_l,raw_r,raw_buttons,ax,ay,cx,cy,l,r,buttons\n";
        for (const auto& s : v)
          out << s.time_us << ',' << (int)s.raw_ax << ',' << (int)s.raw_ay << ',' << (int)s.raw_cx << ',' << (int)s.raw_cy << ','
              << (int)s.raw_l << ',' << (int)s.raw_r << ',' << s.raw_buttons << ',' << (int)s.ax << ',' << (int)s.ay << ','
              << (int)s.cx << ',' << (int)s.cy << ',' << (int)s.l << ',' << (int)s.r << ',' << s.buttons << '\n';
        say(m, out ? std::string("Saved ") + file : std::string("Could not write the file."));
      }));
      break;
    case PHOBCHECK: {
      const char* run = "Run it in PhobScope > Value vs Time Plots; the latest result shows here.";
      rows.push_back(info("Snapback", m.last_snapback < 0 ? "-" : pct(m.last_snapback),
                          m.last_snapback > 0.5f ? "A release can read as a turn the other way. Raise Smart Snapback (Stick Configuration > Stick Filter Settings) on the axis you flicked." : run));
      rows.push_back(info("Vanilla Dashback", pct(m.last_db_van), m.last_db_van >= 0 && m.last_db_van < 99.5f ? "Part of the flick sat in the tilt zone. A faster flick, or Waveshaping, changes it." : run));
      rows.push_back(info("UCF Dashback", pct(m.last_db_ucf), run));
      rows.push_back(info("Empty Pivot", pct(m.last_pivot[1]), m.last_pivot[1] >= 0 ? "Mostly timing: the empty pivot window is one frame." : run));
      rows.push_back(info("Powershield", m.last_trig[2] < 0 ? "-" : (m.last_trig[2] < 0.5f ? "OK" : pct(100 - m.last_trig[2])),
                          m.last_trig[2] >= 0.5f ? "The analog crossed 43 too long before the click. Trigger modes 2 (digital only) or 6 change this." : run));
      break;
    }
    case SETUPS: {
      // Back to the defaults in one place: the firmware's Soft Reset, which keeps the stick calibration.
      rows.push_back(action("Reset to Default", m.reset_pending == 2 ? "Press A again" : "",
                            "Every setting back to PhobGCC's default. Your stick calibration stays.",
                            [&m, &s]() {
                              if (m.reset_pending == 2) { soft_reset(s); m.reset_pending = -1; m.active_setup.clear(); say(m, "Back to the defaults."); }
                              else m.reset_pending = 2;
                            }));
      if (!s.phob_hardware) {
        static int with_cal;
        with_cal = m.setup_cal ? 1 : 0;
        Row c = value("Include Calibration", &with_cal, 0, 1, "A setup without calibration keeps each player's own stick calibration when loaded.",
                      [](int v) { return std::string(v ? "Yes" : "No"); });
        c.act = [&m]() { m.setup_cal = with_cal == 1; };
        rows.push_back(c);
      }
      const std::vector<std::string> names = host::phob_setup_list();
      // The first "Setup N" not taken, so a deleted setup's name never makes a new one overwrite another.
      int n = 1;
      while (std::find(names.begin(), names.end(), "Setup " + std::to_string(n)) != names.end()) ++n;
      const std::string fresh = "Setup " + std::to_string(n);
      rows.push_back(action("Save New Setup", fresh, "Saves these settings as a new setup (the PhobGCC Setups folder beside your settings, ready to share).",
                            [&m, fresh]() {
                              if (host::phob_setup_save(fresh, m.s, m.setup_cal && !m.s.phob_hardware)) { m.active_setup = fresh; say(m, "Saved " + fresh + "."); }
                              else say(m, "Could not save.");
                            }));
      for (const std::string& name : names)
        rows.push_back(action("Load " + name, name == m.active_setup ? "In use" : "", "Loads this setup onto this socket.",
                              [&m, name]() { if (host::phob_setup_load(name, m.s)) { m.active_setup = name; say(m, "Loaded " + name + "."); } }));
      if (!names.empty()) {
        static int pick;
        m.setup_pick = std::clamp(m.setup_pick, 0, (int)names.size() - 1);
        pick = m.setup_pick;
        Row c = value("Change Setup", &pick, 0, (int)names.size() - 1, "The setup the two rows below act on.",
                      [names](int v) { return v >= 0 && v < (int)names.size() ? names[v] : std::string(); });
        c.act = [&m]() { m.setup_pick = pick; m.setup_delete_pending = -1; };
        rows.push_back(c);
        const std::string chosen = names[m.setup_pick];
        rows.push_back(action("Save Over It", "", "Replaces " + chosen + " with the settings on this socket now.",
                              [&m, chosen]() {
                                if (host::phob_setup_save(chosen, m.s, m.setup_cal && !m.s.phob_hardware)) { m.active_setup = chosen; say(m, "Saved over " + chosen + "."); }
                                else say(m, "Could not save.");
                              }));
        rows.push_back(action("Delete It", m.setup_delete_pending == m.setup_pick ? "Press A again" : "", "Deletes " + chosen + ".",
                              [&m, chosen]() {
                                if (m.setup_delete_pending != m.setup_pick) { m.setup_delete_pending = m.setup_pick; return; }
                                m.setup_delete_pending = -1;
                                if (host::phob_setup_delete(chosen)) { if (m.active_setup == chosen) m.active_setup.clear(); m.setup_pick = 0; say(m, "Deleted " + chosen + "."); }
                                else say(m, "Could not delete.");
                              }));
      }
      if (s.phob_hardware)
        rows.push_back(info("PhobGCC", "", "Your controller runs its own settings: a setup here is a record of them to set on the controller, and it runs as is on a stock controller."));
      break;
    }
    default: break;
  }
  return rows;
}

// ---- drawing ----
ImU32 with_alpha(ImU32 c, float a) { return (c & 0x00FFFFFF) | ((ImU32)std::clamp((float)((c >> 24) & 0xFF) * a, 0.0f, 255.0f) << 24); }
constexpr ImU32 kGold = IM_COL32(201, 150, 47, 255), kGoldText = IM_COL32(233, 180, 76, 255), kCream = IM_COL32(242, 230, 200, 255);
constexpr ImU32 kYellow = IM_COL32(255, 216, 90, 255), kOrange = IM_COL32(255, 157, 74, 255), kGreen = IM_COL32(92, 255, 138, 255);

void text(ImDrawList* dl, float x, float y, float px, ImU32 c, const std::string& s) { pm::text(dl, x, y, px, c, s.c_str()); }
float tw(float px, const std::string& s) { return pm::text_width(px, s.c_str()); }
// Text wrapped to `w`; returns the height used.
float para(ImDrawList* dl, float x, float y, float w, float px, ImU32 c, const std::string& s) {
  float yy = y;
  for (const std::string& line : pm::wrap_text(s, px, w)) { text(dl, x, yy, px, c, line); yy += px * 1.15f; }
  return yy - y;
}

// Each line centred on `cx`; returns the height used.
float para_centred(ImDrawList* dl, float cx, float y, float w, float px, ImU32 c, const std::string& s) {
  float yy = y;
  for (const std::string& line : pm::wrap_text(s, px, w)) { text(dl, cx - tw(px, line) * 0.5f, yy, px, c, line); yy += px * 1.15f; }
  return yy - y;
}

struct Card { float x0, y0, x1, y1, u; };
// The card's footnote, wrapped at the bottom; returns where it starts.
float foot_top(const Card& c, const std::string& note) {
  const float u = c.u, px = 15 * u;
  return c.y1 - 18 * u - pm::wrap_text(note, px, c.x1 - c.x0 - 48 * u).size() * px * 1.15f;
}
float foot(ImDrawList* dl, const Card& c, float a, const std::string& note) {
  const float u = c.u, px = 15 * u, top = foot_top(c, note);
  float y = top;
  for (const std::string& line : pm::wrap_text(note, px, c.x1 - c.x0 - 48 * u)) { text(dl, c.x0 + 24 * u, y, px, IM_COL32(180, 165, 130, (int)(255 * a)), line); y += px * 1.15f; }
  return top;
}
Card card(ImDrawList* dl, float x0, float y0, float x1, float y1, float u, const char* title, float a) {
  dl->AddRectFilled(ImVec2(x0 + 3 * u, y0 + 4 * u), ImVec2(x1 + 3 * u, y1 + 4 * u), with_alpha(IM_COL32(0, 0, 0, 150), a), 14 * u);
  pm::gold::fill_rect(dl, x0, y0, x1, y1, with_alpha(IM_COL32(24, 20, 12, 236), a), with_alpha(IM_COL32(6, 5, 3, 236), a), 14 * u);
  dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), with_alpha(kGold, a), 14 * u, 0, 3 * u);
  if (title && *title) pm::text(dl, x0 + 20 * u, y0 + 14 * u, 24 * u, with_alpha(kGoldText, a), title, 0.19f);
  return Card{x0, y0, x1, y1, u};
}

// The gate, Melee's rim and deadzone, and dots: grey for the reading, gold for what the game gets.
void stick_picture(ImDrawList* dl, float cx, float cy, float r, float u, float a, float raw_x, float raw_y, float out_x, float out_y,
                   const float* tx = nullptr, const float* ty = nullptr, bool show_raw = true) {
  const float k = r / 110.0f;
  ImVec2 oct[8];
  for (int i = 0; i < 8; i++) { float x, y; calc_stick_values(3.14159265f * i / 4, x, y); oct[i] = ImVec2(cx + x * k, cy - y * k); }
  dl->AddConvexPolyFilled(oct, 8, with_alpha(IM_COL32(201, 150, 47, 20), a));
  dl->AddPolyline(oct, 8, with_alpha(kGold, a), ImDrawFlags_Closed, 3.0f * u);
  for (int i = 0; i < 48; i += 2) {   // Melee's rim, dashed
    const float a0 = i * 3.14159265f / 24, a1 = (i + 1) * 3.14159265f / 24;
    dl->AddLine(ImVec2(cx + std::cos(a0) * 80 * k, cy - std::sin(a0) * 80 * k), ImVec2(cx + std::cos(a1) * 80 * k, cy - std::sin(a1) * 80 * k), with_alpha(IM_COL32(120, 100, 52, 255), a), 2.0f * u);
  }
  dl->AddRect(ImVec2(cx - 22 * k, cy - 22 * k), ImVec2(cx + 22 * k, cy + 22 * k), with_alpha(IM_COL32(140, 105, 40, 255), a), 0, 0, 2.0f * u);
  if (tx && ty) dl->AddCircle(ImVec2(cx + *tx * k, cy - *ty * k), 11 * u, with_alpha(kGreen, a), 24, 3 * u);
  if (show_raw) dl->AddCircleFilled(ImVec2(cx + raw_x * k, cy - raw_y * k), 6 * u, with_alpha(IM_COL32(150, 150, 150, 255), a), 18);
  dl->AddCircleFilled(ImVec2(cx + out_x * k, cy - out_y * k), 9 * u, with_alpha(IM_COL32(0, 0, 0, 255), a), 20);
  dl->AddCircleFilled(ImVec2(cx + out_x * k, cy - out_y * k), 7 * u, with_alpha(kYellow, a), 20);
}

void coord_lines(ImDrawList* dl, float x, float y, float u, float a, const char* name, int sx, int sy, bool desync_check) {
  float mx, my;
  melee_coords(sx, sy, mx, my);
  char b[64];
  std::snprintf(b, sizeof b, "%s  %d, %d", name, sx, sy);
  text(dl, x, y, 20 * u, with_alpha(kYellow, a), b);
  std::snprintf(b, sizeof b, "Melee  %.4f, %.4f", mx, my);
  text(dl, x, y + 26 * u, 18 * u, with_alpha(kCream, a), b);
  const double qa = std::atan2(std::fabs((double)sy), std::fabs((double)sx)) * 180 / 3.14159265358979;
  std::snprintf(b, sizeof b, "Angle %.2f  Magnitude %.4f", qa, std::sqrt((double)sx * sx + (double)sy * sy) / 80);
  text(dl, x, y + 50 * u, 15 * u, with_alpha(IM_COL32(200, 185, 150, 255), a), b);
  if (desync_check && (sx || sy) && ic_desync_coordinate(sx, sy)) text(dl, x, y + 72 * u, 15 * u, with_alpha(kOrange, a), "Desyncs the Ice Climbers");
}

void plot_area(ImDrawList* dl, float x0, float y0, float x1, float y1, float u, float a) {
  dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), with_alpha(IM_COL32(4, 4, 6, 255), a), 6 * u);
  dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), with_alpha(IM_COL32(90, 70, 30, 255), a), 6 * u, 0, 1.5f * u);
}

// 200 ms of one value, filtered (white) over unfiltered (grey), the way PhobScope draws it.
void plot_capture(ImDrawList* dl, const host::phobscope::Capture& c, bool trigger, float x0, float y0, float x1, float y1, float u, float a) {
  uint8_t f[200], g[200];
  host::phobscope::ordered(c, c.a1, f);
  host::phobscope::ordered(c, c.a1_unfilt, g);
  plot_area(dl, x0, y0, x1, y1, u, a);
  const float w = x1 - x0, h = y1 - y0, step = w / 199.0f;
  const auto y_of = [&](int v) { return trigger ? y1 - v * h / 256.0f : y0 + h / 2 - (v - 127) * h / 256.0f; };
  for (int i = 0; i < 200; i += 17) dl->AddLine(ImVec2(x0 + i * step, y0), ImVec2(x0 + i * step, y1), with_alpha(IM_COL32(40, 34, 20, 255), a), 1.0f * u);
  for (int lvl : trigger ? std::vector<int>{43} : std::vector<int>{127 + 23, 127 - 23, 127 + 64, 127 - 64})
    dl->AddLine(ImVec2(x0, y_of(lvl)), ImVec2(x1, y_of(lvl)), with_alpha(IM_COL32(120, 90, 30, 200), a), 1.2f * u);
  for (int i = 1; i < 200; i++) dl->AddLine(ImVec2(x0 + (i - 1) * step, y_of(g[i - 1])), ImVec2(x0 + i * step, y_of(g[i])), with_alpha(IM_COL32(130, 130, 130, 220), a), 2.0f * u);
  for (int i = 1; i < 200; i++) dl->AddLine(ImVec2(x0 + (i - 1) * step, y_of(f[i - 1])), ImVec2(x0 + i * step, y_of(f[i])), with_alpha(IM_COL32(255, 255, 255, 255), a), 2.6f * u);
  if (trigger) {
    uint8_t d[200];
    host::phobscope::ordered(c, c.buttons, d);
    for (int i = 0; i < 200; i++)
      if (d[i]) dl->AddRectFilled(ImVec2(x0 + i * step, y1 - 8 * u), ImVec2(x0 + (i + 1) * step, y1), with_alpha(IM_COL32(90, 170, 255, 255), a));
  }
}

// A percentage over its label, both fitted to a column `w` wide; 70 units tall.
void big_result(ImDrawList* dl, float x, float y, float w, float u, float a, const char* label, float value, ImU32 color) {
  char b[16];
  std::snprintf(b, sizeof b, "%.0f%%", std::round(value));
  const float npx = std::min(44 * u, 44 * u * (w - 8 * u) / std::max(1.0f, tw(44 * u, b)));
  const float lpx = std::min(16 * u, 16 * u * (w - 8 * u) / std::max(1.0f, tw(16 * u, label)));
  text(dl, x, y + (44 * u - npx) * 0.7f, npx, with_alpha(color, a), b);
  text(dl, x, y + 50 * u, lpx, with_alpha(IM_COL32(200, 185, 150, 255), a), label);
}

// The capture's results in columns across `w`.
void draw_results(ImDrawList* dl, const host::phobscope::Capture& c, float x, float y, float w, float u, float a) {
  const float w2 = w / 2, w3 = w / 3;
  switch (c.mode) {
    case Mode::Snapback:
      big_result(dl, x, y, w2, u, a, "POSITIVE", c.percents[0], kGreen);
      big_result(dl, x + w2, y, w2, u, a, "NEGATIVE", std::fmax(0.0f, 100 - c.percents[0]), c.percents[0] > 99.5f ? kGreen : kOrange);
      break;
    case Mode::Dashback:
      big_result(dl, x, y, w2, u, a, "VANILLA", c.percents[0], c.percents[0] > 99.5f ? kGreen : kYellow);
      big_result(dl, x + w2, y, w2, u, a, "UCF", c.percents[1], c.percents[1] > 99.5f ? kGreen : kYellow);
      break;
    case Mode::Pivot:
      big_result(dl, x, y, w3, u, a, "NO TURN", c.percents[0], kCream);
      big_result(dl, x + w3, y, w3, u, a, "PIVOT", c.percents[1], kGreen);
      big_result(dl, x + w3 * 2, y, w3, u, a, "DASHBACK", c.percents[2], kCream);
      break;
    case Mode::Trigger:
      big_result(dl, x, y, w3, u, a, "DIGITAL PS", c.percents[0], kGreen);
      big_result(dl, x + w3, y, w3, u, a, "ADT PS", c.percents[1], kYellow);
      big_result(dl, x + w3 * 2, y, w3, u, a, "NO PS", c.percents[2], c.percents[2] > 0.5f ? kOrange : kCream);
      break;
    default: break;
  }
}

const char* notch_name(int n) {
  static const char* names[16] = {"right", "notch 1", "up right", "notch 2", "up", "notch 3", "up left", "notch 4",
                                  "left", "notch 5", "down left", "notch 6", "down", "notch 7", "down right", "notch 8"};
  return n >= 0 && n < 16 ? names[n] : "";
}

// ---- calibration: a full screen card, driven by the controller being calibrated ----
bool run_calibration(ImDrawList* dl, int port, MenuState& m, float gx0, float gy0, float gx1, float gy1, float u, float a) {
  bool changed = false;
  const Stick which = m.cal.which;
  StickCal& saved = which == ASTICK ? m.s.a_cal : m.s.c_cal;
  host::phob::HardwareInput in;
  const bool have = host::gcadapter_phob_raw(port, in);
  const float rx = which == ASTICK ? in.ax / 255.0f : in.cx / 255.0f;
  const float ry = which == ASTICK ? in.ay / 255.0f : in.cy / 255.0f;
  // The reading kept for a step: the adapter's last 32 ms averaged, as the firmware averages its
  // readings at the press (whatever the display's frame rate).
  float ax = rx, ay = ry;
  {
    std::vector<host::GCScopeSample> recent;
    latest(port, recent, 64);
    float sx = 0, sy = 0; int n = 0;
    for (const host::GCScopeSample& r : recent) {
      if ((int32_t)(recent.back().time_us - r.time_us) > 32000) continue;
      sx += which == ASTICK ? r.hw_ax : r.hw_cx; sy += which == ASTICK ? r.hw_ay : r.hw_cy; n++;
    }
    if (n) { ax = sx / n / 255.0f; ay = sy / n / 255.0f; }
  }
  const auto now = std::chrono::steady_clock::now();
  const float dt = std::min(50.0f, std::chrono::duration<float, std::milli>(now - m.last_frame).count());
  m.last_frame = now;
  const uint16_t b = have ? in.buttons : 0;
  const uint16_t pressed = m.last_buttons == 0xFFFF ? 0 : (uint16_t)(b & ~m.last_buttons);
  m.last_buttons = b;
  if (pressed & PAD_START) { m.calibrating = false; m.screen = CALIBRATE; g_changed_at = g_in.time; return false; }

  const float W = gx1 - gx0;
  const Card c = card(dl, gx0 + W * 0.06f, gy0 + 110 * u, gx1 - W * 0.06f, gy1 - 110 * u, u, which == ASTICK ? "LEFT STICK" : "C-STICK", a);
  const float r = std::min((c.y1 - c.y0) * 0.36f, (c.x1 - c.x0) * 0.2f), cx = c.x0 + 34 * u + r, cy = (c.y0 + c.y1) * 0.5f + 10 * u;
  const float tx0 = cx + r + 40 * u, tw0 = c.x1 - 28 * u - tx0;
  const ImU32 soft = with_alpha(IM_COL32(200, 185, 150, 255), a);
  char line[96];
  if (!have) para(dl, tx0, c.y0 + 52 * u, tw0, 20 * u, with_alpha(kOrange, a), "Plug a controller into socket " + std::to_string(port + 1) + ".");
  float y = c.y0 + 100 * u;
  if (m.cal.step < kCalPoints) {
    std::snprintf(line, sizeof line, "Measurement   Step %d of %d", m.cal.step + 1, kCalPoints);
    y += para(dl, tx0, y, tw0, 20 * u, with_alpha(kGoldText, a), line) + 16 * u;
    const CalPrompt p = cal_prompt(m.cal.step);
    float px = 0, py = 0;
    if (p.centre) {
      y += para(dl, tx0, y, tw0, 28 * u, with_alpha(kCream, a), "Let go of the stick and press A, L or R.");
    } else {
      calc_stick_values(3.14159265f * p.notch / 8, px, py);
      y += para(dl, tx0, y, tw0, 28 * u, with_alpha(kCream, a), "Hold the stick into");
      y += para(dl, tx0, y, tw0, 28 * u, with_alpha(kYellow, a), std::string("the ") + notch_name(p.notch) + " notch");
      if (p.optional) para(dl, tx0, y + 14 * u, tw0, 18 * u, soft, "If no notch exists, leave the stick centred.");
    }
    stick_picture(dl, cx, cy, r, u, a, (rx - 0.5f) * 200, (ry - 0.5f) * 200, (rx - 0.5f) * 200, (ry - 0.5f) * 200,
                  p.centre ? nullptr : &px, p.centre ? nullptr : &py, false);
    std::snprintf(line, sizeof line, "Raw %.4f, %.4f", ax, ay);
    text(dl, tx0, c.y1 - 70 * u, 18 * u, soft, line);
    // Progress: one tick per step, fitted to the column.
    const float pitch = std::min(12 * u, tw0 / kCalPoints), tick = pitch * 0.66f;
    for (int i = 0; i < kCalPoints; i++) {
      const float x = tx0 + i * pitch, ty = c.y1 - 36 * u;
      dl->AddRectFilled(ImVec2(x, ty), ImVec2(x + tick, ty + tick), with_alpha(i < m.cal.step ? kYellow : i == m.cal.step ? kCream : IM_COL32(70, 56, 26, 255), a), 2 * u);
    }
  } else {
    std::snprintf(line, sizeof line, "Notch Adjust   Step %d of %d", m.cal.step + 1, kCalSteps);
    y += para(dl, tx0, y, tw0, 20 * u, with_alpha(kGoldText, a), line) + 16 * u;
    const int notch = cal_adjust_notch(m.cal.step);
    y += para(dl, tx0, y, tw0, 28 * u, with_alpha(kCream, a), "Put the stick in the");
    y += para(dl, tx0, y, tw0, 28 * u, with_alpha(kYellow, a), std::string(notch_name(notch)) + " notch");
    para(dl, tx0, y + 14 * u, tw0, 18 * u, soft, "X and Y turn its output, B puts it back. A, L or R moves on.");
    cal_adjust(m.cal, (b & PAD_X) != 0, (b & PAD_Y) != 0, (b & PAD_B) != 0, dt);
    const float lx = linearize(rx, m.cal.params.fit_x), ly = linearize(ry, m.cal.params.fit_y);
    float ox = 0, oy = 0;
    Settings preview; preview.a_scaler = preview.c_scaler = m.cal.scaler;
    notch_remap(lx, ly, ox, oy, m.cal.params, 1, preview, which);
    float px, py;
    calc_stick_values(m.cal.measured_angles[notch], px, py);
    stick_picture(dl, cx, cy, r, u, a, (rx - 0.5f) * 200, (ry - 0.5f) * 200, ox, oy, &px, &py);
    coord_lines(dl, tx0, c.y1 - 210 * u, u, a, "Output", (int)std::lround(ox), (int)std::lround(oy), which == ASTICK);
  }
  const float hw = pm::legend(dl, 0, 0, u, "A:Next|Z:Back|X:Turn|Y:Turn|B:Reset|S:Cancel", a, false);
  pm::legend(dl, (gx0 + gx1) * 0.5f - hw * 0.5f, c.y1 + 40 * u, u, "A:Next|Z:Back|X:Turn|Y:Turn|B:Reset|S:Cancel", a);
  if (pressed & PAD_Z) cal_undo(m.cal);
  if ((pressed & (PAD_A | PAD_L | PAD_R)) && have && cal_advance(m.cal, ax, ay, saved)) {
    m.calibrating = false;
    m.screen = CALIBRATE;
    g_changed_at = g_in.time;
    say(m, which == ASTICK ? "Left stick calibrated." : "C-stick calibrated.");
    changed = true;
  }
  return changed;
}

// ---- the card on the right of each screen ----
void draw_card(ImDrawList* dl, int port, MenuState& m, float x0, float y0, float x1, float y1, float u, float a) {
  std::vector<host::GCScopeSample> v;
  latest(port, v, 1);
  const bool have = !v.empty();
  const host::GCScopeSample s = have ? v.back() : host::GCScopeSample{};
  const float W = x1 - x0;
  switch (m.screen) {
    case INPUTVIEW: {
      const Card c = card(dl, x0, y0, x1, y1, u, "INPUT VIEWER", a);
      // Top down: the two sticks, each one's numbers, the triggers, the buttons.
      const float r = std::max(30 * u, std::min(W * 0.2f, (y1 - y0 - 312 * u) * 0.5f));
      stick_picture(dl, c.x0 + W * 0.25f, c.y0 + 66 * u + r, r, u, a, s.raw_ax, s.raw_ay, s.ax, s.ay);
      stick_picture(dl, c.x0 + W * 0.75f, c.y0 + 66 * u + r, r, u, a, s.raw_cx, s.raw_cy, s.cx, s.cy);
      const float ty = c.y0 + 84 * u + 2 * r;
      coord_lines(dl, c.x0 + 24 * u, ty, u, a, "Stick", s.ax, s.ay, true);
      coord_lines(dl, c.x0 + 24 * u, ty + 92 * u, u, a, "C-Stick", s.cx, s.cy, false);
      char b[64];
      std::snprintf(b, sizeof b, "L %d > %d     R %d > %d", s.raw_l, s.l, s.raw_r, s.r);
      text(dl, c.x0 + 24 * u, ty + 168 * u, 19 * u, with_alpha(kCream, a), b);
      std::string held;
      const struct { uint16_t bit; const char* n; } names[] = {{PAD_A, "A"}, {PAD_B, "B"}, {PAD_X, "X"}, {PAD_Y, "Y"}, {PAD_Z, "Z"}, {PAD_L, "L"},
          {PAD_R, "R"}, {PAD_START, "Start"}, {PAD_UP, "Up"}, {PAD_DOWN, "Down"}, {PAD_LEFT, "Left"}, {PAD_RIGHT, "Right"}};
      for (const auto& n : names) if (s.buttons & n.bit) { held += n.n; held += "  "; }
      text(dl, c.x0 + 24 * u, ty + 194 * u, 19 * u, with_alpha(kYellow, a), held.empty() ? std::string("No buttons") : held);
      return;
    }
    case XYSCOPE: {
      const Card c = card(dl, x0, y0, x1, y1, u, "STICKMAP", a);
      const char* note = "White: filtered. Grey: unfiltered. Stickmaps by venus23gg (Altimor's visualizer).";
      const float size = std::min(W - 60 * u, foot_top(c, note) - (c.y0 + 52 * u) - 62 * u), k = size / 255.0f;
      const float px0 = c.x0 + (W - size) * 0.5f, py0 = c.y0 + 52 * u;
      dl->AddRectFilled(ImVec2(px0, py0), ImVec2(px0 + size, py0 + size), with_alpha(IM_COL32(0, 0, 0, 255), a));
      if (m.stickmap > 0)
        for (const Run& r : stickmap_runs(m.stickmap - 1))
          dl->AddRectFilled(ImVec2(px0 + r.col * k, py0 + r.row * k), ImVec2(px0 + (r.col + r.len) * k, py0 + (r.row + 1) * k), with_alpha(IM_COL32(r.grey, r.grey, r.grey, 255), a));
      dl->AddRect(ImVec2(px0, py0), ImVec2(px0 + size, py0 + size), with_alpha(kGold, a), 0, 0, 2 * u);
      if (m.have_shown && m.shown.mode == Mode::StickMap) {
        uint8_t x[100], y[100], ux[100], uy[100], bt[100];
        host::phobscope::ordered(m.shown, m.shown.a1, x); host::phobscope::ordered(m.shown, m.shown.a2, y);
        host::phobscope::ordered(m.shown, m.shown.a1_unfilt, ux); host::phobscope::ordered(m.shown, m.shown.a2_unfilt, uy);
        host::phobscope::ordered(m.shown, m.shown.buttons, bt);
        const auto at = [&](int vx, int vy) { return ImVec2(px0 + vx * k, py0 + (255 - vy) * k); };
        for (int i = 0; i < 100; i++) dl->AddCircleFilled(at(ux[i], uy[i]), 2.5f * u, with_alpha(IM_COL32(130, 130, 130, 255), a), 10);
        for (int i = 0; i < 100; i++) dl->AddCircleFilled(at(x[i], y[i]), 3.2f * u, with_alpha(IM_COL32(255, 255, 255, 255), a), 10);
        const int i = std::clamp(m.view_index, 0, 99);
        dl->AddCircle(at(x[i], y[i]), 10 * u, with_alpha(IM_COL32(255, 80, 80, 255), a), 20, 3 * u);
        float mx, my;
        melee_coords(x[i] - 127, y[i] - 127, mx, my);
        static const char* names[8] = {"A", "B", "X", "Y", "S", "Z", "L", "R"};
        std::string held;
        for (int bb = 0; bb < 8; bb++) if (bt[i] & (1 << bb)) { held += names[bb]; held += " "; }
        char b[96];
        std::snprintf(b, sizeof b, "#%d   %d, %d   %s", i, x[i] - 127, y[i] - 127, held.c_str());
        text(dl, c.x0 + 24 * u, py0 + size + 10 * u, 19 * u, with_alpha(kYellow, a), b);
        std::snprintf(b, sizeof b, "Melee %.4f, %.4f", mx, my);
        text(dl, c.x0 + 24 * u, py0 + size + 34 * u, 17 * u, with_alpha(kCream, a), b);
      } else {
        text(dl, c.x0 + 24 * u, py0 + size + 14 * u, 19 * u, with_alpha(kCream, a), m.cap.mode == Mode::StickMap ? "Waiting for motion..." : "Start records.");
      }
      foot(dl, c, a, note);
      return;
    }
    case TIMESCOPE: {
      const Card c = card(dl, x0, y0, x1, y1, u, "VALUE VS TIME", a);
      const bool trig = m.time_input >= 4;
      const char* note = trig ? "200 ms. White: what the game gets, grey: before the mode, blue: the click. Line at 43."
                              : "200 ms. White: filtered, grey: unfiltered. Lines at 23 and 64. Grid: one frame.";
      const float results_y = foot_top(c, note) - 84 * u, plot_y1 = results_y - 14 * u;
      if (m.have_shown && m.shown.mode == time_mode(m)) {
        plot_capture(dl, m.shown, trig, c.x0 + 24 * u, c.y0 + 56 * u, c.x1 - 24 * u, plot_y1, u, a);
        draw_results(dl, m.shown, c.x0 + 24 * u, results_y, c.x1 - c.x0 - 48 * u, u, a);
      } else {
        plot_area(dl, c.x0 + 24 * u, c.y0 + 56 * u, c.x1 - 24 * u, plot_y1, u, a);
        const char* hint = m.time_input >= 4 ? "Press the trigger." : m.time_measure == 0 ? "Flick from the rim and let go." : m.time_measure == 1 ? "Dash from neutral." : "Hold a dash one way, then flick the other way.";
        const float w = c.x1 - c.x0 - 80 * u, hpx = 22 * u;
        para_centred(dl, (c.x0 + c.x1) * 0.5f, (c.y0 + 56 * u + plot_y1) * 0.5f - pm::wrap_text(hint, hpx, w).size() * hpx * 0.575f, w, hpx, with_alpha(kCream, a), hint);
      }
      foot(dl, c, a, note);
      return;
    }
    case PRESSTIME: {
      const Card c = card(dl, x0, y0, x1, y1, u, "BUTTON TIMING", a);
      static const char* rows[13] = {"A", "B", "X", "Y", "Z", "L", "R", "Ax", "Ay", "Cx", "Cy", "Ra", "La"};
      static const int bits[13] = {0, 1, 2, 3, 5, 6, 7, -1, -2, -3, -4, -5, -6};
      const bool show = m.have_shown && m.shown.mode == Mode::Press;
      const std::string note = show ? "200 ms from the first press. Grid: one frame. Numbers: when each first went down." : "Start records; the recording begins with your first input.";
      const float lx = c.x0 + 60 * u, rx = c.x1 - 90 * u, top = c.y0 + 56 * u, rh = (foot_top(c, note) - 10 * u - top) / 13.0f, step = (rx - lx) / 200.0f;
      for (int i = 0; i <= 200; i += 17) dl->AddLine(ImVec2(lx + i * step, top), ImVec2(lx + i * step, top + 13 * rh), with_alpha(IM_COL32(50, 42, 24, 255), a), 1.0f * u);
      for (int r = 0; r < 13; r++) {
        const float y = top + r * rh;
        text(dl, c.x0 + 22 * u, y + rh * 0.5f - 9 * u, 17 * u, with_alpha(kCream, a), rows[r]);
        if (!show) continue;
        int first = -1;
        for (int i = 0; i < 200; i++) {
          const bool on = bits[r] >= 0 ? (m.shown.buttons[i] >> bits[r]) & 1 : (m.shown.analog_marks[i] >> (-bits[r] - 1)) & 1;
          if (!on) continue;
          if (first < 0) first = i;
          dl->AddRectFilled(ImVec2(lx + i * step, y + rh * 0.2f), ImVec2(lx + (i + 1) * step + 0.5f, y + rh * 0.8f), with_alpha(kYellow, a));
        }
        if (first >= 0) {
          char b[16];
          std::snprintf(b, sizeof b, "%.1ff", first / 16.6667f);
          text(dl, rx + 16 * u, y + rh * 0.5f - 9 * u, 17 * u, with_alpha(kCream, a), b);
        }
      }
      foot(dl, c, a, note);
      return;
    }
    case REACTION: {
      const Card c = card(dl, x0, y0, x1, y1, u, "REACTION TIME", a);
      const float cx = (c.x0 + c.x1) * 0.5f, cy = (c.y0 + c.y1) * 0.5f - 20 * u, sz = 90 * u;
      if (m.reaction_waiting) text(dl, cx - tw(40 * u, "Wait...") * 0.5f, cy - 20 * u, 40 * u, with_alpha(kCream, a), "Wait...");
      else if (m.cap.mode == Mode::Reaction && !m.cap.done) dl->AddRectFilled(ImVec2(cx - sz, cy - sz), ImVec2(cx + sz, cy + sz), with_alpha(IM_COL32(255, 255, 255, 255), a), 6 * u);
      else if (m.have_shown && m.shown.mode == Mode::Reaction) {
        char b[32];
        std::snprintf(b, sizeof b, "%u ms", m.shown.delay_ms);
        text(dl, cx - tw(64 * u, b) * 0.5f, cy - 50 * u, 64 * u, with_alpha(kYellow, a), b);
        std::snprintf(b, sizeof b, "%.1f frames", m.shown.delay_ms / 16.667f);
        text(dl, cx - tw(26 * u, b) * 0.5f, cy + 30 * u, 26 * u, with_alpha(kCream, a), b);
      } else para_centred(dl, cx, cy - 24 * u, c.x1 - c.x0 - 60 * u, 24 * u, with_alpha(kCream, a), "Start begins the test.");
      foot(dl, c, a, "Counted from the frame the square is drawn; your display's own delay is included.");
      return;
    }
    case CONTSCOPE: {
      const Card c = card(dl, x0, y0, x1, y1, u, "OSCILLOSCOPE", a);
      if (m.frozen) v = m.frozen_samples; else latest(port, v, m.cont_window);
      const float px0 = c.x0 + 24 * u, py0 = c.y0 + 56 * u, px1 = c.x1 - 24 * u, py1 = c.y1 - 70 * u;
      plot_area(dl, px0, py0, px1, py1, u, a);
      const float mid = (py0 + py1) * 0.5f, k = (py1 - py0) * 0.5f / 128.0f;
      for (float lvl : {80.0f, -80.0f, 23.0f, -23.0f}) dl->AddLine(ImVec2(px0, mid - lvl * k), ImVec2(px1, mid - lvl * k), with_alpha(IM_COL32(120, 90, 30, 200), a), 1.2f * u);
      if (v.size() >= 2) {
        const auto val = [&](const host::GCScopeSample& q, bool raw) -> float {
          switch (m.cont_input) {
            case 0: return raw ? q.raw_ax : q.ax; case 1: return raw ? q.raw_ay : q.ay;
            case 2: return raw ? q.raw_cx : q.cx; case 3: return raw ? q.raw_cy : q.cy;
            case 4: return ((raw ? q.raw_l : q.l) - 128) / 1.0f; default: return ((raw ? q.raw_r : q.r) - 128) / 1.0f;
          }
        };
        const float step = (px1 - px0) / (float)(v.size() - 1);
        for (int pass = 0; pass < 2; pass++)
          for (size_t i = 1; i < v.size(); i++)
            dl->AddLine(ImVec2(px0 + (i - 1) * step, mid - val(v[i - 1], pass == 0) * k), ImVec2(px0 + i * step, mid - val(v[i], pass == 0) * k),
                        with_alpha(pass == 0 ? IM_COL32(130, 130, 130, 220) : kYellow, a), (pass ? 2.4f : 1.8f) * u);
      }
      foot(dl, c, a, "Gold: what the game gets. Grey: before the settings. Lines at 23 and 80.");
      return;
    }
    case GATE: {
      const Card c = card(dl, x0, y0, x1, y1, u, "GATE", a);
      latest(port, v, 64);
      for (const auto& q : v) {
        const float gx = m.gate_stick ? q.cx : q.ax, gy = m.gate_stick ? q.cy : q.ay;
        const float rr = std::sqrt(gx * gx + gy * gy);
        if (rr < 30) continue;
        const int bin = (int)std::floor((std::atan2(gy, gx) + 3.14159265f) / (2 * 3.14159265f) * 64) % 64;
        m.gate[bin] = std::max(m.gate[bin], rr);
      }
      const float r = std::min(W, y1 - y0 - 80 * u) * 0.42f, cx = (c.x0 + c.x1) * 0.5f, cy = (c.y0 + c.y1) * 0.5f + 10 * u;
      stick_picture(dl, cx, cy, r, u, a, 0, 0, m.gate_stick ? s.cx : s.ax, m.gate_stick ? s.cy : s.ay, nullptr, nullptr, false);
      ImVec2 pts[64]; int n = 0;
      for (int i = 0; i < 64; i++) {
        if (m.gate[i] <= 0) continue;
        const float an = (i + 0.5f) / 64 * 2 * 3.14159265f - 3.14159265f;
        pts[n++] = ImVec2(cx + std::cos(an) * m.gate[i] * r / 110, cy - std::sin(an) * m.gate[i] * r / 110);
      }
      if (n > 2) dl->AddPolyline(pts, n, with_alpha(IM_COL32(255, 255, 255, 255), a), ImDrawFlags_Closed, 3.0f * u);
      return;
    }
    case EXPORT: {
      const Card c = card(dl, x0, y0, x1, y1, u, "EXPORT", a);
      const float y = c.y0 + 70 * u + para(dl, c.x0 + 26 * u, c.y0 + 70 * u, c.x1 - c.x0 - 52 * u, 21 * u, with_alpha(kCream, a),
                                           "Every report the adapter sent for this socket, before and after the settings, as a spreadsheet.");
      para(dl, c.x0 + 26 * u, y + 18 * u, c.x1 - c.x0 - 52 * u, 16 * u, with_alpha(IM_COL32(200, 185, 150, 255), a),
           "Columns: time in microseconds, the sticks, triggers and buttons as read, then as the game gets them.");
      return;
    }
    case LTRIGGER: case RTRIGGER: case TRIGGER: {
      const Card c = card(dl, x0, y0, x1, y1, u, "TRIGGERS", a);
      const char* note = "Grey: the trigger. Gold: what the game gets. Line at 43: lightshield.";
      for (int side = 0; side < 2; side++) {
        const float gcx = c.x0 + W * (side ? 0.73f : 0.27f), bx = gcx - 48 * u;   // two 36 wide bars, 60 apart
        const float top = c.y0 + 60 * u, h = foot_top(c, note) - 70 * u - top;
        const int in_v = side ? s.raw_r : s.raw_l, out_v = side ? s.r : s.l;
        const bool click = s.buttons & (side ? PAD_R : PAD_L);
        for (int k2 = 0; k2 < 2; k2++) {
          const float x = bx + k2 * 60 * u, val = (float)(k2 ? out_v : in_v);
          dl->AddRectFilled(ImVec2(x, top), ImVec2(x + 36 * u, top + h), with_alpha(IM_COL32(20, 16, 8, 255), a), 6 * u);
          dl->AddRectFilled(ImVec2(x, top + h - val / 255.0f * h), ImVec2(x + 36 * u, top + h), with_alpha(k2 ? kYellow : IM_COL32(130, 130, 130, 255), a), 6 * u);
          dl->AddRect(ImVec2(x, top), ImVec2(x + 36 * u, top + h), with_alpha(kGold, a), 6 * u, 0, 2 * u);
          dl->AddLine(ImVec2(x - 4 * u, top + h - 43 / 255.0f * h), ImVec2(x + 40 * u, top + h - 43 / 255.0f * h), with_alpha(IM_COL32(200, 120, 40, 255), a), 2 * u);
        }
        char b[32];
        std::snprintf(b, sizeof b, "%s  %d > %d", side ? "R" : "L", in_v, out_v);
        text(dl, gcx - tw(19 * u, b) * 0.5f, top + h + 12 * u, 19 * u, with_alpha(kCream, a), b);
        if (click) text(dl, gcx - tw(16 * u, "Click") * 0.5f, top + h + 38 * u, 16 * u, with_alpha(kYellow, a), "Click");
      }
      foot(dl, c, a, note);
      return;
    }
    default: break;
  }
  // Everything else: the live stick.
  const Card c = card(dl, x0, y0, x1, y1, u, m.screen == PHOBCHECK ? "HOLD A NOTCH" : "LIVE STICK", a);
  char sub[64];
  std::snprintf(sub, sizeof sub, "Socket %d   Adapter %d Hz", port + 1, (int)std::lround(host::gcadapter_poll_rate_hz()));
  text(dl, c.x0 + 20 * u, c.y0 + 44 * u, 16 * u, with_alpha(IM_COL32(180, 165, 130, 255), a), have ? sub : "No controller in this socket");
  const bool c_stick = m.screen == CSNAPBACK || m.screen == CWAVE;
  const float r = std::min(W * 0.36f, (y1 - y0 - 240 * u) * 0.5f);
  stick_picture(dl, (c.x0 + c.x1) * 0.5f, c.y0 + 78 * u + r, r, u, a, c_stick ? s.raw_cx : s.raw_ax, c_stick ? s.raw_cy : s.raw_ay, c_stick ? s.cx : s.ax, c_stick ? s.cy : s.ay);
  coord_lines(dl, c.x0 + 26 * u, c.y0 + 96 * u + 2 * r, u, a, c_stick ? "C-Stick" : "Stick", c_stick ? s.cx : s.ax, c_stick ? s.cy : s.ay, !c_stick);
  if (m.s.phob_hardware || !m.s.enabled)
    foot(dl, c, a, m.s.phob_hardware ? "Your PhobGCC's own output." : "PhobGCC Settings are off: the controller as it is.");
}

// ---- input ----
struct Input { bool up = false, down = false, left = false, right = false, a = false, b = false, start = false; };
uint16_t g_prev = 0xFFFF;
int g_held_dir = -1;
double g_held_since = 0, g_held_last = 0;
// `stick_nav` off: the D-pad only, for the screens that measure the stick.
Input read_input(const PhobNativeIn& f, bool stick_nav) {
  Input in;
  uint16_t b = f.armed ? f.buttons : 0xFFFF;
  if (stick_nav) {
    if (f.stick_y > 52) b |= PAD_UP;
    if (f.stick_y < -52) b |= PAD_DOWN;
    if (f.stick_x < -52) b |= PAD_LEFT;
    if (f.stick_x > 52) b |= PAD_RIGHT;
  }
  if (!f.armed) { g_prev = 0xFFFF; return in; }
  const uint16_t pressed = (uint16_t)(b & ~g_prev);
  g_prev = b;
  in.a = pressed & PAD_A; in.b = pressed & PAD_B; in.start = pressed & PAD_START;
  int dir = -1;
  if (b & PAD_UP) dir = 0; else if (b & PAD_DOWN) dir = 1; else if (b & PAD_LEFT) dir = 2; else if (b & PAD_RIGHT) dir = 3;
  bool fire = false;
  if (dir != g_held_dir) { g_held_dir = dir; g_held_since = g_held_last = f.time; fire = dir >= 0; }
  else if (dir >= 0 && f.time - g_held_since > 0.32 && f.time - g_held_last > 0.065) { g_held_last = f.time; fire = true; }
  if (fire) { in.up = dir == 0; in.down = dir == 1; in.left = dir == 2; in.right = dir == 3; }
  if (!ImGui::GetIO().WantTextInput) {
    in.up |= ImGui::IsKeyPressed(ImGuiKey_UpArrow, true);
    in.down |= ImGui::IsKeyPressed(ImGuiKey_DownArrow, true);
    in.left |= ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true);
    in.right |= ImGui::IsKeyPressed(ImGuiKey_RightArrow, true);
    in.a |= ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    in.b |= ImGui::IsKeyPressed(ImGuiKey_Backspace, false);
  }
  return in;
}

float ease(float t) { t = std::clamp(t, 0.0f, 1.0f); return 1 - (1 - t) * (1 - t) * (1 - t); }

bool records_with_start(Screen s) { return s == XYSCOPE || s == TIMESCOPE || s == PRESSTIME || s == REACTION; }

void load_port(int port) {
  MenuState& m = g_menu[port];
  if (m.loaded) return;
  m.s = host::gcadapter_phob_get(port);
  m.loaded = true;
}

}  // namespace

bool phob_native_open() { return g_open; }
void phob_native_request(int port) {
  g_open = true;
  g_port = std::clamp(port, 0, 3);
  g_prev = 0xFFFF;
  g_socket_armed = false;
  g_changed_at = ImGui::GetTime();
}
void phob_native_close() {
  g_open = false;
  for (MenuState& m : g_menu) { m.calibrating = false; m.remap_step = -1; m.reaction_waiting = false; m.cap.mode = Mode::None; }
}

PhobNativeOut phob_native_frame(const PhobNativeIn& in_frame) {
  PhobNativeOut out;
  g_in = in_frame;
  host::gcadapter_test_script_start();   // test runs only: the scripted player starts with the menu
  load_port(g_port);
  const int port = g_port;
  MenuState& m = g_menu[port];
  // Test runs: MELEE_TEST_PHOB=<screen> opens that screen; MELEE_TEST_PHOB_ACTION presses something.
  static bool test_done = false;
  if (!test_done) {
    test_done = true;
    if (const char* t = std::getenv("MELEE_TEST_PHOB")) {
      const int n = std::atoi(t);
      if (n >= 0 && n < SCREEN_COUNT) m.screen = (Screen)n;
      m.stickmap = 1;
      m.cap.stick_thresh = m.screen == PRESSTIME || m.screen == REACTION ? 23 : 10;
      m.cap.trigger_thresh = m.screen == REACTION ? 49 : 255;
      if (const char* ac = std::getenv("MELEE_TEST_PHOB_ACTION")) {
        const std::string act = ac;
        const auto arg = [&](const char* key) { return act.rfind(key, 0) == 0 ? std::atoi(act.c_str() + std::strlen(key)) : -1; };
        const Stick which = m.screen == CSTICKCAL || m.screen == CNOTCHFIX ? CSTICK : ASTICK;
        StickCal& saved = which == ASTICK ? m.s.a_cal : m.s.c_cal;
        if (act == "begin") { cal_begin(m.cal, which, saved, which == ASTICK ? m.s.a_scaler : m.s.c_scaler); m.calibrating = true; m.last_buttons = 0xFFFF; }
        if (act == "notchfix") { m.calibrating = cal_begin_notch_fix(m.cal, which, saved, which == ASTICK ? m.s.a_scaler : m.s.c_scaler); m.last_buttons = 0xFFFF; }
        if (arg("remap:") >= 0) { m.remap_step = arg("remap:"); m.last_buttons = 0xFFFF; }
        if (arg("input:") >= 0) m.time_input = arg("input:");
        if (arg("measure:") >= 0) m.time_measure = arg("measure:");
        if (arg("map:") >= 0) m.stickmap = arg("map:");
        if (arg("cursor:") >= 0) m.cursor[m.screen] = arg("cursor:");
      }
      g_changed_at = in_frame.time - 1.0;
    }
  }
  pm::font_ensure();
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  const float gx0 = in_frame.x0, gy0 = in_frame.y0, gx1 = in_frame.x1, gy1 = in_frame.y1;
  const float u = (gy1 - gy0) / 720.0f, W = gx1 - gx0;
  const float a = ease((float)(in_frame.time - g_changed_at) / 0.12f) * 0.25f + 0.75f;
  const Settings before = m.s;
  bool changed = false;

  // The game behind, darkened more on the menu's side.
  {
    const int v0 = dl->VtxBuffer.Size;
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y), IM_COL32(0, 0, 0, 246));
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, v0, dl->VtxBuffer.Size, ImVec2(gx0, 0), ImVec2(gx1, 0), IM_COL32(0, 0, 0, 246), IM_COL32(0, 0, 0, 228));
  }

  // Calibration takes the whole screen and the controller being calibrated. Its screens have no
  // rows of their own; outside a calibration they show the calibration page.
  if (!m.calibrating && (m.screen == ASTICKCAL || m.screen == ANOTCHFIX || m.screen == CSTICKCAL || m.screen == CNOTCHFIX)) m.screen = CALIBRATE;
  if (m.calibrating) {
    pm::gold::title(dl, gx0 + 48 * u, gy0 + 40 * u, u, "PHOBGCC", a);
    text(dl, gx0 + 52 * u, gy0 + 76 * u, 17 * u, with_alpha(kGoldText, a), node(m.cal.which == ASTICK ? (m.cal.step >= kCalPoints ? ANOTCHFIX : ASTICKCAL) : (m.cal.step >= kCalPoints ? CNOTCHFIX : CSTICKCAL)).name);
    changed |= run_calibration(dl, port, m, gx0, gy0, gx1, gy1, u, a);
  } else {
    // Rows, then the controller.
    std::vector<Row> rows = rows_for(port, m);
    int& cur = m.cursor[m.screen];
    cur = rows.empty() ? 0 : std::clamp(cur, 0, (int)rows.size() - 1);
    // Remapping and the reaction test read the controller itself; the menu takes no input meanwhile.
    host::phob::HardwareInput hw;
    const bool have_hw = host::gcadapter_phob_raw(port, hw);
    const uint16_t hb = have_hw ? hw.buttons : 0;
    const uint16_t hpressed = m.last_buttons == 0xFFFF ? 0 : (uint16_t)(hb & ~m.last_buttons);
    const bool listening = m.remap_step >= 0 || m.reaction_waiting || (m.screen == REACTION && m.cap.mode == Mode::Reaction && !m.cap.done);
    if (listening) m.last_buttons = hb;
    // The controller in the socket being set up drives the menu too, whichever port the game gives
    // it (or none), once it has been let go since the menu opened.
    PhobNativeIn nav = in_frame;
    if (have_hw) {
      const uint16_t nb = hb & (PAD_A | PAD_B | PAD_X | PAD_Y | PAD_Z | PAD_START | PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT);
      const int sx = (int)hw.ax - hw.ax0, sy = (int)hw.ay - hw.ay0;
      if (!g_socket_armed) g_socket_armed = nb == 0 && std::abs(sx) < 30 && std::abs(sy) < 30;
      if (g_socket_armed) {
        nav.armed = true;
        nav.buttons |= nb;
        if (std::abs(nav.stick_x) < 30 && std::abs(nav.stick_y) < 30) { nav.stick_x = (int8_t)std::clamp(sx, -127, 127); nav.stick_y = (int8_t)std::clamp(sy, -127, 127); }
      }
    }
    if (std::getenv("MELEE_TEST_PHOB")) {   // test runs: what the menu's controller does
      static uint32_t last = 0xFFFFFFFF;
      const uint32_t sig = nav.buttons | (nav.armed ? 0x10000u : 0);
      if (sig != last) { last = sig; host::log("phob test: menu pad %04X armed %d screen %d", nav.buttons, (int)nav.armed, (int)m.screen); }
    }
    const bool measures_stick = m.screen == INPUTVIEW || m.screen == XYSCOPE || m.screen == TIMESCOPE || m.screen == PRESSTIME ||
                                m.screen == REACTION || m.screen == CONTSCOPE || m.screen == GATE || m.screen == PHOBCHECK;
    Input in = listening ? Input{} : read_input(nav, !measures_stick);
    if (listening) g_prev = 0xFFFF;
    if (m.remap_step >= 0) {
      if (hpressed & PAD_START) m.remap_step = -1;
      for (int i = 0; i < REMAP_COUNT && m.remap_step >= 0; i++)
        if (hpressed & kRemapBits[i]) { m.s.remap[m.remap_step] = (uint8_t)(1 << i); m.remap_step = -1; }
      if (m.remap_step < 0) g_prev = 0xFFFF;   // the press that answered must not also move the menu
    }
    if (!rows.empty()) {
      const int count = (int)rows.size();
      if (in.up) cur = (cur + count - 1) % count;
      if (in.down) cur = (cur + 1) % count;
      Row& r = rows[(size_t)cur];
      if ((in.left || in.right) && r.kind == Kind::Value && r.enabled && r.v) {
        const int dir = in.right ? 1 : -1;
        const int before_v = *r.v;
        *r.v = r.hi - r.lo <= 1 ? (*r.v == r.lo ? r.hi : r.lo) : std::clamp(*r.v + dir * r.step, r.lo, r.hi);
        if (*r.v != before_v && r.act) r.act();
      }
      if (in.a && r.enabled) {
        if (r.kind == Kind::Page) { m.screen = r.target; g_changed_at = in_frame.time; m.reset_pending = -1; m.cap.mode = Mode::None; m.have_shown = false; m.reaction_waiting = false;
                                    m.cap.stick_thresh = r.target == PRESSTIME || r.target == REACTION ? 23 : 10; m.cap.trigger_thresh = r.target == REACTION ? 49 : 255; }
        else if (r.kind == Kind::Action && r.act) r.act();
        else if (r.kind == Kind::Value && r.v && r.hi - r.lo <= 1) { *r.v = *r.v == r.lo ? r.hi : r.lo; if (r.act) r.act(); }
      }
    }
    if (in.start) {
      if (records_with_start(m.screen)) {
        if (m.screen == XYSCOPE) { m.cap.stick_thresh = 10; begin_capture(port, m, Mode::StickMap); m.view_index = 0; }
        else if (m.screen == TIMESCOPE) begin_capture(port, m, time_mode(m));
        else if (m.screen == PRESSTIME) begin_capture(port, m, Mode::Press);
        else if (m.screen == REACTION) { for (Row& r : rows) if (r.label == "Start Test" && r.act) r.act(); }
      } else {
        out.close_all = true;
      }
    }
    if (in.b) {
      if (m.screen == MAIN) out.back_to_panel = true;
      else { m.screen = node(m.screen).parent; g_changed_at = in_frame.time; m.reset_pending = -1; m.cap.mode = Mode::None; m.have_shown = false; m.reaction_waiting = false; }
    }

    // PhobScope: keep the recordings going.
    if (m.screen == TIMESCOPE) {
      const Mode mode = time_mode(m);
      m.cap.c_stick = m.time_input >= 4 ? m.time_input == 5 : m.time_input >= 2;
      m.cap.y_axis = m.time_input < 4 && (m.time_input == 1 || m.time_input == 3);
      if (m.cap.mode != mode && (m.cap.mode != Mode::None || (m.auto_repeat && std::chrono::steady_clock::now() >= m.repeat_at)))
        begin_capture(port, m, mode);
      if (feed_capture(port, m)) {
        m.shown = m.cap; m.have_shown = true;
        if (std::getenv("MELEE_TEST_PHOB")) host::log("phob test: result %d %.1f %.1f %.1f", (int)mode, m.cap.percents[0], m.cap.percents[1], m.cap.percents[2]);
        if (mode == Mode::Snapback) m.last_snapback = std::fmax(0.0f, 100 - m.cap.percents[0]);
        if (mode == Mode::Dashback) { m.last_db_van = m.cap.percents[0]; m.last_db_ucf = m.cap.percents[1]; }
        if (mode == Mode::Pivot) for (int i = 0; i < 3; i++) m.last_pivot[i] = m.cap.percents[i];
        if (mode == Mode::Trigger) for (int i = 0; i < 3; i++) m.last_trig[i] = m.cap.percents[i];
        m.cap.mode = Mode::None;
        m.repeat_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);   // the firmware's repeatTimer
      }
    } else if (m.screen == XYSCOPE) {
      if (feed_capture(port, m)) { m.shown = m.cap; m.have_shown = true; m.cap.mode = Mode::None; }
    } else if (m.screen == PRESSTIME) {
      if (m.cap.mode == Mode::None && m.auto_repeat && std::chrono::steady_clock::now() >= m.repeat_at) begin_capture(port, m, Mode::Press);
      if (feed_capture(port, m)) { m.shown = m.cap; m.have_shown = true; m.cap.mode = Mode::None; m.repeat_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(250); }
    } else if (m.screen == REACTION) {
      if (m.reaction_waiting && std::chrono::steady_clock::now() >= m.reaction_at) { m.reaction_waiting = false; begin_capture(port, m, Mode::Reaction); }
      if (feed_capture(port, m) && m.cap.mode == Mode::Reaction) m.shown = m.cap, m.have_shown = true;
    }

    // Draw: title and path, hints, the bars, the card, the description.
    rows = rows_for(port, m);
    int& cur2 = m.cursor[m.screen];
    cur2 = rows.empty() ? 0 : std::clamp(cur2, 0, (int)rows.size() - 1);
    pm::gold::title(dl, gx0 + 48 * u, gy0 + 34 * u, u, "PHOBGCC", a);
    std::string path;
    for (Screen sc = m.screen; sc != MAIN; sc = node(sc).parent) path = std::string(node(sc).name) + (path.empty() ? "" : "  >  " + path);
    if (path.empty()) path = m.s.phob_hardware ? "PhobGCC Controller" : "Stock Controller";
    // A path wider than the screen is drawn smaller, and if still too wide loses its first steps.
    const float path_room = W - 82 * u;
    for (size_t cut; tw(13 * u, path) > path_room && (cut = path.find("  >  ", 4)) != std::string::npos;)
      path = "...  >  " + path.substr(cut + 5);
    const float path_px = std::max(13 * u, std::min(17 * u, 17 * u * path_room / std::max(1.0f, tw(17 * u, path))));
    text(dl, gx0 + 52 * u, gy0 + 70 * u + (17 * u - path_px) * 0.5f, path_px, with_alpha(kGoldText, a), path);
    const char* spec = records_with_start(m.screen) ? "D:Move|A:Pick|B:Back|S:Record"
                     : measures_stick ? "D:Move|A:Pick|B:Back|S:Close"
                     : m.screen == MAIN ? "A:Pick|B:Controls|S:Close" : "A:Pick|B:Back|S:Close";
    const float hw2 = pm::legend(dl, 0, 0, u, spec, a, false);
    pm::legend(dl, gx1 - hw2 - 40 * u, gy0 + 52 * u, u, spec, a);

    const float list_x0 = gx0 + 66 * u, list_x1 = gx0 + W * 0.52f, top = gy0 + 112 * u;
    const float h = 44 * u, pitch = 54 * u;
    const float desc_y = gy1 - 128 * u;
    const int visible = std::max(1, (int)((desc_y - 24 * u - top) / pitch));
    int& t = m.top[m.screen];
    if (cur2 < t) t = cur2;
    if (cur2 >= t + visible) t = cur2 - visible + 1;
    t = std::clamp(t, 0, std::max(0, (int)rows.size() - visible));
    const float since = (float)(in_frame.time - g_changed_at);
    for (int i = 0; i < std::min(visible, (int)rows.size()); ++i) {
      const int idx = t + i;
      const Row& r = rows[(size_t)idx];
      const bool lit = idx == cur2;
      const float slide = (1.0f - ease((since - 0.025f * i) / 0.18f)) * 70.0f * u;
      const float ba = a * ease((since - 0.025f * i) / 0.14f);
      const float y = top + i * pitch;
      const float l = list_x0 + (lit ? 14 * u : 0) - slide, rr = list_x1 + (lit ? 14 * u : 0) - slide;
      std::string val = value_text(r);
      const float maxw = (rr - l) - tw(h * 0.5f * 0.7f, r.label) - 90 * u;   // the label may shrink to 0.7 to make room
      // A summary that does not fit is left off rather than cut, so a cut value cannot misread.
      if (r.kind != Kind::Value && tw(h * 0.5f * 0.84f, val) > maxw) val.clear();
      while (!val.empty() && tw(h * 0.5f * 0.84f, val) > maxw) val.pop_back();
      pm::gold::bar(dl, l, y, rr, h, u, lit, r.label.c_str(), val, r.enabled, ba);
      if (lit) {
        pm::gold::ring(dl, rr - 6 * u, y + h * 0.5f, u, (float)in_frame.time, ba);
        if (r.kind == Kind::Value && r.enabled && r.hi - r.lo > 1) {
          const float vpx = h * 0.5f * 0.84f, vw = tw(vpx, val), vx = rr - 45 * u - vw, cy = y + h * 0.5f;
          const ImU32 dark = with_alpha(IM_COL32(18, 16, 10, 255), ba);
          dl->AddTriangleFilled(ImVec2(vx - 9 * u, cy - 6 * u), ImVec2(vx - 9 * u, cy + 6 * u), ImVec2(vx - 17 * u, cy), dark);
          dl->AddTriangleFilled(ImVec2(vx + vw + 7 * u, cy - 6 * u), ImVec2(vx + vw + 7 * u, cy + 6 * u), ImVec2(vx + vw + 15 * u, cy), dark);
        }
      }
    }
    if ((int)rows.size() > visible) {
      const float x = list_x1 + 34 * u, y0 = top + 4 * u, y1 = top + (visible - 1) * pitch + h - 4 * u;
      dl->AddRectFilled(ImVec2(x, y0), ImVec2(x + 5 * u, y1), with_alpha(IM_COL32(255, 255, 255, 34), a), 3 * u);
      const float span = y1 - y0, th = span * visible / rows.size(), ty = y0 + (span - th) * t / (float)(rows.size() - visible);
      dl->AddRectFilled(ImVec2(x, ty), ImVec2(x + 5 * u, ty + th), with_alpha(kGold, a), 3 * u);
    }
    draw_card(dl, port, m, list_x1 + 56 * u, top, gx1 - 46 * u, desc_y - 24 * u, u, a);
    std::string about = rows.empty() ? node(m.screen).about : rows[(size_t)cur2].about;
    if (m.remap_step >= 0) about = std::string("Press the button that should press ") + kRemapNames[m.remap_step] + ". Start cancels.";
    if (!m.status.empty() && in_frame.time < m.status_until) about = m.status;
    pm::gold::description(dl, gx0 + 46 * u, desc_y, W - 92 * u, u, about, a);
  }

  if (std::memcmp(&before, &m.s, sizeof(Settings)) != 0) changed = true;
  if (changed) {
    clamp(m.s);
    if (!host::phob_store_save(port, m.s)) say(m, "Could not save the settings file.");
    out.changed = true;
  }
  return out;
}

// ---- the Controls tab's entry ----
bool draw_phob_menu(int port) {
  load_port(port);
  MenuState& m = g_menu[port];
  m.s = host::gcadapter_phob_get(port);
  const Settings& s = m.s;
  std::string state = s.phob_hardware ? "PhobGCC controller: PhobScope and Phob Check"
                    : !s.enabled ? "Off"
                    : std::string("On") + (s.a_cal.measured ? ", left stick calibrated" : "") + (s.c_cal.measured ? ", C-stick calibrated" : "");
  ImGui::TextWrapped("PhobGCC's calibration, notches, snapback filter, trigger modes, PhobScope and Phob Check for this socket.");
  ImGui::TextDisabled("%s", state.c_str());
  if (ImGui::Button("Open PhobGCC menu", ImVec2(260, 0))) phob_native_request(port);
  ImGui::TextDisabled("From PhobGCC by Phobos132, FrostSSBM, CarVac, NiceMitch and contributors.");
  return false;
}

}  // namespace gx
