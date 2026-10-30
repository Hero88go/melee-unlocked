// PhobVision's menus, rebuilt for the settings panel. The screens, their order, their names and
// their help text follow PhobGCC's rp2040 menu (menu.h, menuStrings.h), so a Phob owner finds every
// setting where they expect it. PhobGCC (https://github.com/PhobGCC/PhobGCC-SW, GPL-3.0) is by
// Phobos132, FrostSSBM, CarVac, NiceMitch and the PhobGCC contributors.
//
// The PhobScope screens add the views of GCC Test Suite (https://github.com/greenwave-1/GTS, GPL-3.0,
// by greenwave-1): a continuous oscilloscope, a gate visualizer and a CSV export. They are drawn
// here from the adapter's own report stream; no GTS code is used.
//
// Buttons: the menus are ordinary panel controls. Calibration and remapping listen to the
// controller being set up with PhobVision's buttons (A/L/R next, Z back, X/Y turn a notch, B puts
// it back); while they do, the panel takes no navigation from the controller (phob_menu_capturing),
// and Start cancels.
// SPDX-License-Identifier: GPL-3.0-only
#include "phob_menu.h"
#include "host.h"
#include "phob.h"
#include "phobscope.h"
#include "phob_stickmaps.h"
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <random>
#include <fstream>
#include <string>
#include <vector>

namespace gx {
namespace {
using namespace host::phob;

enum Screen {
  MAIN, STICK, CALIBRATE, ASTICKCAL, ANOTCHFIX, CSTICKCAL, CNOTCHFIX, FILTER, ASNAPBACK, AWAVE, ASMOOTH,
  CSNAPBACK, CWAVE, GEOM, CARDINALS, RADIUS, SETTINGS, SET_OVER, REMAP, TRIGGER, LTRIGGER, RTRIGGER,
  TOURNEY, RESET, SCOPE, INPUTVIEW, XYSCOPE, TIMESCOPE, PRESSTIME, REACTION, CONTSCOPE, GATE, EXPORT,
  PHOBCHECK, SETUPS, SCREEN_COUNT
};
struct Node { Screen parent; const char* name; std::vector<Screen> children; };
const Node& node(Screen s) {
  static const Node nodes[SCREEN_COUNT] = {
      {MAIN, "Main Menu", {STICK, SETTINGS, SCOPE, PHOBCHECK, SETUPS}},
      {MAIN, "Stick Configuration", {CALIBRATE, FILTER, GEOM}},
      {STICK, "Stick Calibration", {ASTICKCAL, ANOTCHFIX, CSTICKCAL, CNOTCHFIX}},
      {CALIBRATE, "Calibrate Left Stick", {}},
      {CALIBRATE, "Left Stick Notch Fix", {}},
      {CALIBRATE, "Calibrate C-Stick", {}},
      {CALIBRATE, "C-Stick Notch Fix", {}},
      {STICK, "Stick Filter Settings", {ASNAPBACK, AWAVE, ASMOOTH, CSNAPBACK, CWAVE}},
      {FILTER, "Left Stick Smart Snapback", {}},
      {FILTER, "Left Stick Waveshaping", {}},
      {FILTER, "Left Stick Smoothing", {}},
      {FILTER, "C-Stick Snapback", {}},
      {FILTER, "C-Stick Waveshaping", {}},
      {STICK, "Stick Geometry Settings", {CARDINALS, RADIUS}},
      {GEOM, "Cardinal Snapping", {}},
      {GEOM, "Stick Radius Scaling", {}},
      {MAIN, "Settings", {SET_OVER, REMAP, TRIGGER, TOURNEY, RESET}},
      {SETTINGS, "Settings Overview", {}},
      {SETTINGS, "Button Remapping", {}},
      {SETTINGS, "Trigger Configuration", {LTRIGGER, RTRIGGER}},
      {TRIGGER, "Left Trigger Configuration", {}},
      {TRIGGER, "Right Trigger Configuration", {}},
      {SETTINGS, "Tourney Button Lockouts", {}},
      {SETTINGS, "Reset Settings", {}},
      {MAIN, "PhobScope", {INPUTVIEW, XYSCOPE, TIMESCOPE, PRESSTIME, REACTION, CONTSCOPE, GATE, EXPORT}},
      {SCOPE, "Input Viewer", {}},
      {SCOPE, "Stickmap Plots", {}},
      {SCOPE, "Value vs Time Plots", {}},
      {SCOPE, "Button Timing Viewer", {}},
      {SCOPE, "Reaction Time Test", {}},
      {SCOPE, "Continuous Oscilloscope", {}},
      {SCOPE, "Gate Visualizer", {}},
      {SCOPE, "Export Data", {}},
      {MAIN, "Phob Check", {}},
      {MAIN, "Setups", {}},
  };
  return nodes[s];
}

constexpr uint16_t PAD_LEFT = 0x0001, PAD_RIGHT = 0x0002, PAD_DOWN = 0x0004, PAD_UP = 0x0008, PAD_Z = 0x0010,
                   PAD_R = 0x0020, PAD_L = 0x0040, PAD_A = 0x0100, PAD_B = 0x0200, PAD_X = 0x0400,
                   PAD_Y = 0x0800, PAD_START = 0x1000;

struct MenuState {
  Screen screen = MAIN;
  bool loaded = false;
  Settings s;
  CalSession cal;
  bool calibrating = false;
  uint16_t last_buttons = 0;
  uint16_t start_last = 0xFFFF;
  uint16_t last_logged = 0;   // test runs: the buttons last written to the log
  std::chrono::steady_clock::time_point last_frame;
  int remap_step = -1;   // Button Remapping: the output being assigned, -1 when idle
  int reset_pick = 0;
  bool reset_confirm = false;
  bool frozen = false;
  std::vector<host::GCScopeSample> frozen_samples;
  float gate[64] = {};
  // PhobScope recordings (phobscope.h): `cap` records, `shown` is the last finished one.
  host::phobscope::Capture cap, shown;
  bool have_shown = false;
  bool auto_repeat = true;
  // Auto-Repeat starts the next recording 15 menu frames (about 250 ms) after one finishes, as the
  // firmware's repeatTimer does; it used to restart at once and trigger on a stick still held out.
  std::chrono::steady_clock::time_point repeat_at{};
  host::GCScopeSample prev{};
  bool have_prev = false;
  uint32_t next_tick_us = 0;   // the recording's 1 ms clock
  int stickmap = 0;        // 0 = none, else Phob's stickmaps 1..6
  int view_index = 0;
  int time_input = 0;      // Value vs Time: Left X, Left Y, C X, C Y, L, R
  int time_measure = 0;    // Snapback, Dashback, Pivots, Trigger
  // The Reaction Time Test: waiting for the random moment, then counting.
  bool reaction_waiting = false;
  std::chrono::steady_clock::time_point reaction_at;
  // The last result of each Value vs Time measurement, for Phob Check.
  float last_snapback = -1, last_db_van = -1, last_db_ucf = -1, last_pivot[3] = {-1, -1, -1}, last_trig[3] = {-1, -1, -1};
  char setup_name[48] = "";
  bool setup_cal = false;
  std::string status;
};
MenuState g_menu[4];
bool g_capturing = false;
bool g_owns_start = false;   // a PhobScope screen that records on Start is showing

}  // namespace
// The Controls tab stops drawing the section when its header closes or the tab changes.
void phob_menu_not_drawn() {
  g_capturing = false;
  g_owns_start = false;
  for (MenuState& m : g_menu) { m.calibrating = false; m.remap_step = -1; m.reaction_waiting = false; m.cap.mode = host::phobscope::Mode::None; }
}
namespace {
void hint(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

bool slider(const char* label, int* v, int lo, int hi, int def) {
  bool changed = ImGui::SliderInt(label, v, lo, hi);
  ImGui::SameLine();
  ImGui::PushID(label);
  if (ImGui::SmallButton("Default")) { *v = def; changed = true; }
  ImGui::PopID();
  return changed;
}

// The stick picture: the gate octagon, Melee's deadzone and rim, the raw dot and the output dot.
void draw_stick(const char* id, float raw_x, float raw_y, float out_x, float out_y, float size,
                const float* target_x = nullptr, const float* target_y = nullptr) {
  ImGui::PushID(id);
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##stick", ImVec2(size, size));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 c(p0.x + size / 2, p0.y + size / 2);
  const float k = size / 2 / 110.0f;
  dl->AddRectFilled(p0, ImVec2(p0.x + size, p0.y + size), IM_COL32(12, 16, 24, 255), 6);
  ImVec2 oct[8];
  for (int i = 0; i < 8; i++) {
    float x, y;
    calc_stick_values(3.14159265f * i / 4, x, y);
    oct[i] = ImVec2(c.x + x * k, c.y - y * k);
  }
  dl->AddPolyline(oct, 8, IM_COL32(90, 100, 120, 255), ImDrawFlags_Closed, 1.5f);
  dl->AddCircle(c, 80 * k, IM_COL32(70, 120, 200, 160), 64, 1.0f);   // Melee's rim (1.0)
  dl->AddRect(ImVec2(c.x - 22 * k, c.y - 22 * k), ImVec2(c.x + 22 * k, c.y + 22 * k), IM_COL32(200, 120, 60, 140));   // deadzone
  if (target_x && target_y) dl->AddCircleFilled(ImVec2(c.x + *target_x * k, c.y - *target_y * k), 5, IM_COL32(80, 220, 120, 255));
  dl->AddCircleFilled(ImVec2(c.x + raw_x * k, c.y - raw_y * k), 4, IM_COL32(150, 150, 150, 255));
  dl->AddCircleFilled(ImVec2(c.x + out_x * k, c.y - out_y * k), 5, IM_COL32(255, 255, 255, 255));
  ImGui::PopID();
}

void coord_line(const char* label, int x, int y) {
  float mx, my;
  melee_coords(x, y, mx, my);
  ImGui::Text("%s %4d, %4d   Melee %.4f, %.4f", label, x, y, mx, my);
}

int latest(int port, std::vector<host::GCScopeSample>& out, int n) {
  out.resize((size_t)n);
  const int got = host::gcadapter_scope(port, out.data(), n);
  out.resize((size_t)got);
  return got;
}

// ---- calibration (Calibrate Left Stick, Notch Fix, and the C-stick ones) ----
const char* notch_name(int n) {
  static const char* names[16] = {"right", "notch 1", "up right", "notch 2", "up", "notch 3", "up left", "notch 4",
                                  "left", "notch 5", "down left", "notch 6", "down", "notch 7", "down right", "notch 8"};
  return n >= 0 && n < 16 ? names[n] : "";
}

bool draw_calibration(int port, MenuState& m, Stick which, bool notch_fix) {
  bool changed = false;
  StickCal& saved = which == ASTICK ? m.s.a_cal : m.s.c_cal;
  const int scaler = which == ASTICK ? m.s.a_scaler : m.s.c_scaler;
  if (!m.calibrating) {
    if (notch_fix) {
      hint("Keeps your last stick measurement and goes straight to adjusting the notches.");
      if (!saved.measured) { hint("Calibrate this stick once first."); return false; }
      if (ImGui::Button("Begin Notch Fix")) { m.calibrating = cal_begin_notch_fix(m.cal, which, saved, scaler); m.last_buttons = 0xFFFF; }
    } else {
      hint("PhobGCC's own calibration: the stick at rest, then each notch, 16 times around the gate.");
      hint("Then the Notch Adjust Phase lets you turn each diagonal and modder notch to the angle you want.");
      if (ImGui::Button("Begin Calibration")) { cal_begin(m.cal, which, saved, scaler); m.calibrating = true; m.last_buttons = 0xFFFF; }
      if (saved.measured) {
        ImGui::SameLine();
        if (ImGui::Button("Clear calibration")) { saved = StickCal{}; changed = true; }
      }
    }
    return changed;
  }

  host::phob::HardwareInput in;
  const bool have = host::gcadapter_phob_raw(port, in);
  if (!have) { ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "Plug a controller into socket %d.", port + 1); }
  const float rx = which == ASTICK ? in.ax / 255.0f : in.cx / 255.0f;
  const float ry = which == ASTICK ? in.ay / 255.0f : in.cy / 255.0f;
  // The reading kept for a step: the adapter's reports of the last 32 ms averaged, as the firmware
  // averages its readings at the press (whatever the display's frame rate).
  float ax = rx, ay = ry;
  {
    std::vector<host::GCScopeSample> recent;
    latest(port, recent, 64);
    float sx = 0, sy = 0; int n = 0;
    for (const host::GCScopeSample& r : recent) {
      if (!recent.empty() && (int32_t)(recent.back().time_us - r.time_us) > 32000) continue;
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
  if (std::getenv("MELEE_TEST_PHOB") && (pressed || b != m.last_logged)) { host::log("phob test: cal step %d buttons %04X raw %.3f,%.3f have %d", m.cal.step, b, rx, ry, (int)have); m.last_logged = b; }

  const bool advance = (pressed & (PAD_A | PAD_L | PAD_R)) || ImGui::Button("Next (A/L/R)");
  ImGui::SameLine();
  const bool back = (pressed & PAD_Z) || ImGui::Button("Back (Z)");
  ImGui::SameLine();
  if ((pressed & PAD_START) || ImGui::Button("Cancel (Start)")) { m.calibrating = false; return false; }

  if (m.cal.step < kCalPoints) {
    ImGui::Text("Measurement Phase   Step: %d / %d", m.cal.step + 1, kCalPoints);
    const CalPrompt p = cal_prompt(m.cal.step);
    float tx = 0, ty = 0;
    if (p.centre) {
      ImGui::TextUnformatted("Let go of the stick");
      ImGui::TextUnformatted("and press A/L/R.");
    } else {
      calc_stick_values(3.14159265f * p.notch / 8, tx, ty);
      ImGui::Text("Hold stick into notch: %s", notch_name(p.notch));
      if (p.optional) { ImGui::TextUnformatted("If no notch exists,"); ImGui::TextUnformatted("leave stick centered."); }
    }
    ImGui::TextUnformatted("Press Z to go back.");
    draw_stick("cal", (rx - 0.5f) * 200, (ry - 0.5f) * 200, (rx - 0.5f) * 200, (ry - 0.5f) * 200, 220, p.centre ? nullptr : &tx, p.centre ? nullptr : &ty);
    ImGui::Text("Raw Values: %.4f, %.4f", ax, ay);
  } else {
    ImGui::Text("Notch Adjust Phase   Step: %d / %d", m.cal.step + 1, kCalSteps);
    const int notch = cal_adjust_notch(m.cal.step);
    ImGui::Text("Adjusting: %s", notch_name(notch));
    ImGui::TextUnformatted("You can now preview the stick motion. Put the");
    ImGui::TextUnformatted("stick in the indicated notch and press X/Y to");
    ImGui::TextUnformatted("move its output CW/CCW. B puts it back.");
    ImGui::TextUnformatted("Press A/L/R to move on.");
    cal_adjust(m.cal, (b & PAD_X) != 0, (b & PAD_Y) != 0, (b & PAD_B) != 0, dt);
    // The stick through the calibration as it stands, the way the firmware previews it.
    const float lx = linearize(rx, m.cal.params.fit_x), ly = linearize(ry, m.cal.params.fit_y);
    float ox = 0, oy = 0;
    Settings preview; preview.a_scaler = preview.c_scaler = m.cal.scaler;
    notch_remap(lx, ly, ox, oy, m.cal.params, 1, preview, which);
    float tx, ty;
    calc_stick_values(m.cal.measured_angles[notch], tx, ty);
    draw_stick("adj", (rx - 0.5f) * 200, (ry - 0.5f) * 200, ox, oy, 220, &tx, &ty);
    coord_line("Stick Coords:", (int)std::lround(ox), (int)std::lround(oy));
  }
  if (back) cal_undo(m.cal);
  if (advance && have && cal_advance(m.cal, ax, ay, saved)) {
    m.calibrating = false;
    m.status = which == ASTICK ? "Left stick calibrated." : "C-stick calibrated.";
    changed = true;
  }
  return changed;
}

// ---- PhobScope ----
void plot_series(const char* id, const std::vector<host::GCScopeSample>& v, int which, float height) {
  ImGui::PushID(id);
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const float w = std::max(200.0f, ImGui::GetContentRegionAvail().x);
  ImGui::InvisibleButton("##plot", ImVec2(w, height));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + height), IM_COL32(12, 16, 24, 255), 4);
  const float mid = p0.y + height / 2, k = height / 2 / 128.0f;
  dl->AddLine(ImVec2(p0.x, mid), ImVec2(p0.x + w, mid), IM_COL32(60, 60, 70, 255));
  for (float lvl : {80.0f, -80.0f, 23.0f, -23.0f})
    dl->AddLine(ImVec2(p0.x, mid - lvl * k), ImVec2(p0.x + w, mid - lvl * k), IM_COL32(50, 70, 110, 160));
  if (v.size() >= 2) {
    auto val = [&](const host::GCScopeSample& s, bool raw) -> float {
      switch (which) {
        case 0: return raw ? s.raw_ax : s.ax;
        case 1: return raw ? s.raw_ay : s.ay;
        case 2: return raw ? s.raw_cx : s.cx;
        case 3: return raw ? s.raw_cy : s.cy;
        case 4: return (raw ? s.raw_l : s.l) / 2.0f;
        default: return (raw ? s.raw_r : s.r) / 2.0f;
      }
    };
    const float step = w / (float)(v.size() - 1);
    for (int pass = 0; pass < 2; pass++) {
      const ImU32 col = pass == 0 ? IM_COL32(140, 140, 140, 200) : IM_COL32(255, 200, 60, 255);
      for (size_t i = 1; i < v.size(); i++)
        dl->AddLine(ImVec2(p0.x + (i - 1) * step, mid - val(v[i - 1], pass == 0) * k),
                    ImVec2(p0.x + i * step, mid - val(v[i], pass == 0) * k), col, 1.2f);
    }
  }
  ImGui::PopID();
}

double sample_rate(const std::vector<host::GCScopeSample>& v) {
  if (v.size() < 2) return 0;
  const double span = (double)(uint32_t)(v.back().time_us - v.front().time_us);
  return span > 0 ? (v.size() - 1) * 1e6 / span : 0;
}


using host::phobscope::Mode;

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

// Feeds the recording every millisecond since the last frame: a report holds until the next one,
// so a 1000 Hz adapter gives one sample per report and a slower one repeats each for its duration.
// Returns true when the recording finished this frame.
bool feed_capture(int port, MenuState& m) {
  if (m.cap.mode == Mode::None || m.cap.done) return false;
  std::vector<host::GCScopeSample> v;
  latest(port, v, 4096);
  if (!m.have_prev) {
    if (v.empty()) return false;
    m.prev = v.back(); m.have_prev = true; m.next_tick_us = m.prev.time_us + 1000;
    return false;
  }
  // A fixed 1 ms clock: every tick takes the report in force at that moment. Reports arrive with
  // USB jitter (and sometimes in bursts); counting each gap as whole milliseconds on its own used
  // to stretch short events (9 ms of tilt recorded as 12).
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

// Phob's stickmap images, decoded once into runs of one grey.
struct Run { int16_t row, col, len; uint8_t grey; };
const std::vector<Run>& stickmap_runs(int map) {
  static std::vector<Run> cache[gx::phob_stickmaps::kMapCount];
  static bool made[gx::phob_stickmaps::kMapCount] = {};
  std::vector<Run>& runs = cache[map];
  if (made[map]) return runs;
  made[map] = true;
  const auto& mp = gx::phob_stickmaps::kMaps[map];
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

const char* kButtonNames[8] = {"A", "B", "X", "Y", "S", "Z", "L", "R"};

void draw_results(const host::phobscope::Capture& c) {
  switch (c.mode) {
    case Mode::Snapback:
      ImGui::Text("Positive: %.2f%%", c.percents[0]);
      ImGui::Text("Negative: %.2f%%", std::fmax(0.0f, 100 - c.percents[0]));
      hint("Negative is the chance the release reads as a turn the other way.");
      break;
    case Mode::Dashback:
      ImGui::Text("Vanilla  Dashback: %.0f%%", c.percents[0]);
      ImGui::Text("UCF      Dashback: %.0f%%", c.percents[1]);
      break;
    case Mode::Pivot:
      ImGui::Text("No Turn: %.0f%%", std::round(c.percents[0]));
      ImGui::Text("Empty Pivot: %.0f%%", std::round(c.percents[1]));
      ImGui::Text("Dashback: %.0f%%", std::round(c.percents[2]));
      break;
    case Mode::Trigger:
      ImGui::Text("D PS: %.0f%%", std::round(c.percents[0]));
      ImGui::Text("ADT PS: %.0f%%", std::round(c.percents[1]));
      ImGui::Text("No PS: %.0f%%", std::round(c.percents[2]));
      break;
    default: break;
  }
}

// 200 ms of one value, filtered (white) over unfiltered (grey), the way PhobScope draws it.
void plot_capture(const host::phobscope::Capture& c, bool trigger) {
  uint8_t a[200], u[200];
  host::phobscope::ordered(c, c.a1, a);
  host::phobscope::ordered(c, c.a1_unfilt, u);
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const float w = std::max(260.0f, ImGui::GetContentRegionAvail().x), h = 200;
  ImGui::InvisibleButton("##capplot", ImVec2(w, h));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), IM_COL32(12, 16, 24, 255), 4);
  const float step = w / 199.0f;
  auto y_of = [&](int v) { return trigger ? p0.y + h - v * h / 256.0f : p0.y + h / 2 - (v - 127) * h / 256.0f; };
  for (int lvl : trigger ? std::vector<int>{43} : std::vector<int>{127 + 23, 127 - 23, 127 + 64, 127 - 64, 127 + 80, 127 - 80})
    dl->AddLine(ImVec2(p0.x, y_of(lvl)), ImVec2(p0.x + w, y_of(lvl)), IM_COL32(50, 70, 110, 160));
  for (int i = 0; i < 200; i += 17) dl->AddLine(ImVec2(p0.x + i * step, p0.y), ImVec2(p0.x + i * step, p0.y + h), IM_COL32(40, 40, 50, 120));
  for (int i = 1; i < 200; i++) {
    dl->AddLine(ImVec2(p0.x + (i - 1) * step, y_of(u[i - 1])), ImVec2(p0.x + i * step, y_of(u[i])), IM_COL32(140, 140, 140, 200), 1.2f);
    dl->AddLine(ImVec2(p0.x + (i - 1) * step, y_of(a[i - 1])), ImVec2(p0.x + i * step, y_of(a[i])), IM_COL32(255, 255, 255, 255), 1.5f);
  }
  if (trigger) {
    uint8_t d[200];
    host::phobscope::ordered(c, c.buttons, d);
    for (int i = 0; i < 200; i++)
      if (d[i]) dl->AddRectFilled(ImVec2(p0.x + i * step, p0.y + h - 6), ImVec2(p0.x + (i + 1) * step, p0.y + h), IM_COL32(80, 160, 255, 255));
  }
  hint(trigger ? "200 ms. White: the trigger the game gets, grey: before the trigger mode, blue: digital press. Line at 43 (lightshield). Grid: one frame."
               : "200 ms. White: filtered, grey: unfiltered. Lines at 23 (deadzone), 64 (dash) and 80. Grid: one frame.");
}

void draw_scope(int port, MenuState& m, Screen screen) {
  // Start records, as in PhobScope (the panel leaves Start to these screens: phob_menu_owns_start).
  host::phob::HardwareInput live;
  const uint16_t now_buttons = host::gcadapter_phob_raw(port, live) ? live.buttons : 0;
  const bool start_pressed = (now_buttons & PAD_START) && !(m.start_last & PAD_START);
  m.start_last = now_buttons;
  std::vector<host::GCScopeSample> v;
  const double hz = host::gcadapter_poll_rate_hz();
  ImGui::Text("Adapter: %.0f Hz", hz);
  if (hz > 0 && hz < 900) hint("Below 1000 Hz: snapback filtering and the scopes see fewer samples. An overclocked or lossless adapter reads at 1000 Hz.");
  switch (screen) {
    case INPUTVIEW: {
      latest(port, v, 1);
      if (v.empty()) { hint("No controller on this socket."); return; }
      const host::GCScopeSample& s = v.back();
      ImGui::TextUnformatted("Hardware Inputs:         Controller Outputs:");
      draw_stick("vl", s.raw_ax, s.raw_ay, s.ax, s.ay, 180);
      ImGui::SameLine();
      draw_stick("vc", s.raw_cx, s.raw_cy, s.cx, s.cy, 180);
      coord_line("Left Coords: ", s.ax, s.ay);
      coord_line("Right Coords:", s.cx, s.cy);
      ImGui::Text("L %3d -> %3d    R %3d -> %3d", s.raw_l, s.l, s.raw_r, s.r);
      std::string held;
      const struct { uint16_t bit; const char* n; } names[] = {{PAD_A, "A"}, {PAD_B, "B"}, {PAD_X, "X"}, {PAD_Y, "Y"},
          {PAD_Z, "Z"}, {PAD_L, "L"}, {PAD_R, "R"}, {PAD_START, "Start"}, {PAD_UP, "Du"}, {PAD_DOWN, "Dd"},
          {PAD_LEFT, "Dl"}, {PAD_RIGHT, "Dr"}};
      for (const auto& n : names) if (s.buttons & n.bit) { held += n.n; held += " "; }
      ImGui::Text("Pressed Buttons: %s", held.c_str());
      break;
    }
    case XYSCOPE: {
      // Stickmap Plots: 100 ms of 2D motion, starting once the stick moves 10 from where it was.
      static const char* maps = "None\0Deadzones\0Wait Attacks\0Wait Movement\0Crouch\0Left Ledge\0Right Ledge\0";
      ImGui::Combo("Stickmap:", &m.stickmap, maps);
      int which = m.cap.c_stick ? 1 : 0;
      ImGui::RadioButton("Left Stick", &which, 0);
      ImGui::SameLine();
      ImGui::RadioButton("Right Stick", &which, 1);
      m.cap.c_stick = which == 1;
      if (ImGui::Button("Record (Start)") || start_pressed) { m.cap.stick_thresh = 10; begin_capture(port, m, Mode::StickMap); m.view_index = 0; }
      if (feed_capture(port, m)) { m.shown = m.cap; m.have_shown = true; m.cap.mode = Mode::None; }
      if (m.cap.mode == Mode::StickMap && !m.cap.done) ImGui::TextUnformatted("Waiting for Motion...");
      const ImVec2 p0 = ImGui::GetCursorScreenPos();
      const float size = 255 * 1.4f, k = 1.4f;
      ImGui::InvisibleButton("##map", ImVec2(size, size));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(p0, ImVec2(p0.x + size, p0.y + size), IM_COL32(0, 0, 0, 255));
      if (m.stickmap > 0)
        for (const Run& r : stickmap_runs(m.stickmap - 1))
          dl->AddRectFilled(ImVec2(p0.x + r.col * k, p0.y + r.row * k), ImVec2(p0.x + (r.col + r.len) * k, p0.y + (r.row + 1) * k),
                            IM_COL32(r.grey, r.grey, r.grey, 255));
      if (m.have_shown && m.shown.mode == Mode::StickMap) {
        uint8_t x[100], y[100], ux[100], uy[100], bt[100];
        host::phobscope::ordered(m.shown, m.shown.a1, x); host::phobscope::ordered(m.shown, m.shown.a2, y);
        host::phobscope::ordered(m.shown, m.shown.a1_unfilt, ux); host::phobscope::ordered(m.shown, m.shown.a2_unfilt, uy);
        host::phobscope::ordered(m.shown, m.shown.buttons, bt);
        auto at = [&](int vx, int vy) { return ImVec2(p0.x + (vx - 127 + 127) * k, p0.y + (128 - (vy - 127)) * k); };
        for (int i = 0; i < 100; i++) dl->AddCircleFilled(at(ux[i], uy[i]), 2.0f, IM_COL32(140, 140, 140, 255));
        for (int i = 0; i < 100; i++) dl->AddCircleFilled(at(x[i], y[i]), 2.5f, IM_COL32(255, 255, 255, 255));
        ImGui::SliderInt("Highlight Sample #:", &m.view_index, 0, 99);
        dl->AddCircle(at(x[m.view_index], y[m.view_index]), 7, IM_COL32(255, 80, 80, 255), 16, 2);
        float mx, my;
        melee_coords(x[m.view_index] - 127, y[m.view_index] - 127, mx, my);
        ImGui::Text("%d, %d   Melee %.4f, %.4f", x[m.view_index] - 127, y[m.view_index] - 127, mx, my);
        std::string held;
        for (int b = 0; b < 8; b++) if (bt[m.view_index] & (1 << b)) { held += kButtonNames[b]; held += " "; }
        ImGui::Text("Pressed Buttons: %s", held.c_str());
      }
      hint("White: filtered. Grey: unfiltered. Stickmaps by venus23gg, made with Altimor's Melee Stick Coordinate Visualizer.");
      break;
    }
    case TIMESCOPE: {
      static const char* inputs = "Left X\0Left Y\0C-Stick X\0C-Stick Y\0L Trigger\0R Trigger\0";
      static const char* measures = "Snapback\0Dashback\0Pivots\0";
      ImGui::Combo("Input:", &m.time_input, inputs);
      const bool trigger = m.time_input >= 4;
      if (!trigger) ImGui::Combo("Measurement:", &m.time_measure, measures);
      else ImGui::TextUnformatted("Measurement: Trigger");
      ImGui::Checkbox("Auto-Repeat", &m.auto_repeat);
      const Mode mode = trigger ? Mode::Trigger : m.time_measure == 0 ? Mode::Snapback : m.time_measure == 1 ? Mode::Dashback : Mode::Pivot;
      m.cap.c_stick = trigger ? m.time_input == 5 : m.time_input >= 2;
      m.cap.y_axis = !trigger && (m.time_input == 1 || m.time_input == 3);
      if (m.cap.mode != mode && (m.cap.mode != Mode::None || (m.auto_repeat && std::chrono::steady_clock::now() >= m.repeat_at)))
        begin_capture(port, m, mode);
      if (ImGui::Button("Record (Start)") || start_pressed) begin_capture(port, m, mode);
      if (feed_capture(port, m)) {
        m.shown = m.cap; m.have_shown = true;
        if (std::getenv("MELEE_TEST_PHOB")) host::log("phob test: result %d %.1f %.1f %.1f", (int)mode, m.cap.percents[0], m.cap.percents[1], m.cap.percents[2]);
        if (mode == Mode::Snapback) m.last_snapback = std::fmax(0.0f, 100 - m.cap.percents[0]);
        if (mode == Mode::Dashback) { m.last_db_van = m.cap.percents[0]; m.last_db_ucf = m.cap.percents[1]; }
        if (mode == Mode::Pivot) for (int i = 0; i < 3; i++) m.last_pivot[i] = m.cap.percents[i];
        if (mode == Mode::Trigger) for (int i = 0; i < 3; i++) m.last_trig[i] = m.cap.percents[i];
        m.cap.mode = Mode::None;
        m.repeat_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
      }
      ImGui::TextUnformatted(mode == Mode::Snapback ? "Flick from the rim and let go." : mode == Mode::Dashback ? "Dash from neutral."
                             : mode == Mode::Pivot ? "Hold a dash one way, then flick the other way." : "Press the trigger.");
      if (m.have_shown && m.shown.mode == mode) { plot_capture(m.shown, trigger); draw_results(m.shown); }
      else ImGui::TextUnformatted("Move Stick or Trigger");
      break;
    }
    case PRESSTIME: {
      ImGui::TextUnformatted("Press Start to initiate 200ms of recording,");
      ImGui::TextUnformatted("starting when you press ABXYLRZ or move a stick.");
      ImGui::SliderInt("Stick:", &m.cap.stick_thresh, 10, 100);
      ImGui::SliderInt("Trigger:", &m.cap.trigger_thresh, 10, 255);
      ImGui::Checkbox("Auto-Repeat:", &m.auto_repeat);
      if (ImGui::Button("Record (Start)") || start_pressed) begin_capture(port, m, Mode::Press);
      if (m.cap.mode == Mode::None && m.auto_repeat && std::chrono::steady_clock::now() >= m.repeat_at) begin_capture(port, m, Mode::Press);
      if (m.cap.mode == Mode::Press && !m.cap.done && !m.cap.begin) ImGui::TextUnformatted("Waiting for input...");
      if (feed_capture(port, m)) { m.shown = m.cap; m.have_shown = true; m.cap.mode = Mode::None; m.repeat_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(250); }
      if (m.have_shown && m.shown.mode == Mode::Press) {
        static const char* rows[14] = {"A", "B", "X", "Y", "S", "Z", "L", "R", "Ax", "Ay", "Cx", "Cy", "Ra", "La"};
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float w = std::max(260.0f, ImGui::GetContentRegionAvail().x), rh = 15, lx = 28;
        ImGui::InvisibleButton("##timing", ImVec2(w, rh * 14 + 4));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float nw = 52;   // the frame-number column, kept clear of the bars
        const float step = (w - lx - nw) / 200.0f;
        for (int i = 0; i <= 200; i += 17) dl->AddLine(ImVec2(p0.x + lx + i * step, p0.y), ImVec2(p0.x + lx + i * step, p0.y + rh * 14), IM_COL32(40, 40, 50, 160));
        for (int r = 0; r < 14; r++) {
          if (r == 4) continue;   // the firmware does not record Start here
          const float y = p0.y + r * rh;
          dl->AddText(ImVec2(p0.x, y), IM_COL32(200, 200, 200, 255), rows[r]);
          int first = -1;
          for (int i = 0; i < 200; i++) {
            const bool on = r < 8 ? (m.shown.buttons[i] >> r) & 1 : (m.shown.analog_marks[i] >> (r - 8)) & 1;
            if (on) { if (first < 0) first = i; dl->AddRectFilled(ImVec2(p0.x + lx + i * step, y + 2), ImVec2(p0.x + lx + (i + 1) * step, y + rh - 2), IM_COL32(255, 200, 60, 255)); }
          }
          if (first >= 0) {
            char buf[24];
            std::snprintf(buf, sizeof buf, "%.1ff", first / 16.6667f);
            dl->AddText(ImVec2(p0.x + w - nw + 6, y), IM_COL32(255, 255, 255, 255), buf);
          }
        }
        hint("200 ms from the first press. Grid: one frame. The number is when each input first went down, in frames.");
      }
      break;
    }
    case REACTION: {
      ImGui::TextUnformatted("Press start to begin the reaction test.");
      ImGui::TextUnformatted("You can press any button or move any stick.");
      ImGui::TextUnformatted("Set stick & trigger thresholds below.");
      ImGui::SliderInt("Stick:", &m.cap.stick_thresh, 10, 100);
      ImGui::SliderInt("Trigger:", &m.cap.trigger_thresh, 10, 200);
      host::phob::HardwareInput in;
      const bool have = host::gcadapter_phob_raw(port, in);
      const uint16_t pressed = m.last_buttons == 0xFFFF ? 0 : (uint16_t)((have ? in.buttons : 0) & ~m.last_buttons);
      m.last_buttons = have ? in.buttons : 0;
      if (!m.reaction_waiting && (m.cap.mode != Mode::Reaction || m.cap.done)) {
        if ((pressed & PAD_START) || ImGui::Button("Start")) {
          static std::mt19937 rng{std::random_device{}()};
          m.reaction_waiting = true;   // firmware: a random 3 to 8 seconds
          m.reaction_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000 + (int)(rng() % 5000));
          m.cap.mode = Mode::None;
        }
      }
      const ImVec2 p0 = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##square", ImVec2(120, 120));
      if (m.reaction_waiting) {
        ImGui::GetWindowDrawList()->AddText(ImVec2(p0.x, p0.y), IM_COL32(255, 255, 255, 255), "Wait...");
        if (std::chrono::steady_clock::now() >= m.reaction_at) { m.reaction_waiting = false; begin_capture(port, m, Mode::Reaction); }
      }
      if (m.cap.mode == Mode::Reaction && !m.cap.done)
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p0.x + 10, p0.y + 10), ImVec2(p0.x + 110, p0.y + 110), IM_COL32(255, 255, 255, 255));
      if (feed_capture(port, m) && m.cap.mode == Mode::Reaction) { m.shown = m.cap; m.have_shown = true; }
      if (m.have_shown && m.shown.mode == Mode::Reaction && !m.reaction_waiting && (m.cap.mode != Mode::Reaction || m.cap.done))
        ImGui::Text("ms: %u   Frames: %.1f", m.shown.delay_ms, m.shown.delay_ms / 16.667f);
      hint("Counted from the frame the square is drawn; your display's own delay is included, as on a console.");
      break;
    }
    case CONTSCOPE: {
      static int which = 0;
      static int window_ms = 200;
      ImGui::Combo("Input", &which, "Left X\0Left Y\0C-Stick X\0C-Stick Y\0L Trigger\0R Trigger\0");
      ImGui::SliderInt("Window (reports)", &window_ms, 50, 4000);
      if (ImGui::Button(m.frozen ? "Unfreeze" : "Freeze")) { m.frozen = !m.frozen; if (m.frozen) latest(port, m.frozen_samples, window_ms); }
      if (m.frozen) v = m.frozen_samples;
      else latest(port, v, window_ms);
      plot_series("ts", v, which, 220);
      ImGui::Text("%zu reports, %.0f Hz measured", v.size(), sample_rate(v));
      hint("Grey: unfiltered. Gold: what the game gets. Lines at +-80 (rim) and +-23 (deadzone).");
      break;
    }
    case GATE: {
      hint("Roll the stick slowly around the gate. The outline is the farthest it reached in each direction.");
      static int which = 0;
      ImGui::RadioButton("Left Stick", &which, 0); ImGui::SameLine(); ImGui::RadioButton("Right Stick", &which, 1);
      ImGui::SameLine();
      if (ImGui::Button("Clear")) std::fill(std::begin(m.gate), std::end(m.gate), 0.0f);
      latest(port, v, 64);
      for (const auto& s : v) {
        const float x = which ? s.cx : s.ax, y = which ? s.cy : s.ay;
        const float r = std::sqrt(x * x + y * y);
        if (r < 30) continue;
        int bin = (int)std::floor((std::atan2(y, x) + 3.14159265f) / (2 * 3.14159265f) * 64) % 64;
        m.gate[bin] = std::max(m.gate[bin], r);
      }
      const ImVec2 p0 = ImGui::GetCursorScreenPos();
      const float size = 260, k = size / 2 / 110.0f;
      draw_stick("gate", 0, 0, 0, 0, size);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 c(p0.x + size / 2, p0.y + size / 2);
      ImVec2 pts[64]; int n = 0;
      for (int i = 0; i < 64; i++) {
        if (m.gate[i] <= 0) continue;
        const float a = (i + 0.5f) / 64 * 2 * 3.14159265f - 3.14159265f;
        pts[n++] = ImVec2(c.x + std::cos(a) * m.gate[i] * k, c.y - std::sin(a) * m.gate[i] * k);
      }
      if (n > 2) dl->AddPolyline(pts, n, IM_COL32(255, 200, 60, 255), ImDrawFlags_Closed, 2.0f);
      break;
    }
    case EXPORT: {
      hint("Writes the last 4096 reports of this socket as CSV beside your settings (PhobGCC folder).");
      if (ImGui::Button("Export CSV")) {
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
        m.status = out ? "Saved " + name : "Could not write the file.";
      }
      break;
    }
    default: break;
  }
}

const char* trigger_mode_text(int mode) {
  switch (mode) {
    case 0: return "This is the default trigger behavior.";
    case 1: return "This disables analog triggers (lightshield).";
    case 2: return "This disables the hard press but preserves LRAS. In Melee you cannot airdodge or tech with this.";
    case 3: return "Trigger plug emulation: this caps the maximum analog value at the offset. In Melee, you get a large lightshield for much of the slider travel, then a hardshield.";
    case 4: return "This outputs an analog value (the offset) when hard pressed, with no digital press.";
    case 5: return "This outputs both an analog value and a digital output when hard pressed.";
    case 6: return "This increases the sensitivity of the analog by a multiplier that changes with the offset.";
    case 7: return "Trigger plug emulation without the digital press.";
    default: return "Trigger plug emulation, but has an analog multiplier of 2.5x, enabling lightshield with short travel triggers.";
  }
}

bool draw_trigger(Settings& s, bool left) {
  int& mode = left ? s.l_mode : s.r_mode;
  int& offset = left ? s.l_offset : s.r_offset;
  bool changed = false;
  int shown = mode + 1;
  if (ImGui::SliderInt("Mode:", &shown, kTriggerModeMin + 1, kTriggerModeMax + 1)) { mode = shown - 1; changed = true; }
  ImGui::TextWrapped("%s", trigger_mode_text(mode));
  if (mode >= 3) changed |= slider("Offset:", &offset, kTriggerOffsetMin, kTriggerOffsetMax, kTriggerOffsetDefault);
  if (mode == 6) ImGui::Text("Multiplier: %.2f", 0.0112f * offset + 0.4494f);
  const int other = left ? s.r_mode : s.l_mode;
  if (mode == 4 && other == 4) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s", left ? "Conflict with R Trigger Mode 5" : "Conflict with L Trigger Mode 5");
  return changed;
}

bool draw_screen(int port, MenuState& m) {
  Settings& s = m.s;
  bool changed = false;
  switch (m.screen) {
    case ASTICKCAL: changed = draw_calibration(port, m, ASTICK, false); break;
    case ANOTCHFIX: changed = draw_calibration(port, m, ASTICK, true); break;
    case CSTICKCAL: changed = draw_calibration(port, m, CSTICK, false); break;
    case CNOTCHFIX: changed = draw_calibration(port, m, CSTICK, true); break;
    case ASNAPBACK:
      hint("Set so that snapback < 23 for Melee.");
      hint("0 disables the filter. >0 shortens rise time.");
      hint("<0 doesn't shorten rise time. Greater magnitude makes the stick return to center slower.");
      changed |= slider("Left stick X:", &s.x_snapback, kSnapbackMin, kSnapbackMax, kSnapbackDefault);
      changed |= slider("Left stick Y:", &s.y_snapback, kSnapbackMin, kSnapbackMax, kSnapbackDefault);
      break;
    case AWAVE: case CWAVE:
      hint("This makes the stick stop during fast inputs.");
      hint("Greater magnitudes reduce the speed threshold.");
      hint("<0 lets you mimic a heavy pode potentiometer.");
      hint(m.screen == AWAVE ? "It affects pivots, DBOOC, dashback, and more." : "This can help C-Stick ledge drop consistency.");
      if (m.screen == AWAVE) {
        changed |= slider("Left stick X:", &s.ax_waveshaping, kWaveshapingMin, kWaveshapingMax, 0);
        changed |= slider("Left stick Y:", &s.ay_waveshaping, kWaveshapingMin, kWaveshapingMax, 0);
      } else {
        changed |= slider("Right stick X:", &s.cx_waveshaping, kWaveshapingMin, kWaveshapingMax, 0);
        changed |= slider("Right stick Y:", &s.cy_waveshaping, kWaveshapingMin, kWaveshapingMax, 0);
      }
      break;
    case ASMOOTH:
      hint("This averages the stick position with the past.");
      hint("It can be used to make one axis lag the other.");
      hint("Use X=5 to make down-and-in ledgedashes work.");
      changed |= slider("Left stick X:", &s.ax_smoothing, kSmoothingMin, kSmoothingMax, 0);
      changed |= slider("Left stick Y:", &s.ay_smoothing, kSmoothingMin, kSmoothingMax, 0);
      break;
    case CSNAPBACK:
      hint("This averages the stick position with the past.");
      hint("Make these 5 or 6 to reduce snapback.");
      hint("Make one axis higher to make it less responsive.");
      changed |= slider("Right stick X:", &s.cx_smoothing, kSmoothingMin, kSmoothingMax, kCSmoothingDefault);
      changed |= slider("Right stick Y:", &s.cy_smoothing, kSmoothingMin, kSmoothingMax, kCSmoothingDefault);
      break;
    case CARDINALS:
      hint("This sets how far the stick snaps to the cardinal axes. 0 disables snapping, and -2");
      hint("disables 1.0 cardinals even on UCF 0.84+. -1 gives 1.0 on 0.84 except for Zelda teleport.");
      changed |= slider("Left stick:", &s.a_cardinal, kCardinalMin, kCardinalMax, kCardinalDefault);
      changed |= slider("Right stick:", &s.c_cardinal, kCardinalMin, kCardinalMax, kCardinalDefault);
      break;
    case RADIUS:
      hint("This sets how far from the center the stick can reach. A normal GCC can get to roughly 100-103.");
      hint("Melee only sees <=80, and max possible is 127. Calibrate again after changing it.");
      changed |= slider("Left stick:", &s.a_scaler, kScalerMin, kScalerMax, kScalerDefault);
      changed |= slider("Right stick:", &s.c_scaler, kScalerMin, kScalerMax, kScalerDefault);
      break;
    case SET_OVER:
      ImGui::Text("Left stick: %s   C-stick: %s", s.a_cal.measured ? "calibrated" : "not calibrated", s.c_cal.measured ? "calibrated" : "not calibrated");
      ImGui::Text("Snapback X %d Y %d   Waveshaping X %d Y %d   Smoothing X %d Y %d", s.x_snapback, s.y_snapback, s.ax_waveshaping, s.ay_waveshaping, s.ax_smoothing, s.ay_smoothing);
      ImGui::Text("C-Stick Snapback X %d Y %d   Waveshaping X %d Y %d", s.cx_smoothing, s.cy_smoothing, s.cx_waveshaping, s.cy_waveshaping);
      ImGui::Text("Cardinal snapping %d / %d   Radius %d / %d", s.a_cardinal, s.c_cardinal, s.a_scaler, s.c_scaler);
      ImGui::Text("L Mode %d Offset %d   R Mode %d Offset %d", s.l_mode + 1, s.l_offset, s.r_mode + 1, s.r_offset);
      ImGui::Text("Tourney mode %d", s.tourney);
      break;
    case REMAP: {
      hint("You may remap A, B, Du, L, R, X, Y, or Z.");
      hint("When L or R are swapped, they are digital only.");
      hint("Use Trigger Mode 5 to get an analog trigger value on a digital button for lightshielding.");
      static const char* names[REMAP_COUNT] = {"A", "B", "D-pad up", "L", "R", "X", "Y", "Z"};
      static const uint16_t bits[REMAP_COUNT] = {PAD_A, PAD_B, PAD_UP, PAD_L, PAD_R, PAD_X, PAD_Y, PAD_Z};
      for (int out = 0; out < REMAP_COUNT; out++) {
        std::string from;
        for (int i = 0; i < REMAP_COUNT; i++) if (s.remap[out] & (1 << i)) { if (!from.empty()) from += "+"; from += names[i]; }
        ImGui::PushID(out);
        if (ImGui::Button(m.remap_step == out ? "Press a button..." : "Set")) { m.remap_step = out; m.last_buttons = 0xFFFF; }
        ImGui::SameLine();
        ImGui::Text("%s  <=  %s", names[out], from.empty() ? "(none)" : from.c_str());
        ImGui::PopID();
      }
      if (m.remap_step >= 0) {
        host::phob::HardwareInput in;
        if (host::gcadapter_phob_raw(port, in)) {
          const uint16_t pressed = m.last_buttons == 0xFFFF ? 0 : (uint16_t)(in.buttons & ~m.last_buttons);
          m.last_buttons = in.buttons;
          if (pressed & PAD_START) m.remap_step = -1;
          for (int i = 0; i < REMAP_COUNT && m.remap_step >= 0; i++)
            if (pressed & bits[i]) { s.remap[m.remap_step] = (uint8_t)(1 << i); m.remap_step = -1; changed = true; break; }
        }
      }
      if (ImGui::Button("Reset remapping")) { for (int i = 0; i < REMAP_COUNT; i++) s.remap[i] = (uint8_t)(1 << i); changed = true; }
      break;
    }
    case LTRIGGER: hint("This changes the behavior of the L trigger."); changed |= draw_trigger(s, true); break;
    case RTRIGGER: hint("This changes the behavior of the R trigger."); changed |= draw_trigger(s, false); break;
    case TOURNEY: {
      hint("This prevents misinputs during tournaments by delaying or disabling the start button, which");
      hint("pauses the match, and/or dpad-up, which taunts. You must hold delayed buttons for 1 second.");
      static const char* modes[6] = {"Tourney Mode Off", "Taunt Delayed", "Taunt Disabled", "Start Delayed", "Start and Taunt Delayed", "Start Delayed, Taunt Disabled"};
      for (int i = 0; i < 6; i++) if (ImGui::RadioButton(modes[i], &s.tourney, i)) changed = true;
      break;
    }
    case RESET:
      ImGui::RadioButton("Soft Reset", &m.reset_pick, 0);
      ImGui::RadioButton("Hard Reset", &m.reset_pick, 1);
      hint(m.reset_pick == 0 ? "This resets everything to defaults except for stick calibration, which is untouched."
                             : "This resets everything including stick cal. You'll need to perform stick calibration again.");
      if (!m.reset_confirm) { if (ImGui::Button("Reset")) m.reset_confirm = true; }
      else {
        ImGui::TextUnformatted("Are you sure you want to reset?");
        if (ImGui::Button("Yes, reset")) { if (m.reset_pick == 0) soft_reset(s); else hard_reset(s); m.reset_confirm = false; changed = true; }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) m.reset_confirm = false;
      }
      break;
    case INPUTVIEW: case XYSCOPE: case TIMESCOPE: case CONTSCOPE: case PRESSTIME: case REACTION: case GATE: case EXPORT:
      draw_scope(port, m, m.screen);
      break;
    case PHOBCHECK: {
      hint("What this controller sends, measured the way PhobScope measures it. Run each test in PhobScope > Value vs Time Plots; the latest result of each shows here.");
      const double hz = host::gcadapter_poll_rate_hz();
      ImGui::Text("Adapter: %.0f Hz %s", hz, hz >= 900 ? "(1000 Hz: full detail)" : hz > 0 ? "(below 1000 Hz: results are coarser)" : "");
      auto row = [](const char* what, bool have, bool good, const char* value, const char* fix) {
        ImGui::TextColored(!have ? ImVec4(0.6f, 0.6f, 0.6f, 1) : good ? ImVec4(0.5f, 0.9f, 0.6f, 1) : ImVec4(1, 0.6f, 0.3f, 1), "%s %s", what, have ? value : "not measured yet");
        if (have && !good) hint(fix);
      };
      char buf[96];
      std::snprintf(buf, sizeof buf, "%.0f%% chance a release reads as the other direction", m.last_snapback);
      row("Snapback:", m.last_snapback >= 0, m.last_snapback < 0.5f, buf,
          "Raise Smart Snapback (Stick Configuration > Stick Filter Settings > Left Stick Smart Snapback) on the axis you flicked.");
      std::snprintf(buf, sizeof buf, "vanilla %.0f%%, UCF %.0f%%", m.last_db_van, m.last_db_ucf);
      row("Dashback:", m.last_db_van >= 0, m.last_db_ucf >= 99.5f, buf,
          "A slow flick spends time in the tilt zone. Waveshaping (Stick Filter Settings > Left Stick Waveshaping) changes how fast inputs are treated.");
      std::snprintf(buf, sizeof buf, "no turn %.0f%%, empty pivot %.0f%%, dashback %.0f%%", m.last_pivot[0], m.last_pivot[1], m.last_pivot[2]);
      row("Pivot:", m.last_pivot[0] >= 0, m.last_pivot[1] >= 50, buf, "This one is mostly timing: the empty pivot window is one frame.");
      std::snprintf(buf, sizeof buf, "digital %.0f%%, ADT %.0f%%, none %.0f%%", m.last_trig[0], m.last_trig[1], m.last_trig[2]);
      row("Powershield:", m.last_trig[0] >= 0, m.last_trig[2] < 0.5f, buf,
          "The analog crossed 43 too long before the click. Trigger Configuration modes 1 (digital only) or 5 change this.");
      ImGui::Separator();
      ImGui::TextUnformatted("Notches: hold the stick still in a notch.");
      std::vector<host::GCScopeSample> v;
      if (latest(port, v, 1)) {
        const host::GCScopeSample& s2 = v.back();
        for (int stick = 0; stick < 2; stick++) {
          const int x = stick ? s2.cx : s2.ax, y = stick ? s2.cy : s2.ay;
          float mx, my;
          melee_coords(x, y, mx, my);
          const double deg = std::atan2((double)y, (double)x) * 180 / 3.14159265358979;
          // Angle within the quadrant and magnitude over 80, as Altimor's visualizer shows them.
          const double qangle = std::atan2(std::fabs((double)y), std::fabs((double)x)) * 180 / 3.14159265358979;
          ImGui::Text("%s %4d, %4d   Melee %.4f, %.4f   %.1f deg (%.4f in quadrant)   magnitude %.4f", stick ? "C-Stick:" : "Stick:  ",
                      x, y, mx, my, deg < 0 ? deg + 360 : deg, qangle, std::sqrt((double)x * x + (double)y * y) / 80);
          if (!stick && (x || y) && host::phob::ic_desync_coordinate(x, y))
            ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "  This coordinate desyncs the Ice Climbers (Popo and Nana read it differently).");
        }
      }
      hint(m.s.phob_hardware ? "To move a notch, use Notch Fix on the controller (PhobVision: Stick Configuration > Stick Calibration)."
                             : "To move a notch: Stick Configuration > Stick Calibration > Notch Fix.");
      break;
    }
    case SETUPS: {
      hint("Save these settings under a name to switch between them or give them to a friend (PhobGCC\\Setups).");
      hint("A setup without calibration keeps each player's own stick calibration when loaded.");
      ImGui::InputText("Name", m.setup_name, sizeof m.setup_name);
      ImGui::Checkbox("Include stick calibration", &m.setup_cal);
      if (ImGui::Button("Save setup")) m.status = host::phob_setup_save(m.setup_name, s, m.setup_cal) ? "Setup saved." : "Could not save the setup.";
      for (const std::string& name : host::phob_setup_list()) {
        ImGui::PushID(name.c_str());
        if (ImGui::Button("Load")) { if (host::phob_setup_load(name, s)) { changed = true; m.status = "Loaded " + name + "."; } }
        ImGui::SameLine();
        ImGui::TextUnformatted(name.c_str());
        ImGui::PopID();
      }
      break;
    }
    default: break;
  }
  return changed;
}
}  // namespace

bool phob_menu_capturing() { return g_capturing; }
bool phob_menu_owns_start() { return g_owns_start; }

bool draw_phob_menu(int port) {
  if (port < 0 || port > 3) return false;
  MenuState& m = g_menu[port];
  // Only this socket's screen is drawn this frame; the others stop listening when it changes.
  for (int i = 0; i < 4; i++) if (i != port) { g_menu[i].calibrating = false; g_menu[i].remap_step = -1; }
  host::gcadapter_test_script_start();   // test runs only: the scripted player starts with the screen
  if (!m.loaded) {
    m.s = host::gcadapter_phob_get(port);
    m.loaded = true;
    // Test runs: MELEE_TEST_PHOB=<screen number> opens that screen, so every one can be drawn hidden.
    if (const char* t = std::getenv("MELEE_TEST_PHOB")) { const int n = std::atoi(t); if (n >= 0 && n < SCREEN_COUNT) m.screen = (Screen)n; m.stickmap = 1;
      m.cap.stick_thresh = m.screen == PRESSTIME || m.screen == REACTION ? 23 : 10; m.cap.trigger_thresh = m.screen == REACTION ? 49 : 255; }
    // MELEE_TEST_PHOB_ACTION presses an on-screen control for scripted players: begin (calibration),
    // notchfix, remap:<output>, input:<n>, measure:<n>, map:<n>.
    if (const char* a = std::getenv("MELEE_TEST_PHOB_ACTION")) {
      const std::string act = a;
      const auto arg = [&](const char* key) { return act.rfind(key, 0) == 0 ? std::atoi(act.c_str() + std::strlen(key)) : -1; };
      const Stick which = m.screen == CSTICKCAL || m.screen == CNOTCHFIX ? CSTICK : ASTICK;
      StickCal& saved = which == ASTICK ? m.s.a_cal : m.s.c_cal;
      if (act == "begin") { cal_begin(m.cal, which, saved, which == ASTICK ? m.s.a_scaler : m.s.c_scaler); m.calibrating = true; m.last_buttons = 0xFFFF; }
      if (act == "notchfix") { m.calibrating = cal_begin_notch_fix(m.cal, which, saved, which == ASTICK ? m.s.a_scaler : m.s.c_scaler); m.last_buttons = 0xFFFF; }
      if (arg("remap:") >= 0) { m.remap_step = arg("remap:"); m.last_buttons = 0xFFFF; }
      if (arg("input:") >= 0) m.time_input = arg("input:");
      if (arg("measure:") >= 0) m.time_measure = arg("measure:");
      if (arg("map:") >= 0) m.stickmap = arg("map:");
    }
  }
  bool changed = false;
  int kind = m.s.phob_hardware ? 1 : 0;
  if (ImGui::Combo("This controller", &kind, "Stock controller: PhobGCC settings here\0PhobGCC: tools only\0")) {
    m.s.phob_hardware = kind == 1;
    if (m.s.phob_hardware) m.s.enabled = false;
    m.screen = MAIN;
    changed = true;
  }
  const bool on_main = m.screen == MAIN;
  if (m.s.phob_hardware) {
    if (on_main) hint("Your PhobGCC already calibrates, filters and remaps itself, and keeps doing so here. Nothing below changes what it sends; tune it with PhobVision or its button combos. PhobScope and Phob Check read exactly what it sends.");
  } else {
    if (ImGui::Checkbox("PhobGCC settings on this socket", &m.s.enabled)) changed = true;
    if (on_main) hint("PhobGCC's stick calibration, notches, snapback filter, trigger modes and lockouts, for any controller on the adapter.");
    if (m.s.enabled && on_main)
    {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.75f, 0.3f, 1));
      ImGui::TextWrapped("If this controller is a PhobGCC, choose PhobGCC above: these settings on top of its firmware would apply everything twice.");
      ImGui::PopStyleColor();
    }
  }
  if (on_main) hint("From PhobGCC by Phobos132, FrostSSBM, CarVac, NiceMitch and contributors. Stickmaps by venus23gg; Melee coordinate checks by Altimor; extra scopes after GCC Test Suite by greenwave-1.");

  // PhobVision's breadcrumb and menu list.
  std::vector<Screen> path;
  for (Screen s = m.screen; ; s = node(s).parent) { path.insert(path.begin(), s); if (s == MAIN) break; }
  for (size_t i = 0; i < path.size(); i++) {
    if (i) {
      ImGui::SameLine();
      const ImGuiStyle& st = ImGui::GetStyle();
      const float need = ImGui::CalcTextSize("> ").x + ImGui::CalcTextSize(node(path[i]).name).x + st.FramePadding.x * 2 + st.ItemSpacing.x * 2;
      if (ImGui::GetContentRegionAvail().x < need) ImGui::NewLine();
      ImGui::TextDisabled(">");
      ImGui::SameLine();
    }
    ImGui::PushID((int)i);
    if (i + 1 < path.size()) { if (ImGui::SmallButton(node(path[i]).name)) { m.screen = path[i]; m.calibrating = false; } }
    else ImGui::TextUnformatted(node(path[i]).name);
    ImGui::PopID();
  }
  ImGui::Separator();
  const Node& n = node(m.screen);
  if (m.s.phob_hardware && m.screen != MAIN && m.screen != PHOBCHECK && node(m.screen).parent != SCOPE && m.screen != SCOPE) m.screen = MAIN;
  if (!n.children.empty()) {
    for (Screen child : n.children)
      if (!m.s.phob_hardware || m.screen != MAIN || child == SCOPE || child == PHOBCHECK)
        if (ImGui::Button(node(child).name, ImVec2(260, 0))) {
          m.screen = child; m.calibrating = false; m.reset_confirm = false;
          m.cap.mode = host::phobscope::Mode::None; m.have_shown = false; m.reaction_waiting = false;
          // the firmware's thresholds on entering each screen
          m.cap.stick_thresh = child == PRESSTIME || child == REACTION ? 23 : 10;
          m.cap.trigger_thresh = child == REACTION ? 49 : 255;
        }
  } else {
    changed |= draw_screen(port, m);
  }
  if (m.screen != MAIN && ImGui::Button("Back")) {
    m.screen = node(m.screen).parent; m.calibrating = false;
    m.cap.mode = host::phobscope::Mode::None; m.have_shown = false; m.reaction_waiting = false;
  }
  if (!m.status.empty()) ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.6f, 1), "%s", m.status.c_str());

  g_owns_start = m.screen == XYSCOPE || m.screen == TIMESCOPE || m.screen == PRESSTIME || m.screen == REACTION;
  g_capturing = m.calibrating || m.remap_step >= 0 || m.reaction_waiting ||
                (m.screen == REACTION && m.cap.mode == Mode::Reaction && !m.cap.done);
  if (changed) {
    clamp(m.s);
    if (!host::phob_store_save(port, m.s)) m.status = "Could not save the settings file.";
  }
  return changed;
}

}  // namespace gx
