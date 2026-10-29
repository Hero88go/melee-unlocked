// PhobVision's menus, rebuilt for the settings panel. The screens, their order, their names and
// their help text follow PhobGCC's rp2040 menu (menu.h, menuStrings.h), so a Phob owner finds every
// setting where they expect it. PhobGCC (https://github.com/PhobGCC/PhobGCC-SW, GPL-3.0) is by
// Phobos132, FrostSSBM, CarVac, NiceMitch and the PhobGCC contributors.
//
// The PhobScope screens add the views of GCC Test Suite (https://github.com/greenwave-1/GTS, GPL-3.0,
// by greenwave-1): a continuous oscilloscope, a gate visualizer and a CSV export. They are drawn
// here from the adapter's own report stream; no GTS code is used.
//
// Buttons: the menus are ordinary panel controls. Calibration listens to the controller being
// calibrated, the way PhobVision does, except that B (which closes the panel) does not reset a
// notch: D-pad down does.
// SPDX-License-Identifier: GPL-3.0-only
#include "phob_menu.h"
#include "host.h"
#include "phob.h"
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace gx {
namespace {
using namespace host::phob;

enum Screen {
  MAIN, STICK, CALIBRATE, ASTICKCAL, ANOTCHFIX, CSTICKCAL, CNOTCHFIX, FILTER, ASNAPBACK, AWAVE, ASMOOTH,
  CSNAPBACK, CWAVE, GEOM, CARDINALS, RADIUS, SETTINGS, SET_OVER, REMAP, TRIGGER, LTRIGGER, RTRIGGER,
  TOURNEY, RESET, SCOPE, INPUTVIEW, XYSCOPE, TIMESCOPE, CONTSCOPE, PRESSTIME, GATE, EXPORT, SETUPS, SCREEN_COUNT
};
struct Node { Screen parent; const char* name; std::vector<Screen> children; };
const Node& node(Screen s) {
  static const Node nodes[SCREEN_COUNT] = {
      {MAIN, "Main Menu", {STICK, SETTINGS, SCOPE, SETUPS}},
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
      {MAIN, "PhobScope", {INPUTVIEW, XYSCOPE, TIMESCOPE, CONTSCOPE, PRESSTIME, GATE, EXPORT}},
      {SCOPE, "Input Viewer", {}},
      {SCOPE, "Stickmap Plots", {}},
      {SCOPE, "Value vs Time Plots", {}},
      {SCOPE, "Continuous Oscilloscope", {}},
      {SCOPE, "Button Timing Viewer", {}},
      {SCOPE, "Gate Visualizer", {}},
      {SCOPE, "Export Data", {}},
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
  std::chrono::steady_clock::time_point last_frame;
  float avg_x[24] = {}, avg_y[24] = {};
  int avg_n = 0, avg_i = 0;
  int remap_step = -1;   // Button Remapping: the output being assigned, -1 when idle
  int reset_pick = 0;
  bool reset_confirm = false;
  bool frozen = false;
  std::vector<host::GCScopeSample> frozen_samples;
  float gate[64] = {};
  char setup_name[48] = "";
  bool setup_cal = false;
  std::string status;
};
MenuState g_menu[4];

void hint(const char* text) { ImGui::TextDisabled("%s", text); }

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
  m.avg_x[m.avg_i] = rx; m.avg_y[m.avg_i] = ry;
  m.avg_i = (m.avg_i + 1) % 24; m.avg_n = std::min(m.avg_n + 1, 24);
  float ax = 0, ay = 0;
  for (int i = 0; i < m.avg_n; i++) { ax += m.avg_x[i]; ay += m.avg_y[i]; }
  if (m.avg_n) { ax /= m.avg_n; ay /= m.avg_n; }

  const auto now = std::chrono::steady_clock::now();
  const float dt = std::min(50.0f, std::chrono::duration<float, std::milli>(now - m.last_frame).count());
  m.last_frame = now;
  const uint16_t b = have ? in.buttons : 0;
  const uint16_t pressed = m.last_buttons == 0xFFFF ? 0 : (uint16_t)(b & ~m.last_buttons);
  m.last_buttons = b;

  const bool advance = (pressed & (PAD_L | PAD_R)) || ImGui::Button("Next (A/L/R)");
  ImGui::SameLine();
  const bool back = (pressed & PAD_Z) || ImGui::Button("Back (Z)");
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) { m.calibrating = false; return false; }

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
    ImGui::TextUnformatted("move its output CW/CCW. D-pad down puts it back.");
    ImGui::TextUnformatted("Press A/L/R to move on.");
    cal_adjust(m.cal, (b & PAD_X) != 0, (b & PAD_Y) != 0, (b & PAD_DOWN) != 0, dt);
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

void draw_scope(int port, MenuState& m, Screen screen) {
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
      static int which = 0;
      ImGui::RadioButton("Left Stick", &which, 0); ImGui::SameLine(); ImGui::RadioButton("Right Stick", &which, 1);
      latest(port, v, 400);
      const ImVec2 p0 = ImGui::GetCursorScreenPos();
      const float size = 260, k = size / 2 / 110.0f;
      draw_stick("xy", 0, 0, 0, 0, size);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 c(p0.x + size / 2, p0.y + size / 2);
      for (const auto& s : v) {
        dl->AddCircleFilled(ImVec2(c.x + (which ? s.raw_cx : s.raw_ax) * k, c.y - (which ? s.raw_cy : s.raw_ay) * k), 1.5f, IM_COL32(140, 140, 140, 160));
        dl->AddCircleFilled(ImVec2(c.x + (which ? s.cx : s.ax) * k, c.y - (which ? s.cy : s.ay) * k), 2.0f, IM_COL32(255, 200, 60, 220));
      }
      hint("Unfiltered (grey) and filtered (gold), the last 400 reports. Blue ring: Melee's 1.0 rim; orange square: deadzone.");
      break;
    }
    case TIMESCOPE: case CONTSCOPE: {
      static int which = 0;
      static int window_ms = 200;
      ImGui::Combo("Input", &which, "Left X\0Left Y\0C-Stick X\0C-Stick Y\0L Trigger\0R Trigger\0");
      if (screen == CONTSCOPE) {
        ImGui::SliderInt("Window (reports)", &window_ms, 50, 4000);
        if (ImGui::Button(m.frozen ? "Unfreeze" : "Freeze")) { m.frozen = !m.frozen; if (m.frozen) latest(port, m.frozen_samples, window_ms); }
      }
      if (screen == CONTSCOPE && m.frozen) v = m.frozen_samples;
      else latest(port, v, screen == CONTSCOPE ? window_ms : 200);
      plot_series("ts", v, which, 220);
      ImGui::Text("%zu reports, %.0f Hz measured", v.size(), sample_rate(v));
      hint("Grey: unfiltered. Gold: what the game gets. Lines at +-80 (rim) and +-23 (deadzone).");
      // Snapback: the farthest the output went past centre after a release from the rim.
      if (which < 4 && !v.empty()) {
        int peak = 0, worst_raw = 0, worst = 0, sign = 0;
        for (const auto& s : v) {
          const int raw = which == 0 ? s.raw_ax : which == 1 ? s.raw_ay : which == 2 ? s.raw_cx : s.raw_cy;
          const int out = which == 0 ? s.ax : which == 1 ? s.ay : which == 2 ? s.cx : s.cy;
          if (std::abs(raw) > std::abs(peak)) { peak = raw; sign = raw > 0 ? 1 : -1; }
          if (sign && raw * sign < worst_raw * sign) worst_raw = raw;
          if (sign && out * sign < worst * sign) worst = out;
        }
        if (sign && std::abs(peak) > 60)
          ImGui::Text("Snapback: unfiltered %d, filtered %d (Melee wants under 23)", std::abs(std::min(0, worst_raw * sign)), std::abs(std::min(0, worst * sign)));
      }
      break;
    }
    case PRESSTIME: {
      latest(port, v, 600);
      hint("The last 600 reports. Each row is a button; marks are presses, labelled in frames from the first.");
      const struct { uint16_t bit; const char* n; } rows[] = {{PAD_A, "A"}, {PAD_B, "B"}, {PAD_X, "X"}, {PAD_Y, "Y"},
          {PAD_Z, "Z"}, {PAD_L, "L"}, {PAD_R, "R"}, {PAD_START, "S"}};
      const ImVec2 p0 = ImGui::GetCursorScreenPos();
      const float w = std::max(200.0f, ImGui::GetContentRegionAvail().x), rh = 16;
      ImGui::InvisibleButton("##timing", ImVec2(w, rh * 8 + 4));
      ImDrawList* dl = ImGui::GetWindowDrawList();
      if (v.size() < 2) break;
      const uint32_t t0 = v.front().time_us, span = std::max<uint32_t>(1, v.back().time_us - t0);
      for (int r = 0; r < 8; r++) {
        const float y = p0.y + r * rh;
        dl->AddText(ImVec2(p0.x, y), IM_COL32(200, 200, 200, 255), rows[r].n);
        for (const auto& s : v)
          if (s.buttons & rows[r].bit) {
            const float x = p0.x + 20 + (w - 20) * (float)(s.time_us - t0) / span;
            dl->AddRectFilled(ImVec2(x, y + 2), ImVec2(x + 2, y + rh - 2), IM_COL32(255, 200, 60, 255));
          }
      }
      bool first = true; uint32_t start = 0; uint16_t prev = v.front().buttons;
      std::string line;
      for (const auto& s : v) {
        const uint16_t down = (uint16_t)(s.buttons & ~prev);
        prev = s.buttons;
        if (!down) continue;
        if (first) { start = s.time_us; first = false; }
        char buf[48];
        std::snprintf(buf, sizeof buf, "%s%.1ff ", down & PAD_A ? "A" : down & PAD_B ? "B" : down & PAD_X ? "X" : down & PAD_Y ? "Y" : down & PAD_Z ? "Z" : down & PAD_L ? "L" : down & PAD_R ? "R" : "?",
                      (s.time_us - start) / 16683.0);
        line += buf;
      }
      ImGui::TextWrapped("%s", line.c_str());
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
          for (int i = 0; i < REMAP_COUNT; i++)
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
    case INPUTVIEW: case XYSCOPE: case TIMESCOPE: case CONTSCOPE: case PRESSTIME: case GATE: case EXPORT:
      draw_scope(port, m, m.screen);
      break;
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

bool draw_phob_menu(int port) {
  if (port < 0 || port > 3) return false;
  MenuState& m = g_menu[port];
  if (!m.loaded) { m.s = host::gcadapter_phob_get(port); m.loaded = true; }
  bool changed = false;
  if (ImGui::Checkbox("PhobGCC settings on this socket", &m.s.enabled)) changed = true;
  hint("PhobGCC's stick calibration, notches, snapback filter, trigger modes and lockouts, for any controller on the adapter.");
  hint("Settings and menus from PhobGCC by Phobos132, FrostSSBM, CarVac, NiceMitch and contributors. Scopes after GCC Test Suite by greenwave-1.");

  // PhobVision's breadcrumb and menu list.
  std::vector<Screen> path;
  for (Screen s = m.screen; ; s = node(s).parent) { path.insert(path.begin(), s); if (s == MAIN) break; }
  for (size_t i = 0; i < path.size(); i++) {
    if (i) { ImGui::SameLine(); ImGui::TextDisabled(">"); ImGui::SameLine(); }
    ImGui::PushID((int)i);
    if (i + 1 < path.size()) { if (ImGui::SmallButton(node(path[i]).name)) { m.screen = path[i]; m.calibrating = false; } }
    else ImGui::TextUnformatted(node(path[i]).name);
    ImGui::PopID();
  }
  ImGui::Separator();
  const Node& n = node(m.screen);
  if (!n.children.empty()) {
    for (Screen child : n.children)
      if (ImGui::Button(node(child).name, ImVec2(260, 0))) { m.screen = child; m.calibrating = false; m.reset_confirm = false; }
  } else {
    changed |= draw_screen(port, m);
  }
  if (m.screen != MAIN && ImGui::Button("Back")) { m.screen = node(m.screen).parent; m.calibrating = false; }
  if (!m.status.empty()) ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.6f, 1), "%s", m.status.c_str());

  if (changed) {
    clamp(m.s);
    if (!host::phob_store_save(port, m.s)) m.status = "Could not save the settings file.";
  }
  return changed;
}

}  // namespace gx
