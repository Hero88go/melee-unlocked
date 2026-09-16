// SPDX-License-Identifier: GPL-2.0-or-later
#include "pc_settings.h"
#include "pc_settings_shared.h"
#include "gx_backend.h"
#include "jukebox.h"
#include "window.h"
#include "audio.h"
#include "host.h"
#include "input_bindings.h"
#include "lcancel.h"
#include "updater.h"
#include "discord_presence.h"
#ifndef MELEE_PORT_VERSION
#define MELEE_PORT_VERSION "dev"
#endif
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"
#include <windows.h>
#include <xinput.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <string>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <cmath>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
namespace gx {

// Names used both when drawing the Controls list and when saving/loading bindings
// to port-settings.ini (as "key_<name>" / "pad<idx>_<name>" / "gc<idx>_<name>" lines).
// Order must match host::BindAction exactly.
static const char* kActionNames[(size_t)host::BindAction::Count] = {
  "A", "B", "X", "Y", "Z", "Start", "L", "R", "DUp", "DDown", "DLeft", "DRight"
};

// ---- Port-source <-> combo-box index, shared by load/save and the Port assignment UI ----
// 0 = None, 1 = Keyboard, 2..5 = XInput, 6..9 = DS4, 10..13 = GC Adapter, 14..17 = Switch Pro.
// Saved settings store the index, so new devices are appended and the existing ones never move.
// Switch Pro says "experimental" because it has never been tried against the hardware.
static const int kPortSourceCount = 18;
static const char* kPortSourceNames[kPortSourceCount] = {
  "None", "Keyboard",
  "XInput Pad 1", "XInput Pad 2", "XInput Pad 3", "XInput Pad 4",
  "DS4 1", "DS4 2", "DS4 3", "DS4 4",
  "GC Adapter 1", "GC Adapter 2", "GC Adapter 3", "GC Adapter 4",
  "Switch Pro 1 (experimental)", "Switch Pro 2 (experimental)",
  "Switch Pro 3 (experimental)", "Switch Pro 4 (experimental)"
};

static int port_source_to_combo(const host::PortSource& s) {
  switch (s.kind) {
    case host::DeviceKind::Keyboard:  return 1;
    case host::DeviceKind::XInputPad: return 2 + std::clamp(s.index, 0, 3);
    case host::DeviceKind::DS4Pad:     return 6 + std::clamp(s.index, 0, 3);
    case host::DeviceKind::GCAdapter: return 10 + std::clamp(s.index, 0, 3);
    case host::DeviceKind::SwitchPro: return 14 + std::clamp(s.index, 0, 3);
    case host::DeviceKind::None: default: return 0;
  }
}

static host::PortSource combo_to_port_source(int idx) {
  if (idx == 1) return { host::DeviceKind::Keyboard, 0 };
  if (idx >= 2 && idx <= 5) return { host::DeviceKind::XInputPad, idx - 2 };
  if (idx >= 6 && idx <= 9) return { host::DeviceKind::DS4Pad, idx - 6 };
  if (idx >= 10 && idx <= 13) return { host::DeviceKind::GCAdapter, idx - 10 };
  if (idx >= 14 && idx <= 17) return { host::DeviceKind::SwitchPro, idx - 14 };
  return { host::DeviceKind::None, 0 };
}

// ---- binding -> label helpers, used by the Controls tabs ----
static void format_key_label(int vk, char* buf, size_t n) {
  if (!vk) { std::snprintf(buf, n, "Unbound"); return; }
  if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) { std::snprintf(buf, n, "%c", (char)vk); return; }
  switch (vk) {
    case VK_RETURN: std::snprintf(buf, n, "Enter"); return;
    case VK_SPACE:  std::snprintf(buf, n, "Space"); return;
    case VK_TAB:    std::snprintf(buf, n, "Tab"); return;
    case VK_SHIFT:  std::snprintf(buf, n, "Shift"); return;
    case VK_CONTROL:std::snprintf(buf, n, "Ctrl"); return;
    case VK_LEFT:   std::snprintf(buf, n, "Left"); return;
    case VK_RIGHT:  std::snprintf(buf, n, "Right"); return;
    case VK_UP:     std::snprintf(buf, n, "Up"); return;
    case VK_DOWN:   std::snprintf(buf, n, "Down"); return;
    default:        std::snprintf(buf, n, "VK 0x%02X", vk); return;
  }
}

// XInput wButtons is a different bit layout than the GC-style kActionPadBit table,
// so it needs its own name lookup (unlike the GC adapter, whose raw mask already
// matches kActionPadBit -- see gc_button_name below).
static const char* xinput_button_name(unsigned short mask) {
  switch (mask) {
    case 0: return "Unbound";
    case XINPUT_GAMEPAD_DPAD_UP: return "D-Up";
    case XINPUT_GAMEPAD_DPAD_DOWN: return "D-Down";
    case XINPUT_GAMEPAD_DPAD_LEFT: return "D-Left";
    case XINPUT_GAMEPAD_DPAD_RIGHT: return "D-Right";
    case XINPUT_GAMEPAD_START: return "Start";
    case XINPUT_GAMEPAD_BACK: return "Back";
    case XINPUT_GAMEPAD_LEFT_THUMB: return "L3";
    case XINPUT_GAMEPAD_RIGHT_THUMB: return "R3";
    case XINPUT_GAMEPAD_LEFT_SHOULDER: return "LB";
    case XINPUT_GAMEPAD_RIGHT_SHOULDER: return "RB";
    case XINPUT_GAMEPAD_A: return "A";
    case XINPUT_GAMEPAD_B: return "B";
    case XINPUT_GAMEPAD_X: return "X";
    case XINPUT_GAMEPAD_Y: return "Y";
    default: return "?";
  }
}

static const char* ds4_button_name(unsigned short mask) {
  switch (mask) {
    case 0: return "Unbound";
    case host::DS4_DPAD_UP: return "D-Up"; case host::DS4_DPAD_DOWN: return "D-Down";
    case host::DS4_DPAD_LEFT: return "D-Left"; case host::DS4_DPAD_RIGHT: return "D-Right";
    case host::DS4_SQUARE: return "Square"; case host::DS4_CROSS: return "Cross";
    case host::DS4_CIRCLE: return "Circle"; case host::DS4_TRIANGLE: return "Triangle";
    case host::DS4_L1: return "L1"; case host::DS4_R1: return "R1";
    case host::DS4_L2: return "L2"; case host::DS4_R2: return "R2";
    case host::DS4_SHARE: return "Share"; case host::DS4_OPTIONS: return "Options";
    case host::DS4_L3: return "L3"; case host::DS4_R3: return "R3";
    default: return "?";
  }
}

static const char* swpro_button_name(unsigned short mask) {
  switch (mask) {
    case 0: return "Unbound";
    case host::SWPRO_DPAD_UP: return "D-Up"; case host::SWPRO_DPAD_DOWN: return "D-Down";
    case host::SWPRO_DPAD_LEFT: return "D-Left"; case host::SWPRO_DPAD_RIGHT: return "D-Right";
    case host::SWPRO_B: return "B"; case host::SWPRO_A: return "A";
    case host::SWPRO_Y: return "Y"; case host::SWPRO_X: return "X";
    case host::SWPRO_L: return "L"; case host::SWPRO_R: return "R";
    case host::SWPRO_ZL: return "ZL"; case host::SWPRO_ZR: return "ZR";
    case host::SWPRO_MINUS: return "Minus"; case host::SWPRO_PLUS: return "Plus";
    case host::SWPRO_L3: return "L-Stick"; case host::SWPRO_R3: return "R-Stick";
    default: return "?";
  }
}

// GC adapter raw button bits match kActionPadBit exactly (see input_bindings.h /
// default_gc_bindings comments), so this is a reverse lookup into kActionNames.
static const char* gc_button_name(unsigned short mask) {
  if (!mask) return "Unbound";
  for (int i = 0; i < (int)host::BindAction::Count; ++i)
    if (host::kActionPadBit[i] == mask) return kActionNames[i];
  return "?";
}

// Space-joined list of action names whose bit is set, for the live "what's this
// device pressing right now" overlay lines. Bit i corresponds to BindAction i.
static std::string active_actions_label(uint16_t bits) {
  std::string s;
  for (int i = 0; i < (int)host::BindAction::Count; ++i)
    if (bits & (uint16_t)(1u << i)) { if (!s.empty()) s += " "; s += kActionNames[i]; }
  return s.empty() ? std::string("-") : s;
}

// Same idea but decoding a final PadState.button field, which uses the GC-native
// bits (kActionPadBit), not the BindAction-bit-index encoding active_actions_label
// reads -- used for the per-port output overlay.
static std::string active_pad_buttons_label(uint16_t button) {
  std::string s;
  for (int i = 0; i < (int)host::BindAction::Count; ++i)
    if (button & host::kActionPadBit[i]) { if (!s.empty()) s += " "; s += kActionNames[i]; }
  return s.empty() ? std::string("-") : s;
}

// On-screen controller display for streaming: the octagonal gate, C-stick, analog triggers and the
// face buttons, drawn from the state the game read on its last PADRead rather than a fresh poll, so
// it shows what the game acted on and device polling stays on one thread at one rate.
// `row` stacks several overlays upward so two to four players can be shown at once. `editable` is on
// while the settings panel is open, which is when the overlay may be dragged and resized.
static void draw_input_overlay(int port, int row, bool lone, bool editable, bool borderless) {
  host::PadState pads[4]{};
  host::input_last_pads(pads);
  int slot = port < 0 || port > 3 ? 0 : port;
  // An adapter socket the player is not using reports nothing, and the overlay then drew an empty
  // controller with no hint why. With a single port selected, show the first one that has a device
  // instead, so picking the wrong one (or plugging into socket 3) is not mistaken for a broken
  // overlay. With several selected the player asked for specific ports, so leave them as they are.
  if (lone && pads[slot].err != 0)
    for (int i = 0; i < 4; ++i)
      if (pads[i].err == 0) { slot = i; break; }
  const host::PadState& pad = pads[slot];

  const float pad_w = 300.f, pad_h = 132.f;
  char title[32];
  std::snprintf(title, sizeof title, "Controller%d", slot);
  // Laid out for this size and scaled to whatever the window is dragged to, so it can be sized to
  // taste for a stream layout. While the settings panel is open it can be moved and resized; the
  // rest of the time it ignores the mouse entirely so it can never swallow a click meant for the
  // game. ImGui remembers each overlay's position and size between launches by window name.
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
                           ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoNav;
  if (!editable) flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
  if (borderless) flags |= ImGuiWindowFlags_NoBackground;
  ImGui::SetNextWindowPos(ImVec2(16, ImGui::GetIO().DisplaySize.y - 16 - row * (pad_h + 6)), ImGuiCond_FirstUseEver, ImVec2(0, 1));
  ImGui::SetNextWindowSize(ImVec2(pad_w, pad_h), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(borderless ? 0.0f : 0.30f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, borderless ? 0.0f : 1.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  // The resize grip draws even with the background off, leaving a triangle in the bottom right of an
  // otherwise invisible overlay. Hidden in borderless mode; the window edges still resize it.
  if (borderless) {
    ImGui::PushStyleColor(ImGuiCol_ResizeGrip, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ResizeGripHovered, IM_COL32(255, 255, 255, 40));
    ImGui::PushStyleColor(ImGuiCol_ResizeGripActive, IM_COL32(255, 255, 255, 70));
  }
  ImGui::Begin(title, nullptr, flags);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 o = ImGui::GetCursorScreenPos();
  // Everything below is authored against a 300x132 controller and scaled to the current size.
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float k = std::max(0.25f, std::min(avail.x / pad_w, avail.y / pad_h));
  auto P = [&](float x, float y) { return ImVec2(o.x + x * k, o.y + y * k); };
  auto S = [&](float v) { return v * k; };
  const float gate = S(46.f), cgate = S(30.f);
  const ImU32 line = IM_COL32(255, 255, 255, 190), dim = IM_COL32(255, 255, 255, 70);
  const ImU32 yellow = IM_COL32(245, 215, 65, 235), green = IM_COL32(120, 225, 150, 240), red = IM_COL32(235, 95, 95, 240);

  // Analog triggers: the bar fills with how far it is pressed, so light presses are visible.
  auto trigger = [&](float x, uint8_t value) {
    const ImVec2 a = P(x, 4), b = P(x + 58, 12);
    dl->AddRectFilled(a, ImVec2(a.x + S(58) * (value / 255.f), b.y), line, S(4.f));
    dl->AddRect(a, b, dim, S(4.f));
  };
  trigger(6, pad.trig_l);
  trigger(74, pad.trig_r);

  // Melee's gate is an octagon with vertices on the cardinals and diagonals, which is what an eight
  // sided ImGui n-gon gives. Stick values are signed and screen Y grows downward.
  auto stick = [&](ImVec2 c, float r, int8_t sx, int8_t sy, ImU32 colour) {
    dl->AddNgon(c, r, colour, 8, S(2.f));
    dl->AddCircle(c, S(2.f), dim, 8, 1.f);
    const ImVec2 tip(c.x + (sx / 128.f) * r, c.y - (sy / 128.f) * r);
    // Sized off the gate so the knob reads like the real stick rather than a small marker.
    dl->AddCircleFilled(tip, std::max(S(5.f), r * 0.20f), colour, 16);
  };
  stick(P(52, 74), gate, pad.stick_x, pad.stick_y, line);
  stick(P(146, 82), cgate, pad.sub_x, pad.sub_y, yellow);

  auto button = [&](ImVec2 c, float r, ImU32 colour, bool down, const char* label) {
    if (down) dl->AddCircleFilled(c, r, colour, 16);
    else dl->AddCircle(c, r, colour, 16, 1.5f);
    if (label) {
      const ImVec2 size = ImGui::CalcTextSize(label);
      dl->AddText(ImVec2(c.x - size.x * 0.5f, c.y - size.y * 0.5f), down ? IM_COL32(20, 20, 20, 230) : colour, label);
    }
  };
  const uint16_t b = pad.button;
  // GameCube face layout: A large in the middle, B low and left of it, X out to the right and Y up
  // over the top (the two kidney buttons wrap around A rather than sitting on the diagonals).
  button(P(232, 76), S(19.f), green, (b & 0x0100) != 0, "A");
  button(P(202, 101), S(10.f), red, (b & 0x0200) != 0, "B");
  button(P(267, 69), S(10.f), line, (b & 0x0400) != 0, "X");
  button(P(221, 42), S(10.f), line, (b & 0x0800) != 0, "Y");
  button(P(254, 28), S(9.f), IM_COL32(170, 130, 235, 240), (b & 0x0010) != 0, "Z");
  button(P(150, 24), S(7.f), line, (b & 0x1000) != 0, nullptr);

  // D-pad, small, only drawn when held: it is rarely used and should not clutter a stream.
  if (b & 0x0008) dl->AddTriangleFilled(P(104, 100), P(99, 108), P(109, 108), line);
  if (b & 0x0004) dl->AddTriangleFilled(P(104, 124), P(99, 116), P(109, 116), line);
  if (b & 0x0001) dl->AddTriangleFilled(P(92, 112), P(100, 107), P(100, 117), line);
  if (b & 0x0002) dl->AddTriangleFilled(P(116, 112), P(108, 107), P(108, 117), line);
  ImGui::End();
  if (borderless) ImGui::PopStyleColor(3);
  ImGui::PopStyleVar(2);
}

// The missed L-cancel itself is shown by tinting the fighter red in the renderer (see lcancel.cpp
// and the tint in gx_d3d12.cpp / gx_d3d11.cpp), not here: an on-screen panel was replaced by the
// red flash players already know from the Gecko code. What is left here is the notice that says
// the automatic press is switched off because this is a matchmaking mode.
static void draw_lcancel_overlays() {
  // Character select of a matchmaking mode, with the setting on: say plainly that it is off here.
  if (lcancel::automatic_enabled() && lcancel::online_session_pending()) {
    if (const char* mode = lcancel::auto_suppressed_mode()) {
      std::string lower = mode;
      for (char& c : lower) c = (char)std::tolower((unsigned char)c);
      ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, 10), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
      ImGui::SetNextWindowBgAlpha(0.75f);
      ImGui::Begin("LCancelModeNotice", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
      ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                         "You currently have auto L-cancel on, but we have disabled it for %s mode", lower.c_str());
      ImGui::End();
    }
  }
}

void load_pc_settings(D3D12Options& options, int& volume) {
  std::ifstream file(options.settings_path);
  // First launch (no saved settings yet): open the PC settings panel so nobody has to find it.
  options.settings_open = true;   // opens at every launch unless "startup 0" was saved
  std::string key, value;
  while (file >> key >> value) {
    try {
      if (key == "fps") { double rate = std::stod(value); if (rate == -1 || rate == 0 || (rate >= 30 && rate <= 2000)) options.fps_cap = rate; }
      else if (key == "scale") { int scale = std::stoi(value); if (scale >= 0 && scale <= 8) options.efb_scale = scale; }
      else if (key == "fullscreen") options.fullscreen = value == "1";
      else if (key == "vsync") options.vsync = value == "1";
      else if (key == "widescreen") options.widescreen = value == "1";
      else if (key == "truewidescreen") options.true_widescreen = value == "1";
      else if (key == "aspect") { int a = std::stoi(value); if (a >= 0 && a <= 4) options.aspect = (AspectMode)a; }
      // "window <w>x<h>", or "window follow" for the old behaviour of using whatever size the
      // window has been dragged to.
      else if (key == "window") {
        int w = 0, h = 0;
        if (std::sscanf(value.c_str(), "%dx%d", &w, &h) == 2 && w >= 320 && h >= 240) {
          options.window_w = w; options.window_h = h; options.window_pinned = true;
        } else options.window_pinned = false;
      }
      else if (key == "sharpness") options.sharpness = std::clamp(std::stof(value), 0.0f, 1.0f);
      else if (key == "anisotropy") { int a = std::stoi(value); if (a == 1 || a == 2 || a == 4 || a == 8 || a == 16) options.anisotropy = a; }
      else if (key == "ssaa") { int a = std::stoi(value); if (a == 1 || a == 2) options.ssaa = a; }
      else if (key == "subframe") options.subframe = value == "0" ? SubFrameMode::Off : value == "2" ? SubFrameMode::AuthoredInterpolate : SubFrameMode::Authored;
      else if (key == "music") slippi::jukebox::set_user_volume(std::stoi(value));
      else if (key == "performance") options.performance_overlay = value == "1";
      else if (key == "effects") { int n = std::atoi(value.c_str()); if (n >= 0 && n <= 2) options.effects_level = n; }
      else if (key == "inputoverlay") options.input_overlay = value == "1";
      // Settings saved before the overlay could show several ports name a single port number.
      else if (key == "inputoverlayport") { int n = std::atoi(value.c_str()); if (n >= 0 && n < 4) options.input_overlay_ports = 1 << n; }
      else if (key == "inputoverlayports") { int n = std::atoi(value.c_str()); if (n >= 0 && n < 16) options.input_overlay_ports = n; }
      else if (key == "inputoverlayhideborder") options.input_overlay_hide_border = value == "1";
      else if (key == "lcancelindicator") lcancel::set_indicator(value == "1");
      else if (key == "autolcancel") lcancel::set_automatic(value == "1");
      else if (key == "startup") options.settings_open = value != "0";
      else if (key == "dlss") { int m = std::stoi(value); if (m >= 0 && m <= 5) options.dlss_mode = m; }
      // Low spec: the switch, then what the player had before it was turned on, so turning it off
      // after a restart still restores their own settings rather than the defaults.
      else if (key == "lowspec") options.low_spec = value == "1";
      else if (key == "lowspec_prev_backend") options.low_spec_previous.api = value == "d3d11" ? RenderApi::D3D11 : RenderApi::D3D12;
      else if (key == "lowspec_prev_fps") { double rate = std::stod(value); if (rate == -1 || rate == 0 || (rate >= 30 && rate <= 2000)) options.low_spec_previous.fps_cap = rate; }
      else if (key == "lowspec_prev_scale") { int n = std::stoi(value); if (n >= 0 && n <= 8) options.low_spec_previous.efb_scale = n; }
      else if (key == "lowspec_prev_ssaa") { int n = std::stoi(value); if (n == 1 || n == 2) options.low_spec_previous.ssaa = n; }
      else if (key == "lowspec_prev_anisotropy") { int n = std::stoi(value); if (n == 1 || n == 2 || n == 4 || n == 8 || n == 16) options.low_spec_previous.anisotropy = n; }
      else if (key == "lowspec_prev_effects") { int n = std::stoi(value); if (n >= 0 && n <= 2) options.low_spec_previous.effects_level = n; }
      else if (key == "lowspec_prev_dlss") { int n = std::stoi(value); if (n >= 0 && n <= 5) options.low_spec_previous.dlss_mode = n; }
      else if (key == "lowspec_prev_subframe") options.low_spec_previous.subframe = value == "0" ? SubFrameMode::Off : value == "2" ? SubFrameMode::AuthoredInterpolate : SubFrameMode::Authored;
      else if (key == "discord") options.discord_presence = value == "1";
      // A Discord application id is a snowflake; anything else would only be rejected by Discord.
      else if (key == "discord_app_id") { if (value.find_first_not_of("0123456789") == std::string::npos && value.size() <= 24) options.discord_app_id = value; }
      else if (key == "backend") options.api = value == "d3d11" ? RenderApi::D3D11 : RenderApi::D3D12;
      else if (key == "volume") volume = std::clamp(std::stoi(value), 0, 100);
      else if (key.rfind("key_", 0) == 0) {
        for (int i = 0; i < (int)host::BindAction::Count; ++i)
          if (key == std::string("key_") + kActionNames[i]) host::g_key_bindings.vk[i] = std::stoi(value);
      }
      // Legacy pre-multi-device format: a single unindexed "pad_<Action>" line.
      // Migrate it onto XInput pad 0 so upgrading doesn't silently reset bindings.
      else if (key.rfind("pad_", 0) == 0) {
        for (int i = 0; i < (int)host::BindAction::Count; ++i)
          if (key == std::string("pad_") + kActionNames[i]) host::g_pad_bindings[0].mask[i] = (unsigned short)std::stoi(value);
      }
      // Current format: "pad<idx>_<Action>" / "gc<idx>_<Action>", idx 0-3.
      else if (key.size() > 4 && key.rfind("pad", 0) == 0 && std::isdigit((unsigned char)key[3])) {
        size_t us = key.find('_');
        if (us != std::string::npos) {
          int idx = std::stoi(key.substr(3, us - 3));
          std::string action = key.substr(us + 1);
          if (idx >= 0 && idx < 4)
            for (int i = 0; i < (int)host::BindAction::Count; ++i)
              if (action == kActionNames[i]) host::g_pad_bindings[idx].mask[i] = (unsigned short)std::stoi(value);
        }
      }
      else if (key.size() > 3 && key.rfind("gc", 0) == 0 && std::isdigit((unsigned char)key[2])) {
        size_t us = key.find('_');
        if (us != std::string::npos) {
          int idx = std::stoi(key.substr(2, us - 2));
          std::string action = key.substr(us + 1);
          if (idx >= 0 && idx < 4)
            for (int i = 0; i < (int)host::BindAction::Count; ++i)
              if (action == kActionNames[i]) host::g_gc_bindings[idx].mask[i] = (unsigned short)std::stoi(value);
        }
      }
      else if (key.size() > 4 && key.rfind("ds4", 0) == 0 && std::isdigit((unsigned char)key[3])) {
        size_t us = key.find('_');
        if (us != std::string::npos) {
          int idx = std::stoi(key.substr(3, us - 3));
          std::string action = key.substr(us + 1);
          if (idx >= 0 && idx < 4)
            for (int i = 0; i < (int)host::BindAction::Count; ++i)
              if (action == kActionNames[i]) host::g_ds4_bindings[idx].mask[i] = (unsigned short)std::stoi(value);
        }
      }
      else if (key.size() > 6 && key.rfind("swpro", 0) == 0 && std::isdigit((unsigned char)key[5])) {
        size_t us = key.find('_');
        if (us != std::string::npos) {
          int idx = std::stoi(key.substr(5, us - 5));
          std::string action = key.substr(us + 1);
          if (idx >= 0 && idx < 4)
            for (int i = 0; i < (int)host::BindAction::Count; ++i)
              if (action == kActionNames[i]) host::g_swpro_bindings[idx].mask[i] = (unsigned short)std::stoi(value);
        }
      }
      // "port<n> <comboIndex>" - comboIndex uses the same 0-9 encoding as the UI combo box.
      else if (key.size() > 4 && key.rfind("port", 0) == 0 && std::isdigit((unsigned char)key[4])) {
        int n = std::stoi(key.substr(4));
        if (n >= 0 && n < 4) host::g_port_sources[n] = combo_to_port_source(std::stoi(value));
      }
    } catch (...) { /* Ignore a malformed preference, retaining the safe default. */ }
  }
}

// The ImGui context and the Win32 platform backend are the same for every renderer backend.
void settings_context_create(void* window, bool open_at_startup) {
  (void)open_at_startup;
  IMGUI_CHECKVERSION(); ImGui::CreateContext();
  auto& io = ImGui::GetIO(); io.IniFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
  ImGui::StyleColorsDark(); ImGui::GetStyle().ScaleAllSizes(1.25f);
  ImGui_ImplWin32_Init(window);
  host::window_set_message_callback([](void* w, uint32_t m, uintptr_t a, intptr_t b) {
    return ImGui_ImplWin32_WndProcHandler((HWND)w, m, a, b) != 0;
  });
}

void settings_context_destroy() {
  host::window_set_message_callback({}); host::window_input_capture(false);
  ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
}

struct PcSettingsUI::Impl {
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
  std::array<bool, 64> used{};
  UINT stride = 0;
  SettingsState state;
};

PcSettingsUI::PcSettingsUI(void* window, ID3D12Device* device, ID3D12CommandQueue* queue, const D3D12Options& options)
    : impl_(std::make_unique<Impl>()) {
  auto& state = *impl_;
  state.state.open = options.settings_open;
  settings_context_create(window, options.settings_open);
  D3D12_DESCRIPTOR_HEAP_DESC desc{}; desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  desc.NumDescriptors = (UINT)state.used.size(); desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&state.heap)))) host::die("PC settings descriptor heap creation failed");
  state.stride = device->GetDescriptorHandleIncrementSize(desc.Type);
  ImGui_ImplDX12_InitInfo info{}; info.Device = device; info.CommandQueue = queue; info.NumFramesInFlight = 3;
  info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM; info.DSVFormat = DXGI_FORMAT_UNKNOWN;
  info.SrvDescriptorHeap = state.heap.Get(); info.UserData = &state;
  info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* i, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
    auto& s = *(Impl*)i->UserData;
    for (size_t slot = 0; slot < s.used.size(); ++slot) if (!s.used[slot]) {
      s.used[slot] = true; *cpu = s.heap->GetCPUDescriptorHandleForHeapStart(); *gpu = s.heap->GetGPUDescriptorHandleForHeapStart();
      cpu->ptr += slot*s.stride; gpu->ptr += slot*s.stride; return;
    }
    host::die("PC settings descriptor heap exhausted");
  };
  info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* i, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE) {
    auto& s = *(Impl*)i->UserData;
    size_t slot = (cpu.ptr-s.heap->GetCPUDescriptorHandleForHeapStart().ptr)/s.stride;
    if (slot < s.used.size()) s.used[slot] = false;
  };
  if (!ImGui_ImplDX12_Init(&info)) host::die("PC settings renderer initialization failed");
}

PcSettingsUI::~PcSettingsUI() {
  ImGui_ImplDX12_Shutdown();
  settings_context_destroy();
}

bool PcSettingsUI::begin(D3D12Options& options) {
  ImGui_ImplDX12_NewFrame();
  return settings_frame(impl_->state, options);
}

bool settings_frame(SettingsState& state, D3D12Options& options) {
  // Dear ImGui's Win32 backend polls XInput itself whenever gamepad navigation is enabled, and maps
  // the Xbox X button to its "menu" key, which pops up ImGui's window switcher for as long as the
  // button is held. Players pressing X mid-match got a little window they could not get rid of.
  // Controller navigation is only wanted while this panel is open, so it is switched off otherwise,
  // before the backend's NewFrame does that polling.
  {
    auto& io = ImGui::GetIO();
    if (state.open) {
      io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    } else {
      io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
      io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    }
  }
  ImGui_ImplWin32_NewFrame();
  host::PadState pad{};
  if (host::window_ui_gamecube_pad(pad)) {
    auto& io = ImGui::GetIO(); io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.AddKeyEvent(ImGuiKey_GamepadStart, (pad.button & 0x1000) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadBack, (pad.button & 0x10) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, (pad.button & 0x100) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadFaceRight, (pad.button & 0x200) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadDpadUp, (pad.button & 8) || pad.stick_y > 40);
    io.AddKeyEvent(ImGuiKey_GamepadDpadDown, (pad.button & 4) || pad.stick_y < -40);
    io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, (pad.button & 1) || pad.stick_x < -40);
    io.AddKeyEvent(ImGuiKey_GamepadDpadRight, (pad.button & 2) || pad.stick_x > 40);
  }
  ImGui::NewFrame();
  if (ImGui::IsKeyPressed(ImGuiKey_F1)) state.open = !state.open;
  if (state.open && ImGui::IsKeyPressed(ImGuiKey_Escape)) state.open = false;
  host::window_input_capture(state.open);
  state.intervals[state.cursor++ % state.intervals.size()] = ImGui::GetIO().DeltaTime*1000.f;
  bool changed = false;
  if (state.open) {
    // Tall enough that the Low spec switch at the end of the settings section is on screen when the
    // panel is first opened, and still short enough for a 768-line laptop display.
    ImGui::SetNextWindowSize(ImVec2(560, 620), ImGuiCond_FirstUseEver);
    ImGui::Begin("PC settings", &state.open, ImGuiWindowFlags_NoCollapse);
    ImGui::TextUnformatted("F1: settings    Escape: return to game");
    ImGui::Separator();
    // Grouped into tabs so the panel is scannable: it had grown to one long column where the
    // audio sliders sat between the sub-frame mode and the visual effects level. Save settings and
    // the version line stay outside the tabs, so Save is reachable from whichever tab is open.
    if (ImGui::BeginTabBar("settings_tabs")) {
      if (ImGui::BeginTabItem("Video")) {
    changed |= ImGui::Checkbox("Borderless fullscreen", &options.fullscreen);
    const double rates[] = {-1, 0, 60, 120, 144, 165, 200, 240, 360, 480};
    const char* names[] = {"Match monitor", "Unlocked", "60", "120", "144", "165", "200", "240", "360", "480"};
    int selected = -1; for (int i = 0; i < 10; ++i) if (options.fps_cap == rates[i]) selected = i;
    if (ImGui::Combo("Frame rate", &selected, names, 10)) { options.fps_cap = rates[selected]; changed = true; }
    changed |= ImGui::Checkbox("VSync", &options.vsync);
    if (ImGui::Checkbox("Widescreen 16:9 (Slippi code, online safe)", &options.widescreen)) {
      if (options.widescreen) options.true_widescreen = false;   // one or the other, never both
      changed = true;
    }
    // True 16:9 widens the frustum here in the renderer instead of running the Gecko code, so the
    // HUD and every 2D element keep the size they were authored at rather than stretching with the
    // frame. Experimental because the game still lays out and culls for 73:60: geometry can be
    // missing or pop in at the new edges, which is the part only play testing finds.
    if (ImGui::Checkbox("True 16:9 (experimental, no game code)", &options.true_widescreen)) {
      if (options.true_widescreen) options.widescreen = false;
      changed = true;
    }
    if (options.true_widescreen)
      ImGui::TextDisabled("Widens the camera in the renderer: the HUD stays the right size and nothing is\n"
                          "written to game memory, so it cannot desync. Watch the edges for missing or\n"
                          "popping scenery, which is what makes this experimental.");
    float win_w = ImGui::GetIO().DisplaySize.x, win_h = ImGui::GetIO().DisplaySize.y;

    // Aspect ratio and window size are presentation only: they change nothing the game computes,
    // so they cannot desync and the two players in a match may each pick their own.
    {
      const char* aspects[] = {"Auto (Melee's own: 73:60, or 16:9 with widescreen on)",
                               "73:60 (Melee's native)", "4:3", "16:9", "Stretch to window (no black bars)"};
      int index = std::clamp((int)options.aspect, 0, 4);
      if (ImGui::Combo("Aspect ratio", &index, aspects, 5)) { options.aspect = (AspectMode)index; changed = true; }
      if (options.aspect == AspectMode::Stretch)
        ImGui::TextDisabled("Fills the whole window or screen, so the picture is stretched. Pick a 4:3 window\n"
                            "size below and a wider screen to get the stretched resolution players use.");
      else
        ImGui::TextDisabled("Melee's camera asks for 73:60, not 4:3; the Slippi widescreen code widens it to 16:9.\n"
                            "Auto follows the checkbox above, which is what Slippi Dolphin does.");

      // Window size, the way Dolphin lets a player choose one. 4:3 sizes first: those are what
      // Melee players run (1440x1080 is the common one), then the 16:9 sizes.
      struct Size { int w, h; const char* name; };
      static const Size kSizes[] = {
        {0, 0, nullptr},                     // slot 0 is the "follow window" label, built below
        {640, 480, "640x480 (4:3)"},         {960, 720, "960x720 (4:3)"},
        {1280, 960, "1280x960 (4:3)"},       {1440, 1080, "1440x1080 (4:3)"},
        {1600, 1200, "1600x1200 (4:3)"},     {1920, 1440, "1920x1440 (4:3)"},
        {1280, 720, "1280x720 (16:9)"},      {1920, 1080, "1920x1080 (16:9)"},
        {2560, 1440, "2560x1440 (16:9)"},
      };
      const int kPresets = (int)(sizeof kSizes / sizeof kSizes[0]);
      char follow[80], custom[80];
      std::snprintf(follow, sizeof follow, "Follow window (now %dx%d)", (int)win_w, (int)win_h);
      std::snprintf(custom, sizeof custom, "Custom (%dx%d)", options.window_w, options.window_h);
      const char* items[kPresets + 1];
      items[0] = follow;
      for (int i = 1; i < kPresets; ++i) items[i] = kSizes[i].name;
      int count = kPresets, size_index = 0;
      if (options.window_pinned) {
        size_index = -1;
        for (int i = 1; i < kPresets; ++i)
          if (kSizes[i].w == options.window_w && kSizes[i].h == options.window_h) size_index = i;
        if (size_index < 0) { items[kPresets] = custom; count = kPresets + 1; size_index = kPresets; }
      }
      const bool full = options.fullscreen || host::window_is_fullscreen();
      if (full) ImGui::BeginDisabled();
      if (ImGui::Combo("Window size", &size_index, items, count)) {
        if (size_index == 0) options.window_pinned = false;
        else if (size_index < kPresets) {
          options.window_pinned = true;
          options.window_w = kSizes[size_index].w; options.window_h = kSizes[size_index].h;
          host::window_set_client_size(options.window_w, options.window_h);
        }
        changed = true;
      }
      if (full) { ImGui::EndDisabled(); ImGui::TextDisabled("Fullscreen uses the whole screen. Stretch above fills it; the other aspects add bars."); }
      // A window bigger than the desktop cannot be shown with its title bar on screen, so Windows
      // (and the clamp in window_set_client_size) gives back a smaller one. The player can also just
      // have dragged the edge since. Either way, say what the window actually is.
      else if (options.window_pinned && ((int)win_w != options.window_w || (int)win_h != options.window_h))
        ImGui::TextDisabled("Picked %dx%d, window is %dx%d (dragged, or capped to your desktop).\nFullscreen is never capped.",
                            options.window_w, options.window_h, (int)win_w, (int)win_h);
      ImGui::TextDisabled("Aspect ratio and window size only change how the picture is fitted to your screen.\n"
                          "They cannot desync, and your opponent can be on different ones.");
    }

    // Same numbers Dolphin shows (EFB 640x528 per multiplier). Auto = the smallest multiplier
    // whose 640x480 image covers the window, like Dolphin's "Auto (Window Size)".
    float aspect = presented_aspect(options, (int)win_w, (int)win_h);
    float vw = win_w, vh = win_w / aspect; if (vh > win_h) { vh = win_h; vw = win_h * aspect; }
    int auto_scale = std::clamp(std::max((int)std::ceil(vw / (480.0f * aspect)), (int)std::ceil(vh / 480.0f)), 1, 8);
    char auto_label[64]; std::snprintf(auto_label, sizeof auto_label, "Auto (%dx = %dx%d for this window)", auto_scale, 640 * auto_scale, 528 * auto_scale);
    const char* scales[] = {auto_label, "Native (640x528)", "2x (1280x1056) for 720p", "3x (1920x1584) for 1080p", "4x (2560x2112) for 1440p",
                            "5x (3200x2640)", "6x (3840x3168) for 4K", "7x (4480x3696)", "8x (5120x4224)"};
    // DLSS chooses its own render size, so the internal resolution and supersampling settings do
    // nothing while it is on and are shown greyed out. DLAA renders at the internal resolution the
    // player picked and only anti-aliases it, so it leaves both of them working.
    const bool dlss_picks_resolution = options.dlss_mode >= 2;
    if (dlss_picks_resolution) ImGui::BeginDisabled();
    changed |= ImGui::Combo("Internal resolution", &options.efb_scale, scales, 9);
    // DLSS (DLAA included) does its own anti-aliasing, so supersampling on top would render larger
    // for a second pass over the same edges: grey it out rather than let the two stack.
    const char* aa[] = {"None", "4x SSAA (supersampling)"};
    int aa_index = options.ssaa == 2 ? 1 : 0;
    if (options.dlss_mode && !dlss_picks_resolution) ImGui::BeginDisabled();
    if (ImGui::Combo("Anti-aliasing", &aa_index, aa, 2)) { options.ssaa = aa_index ? 2 : 1; changed = true; }
    if (options.dlss_mode && !dlss_picks_resolution) ImGui::EndDisabled();
    if (dlss_picks_resolution) ImGui::EndDisabled();
    const char* anis[] = {"1x", "2x", "4x", "8x", "16x"};
    int an_index = options.anisotropy >= 16 ? 4 : options.anisotropy >= 8 ? 3 : options.anisotropy >= 4 ? 2 : options.anisotropy >= 2 ? 1 : 0;
    if (ImGui::Combo("Anisotropic filtering", &an_index, anis, 5)) { options.anisotropy = 1 << an_index; changed = true; }
    // Creating a device and a swapchain on another API means restarting; the choice is saved and
    // read again at the next launch (see load_pc_settings and --backend).
    const char* backends[] = {"Direct3D 12 (default)", "Direct3D 11 (older GPUs and drivers)"};
    int api_index = options.api == RenderApi::D3D11 ? 1 : 0;
    if (ImGui::Combo("Graphics backend", &api_index, backends, 2)) { options.api = api_index ? RenderApi::D3D11 : RenderApi::D3D12; changed = true; }
    ImGui::TextDisabled("Takes effect at the next launch: save settings, then restart.");
    const bool d3d11 = options.api == RenderApi::D3D11;
    const char* upscalers[] = {"Native", "DLAA", "DLSS Quality", "DLSS Balanced", "DLSS Performance", "DLSS Ultra Performance"};
    if (d3d11) ImGui::BeginDisabled();
    if (ImGui::Combo("Upscaling (NVIDIA DLSS)", &options.dlss_mode, upscalers, 6)) changed = true;
    if (d3d11) { ImGui::EndDisabled(); ImGui::TextDisabled("DLSS needs Direct3D 12 and an NVIDIA GPU."); }
    if (options.dlss_mode == 1) {
      ImGui::TextWrapped("DLAA anti-aliases the game at the Internal resolution above without changing it, then the picture is fitted to the window as usual. Internal resolution and Anti-aliasing keep working, so DLAA stacks with 4x SSAA if you want both.");
    } else if (dlss_picks_resolution) {
      static const char* ratios[] = {"", "", "67% (Quality)", "58% (Balanced)", "50% (Performance)", "33% (Ultra Performance)"};
      ImGui::TextWrapped("DLSS renders the game at %s of the window size (at 1080p about 1280x960) and upscales it. That is what DLSS is for in heavy games; Melee is cheap to render, so here it is a downgrade in sharpness, and Internal resolution and Anti-aliasing above are ignored while it is on. For the sharpest image choose Native, set Internal resolution to 3x or higher and Anti-aliasing to 4x SSAA (the Dolphin look), or choose DLAA (full resolution, DLSS used only as anti-aliasing).", ratios[options.dlss_mode]);
    }
    int sharp = (int)std::lround(options.sharpness * 100.0f);
    if (ImGui::SliderInt("Sharpening", &sharp, 0, 100, "%d%%")) { options.sharpness = sharp / 100.0f; changed = true; }
    const char* subframe_modes[] = {"Off (60 Hz poses only)", "Predict ahead (no delay, can overshoot on speed changes)", "Interpolate (exact, one frame of delay)"};
    int sf = options.subframe == SubFrameMode::Off ? 0 : options.subframe == SubFrameMode::AuthoredInterpolate ? 2 : 1;
    if (ImGui::Combo("Sub-frame animation", &sf, subframe_modes, 3)) { options.subframe = sf == 0 ? SubFrameMode::Off : sf == 2 ? SubFrameMode::AuthoredInterpolate : SubFrameMode::Authored; changed = true; }
    if (sf == 1) ImGui::TextWrapped("Samples supported animation beyond the latest pose. Sudden stops can require correction.");
    if (sf == 2) ImGui::TextWrapped("Samples between completed poses. This adds up to one simulation tick of visual delay; unsupported motion may hold.");
    {
      const char* levels[] = {"Full", "Reduced (no sparks or glow)", "Minimal (no translucent effects)"};
      ImGui::SetNextItemWidth(260);
      changed |= ImGui::Combo("Visual effects", &options.effects_level, levels, 3);
      if (options.effects_level > 0 && ImGui::IsItemHovered())
        ImGui::SetTooltip("Skips decorative draws to raise frame rate on slower machines.\nDisplay only: safe online, and players may use different settings.");
    }

    // ---- Low spec ----
    // One switch for every setting above that costs frames. Turning it on remembers what the player
    // had; turning it off puts exactly that back, not a hardcoded default. Both halves are saved, so
    // the switch and the remembered settings survive a restart. Everything it changes takes effect
    // immediately except the graphics backend, which needs a new device and so a new launch.
    {
      bool low = options.low_spec;
      if (ImGui::Checkbox("Low spec", &low)) {
        if (low) {
          options.low_spec_previous = {options.api, options.fps_cap, options.efb_scale, options.ssaa,
                                       options.anisotropy, options.effects_level, options.dlss_mode, options.subframe};
          if (d3d11_available()) options.api = RenderApi::D3D11;   // the better exercised driver path on old integrated GPUs
          options.fps_cap = 60;
          options.efb_scale = 1;                  // native 640x528, the floor
          options.ssaa = 1;                       // no supersampling
          options.anisotropy = 1;                 // no anisotropic filtering
          options.effects_level = 2;              // skip sparks, glow and overlay draws
          options.dlss_mode = 0;                  // NVIDIA and Direct3D 12 only
          options.subframe = SubFrameMode::Off;   // the sub-frame solver is the largest CPU cost here
        } else {
          const auto& p = options.low_spec_previous;
          options.api = p.api; options.fps_cap = p.fps_cap; options.efb_scale = p.efb_scale;
          options.ssaa = p.ssaa; options.anisotropy = p.anisotropy; options.effects_level = p.effects_level;
          options.dlss_mode = p.dlss_mode; options.subframe = p.subframe;
        }
        options.low_spec = low;
        changed = true;
      }
      ImGui::SameLine();
      ImGui::TextDisabled("For integrated graphics and older laptops.");
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Puts internal resolution, anti-aliasing, anisotropic filtering, visual effects,\n"
                          "sub-frame animation, DLSS and the frame cap at their cheapest settings.\n"
                          "Every control above keeps working, and turning this off puts back exactly\n"
                          "what you had before rather than the defaults.");
      // The backend is the one thing here that cannot change while the game is running.
      const RenderApi running = state.running_d3d11 ? RenderApi::D3D11 : RenderApi::D3D12;
      if (options.api != running)
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "Graphics backend: %s at the next launch. Save settings, then restart.",
                           options.api == RenderApi::D3D11 ? "Direct3D 11" : "Direct3D 12");
    }
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Audio")) {
    int music = slippi::jukebox::user_volume();
    if (ImGui::SliderInt("Music", &music, 0, 100, "%d%%")) slippi::jukebox::set_user_volume(music);
    state.volume = host::audio_volume();
    if (ImGui::SliderInt("Volume", &state.volume, 0, 100, "%d%%")) host::audio_set_volume(state.volume);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Overlays")) {
    ImGui::Checkbox("Performance overlay", &options.performance_overlay);
    changed |= ImGui::Checkbox("Controller overlay", &options.input_overlay);
    if (options.input_overlay) {
      // Several ports can be shown at once (doubles and crew streams want every player visible);
      // they stack upward from the bottom left corner.
      for (int i = 0; i < 4; ++i) {
        ImGui::SameLine();
        char label[16];
        std::snprintf(label, sizeof label, "P%d", i + 1);
        bool on = (options.input_overlay_ports & (1 << i)) != 0;
        if (ImGui::Checkbox(label, &on)) {
          options.input_overlay_ports = on ? (options.input_overlay_ports | (1 << i)) : (options.input_overlay_ports & ~(1 << i));
          changed = true;
        }
      }
      ImGui::SameLine();
      changed |= ImGui::Checkbox("Hide border", &options.input_overlay_hide_border);
      ImGui::TextDisabled("  Drag an overlay to move it, and its edges to resize, while this panel is open.");
    }
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Controls")) {

    // ---- Controls (rebinding) ----
    ImGui::TextUnformatted("Controls");
    ImGui::TextWrapped("Pick a device tab to rebind its actions. Each tab's top line shows what that device is pressing right now; the Port assignment section below shows what actually reaches the game.");

    static const int kDeviceTabCount = 17;
    static const char* kDeviceTabNames[kDeviceTabCount] = {
      "Keyboard", "XInput Pad 1", "XInput Pad 2", "XInput Pad 3", "XInput Pad 4",
      "DS4 1", "DS4 2", "DS4 3", "DS4 4",
      "GC Adapter 1", "GC Adapter 2", "GC Adapter 3", "GC Adapter 4",
      "Switch Pro 1", "Switch Pro 2", "Switch Pro 3", "Switch Pro 4"
    };

    host::InputDebugSnapshot snap;
    host::input_debug_snapshot(snap);

    if (ImGui::BeginTabBar("device_tabs")) {
      for (int tab = 0; tab < kDeviceTabCount; ++tab) {
        if (!ImGui::BeginTabItem(kDeviceTabNames[tab])) continue;

        host::CaptureDevice tab_kind;
        int tab_index = 0;
        if (tab == 0) { tab_kind = host::CaptureDevice::Keyboard; }
        else if (tab <= 4) { tab_kind = host::CaptureDevice::XInputPad; tab_index = tab - 1; }
        else if (tab <= 8) { tab_kind = host::CaptureDevice::DS4Pad; tab_index = tab - 5; }
        else if (tab <= 12) { tab_kind = host::CaptureDevice::GCAdapter; tab_index = tab - 9; }
        else { tab_kind = host::CaptureDevice::SwitchPro; tab_index = tab - 13; }

        // Live "what's this device pressing right now" line.
        if (tab_kind == host::CaptureDevice::Keyboard) {
          ImGui::TextColored(ImVec4(0.25f, 0.85f, 0.35f, 1.0f), "[+] Connected");
          ImGui::SameLine();
          ImGui::Text("Active: %s", active_actions_label(snap.keyboard_actions).c_str());
        } else if (tab_kind == host::CaptureDevice::XInputPad) {
          const bool connected = snap.xinput_connected[tab_index];
          ImGui::TextColored(connected ? ImVec4(0.25f, 0.85f, 0.35f, 1.0f) : ImVec4(0.95f, 0.3f, 0.25f, 1.0f),
                             connected ? "[+] Connected" : "[-] Not connected");
          ImGui::SameLine();
          ImGui::Text("Active: %s", active_actions_label(snap.xinput_actions[tab_index]).c_str());
        } else if (tab_kind == host::CaptureDevice::DS4Pad) {
          const bool connected = snap.ds4_connected[tab_index];
          ImGui::TextColored(connected ? ImVec4(0.25f, 0.85f, 0.35f, 1.0f) : ImVec4(0.95f, 0.3f, 0.25f, 1.0f),
                             connected ? "[+] Connected" : "[-] Not connected");
          ImGui::SameLine();
          ImGui::Text("Active: %s", active_actions_label(snap.ds4_actions[tab_index]).c_str());
        } else if (tab_kind == host::CaptureDevice::SwitchPro) {
          const bool connected = snap.swpro_connected[tab_index];
          ImGui::TextColored(connected ? ImVec4(0.25f, 0.85f, 0.35f, 1.0f) : ImVec4(0.95f, 0.3f, 0.25f, 1.0f),
                             connected ? "[+] Connected" : "[-] Not connected");
          ImGui::SameLine();
          ImGui::Text("Active: %s", active_actions_label(snap.swpro_actions[tab_index]).c_str());
          ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.2f, 1.0f), "Experimental: written from the protocol documentation and never tried");
          ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.2f, 1.0f), "against a real controller. If it does not work, Steam Input or BetterJoy");
          ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.2f, 1.0f), "present the pad as an Xbox controller, which the XInput tabs above handle.");
        } else {
          bool plugged = (snap.gc_mask & (1u << tab_index)) != 0;
          ImGui::TextColored(plugged ? ImVec4(0.25f, 0.85f, 0.35f, 1.0f) : ImVec4(0.95f, 0.3f, 0.25f, 1.0f),
                             plugged ? "[+] Plugged in" : "[-] Not plugged in");
          ImGui::SameLine();
          ImGui::Text("Active: %s", active_actions_label(snap.gc_actions[tab_index]).c_str());
        }
        ImGui::Separator();

        for (int i = 0; i < (int)host::BindAction::Count; ++i) {
          ImGui::PushID(i);
          bool this_row_capturing = state.rebind_action == i && state.rebind_kind == tab_kind && state.rebind_index == tab_index;
          if (this_row_capturing) {
            ImGui::Text("%-8s Press a key or button (Esc to cancel)...", kActionNames[i]);
            host::CaptureDevice dev; int value; int device_index;
            if (host::input_poll_capture(dev, value, device_index)) {
              if (dev == host::CaptureDevice::None) {
                state.rebind_action = -1;   // Escape cancelled; binding unchanged.
              } else if (dev == tab_kind && (tab_kind == host::CaptureDevice::Keyboard || device_index == tab_index)) {
                if (dev == host::CaptureDevice::Keyboard) host::g_key_bindings.vk[i] = value;
                else if (dev == host::CaptureDevice::XInputPad) host::g_pad_bindings[tab_index].mask[i] = (unsigned short)value;
                else if (dev == host::CaptureDevice::DS4Pad) host::g_ds4_bindings[tab_index].mask[i] = (unsigned short)value;
                else if (dev == host::CaptureDevice::SwitchPro) host::g_swpro_bindings[tab_index].mask[i] = (unsigned short)value;
                else host::g_gc_bindings[tab_index].mask[i] = (unsigned short)value;
                state.rebind_action = -1;
                changed = true;
              } else {
                // A different device than the one being rebound fired (e.g. you bumped
                // the keyboard while rebinding Xbox Pad 2). Ignore it, keep listening.
                host::input_begin_capture();
              }
            }
          } else {
            char label[32];
            if (tab_kind == host::CaptureDevice::Keyboard) format_key_label(host::g_key_bindings.vk[i], label, sizeof label);
            else if (tab_kind == host::CaptureDevice::XInputPad) std::snprintf(label, sizeof label, "%s", xinput_button_name(host::g_pad_bindings[tab_index].mask[i]));
            else if (tab_kind == host::CaptureDevice::DS4Pad) std::snprintf(label, sizeof label, "%s", ds4_button_name(host::g_ds4_bindings[tab_index].mask[i]));
            else if (tab_kind == host::CaptureDevice::SwitchPro) std::snprintf(label, sizeof label, "%s", swpro_button_name(host::g_swpro_bindings[tab_index].mask[i]));
            else std::snprintf(label, sizeof label, "%s", gc_button_name(host::g_gc_bindings[tab_index].mask[i]));
            ImGui::Text("%-8s %-10s", kActionNames[i], label);
            ImGui::SameLine();
            if (ImGui::Button("Rebind")) {
              host::input_begin_capture();
              state.rebind_action = i; state.rebind_kind = tab_kind; state.rebind_index = tab_index;
            }
          }
          ImGui::PopID();
        }
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
    ImGui::Separator();

    // ---- Port assignment + live per-port output ----
    ImGui::TextUnformatted("Port assignment");
    ImGui::TextWrapped("Which physical device feeds each of the 4 in-game controller ports.");
    for (int port = 0; port < 4; ++port) {
      ImGui::PushID(100 + port);
      int combo = port_source_to_combo(host::g_port_sources[port]);
      char port_label[16]; std::snprintf(port_label, sizeof port_label, "Port %d", port + 1);
      if (ImGui::Combo(port_label, &combo, kPortSourceNames, kPortSourceCount)) {
        host::g_port_sources[port] = combo_to_port_source(combo);
        changed = true;
      }
      const host::PortSource& source = host::g_port_sources[port];
      bool source_available = false;
      const char* source_status = "[-] Disconnected";
      if (source.kind == host::DeviceKind::Keyboard) {
        source_available = true;
        source_status = "[+] Connected";
      } else if (source.kind == host::DeviceKind::XInputPad && source.index >= 0 && source.index < 4) {
        source_available = snap.xinput_connected[source.index];
        source_status = source_available ? "[+] Connected" : "[-] Disconnected";
      } else if (source.kind == host::DeviceKind::DS4Pad && source.index >= 0 && source.index < 4) {
        source_available = snap.ds4_connected[source.index];
        source_status = source_available ? "[+] Connected" : "[-] Disconnected";
      } else if (source.kind == host::DeviceKind::SwitchPro && source.index >= 0 && source.index < 4) {
        source_available = snap.swpro_connected[source.index];
        source_status = source_available ? "[+] Connected" : "[-] Disconnected";
      } else if (source.kind == host::DeviceKind::GCAdapter && source.index >= 0 && source.index < 4) {
        source_available = (snap.gc_mask & (1u << source.index)) != 0;
        source_status = source_available ? "[+] Plugged in" : "[-] Disconnected";
      }
      ImGui::SameLine();
      ImGui::TextColored(source_available ? ImVec4(0.25f, 0.85f, 0.35f, 1.0f) : ImVec4(0.95f, 0.3f, 0.25f, 1.0f), "%s", source_status);
      const host::PadState& p = snap.ports[port];
      ImGui::TextDisabled("  Output: %-16s Stick %4d,%4d  C-Stick %4d,%4d  L/R %3d/%3d",
                           active_pad_buttons_label(p.button).c_str(), p.stick_x, p.stick_y, p.sub_x, p.sub_y, p.trig_l, p.trig_r);
      ImGui::PopID();
    }
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Game")) {

    // ---- L-cancel helpers ----
    // The indicator reads the fighter's action state and never writes anything, so it is display
    // only and safe in every mode. The automatic press is a real analog trigger press injected into
    // the local pad before the game reads it, so it is transmitted like any other input and both
    // clients compute the same landing lag: it cannot desync. It is still gated to offline and
    // Direct because it is a fairness question, not a safety one.
    ImGui::TextUnformatted("L-cancel");
    {
      bool indicator = lcancel::indicator_enabled();
      if (ImGui::Checkbox("Flash red on missed L-cancel", &indicator)) lcancel::set_indicator(indicator);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Flashes the fighter red when an aerial lands without the landing lag halved.\n"
                          "Display only: the tint is applied by the renderer and never written into the\n"
                          "game, so it is safe in every mode. It is skipped while auto L-cancel is doing\n"
                          "the press for you, since there is then nothing to report.");
      bool automatic = lcancel::automatic_enabled();
      if (ImGui::Checkbox("Auto L-cancel", &automatic)) lcancel::set_automatic(automatic);
      ImGui::SameLine();
      ImGui::TextDisabled("(NOTE: Will not work in Unranked or Ranked, only offline and direct)");
      if (automatic) {
        ImGui::TextWrapped("Presses the analog trigger for you during an aerial. It is a real input, sent over the "
                           "network like any other, so it cannot desync. In a Direct match both players should agree "
                           "to use it: it is a fairness question, not a safety one.");
        if (const char* mode = lcancel::auto_suppressed_mode())
          ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "Disabled right now: this is %s.", mode);
      }
    }


    // ---- Discord presence ----
    // Off by default, and inert without an application ID. Nothing reaches Discord until the box
    // below is ticked. See scratchpad/discord_invite_design.md for the whole design.
    ImGui::TextUnformatted("Discord");
    char app_id[32];
    std::snprintf(app_id, sizeof app_id, "%s", options.discord_app_id.c_str());
    if (ImGui::InputText("Application ID", app_id, sizeof app_id, ImGuiInputTextFlags_CharsDecimal)) {
      options.discord_app_id = app_id;
      host::discord::configure(options.discord_app_id);
    }
    const bool discord_was = options.discord_presence;
    ImGui::Checkbox("Discord presence (show what you are playing; friends can press Join)", &options.discord_presence);
    if (options.discord_presence != discord_was) {
      host::discord::configure(options.discord_app_id);
      host::discord::enable(options.discord_presence);   // starts or stops one background thread
    }
    if (options.discord_presence) {
      ImGui::TextWrapped("%s", host::discord::status().c_str());
      ImGui::TextDisabled("A new Application ID is picked up the next time you switch this off and on.");
      ImGui::TextWrapped("Your Slippi connect code is published as the join secret so a friend who presses Join gets it filled in under Online > Direct. Your IP address is never published. Rich Presence is visible to anyone who can see your Discord profile.");
    } else {
      ImGui::TextDisabled("Off. Nothing is sent to Discord. Needs an Application ID from discord.com/developers/applications.");
    }

    ImGui::Checkbox("Open this panel at startup", &options.settings_open);
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }

    ImGui::Separator();

    {
      auto st = host::updater::state();
      if (st == host::updater::State::Idle) host::updater::check(MELEE_PORT_VERSION);
      ImGui::Text("Version %s. %s", MELEE_PORT_VERSION, host::updater::message().c_str());
      if (st == host::updater::State::UpdateAvailable) { ImGui::SameLine(); if (ImGui::Button("Update and restart")) host::updater::download_and_install(); }
      if (st == host::updater::State::Failed) { ImGui::SameLine(); if (ImGui::Button("Retry")) host::updater::check(MELEE_PORT_VERSION); }
    }
    ImGui::Separator();
    if (ImGui::Button("Save settings")) {
      std::filesystem::path path(options.settings_path), temporary = path; temporary += ".tmp";
      std::ofstream file(temporary);
      file << "fps " << options.fps_cap << "\nscale " << options.efb_scale << "\nfullscreen " << options.fullscreen
           << "\nvsync " << options.vsync << "\nwidescreen " << options.widescreen
           << "\ntruewidescreen " << options.true_widescreen << "\naspect " << (int)options.aspect
           << "\nwindow " << (options.window_pinned ? std::to_string(options.window_w) + "x" + std::to_string(options.window_h) : std::string("follow"))
           << "\nvolume " << state.volume << "\nperformance " << options.performance_overlay
           << "\ndlss " << options.dlss_mode << "\nbackend " << (options.api == RenderApi::D3D11 ? "d3d11" : "d3d12")
           << "\nsharpness " << options.sharpness << "\nanisotropy " << options.anisotropy << "\nssaa " << options.ssaa
           << "\nsubframe " << (options.subframe == SubFrameMode::Off ? 0 : options.subframe == SubFrameMode::AuthoredInterpolate ? 2 : 1) << "\nmusic " << slippi::jukebox::user_volume()
           << "\nstartup " << (options.settings_open ? 1 : 0)
           << "\ninputoverlay " << options.input_overlay << "\ninputoverlayports " << options.input_overlay_ports
           << "\ninputoverlayhideborder " << options.input_overlay_hide_border
           << "\neffects " << options.effects_level
           // Low spec: the switch, and the settings it is holding for the player while it is on.
           << "\nlowspec " << (options.low_spec ? 1 : 0)
           << "\nlowspec_prev_backend " << (options.low_spec_previous.api == RenderApi::D3D11 ? "d3d11" : "d3d12")
           << "\nlowspec_prev_fps " << options.low_spec_previous.fps_cap
           << "\nlowspec_prev_scale " << options.low_spec_previous.efb_scale
           << "\nlowspec_prev_ssaa " << options.low_spec_previous.ssaa
           << "\nlowspec_prev_anisotropy " << options.low_spec_previous.anisotropy
           << "\nlowspec_prev_effects " << options.low_spec_previous.effects_level
           << "\nlowspec_prev_dlss " << options.low_spec_previous.dlss_mode
           << "\nlowspec_prev_subframe " << (options.low_spec_previous.subframe == SubFrameMode::Off ? 0 : options.low_spec_previous.subframe == SubFrameMode::AuthoredInterpolate ? 2 : 1)
           << "\nlcancelindicator " << (lcancel::indicator_enabled() ? 1 : 0)
           << "\nautolcancel " << (lcancel::automatic_enabled() ? 1 : 0)
           << "\ndiscord " << (options.discord_presence ? 1 : 0);
      // Only when set: "key value" parsing would swallow the next line on an empty value.
      if (!options.discord_app_id.empty()) file << "\ndiscord_app_id " << options.discord_app_id;
      for (int i = 0; i < (int)host::BindAction::Count; ++i)
        file << "\nkey_" << kActionNames[i] << " " << host::g_key_bindings.vk[i];
      for (int idx = 0; idx < 4; ++idx)
        for (int i = 0; i < (int)host::BindAction::Count; ++i)
          file << "\npad" << idx << "_" << kActionNames[i] << " " << host::g_pad_bindings[idx].mask[i];
      for (int idx = 0; idx < 4; ++idx)
        for (int i = 0; i < (int)host::BindAction::Count; ++i)
          file << "\ngc" << idx << "_" << kActionNames[i] << " " << host::g_gc_bindings[idx].mask[i];
      for (int idx = 0; idx < 4; ++idx)
        for (int i = 0; i < (int)host::BindAction::Count; ++i)
          file << "\nds4" << idx << "_" << kActionNames[i] << " " << host::g_ds4_bindings[idx].mask[i];
      for (int idx = 0; idx < 4; ++idx)
        for (int i = 0; i < (int)host::BindAction::Count; ++i)
          file << "\nswpro" << idx << "_" << kActionNames[i] << " " << host::g_swpro_bindings[idx].mask[i];
      for (int n = 0; n < 4; ++n)
        file << "\nport" << n << " " << port_source_to_combo(host::g_port_sources[n]);
      file << '\n';
      file.close();
      state.saved = file.good() && MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
    ImGui::SameLine(); if (ImGui::Button("Return to game")) state.open = false;
    // Quitting from here shuts down the same way closing the window does, so the replay is finalised,
    // the pipeline cache is written and the adapter is released rather than left mid-stream.
    ImGui::SameLine();
    if (ImGui::Button("Quit game")) host::request_exit(0);
    if (state.saved) ImGui::TextUnformatted("Settings saved");
    ImGui::End();
  }
  if (!state.open) {
    // Keep the closed state passive: opening is intentionally F1-only so controller
    // navigation cannot activate a settings button by accident.
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 12, 12), ImGuiCond_Always, ImVec2(1, 0));
    ImGui::SetNextWindowBgAlpha(ImGui::GetTime() < 20.0 ? 0.8f : 0.35f);
    ImGui::Begin("SettingsButton", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted("Settings: F1");
    ImGui::End();
  }
  if (options.input_overlay) {
    const int mask = options.input_overlay_ports ? options.input_overlay_ports : 1;
    const bool lone = (mask & (mask - 1)) == 0;   // exactly one port selected
    int row = 0;
    for (int i = 0; i < 4; ++i)
      if (mask & (1 << i)) draw_input_overlay(i, row++, lone, state.open, options.input_overlay_hide_border);
  }
  draw_lcancel_overlays();
  if (options.performance_overlay) {
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.75f);
    ImGui::Begin("Performance", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);
    ImGui::Text("%.0f presentations/s | %.2f ms", ImGui::GetIO().Framerate, 1000.f/std::max(1.f, ImGui::GetIO().Framerate));
    ImGui::PlotLines("##frametimes", state.intervals.data(), (int)state.intervals.size(), state.cursor % state.intervals.size(), nullptr, 0, 33.4f, ImVec2(250, 60));
    ImGui::End();
  }
  host::window_input_capture(state.open);
  ImGui::Render();
  return changed;
}

void PcSettingsUI::draw(ID3D12GraphicsCommandList* list) {
  ID3D12DescriptorHeap* heap = impl_->heap.Get(); list->SetDescriptorHeaps(1, &heap);
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), list);
}
}
