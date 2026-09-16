// SPDX-License-Identifier: GPL-2.0-or-later
#include "pc_settings.h"
#include "jukebox.h"
#include "window.h"
#include "audio.h"
#include "host.h"
#include "input_bindings.h"
#include "updater.h"
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
      else if (key == "startup") options.settings_open = value != "0";
      else if (key == "dlss") { int m = std::stoi(value); if (m >= 0 && m <= 5) options.dlss_mode = m; }
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

struct PcSettingsUI::Impl {
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
  std::array<bool, 64> used{};
  UINT stride = 0;
  bool open = false, saved = false;
  int volume = 0;
  std::array<float, 180> intervals{};
  unsigned cursor = 0;
  int rebind_action = -1;                                         // index into BindAction while a "press a button" capture is in progress, -1 = none
  host::CaptureDevice rebind_kind = host::CaptureDevice::None;     // which device tab the in-progress capture belongs to
  int rebind_index = 0;                                            // XInput pad / GC adapter port index for that tab (unused for Keyboard)
};

PcSettingsUI::PcSettingsUI(void* window, ID3D12Device* device, ID3D12CommandQueue* queue, const D3D12Options& options)
    : impl_(std::make_unique<Impl>()) {
  auto& state = *impl_;
  state.open = options.settings_open;
  IMGUI_CHECKVERSION(); ImGui::CreateContext();
  auto& io = ImGui::GetIO(); io.IniFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
  ImGui::StyleColorsDark(); ImGui::GetStyle().ScaleAllSizes(1.25f);
  D3D12_DESCRIPTOR_HEAP_DESC desc{}; desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  desc.NumDescriptors = (UINT)state.used.size(); desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&state.heap)))) host::die("PC settings descriptor heap creation failed");
  state.stride = device->GetDescriptorHandleIncrementSize(desc.Type);
  ImGui_ImplWin32_Init(window);
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
  host::window_set_message_callback([](void* w, uint32_t m, uintptr_t a, intptr_t b) {
    return ImGui_ImplWin32_WndProcHandler((HWND)w, m, a, b) != 0;
  });
}

PcSettingsUI::~PcSettingsUI() {
  host::window_set_message_callback({}); host::window_input_capture(false);
  ImGui_ImplDX12_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
}

bool PcSettingsUI::begin(D3D12Options& options) {
  auto& state = *impl_;
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
  ImGui_ImplDX12_NewFrame(); ImGui_ImplWin32_NewFrame();
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
    ImGui::SetNextWindowSize(ImVec2(560, 560), ImGuiCond_FirstUseEver);
    ImGui::Begin("PC settings", &state.open, ImGuiWindowFlags_NoCollapse);
    ImGui::TextUnformatted("F1: settings    Escape: return to game");
    ImGui::Separator();
    changed |= ImGui::Checkbox("Borderless fullscreen", &options.fullscreen);
    const double rates[] = {-1, 0, 60, 120, 144, 165, 200, 240, 360, 480};
    const char* names[] = {"Match monitor", "Unlocked", "60", "120", "144", "165", "200", "240", "360", "480"};
    int selected = -1; for (int i = 0; i < 10; ++i) if (options.fps_cap == rates[i]) selected = i;
    if (ImGui::Combo("Frame rate", &selected, names, 10)) { options.fps_cap = rates[selected]; changed = true; }
    changed |= ImGui::Checkbox("VSync", &options.vsync);
    changed |= ImGui::Checkbox("Widescreen 16:9 (Slippi code, online safe)", &options.widescreen);
    // Same numbers Dolphin shows (EFB 640x528 per multiplier). Auto = the smallest multiplier
    // whose 640x480 image covers the window, like Dolphin's "Auto (Window Size)".
    float win_w = ImGui::GetIO().DisplaySize.x, win_h = ImGui::GetIO().DisplaySize.y;
    float aspect = options.widescreen ? 16.0f / 9.0f : 4.0f / 3.0f;
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
    const char* upscalers[] = {"Native", "DLAA", "DLSS Quality", "DLSS Balanced", "DLSS Performance", "DLSS Ultra Performance"};
    if (ImGui::Combo("Upscaling (NVIDIA DLSS)", &options.dlss_mode, upscalers, 6)) changed = true;
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
    int music = slippi::jukebox::user_volume();
    if (ImGui::SliderInt("Music", &music, 0, 100, "%d%%")) slippi::jukebox::set_user_volume(music);
    state.volume = host::audio_volume();
    if (ImGui::SliderInt("Volume", &state.volume, 0, 100, "%d%%")) host::audio_set_volume(state.volume);
    {
      const char* levels[] = {"Full", "Reduced (no sparks or glow)", "Minimal (no translucent effects)"};
      ImGui::SetNextItemWidth(260);
      changed |= ImGui::Combo("Visual effects", &options.effects_level, levels, 3);
      if (options.effects_level > 0 && ImGui::IsItemHovered())
        ImGui::SetTooltip("Skips decorative draws to raise frame rate on slower machines.\nDisplay only: safe online, and players may use different settings.");
    }
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
    ImGui::Checkbox("Open this panel at startup", &options.settings_open);
    ImGui::Separator();

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
           << "\nvsync " << options.vsync << "\nwidescreen " << options.widescreen << "\nvolume " << state.volume << "\nperformance " << options.performance_overlay
           << "\ndlss " << options.dlss_mode << "\nsharpness " << options.sharpness << "\nanisotropy " << options.anisotropy << "\nssaa " << options.ssaa
           << "\nsubframe " << (options.subframe == SubFrameMode::Off ? 0 : options.subframe == SubFrameMode::AuthoredInterpolate ? 2 : 1) << "\nmusic " << slippi::jukebox::user_volume()
           << "\nstartup " << (options.settings_open ? 1 : 0)
           << "\ninputoverlay " << options.input_overlay << "\ninputoverlayports " << options.input_overlay_ports
           << "\ninputoverlayhideborder " << options.input_overlay_hide_border
           << "\neffects " << options.effects_level;
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
