// SPDX-License-Identifier: GPL-2.0-or-later
#include "pc_settings.h"
#include "gx_streamline.h"
#ifdef GX_DLSS5
#include "gx_dlss5.h"
#endif
#include <unordered_map>
#include "texture_pack.h"
#include "video_background.h"
#include <atomic>
#include <memory>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <cfloat>
#include <sstream>
#include <sstream>
#include "pc_settings_shared.h"
#include "ui_sources/gd_melee/motion.h"
#include "radial_navigation.h"
#include "gx_backend.h"
#include "jukebox.h"
#include "window.h"
#include "audio.h"
#include "host.h"
#include "input_bindings.h"
#include "lcancel.h"
#include "user_gecko.h"
#include "gecko_data.h"
#include "slippi_online.h"
#include "native_practice.h"
#include "hid_pad.h"
#include "updater.h"
#include "discord_presence.h"
#include "controller_profiles.h"
#include "cosmetic_mods.h"
#ifndef MELEE_PORT_VERSION
#define MELEE_PORT_VERSION "dev"
#endif
#include "imgui.h"
#include "imgui_internal.h"
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"
#include <windows.h>
#include <xinput.h>
#include <mmsystem.h>
#include <mmsystem.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <functional>
#include <map>
#include <string>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <cmath>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
namespace gx {
namespace {
std::atomic<uint32_t> g_guest_options_selection{0};
std::atomic<bool> g_guest_options_open_request{false};
bool g_menu_style_gallery_open = false;
std::unordered_map<std::string, std::unique_ptr<ImTextureData>> g_cosmetic_previews;

std::string ui_source_asset_path_uncached(const char* source, const char* filename);
// Resolved once per asset. The GD Melee page asks for its font atlas and artwork dozens of times a
// frame; resolving each call meant two filesystem checks and a module-path lookup every time,
// which held that page near 80 fps with spikes past 200 ms.
std::string ui_source_asset_path(const char* source, const char* filename) {
  static std::unordered_map<std::string, std::string> resolved;
  std::string key(source);
  key += '/';
  key += filename;
  auto found = resolved.find(key);
  if (found != resolved.end()) return found->second;
  return resolved.emplace(std::move(key), ui_source_asset_path_uncached(source, filename)).first->second;
}

std::string ui_source_asset_path_uncached(const char* source, const char* filename) {
  const std::filesystem::path source_asset =
      std::filesystem::path("port/runtime/gx/ui_sources") / source / "assets" / filename;
  if (std::filesystem::exists(source_asset)) return source_asset.string();
  wchar_t module[MAX_PATH]{};
  const DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH);
  if (length > 0 && length < MAX_PATH) {
    const std::filesystem::path packaged =
        std::filesystem::path(module).parent_path() / "ui_sources" / source / filename;
    if (std::filesystem::exists(packaged)) return packaged.string();
  }
  return source_asset.string();
}

ImTextureData* cosmetic_preview(const std::string& path) {
  if (path.empty()) return nullptr;
  auto cached = g_cosmetic_previews.find(path);
  if (cached != g_cosmetic_previews.end()) return cached->second.get();
  std::unique_ptr<ImTextureData> texture;
  std::ifstream file(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
  if (file && file.tellg() > 0 && file.tellg() <= 16 * 1024 * 1024) {
    const size_t size = (size_t)file.tellg();
    std::vector<unsigned char> encoded(size);
    file.seekg(0);
    if (file.read((char*)encoded.data(), size)) {
      int width = 0, height = 0, channels = 0;
      if (stbi_info_from_memory(encoded.data(), (int)size, &width, &height, &channels) &&
          width > 0 && height > 0 && (int64_t)width * height <= 4'000'000) {
        unsigned char* pixels = stbi_load_from_memory(encoded.data(), (int)size,
                                                       &width, &height, &channels, 4);
        if (pixels) {
          texture = std::make_unique<ImTextureData>();
          texture->Create(ImTextureFormat_RGBA32, width, height);
          std::memcpy(texture->Pixels, pixels, (size_t)width * height * 4);
          texture->UseColors = true;
          ImGui::RegisterUserTexture(texture.get());
          stbi_image_free(pixels);
        }
      }
    }
  }
  auto* result = texture.get();
  g_cosmetic_previews.emplace(path, std::move(texture));
  return result;
}

void draw_cosmetic_preview(const host::cosmetics::AssetInfo& asset) {
  if (auto* preview = cosmetic_preview(asset.preview_path)) {
    const float scale = std::min(280.0f / preview->Width, 170.0f / preview->Height);
    ImGui::Image(preview->GetTexRef(), ImVec2(preview->Width * scale, preview->Height * scale));
  }
}
}

void settings_guest_options_frame(uint8_t menu, uint16_t selection, uint32_t buttons) {
  static uint8_t previous_menu = 0xff;
  if (menu != previous_menu && menu == 4)
    host::log("pc settings: original Options menu detected; PC Settings row enabled");
  previous_menu = menu;
  g_guest_options_selection.store(menu == 4 ? (uint32_t)selection + 1 : 0,
                                  std::memory_order_relaxed);
  if (menu == 4 && selection == 3 && (buttons & 0x10)) {
    g_guest_options_open_request.store(true, std::memory_order_release);
    host::log("pc settings: original Options row selected; opening PC Settings");
  }
}
namespace {
float g_overlay_game_aspect = 73.0f / 60.0f;
std::array<HudPlayerSnapshot, 4> g_hud_players{};
std::array<std::string, 4> g_hud_player_names{};
bool g_hud_match = false;

struct OverlayBounds { float left, top, right, bottom; };

OverlayBounds overlay_bounds() {
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const float aspect = std::max(0.1f, g_overlay_game_aspect);
  float width = display.x, height = width / aspect;
  if (height > display.y) { height = display.y; width = height * aspect; }
  const float x = (display.x - width) * 0.5f;
  const float y = (display.y - height) * 0.5f;
  return {x, y, x + width, y + height};
}

void clamp_overlay_window() {
  const OverlayBounds bounds = overlay_bounds();
  const ImVec2 pos = ImGui::GetWindowPos();
  const ImVec2 size = ImGui::GetWindowSize();
  const float max_x = std::max(bounds.left, bounds.right - size.x);
  const float max_y = std::max(bounds.top, bounds.bottom - size.y);
  const ImVec2 clamped(std::clamp(pos.x, bounds.left, max_x), std::clamp(pos.y, bounds.top, max_y));
  if (clamped.x != pos.x || clamped.y != pos.y) ImGui::SetWindowPos(clamped);
}
}

void settings_set_game_aspect(float aspect) {
  if (std::isfinite(aspect) && aspect > 0.1f) g_overlay_game_aspect = aspect;
}

void settings_set_hud_snapshot(const Frame& frame) {
  g_hud_match = frame_in_match(frame);
  g_hud_players = frame.hud_players;
  g_hud_player_names = frame.player_names;
}

static void draw_player_nicknames() {
  if (!g_hud_match) return;
  const OverlayBounds bounds = overlay_bounds();
  const float width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
  const float font_size = std::clamp(height / 27.0f, 15.0f, 24.0f);
  static constexpr ImU32 colors[4] = {
    IM_COL32(255, 99, 107, 255), IM_COL32(83, 164, 255, 255),
    IM_COL32(255, 211, 84, 255), IM_COL32(93, 220, 143, 255)};
  ImDrawList* draw = ImGui::GetForegroundDrawList();
  draw->PushClipRect(ImVec2(bounds.left, bounds.top), ImVec2(bounds.right, bounds.bottom), true);
  for (int slot = 0; slot < 4; ++slot) {
    const auto& player = g_hud_players[slot];
    if (!player.tag_visible || g_hud_player_names[slot].empty()) continue;
    const std::string label = "P" + std::to_string(slot + 1) + "  " + g_hud_player_names[slot];
    const ImVec2 text_size = ImGui::GetFont()->CalcTextSizeA(font_size, FLT_MAX, 0.0f, label.c_str());
    const float x = std::clamp(bounds.left + player.tag_x / 640.0f * width - text_size.x * 0.5f,
                               bounds.left + 4.0f, std::max(bounds.left + 4.0f, bounds.right - text_size.x - 4.0f));
    const float y = std::clamp(bounds.top + player.tag_y / 480.0f * height - font_size * 2.4f,
                               bounds.top + 4.0f, bounds.bottom - font_size - 8.0f);
    draw->AddRectFilled(ImVec2(x - 5, y - 3), ImVec2(x + text_size.x + 5, y + font_size + 4),
                        IM_COL32(9, 11, 22, 215), 5.0f);
    draw->AddRect(ImVec2(x - 5, y - 3), ImVec2(x + text_size.x + 5, y + font_size + 4),
                  colors[slot], 5.0f, 0, 1.5f);
    draw->AddText(ImGui::GetFont(), font_size, ImVec2(x, y), IM_COL32(255, 255, 255, 255), label.c_str());
  }
  draw->PopClipRect();
}

// Names used both when drawing the Controls list and when saving/loading bindings
// to port-settings.ini (as "key_<name>" / "pad<idx>_<name>" / "gc<idx>_<name>" lines).
// Order must match host::BindAction exactly.
static const char* kActionNames[(size_t)host::BindAction::Count] = {
  "A", "B", "X", "Y", "Z", "Start", "L", "R", "DUp", "DDown", "DLeft", "DRight",
  "CUp", "CDown", "CLeft", "CRight",
  // Added after the control stick became rebindable. A settings file written before that has no
  // these lines, so the arrow key defaults stand and nobody's setup changes on upgrade.
  "SUp", "SDown", "SLeft", "SRight"
};
// The same, as the controls picture writes them.
static const char* kActionTitles[(size_t)host::BindAction::Count] = {
  "A", "B", "X", "Y", "Z", "Start", "L", "R", "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right",
  "C Up", "C Down", "C Left", "C Right",
  "Stick Up", "Stick Down", "Stick Left", "Stick Right"
};

// ---- Port-source <-> combo-box index, shared by load/save and the Port assignment UI ----
// 0 = None, 1 = Keyboard, 2..5 = XInput, 6..9 = DS4, 10..13 = GC Adapter, 14..17 = Switch Pro,
// 18..21 = generic HID (B0XX, Frame1, vJoy and any other pad without a reader of its own).
// Saved settings store the index, so new devices are appended and the existing ones never move.
// Switch Pro says "experimental" because it has never been tried against the hardware.
static const int kPortSourceCount = 22;
static const char* kPortSourceNames[kPortSourceCount] = {
  "None", "Keyboard",
  "XInput Pad 1", "XInput Pad 2", "XInput Pad 3", "XInput Pad 4",
  "PlayStation 1", "PlayStation 2", "PlayStation 3", "PlayStation 4",
  "GC Adapter 1", "GC Adapter 2", "GC Adapter 3", "GC Adapter 4",
  "Switch Pro 1 (experimental)", "Switch Pro 2 (experimental)",
  "Switch Pro 3 (experimental)", "Switch Pro 4 (experimental)",
  "HID Pad 1", "HID Pad 2", "HID Pad 3", "HID Pad 4"
};

static int port_source_to_combo(const host::PortSource& s) {
  switch (s.kind) {
    case host::DeviceKind::Keyboard:  return 1;
    case host::DeviceKind::XInputPad: return 2 + std::clamp(s.index, 0, 3);
    case host::DeviceKind::DS4Pad:     return 6 + std::clamp(s.index, 0, 3);
    case host::DeviceKind::GCAdapter: return 10 + std::clamp(s.index, 0, 3);
    case host::DeviceKind::SwitchPro: return 14 + std::clamp(s.index, 0, 3);
    case host::DeviceKind::HidPad:    return 18 + std::clamp(s.index, 0, 3);
    case host::DeviceKind::None: default: return 0;
  }
}

static host::PortSource combo_to_port_source(int idx) {
  if (idx == 1) return { host::DeviceKind::Keyboard, 0 };
  if (idx >= 2 && idx <= 5) return { host::DeviceKind::XInputPad, idx - 2 };
  if (idx >= 6 && idx <= 9) return { host::DeviceKind::DS4Pad, idx - 6 };
  if (idx >= 10 && idx <= 13) return { host::DeviceKind::GCAdapter, idx - 10 };
  if (idx >= 14 && idx <= 17) return { host::DeviceKind::SwitchPro, idx - 14 };
  if (idx >= 18 && idx <= 21) return { host::DeviceKind::HidPad, idx - 18 };
  return { host::DeviceKind::None, 0 };
}

// ---- per-family stick options (deadzones, C-stick mode), saved as deadzone_<family>_main etc ----
static const char* kFamilyKeys[(size_t)host::PadFamily::Count] = {"gamecube", "xbox", "playstation", "switch", "box"};

static bool load_family_option(const std::string& key, const std::string& value) {
  for (int f = 0; f < (int)host::PadFamily::Count; ++f) {
    const std::string k = kFamilyKeys[f];
    if (key == "deadzone_" + k + "_main") { host::g_deadzones[f].main = std::clamp(std::atoi(value.c_str()), 0, 100); return true; }
    if (key == "deadzone_" + k + "_c") { host::g_deadzones[f].c = std::clamp(std::atoi(value.c_str()), 0, 100); return true; }
  }
  return false;
}

static std::string family_options_text() {
  std::string out;
  for (int f = 0; f < (int)host::PadFamily::Count; ++f) {
    const std::string k = kFamilyKeys[f];
    out += "\ndeadzone_" + k + "_main " + std::to_string(host::g_deadzones[f].main);
    out += "\ndeadzone_" + k + "_c " + std::to_string(host::g_deadzones[f].c);
  }
  return out;
}

// ---- binding -> label helpers, used by the Controls tabs ----
// A generic HID pad publishes no names for its buttons, only numbers, so that is what is shown.
// The number is the device's own: button 1 is what the device calls button 1.
static std::string hid_button_name(uint32_t mask) {
  if (!mask) return "Unbound";
  if (mask == host::HID_HAT_UP) return "Hat Up";
  if (mask == host::HID_HAT_DOWN) return "Hat Down";
  if (mask == host::HID_HAT_LEFT) return "Hat Left";
  if (mask == host::HID_HAT_RIGHT) return "Hat Right";
  int bit = 0;
  while (bit < 31 && !(mask & (1u << bit))) ++bit;
  return "Button " + std::to_string(bit + 1);
}

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
    if (host::kActionPadBit[i] && host::kActionPadBit[i] == mask) return kActionTitles[i];
  return "?";
}

// Space-joined list of action names whose bit is set, for the live "what's this
// device pressing right now" overlay lines. Bit i corresponds to BindAction i.
static std::string active_actions_label(uint32_t bits) {
  std::string s;
  for (int i = 0; i < (int)host::BindAction::Count; ++i)
    if (bits & (uint32_t)(1u << i)) { if (!s.empty()) s += " "; s += kActionNames[i]; }
  return s.empty() ? std::string("-") : s;
}

// Same idea but decoding a final PadState.button field, which uses the GC-native
// bits (kActionPadBit), not the BindAction-bit-index encoding active_actions_label
// reads -- used for the per-port output overlay.
static std::string active_pad_buttons_label(uint16_t button) {
  std::string s;
  for (int i = 0; i < (int)host::BindAction::Count; ++i)
    if (host::kActionPadBit[i] && (button & host::kActionPadBit[i])) { if (!s.empty()) s += " "; s += kActionNames[i]; }
  return s.empty() ? std::string("-") : s;
}

// On-screen controller display for streaming: the octagonal gate, C-stick, analog triggers and the
// face buttons, drawn from the state the game read on its last PADRead rather than a fresh poll, so
// it shows what the game acted on and device polling stays on one thread at one rate.
// `row` stacks several overlays upward so two to four players can be shown at once. `editable` is on
// while the settings panel is open, which is when the overlay may be dragged and resized.
static void draw_input_overlay(int port, int row, bool lone, bool editable, bool borderless, bool values, int knob) {
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

  // Taller with the stick values on, so the numbers get their own row under the sticks.
  const float pad_w = 300.f, pad_h = values ? 150.f : 132.f;
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
  const OverlayBounds bounds = overlay_bounds();
  ImGui::SetNextWindowPos(ImVec2(bounds.left + 16, bounds.bottom - 16 - row * (pad_h + 6)), ImGuiCond_FirstUseEver, ImVec2(0, 1));
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
  clamp_overlay_window();
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
  // Scaled like the settings panel's sticks: 80 is the game's full tilt, so a stick at the rim reads
  // at the rim. It used to be 128, the raw range, which drew a full press at under two thirds out.
  auto stick = [&](ImVec2 c, float r, int8_t sx, int8_t sy, ImU32 colour) {
    dl->AddNgon(c, r, colour, 8, S(2.f));
    dl->AddCircle(c, S(2.f), dim, 8, 1.f);
    float fx = sx / 80.f, fy = sy / 80.f;
    const float mag = std::sqrt(fx * fx + fy * fy);
    if (mag > 1.f) { fx /= mag; fy /= mag; }
    // Sized off the gate; "Stick size" 5 is the look the overlay has always had (a fifth of the gate).
    const float knob_r = std::max(S(3.f), r * 0.04f * std::clamp(knob, 1, 10));
    // The knob is drawn as a circle around its centre, so at a full press the centre alone reaching
    // the gate's radius let the far side of a big knob bulge past the outline. Pull the centre's own
    // travel in by the knob's radius so the whole knob stays inside the gate and just touches the
    // edge at full tilt, rather than overlapping it -- the larger "Stick size" is, the more this matters.
    const float reach = std::max(0.f, r - knob_r);
    const ImVec2 tip(c.x + fx * reach, c.y - fy * reach);
    dl->AddCircleFilled(tip, knob_r, colour, 20);
  };
  stick(P(52, 74), gate, pad.stick_x, pad.stick_y, line);
  stick(P(146, 82), cgate, pad.sub_x, pad.sub_y, yellow);
  // The values the game works with: 1/80 steps, the vector clamped to length 1 as the game does,
  // so a full press reads 1.0000 and any B0XX or GRAM coordinate can be checked against its chart.
  if (values) {
    auto text = [&](float x, int8_t sx, int8_t sy, ImU32 colour) {
      float fx = sx / 80.f, fy = sy / 80.f;
      const float mag = std::sqrt(fx * fx + fy * fy);
      if (mag > 1.f) { fx /= mag; fy /= mag; }
      char s[32];
      std::snprintf(s, sizeof s, "%.4f %.4f", fx, fy);
      dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * k, P(x, 134), colour, s);
    };
    text(6, pad.stick_x, pad.stick_y, line);
    text(150, pad.sub_x, pad.sub_y, yellow);
  }

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

// ---- Controls: one device's bindings, read and written the same way whatever the device ----
// The binding tables differ per family (a key code for the keyboard, a button mask of one width or
// another for each pad), so every place that touched them used to repeat the same six-way branch.

static uint32_t binding_get(host::CaptureDevice kind, int index, int action) {
  switch (kind) {
    case host::CaptureDevice::Keyboard:  return (uint32_t)host::g_key_bindings.vk[action];
    case host::CaptureDevice::XInputPad: return host::g_pad_bindings[index].mask[action];
    case host::CaptureDevice::DS4Pad:    return host::g_ds4_bindings[index].mask[action];
    case host::CaptureDevice::SwitchPro: return host::g_swpro_bindings[index].mask[action];
    case host::CaptureDevice::HidPad:    return host::g_hid_bindings[index].mask[action];
    case host::CaptureDevice::GCAdapter: return host::g_gc_bindings[index].mask[action];
    default: return 0;
  }
}

static void binding_set(host::CaptureDevice kind, int index, int action, uint32_t value) {
  switch (kind) {
    case host::CaptureDevice::Keyboard:  host::g_key_bindings.vk[action] = (int)value; break;
    case host::CaptureDevice::XInputPad: host::g_pad_bindings[index].mask[action] = (unsigned short)value; break;
    case host::CaptureDevice::DS4Pad:    host::g_ds4_bindings[index].mask[action] = (unsigned short)value; break;
    case host::CaptureDevice::SwitchPro: host::g_swpro_bindings[index].mask[action] = (unsigned short)value; break;
    case host::CaptureDevice::HidPad:    host::g_hid_bindings[index].mask[action] = value; break;
    case host::CaptureDevice::GCAdapter: host::g_gc_bindings[index].mask[action] = (unsigned short)value; break;
    default: break;
  }
}

static std::string binding_label(host::CaptureDevice kind, int index, int action) {
  const uint32_t v = binding_get(kind, index, action);
  char label[32];
  switch (kind) {
    case host::CaptureDevice::Keyboard:  format_key_label((int)v, label, sizeof label); return label;
    case host::CaptureDevice::XInputPad: return xinput_button_name((unsigned short)v);
    case host::CaptureDevice::DS4Pad:    return ds4_button_name((unsigned short)v);
    case host::CaptureDevice::SwitchPro: return swpro_button_name((unsigned short)v);
    case host::CaptureDevice::HidPad:    return hid_button_name(v);
    case host::CaptureDevice::GCAdapter: return gc_button_name((unsigned short)v);
    default: return "?";
  }
}

static host::ProfileDevice profile_device_for(host::CaptureDevice kind) {
  switch (kind) {
    case host::CaptureDevice::Keyboard:  return host::ProfileDevice::Keyboard;
    case host::CaptureDevice::XInputPad: return host::ProfileDevice::XInput;
    case host::CaptureDevice::DS4Pad:    return host::ProfileDevice::PlayStation;
    case host::CaptureDevice::SwitchPro: return host::ProfileDevice::SwitchPro;
    case host::CaptureDevice::HidPad:    return host::ProfileDevice::Hid;
    default:                             return host::ProfileDevice::GCAdapter;
  }
}

// Which actions this device is pressing right now (bit i = BindAction i).
static uint32_t live_actions(const host::InputDebugSnapshot& snap, host::CaptureDevice kind, int index) {
  switch (kind) {
    case host::CaptureDevice::Keyboard:  return snap.keyboard_actions;
    case host::CaptureDevice::XInputPad: return snap.xinput_actions[index];
    case host::CaptureDevice::DS4Pad:    return snap.ds4_actions[index];
    case host::CaptureDevice::SwitchPro: return snap.swpro_actions[index];
    case host::CaptureDevice::HidPad:    return snap.hid_actions[index];
    case host::CaptureDevice::GCAdapter: return snap.gc_actions[index];
    default: return 0;
  }
}

// Saved profiles, listed once and then again only after a save or delete, or every few seconds:
// listing reads the folder, and this runs on every frame the Controls tab is drawn.
static const std::vector<std::string>& cached_profiles(host::ProfileDevice device, bool refresh) {
  static std::vector<std::string> lists[(int)host::ProfileDevice::Count];
  static double listed_at[(int)host::ProfileDevice::Count] = {};
  const int d = (int)device;
  const double now = ImGui::GetTime();
  if (refresh || listed_at[d] == 0.0 || now - listed_at[d] > 3.0) { lists[d] = host::profile_list(device); listed_at[d] = now > 0.0 ? now : 1e-9; }
  return lists[d];
}

// Load, save and delete named layouts for this kind of device. Returns true when the bindings of the
// device on this tab changed.
// ---- automatic profiles ----
// A player's layout is never lost: whenever a controller's buttons differ from its defaults they are
// kept in a profile ("Profile 1" unless the player loaded or saved another one), and "Default" in the
// profile list puts the defaults back without touching that profile. Device numbers are the panel's
// (0 keyboard, 1-4 Xbox, 5-8 PlayStation, 9-12 adapter, 13-16 Switch, 17-20 box/HID).
constexpr int kDeviceTabs = 21;
static host::ProfileBindings g_default_bindings[kDeviceTabs];   // captured before the settings file is read
static bool g_defaults_captured = false;
static std::string g_active_profile[kDeviceTabs];
// Whether a named profile (rather than "Default") is the active selection. Kept separate from
// comparing live bindings to the defaults: a profile whose saved buttons happen to equal the
// defaults (a fresh "New" profile nobody has rebound yet) must still show and highlight as itself,
// not silently read back as "Default" the next time the panel draws. Zero-initialised false, which
// is correct at first boot: nothing has been picked yet, so "Default" is what is showing.
static bool g_named_profile_active[kDeviceTabs];

static host::CaptureDevice tab_kind_of(int tab, int* index) {
  *index = 0;
  if (tab == 0) return host::CaptureDevice::Keyboard;
  if (tab <= 4) { *index = tab - 1; return host::CaptureDevice::XInputPad; }
  if (tab <= 8) { *index = tab - 5; return host::CaptureDevice::DS4Pad; }
  if (tab <= 12) { *index = tab - 9; return host::CaptureDevice::GCAdapter; }
  if (tab <= 16) { *index = tab - 13; return host::CaptureDevice::SwitchPro; }
  *index = tab - 17; return host::CaptureDevice::HidPad;
}
static host::ProfileBindings bindings_of(int tab) {
  int index; const host::CaptureDevice kind = tab_kind_of(tab, &index);
  host::ProfileBindings b{};
  for (int i = 0; i < (int)host::BindAction::Count; ++i) b[i] = binding_get(kind, index, i);
  return b;
}
static void set_bindings_of(int tab, const host::ProfileBindings& b) {
  int index; const host::CaptureDevice kind = tab_kind_of(tab, &index);
  for (int i = 0; i < (int)host::BindAction::Count; ++i) binding_set(kind, index, i, b[i]);
}
static void capture_default_bindings() {
  if (g_defaults_captured) return;
  for (int t = 0; t < kDeviceTabs; ++t) g_default_bindings[t] = bindings_of(t);
  g_defaults_captured = true;
}
static bool bindings_are_default(int tab) { return bindings_of(tab) == g_default_bindings[tab]; }
static std::string active_profile_name(int tab) { return g_active_profile[tab].empty() ? std::string("Profile 1") : g_active_profile[tab]; }
static host::ProfileDevice profile_device_of_tab(int tab) { int index; return profile_device_for(tab_kind_of(tab, &index)); }
// The first "Profile N" not yet used for this kind of controller.
static std::string next_free_profile_name(host::ProfileDevice device) {
  const std::vector<std::string> list = host::profile_list(device);
  for (int n = 1;; ++n) {
    const std::string name = "Profile " + std::to_string(n);
    if (std::find(list.begin(), list.end(), name) == list.end()) return name;
  }
}
static const std::vector<std::string>& cached_profiles(host::ProfileDevice device, bool refresh);
// Keeps a changed layout in the device's profile. Called whenever its buttons change in the panel.
static void autosave_profile(int tab) {
  if (!g_defaults_captured || bindings_are_default(tab)) return;
  const host::ProfileDevice device = profile_device_of_tab(tab);
  host::profile_save(device, active_profile_name(tab), bindings_of(tab));
  g_named_profile_active[tab] = true;
  cached_profiles(device, true);
}
// Upgrading: layouts an older version saved (in port-settings.ini, before profiles existed or before
// the player made one) become Profile 1, Profile 2 ... for their kind of controller, once.
static void migrate_profiles() {
  for (int d = 0; d < (int)host::ProfileDevice::Count; ++d) {
    const host::ProfileDevice device = (host::ProfileDevice)d;
    if (!host::profile_list(device).empty()) continue;
    std::vector<host::ProfileBindings> saved;
    for (int t = 0; t < kDeviceTabs; ++t) {
      if (profile_device_of_tab(t) != device || bindings_are_default(t)) continue;
      const host::ProfileBindings b = bindings_of(t);
      auto it = std::find(saved.begin(), saved.end(), b);
      if (it == saved.end()) {
        const std::string name = "Profile " + std::to_string(saved.size() + 1);
        if (host::profile_save(device, name, b)) host::log("controls: kept this controller's saved buttons as %s", name.c_str());
        saved.push_back(b);
        g_active_profile[t] = name;
        g_named_profile_active[t] = true;
      } else {
        g_active_profile[t] = "Profile " + std::to_string(it - saved.begin() + 1);
        g_named_profile_active[t] = true;
      }
    }
  }
}

// The player's own video settings, kept as the "Custom" quality preset: updated whenever the settings
// match none of the presets, so picking a preset and then Custom puts them back (and the two can be
// compared by switching). Saved as "custompreset efb ssaa aniso dlss fps subframe".
struct CustomPreset { bool set = false; int efb = 0, ssaa = 1, aniso = 16, dlss = 0; double fps = -1; int sub = 1; bool dlss5 = false; };
static CustomPreset g_custom_preset;
// The panel has saved which user Gecko codes are on (until then GeckoCodes.ini's own list is used).
static bool g_gecko_chosen = false;

// The controller last shown in the Controls tab (its device number), so the tab opens on it again
// rather than on whatever plays as port 1. -1: nothing saved yet.
static int g_saved_edit_tab = -1;

// Width left on the current row after the last item, so a hint that would be cut off is left out.
static float room_after_last_item() {
  return ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - ImGui::GetItemRectMax().x;
}

static bool draw_profile_row(host::CaptureDevice kind, int index, int tab) {
  static char new_name[48];
  static std::string status[32];
  const host::ProfileDevice device = profile_device_for(kind);
  bool changed = false;
  const std::vector<std::string>& list = cached_profiles(device, false);
  // What the dropdown shows: whichever entry the player picked, not whether the live buttons happen
  // to match the factory defaults right now (a profile can legitimately hold default-equal buttons).
  const bool is_default = !g_named_profile_active[tab];
  const std::string current = is_default ? std::string("Default") : active_profile_name(tab);

  // One line: the layout in use (picking another loads it at once), a new one, delete.
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Profile");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(220.0f);
  if (ImGui::BeginCombo("##profile", current.c_str())) {
    // The defaults, always one click away. The player's own profile is left as it is, and changes
    // made after this go to a new profile, so neither layout is lost.
    if (ImGui::Selectable("Default", is_default)) {
      set_bindings_of(tab, g_default_bindings[tab]);
      g_active_profile[tab] = next_free_profile_name(device);
      g_named_profile_active[tab] = false;
      status[tab] = "Back to the default buttons.";
      changed = true;
    }
    for (const std::string& name : list) {
      if (ImGui::Selectable(name.c_str(), !is_default && name == current)) {
        host::ProfileBindings pb{};
        if (host::profile_load(device, name, pb)) {
          for (int i = 0; i < (int)host::BindAction::Count; ++i) binding_set(kind, index, i, pb[i]);
          g_active_profile[tab] = name;
          g_named_profile_active[tab] = true;
          status[tab] = "Loaded " + name + ".";
          changed = true;
        } else {
          status[tab] = "Could not read " + name + ".";
        }
      }
    }
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Pick a layout to use it. Every change you make is saved to the profile shown here.\n"
                      "Profiles belong to a kind of controller: the same number is a different button\n"
                      "on a different controller.");
  ImGui::SameLine();
  if (ImGui::Button("New")) { std::snprintf(new_name, sizeof new_name, "%s", next_free_profile_name(device).c_str()); ImGui::OpenPopup("new_profile"); }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keep these buttons as a new profile, under a name you choose.");
  ImGui::SameLine();
  ImGui::BeginDisabled(is_default || std::find(list.begin(), list.end(), current) == list.end());
  if (ImGui::Button("Delete")) {
    status[tab] = host::profile_delete(device, current) ? "Deleted " + current + "." : "Could not delete " + current + ".";
    set_bindings_of(tab, g_default_bindings[tab]);
    g_active_profile[tab].clear();
    cached_profiles(device, true);
    g_active_profile[tab] = next_free_profile_name(device);
    g_named_profile_active[tab] = false;
    changed = true;
  }
  ImGui::EndDisabled();
  if (ImGui::BeginPopup("new_profile")) {
    ImGui::TextUnformatted("Name");
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    const bool enter = ImGui::InputText("##new_name", new_name, sizeof new_name, ImGuiInputTextFlags_EnterReturnsTrue);
    const std::string clean = host::profile_clean_name(new_name);
    ImGui::BeginDisabled(clean.empty());
    if ((ImGui::Button("Save") || enter) && !clean.empty()) {
      status[tab] = host::profile_save(device, clean, bindings_of(tab)) ? "Saved " + clean + "." : "Could not save to " + host::profiles_folder() + ".";
      g_active_profile[tab] = clean;
      g_named_profile_active[tab] = true;
      cached_profiles(device, true);
      changed = true;   // the profile in use is kept in the settings file
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
  }
  {
    char hint[160];
    if (!status[tab].empty()) std::snprintf(hint, sizeof hint, "%s", status[tab].c_str());
    else if (is_default) std::snprintf(hint, sizeof hint, "Change any button and it is kept as %s.", active_profile_name(tab).c_str());
    else std::snprintf(hint, sizeof hint, "Changes save automatically.");
    if (room_after_last_item() > ImGui::CalcTextSize(hint).x + 16) { ImGui::SameLine(); ImGui::TextDisabled("%s", hint); }
  }
  return changed;
}

// The GameCube controller to rebind by pointing at it, laid out the way Smash Ultimate's button
// settings screen is: the controller in the middle of a light panel, callout boxes grouped in grey
// panels around it, each joined to its part by a cyan line. Click a part or its box to bind it,
// right-click to clear. A part lights while pressed; the box being bound turns purple.
// Authored on a 960x392 canvas and scaled to the panel's width.
struct GcCallout { int action; float x, y, w; };
// The keyboard has two rows of direction boxes under the pad: the C-stick, and the control stick
// (added once the stick stopped being hard wired to the arrow keys).
constexpr float kGcCanvasHKeys = 434, kGcCanvasHAnalog = 350;
enum { kCStickModeBox = 100 };   // the single C-stick box of an analog controller: shows and toggles the mode

// The deadzone as the settings picture shows it: an orange ring sized to the deadzone, and a dot at
// the stick's real position, orange while it is inside the ring (where the game reads the stick as
// centred), white outside. `raw` and `dz` are in stick units over 80, the full throw of a stick.
// Shown only while a deadzone slider is being moved, and for a moment after (these hold until when).
static double g_show_dz_main_until = 0, g_show_dz_c_until = 0;
static bool inside_deadzone(ImVec2 raw, float dz) { return dz > 0 && raw.x * raw.x + raw.y * raw.y < dz * dz; }
static void draw_deadzone_view(ImDrawList* dl, ImVec2 c, float reach, ImVec2 raw, float dz, float k, bool c_stick) {
  if (ImGui::GetTime() > (c_stick ? g_show_dz_c_until : g_show_dz_main_until)) return;
  const ImU32 zone = IM_COL32(255, 140, 40, 255);
  if (dz > 0) {
    dl->AddCircleFilled(c, dz * reach, IM_COL32(255, 140, 40, 60), 40);
    dl->AddCircle(c, dz * reach, zone, 40, 2.0f * k);
  }
  const ImVec2 p(c.x + raw.x * reach, c.y - raw.y * reach);
  dl->AddCircleFilled(p, 4.5f * k, inside_deadzone(raw, dz) ? zone : IM_COL32(255, 255, 255, 255), 16);
  dl->AddCircle(p, 4.5f * k, IM_COL32(20, 20, 24, 255), 16, 1.5f * k);
}

static int draw_gc_bind_picture(uint32_t live, int capturing, int* right_clicked, int* hovered_out,
                                const std::function<std::string(int)>& label, bool analog_c,
                                const char* c_mode_text, ImVec2 stick_pos, ImVec2 c_pos, float dz_main, float dz_c) {
  using A = host::BindAction;
  const float avail = ImGui::GetContentRegionAvail().x;
  const float k = std::clamp(avail / 960.0f, 0.5f, 1.4f);
  const ImVec2 o = ImGui::GetCursorScreenPos();
  const float kGcCanvasH = analog_c ? kGcCanvasHAnalog : kGcCanvasHKeys;
  ImGui::InvisibleButton("gc_picture", ImVec2(960.0f * k, kGcCanvasH * k));
  const bool canvas_hovered = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  auto P = [&](float x, float y) { return ImVec2(o.x + x * k, o.y + y * k); };
  const ImVec2 mouse = ImGui::GetMousePos();
  const float pulse = 0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 6.0f);
  const ImU32 cyan = IM_COL32(40, 200, 225, 255);

  // Light panel behind everything, as Ultimate's screen.
  dl->AddRectFilled(P(0, 0), P(960, kGcCanvasH), IM_COL32(222, 223, 229, 255), 12 * k);

  // ---- the controller ----
  // Traced, not drawn by eye: the outline is the silhouette of the controller in a 1200x675 capture
  // of Ultimate's button settings screen (dark body pixels, overlays closed over, boundary walked
  // and simplified), and every part sits where it measures in that same capture. Coordinates are
  // that capture's pixels, mapped onto the canvas by C().
  constexpr float kS = 0.85f;
  auto C = [&](float x, float y) { return P(480 + (x - 599) * kS, 50 + (y - 130) * kS); };
  const float kc = kS * k;   // a controller-space length in screen pixels
  // The left half, from the top centre round the stick's side and handle, up the handle's inner
  // edge, round the D-pad's lobe and up the keyhole arch to its top. The right half is its mirror.
  static const float left_half[][2] = {
    {599, 130}, {544, 135}, {496, 146}, {470, 157}, {445, 168}, {422, 181}, {405, 194}, {397, 203},
    {390, 217}, {386, 230}, {379, 274}, {378, 297}, {376, 333}, {375, 403}, {377, 423}, {381, 442},
    {386, 452}, {393, 459}, {399, 462}, {413, 462}, {420, 459}, {427, 452}, {436, 433}, {447, 385},
    {449, 379}, {457, 351}, {463, 345}, {471, 350}, {475, 356}, {485, 374}, {493, 382}, {504, 389},
    {516, 393}, {537, 393}, {544, 391}, {557, 384}, {566, 376}, {575, 361}, {578, 351}, {578, 331},
    {571, 313}, {563, 303}, {557, 292}, {557, 276}, {570, 273}, {599, 271}};
  constexpr int kHalf = (int)(sizeof left_half / sizeof left_half[0]);
  ImVec2 pts[kHalf * 2];
  int n = 0;
  for (int i = 0; i < kHalf; ++i) pts[n++] = C(left_half[i][0], left_half[i][1]);
  for (int i = kHalf - 2; i >= 1; --i) pts[n++] = C(1198 - left_half[i][0], left_half[i][1]);
  // Shoulders sit behind the body, only their tops showing: L grey on the left, R grey on the right
  // with the blue Z on top of it (measured: L 416..485 x 138..183, Z 713..779 x 147..177).
  const bool l_on = (live >> (int)A::L) & 1, r_on = (live >> (int)A::R) & 1, z_on = (live >> (int)A::Z) & 1;
  const ImU32 shoulder = IM_COL32(150, 150, 158, 255), shoulder_on = IM_COL32(240, 150, 40, 255);
  dl->AddEllipseFilled(C(451, 166), ImVec2(37 * kc, 21 * kc), l_on ? shoulder_on : shoulder, -0.45f, 40);
  dl->AddEllipseFilled(C(747, 166), ImVec2(37 * kc, 21 * kc), r_on ? shoulder_on : shoulder, 0.45f, 40);
  dl->AddEllipseFilled(C(746, 164), ImVec2(34 * kc, 13 * kc), z_on ? IM_COL32(110, 150, 255, 255) : IM_COL32(45, 75, 190, 255), 0.38f, 32);
  // Filled a pixel row at a time between the edge crossings (ImGui's concave triangulator gives up on
  // this outline), then outlined anti-aliased on top.
  {
    float y0 = pts[0].y, y1 = pts[0].y;
    for (int i = 1; i < n; ++i) { y0 = std::min(y0, pts[i].y); y1 = std::max(y1, pts[i].y); }
    std::vector<float> xs;
    for (float y = std::floor(y0) + 0.5f; y < y1; y += 1.0f) {
      xs.clear();
      for (int i = 0; i < n; ++i) {
        const ImVec2 a = pts[i], b = pts[(i + 1) % n];
        if ((a.y <= y && b.y > y) || (b.y <= y && a.y > y)) xs.push_back(a.x + (y - a.y) / (b.y - a.y) * (b.x - a.x));
      }
      std::sort(xs.begin(), xs.end());
      for (size_t j = 0; j + 1 < xs.size(); j += 2)
        dl->AddRectFilled(ImVec2(xs[j], y - 0.5f), ImVec2(xs[j + 1], y + 0.5f), IM_COL32(52, 52, 57, 255));
    }
  }
  dl->AddPolyline(pts, n, IM_COL32(28, 28, 31, 255), ImDrawFlags_Closed, 2.0f * k);
  // The lighter well round the control stick (the only one on Ultimate's picture).
  dl->AddCircleFilled(C(467, 246), 72 * kc, IM_COL32(68, 68, 74, 255), 48);

  // ---- parts, at their measured centres and sizes ----
  const ImVec2 stick = C(466, 245), a_c = C(732, 245), b_c = C(683, 270), start_c = C(599, 245),
               dpad_c = C(528, 344), cst_c = C(670, 342);
  const ImVec2 y_c = C(719, 199), x_c = C(779, 234);   // the Y and X beans
  constexpr float kYrx = 22, kYry = 13, kYrot = -0.22f, kXrx = 13, kXry = 23, kXrot = 0.16f;
  // The caps move with what the game gets: the real stick, or centre while it is inside the deadzone.
  const ImVec2 stick_game = inside_deadzone(stick_pos, dz_main) ? ImVec2(0, 0) : stick_pos;
  const ImVec2 c_game = inside_deadzone(c_pos, dz_c) ? ImVec2(0, 0) : c_pos;
  const ImVec2 stick_cap(stick.x + stick_game.x * 11 * kc, stick.y - stick_game.y * 11 * kc);
  const ImVec2 c_cap(cst_c.x + c_game.x * 10 * kc, cst_c.y - c_game.y * 10 * kc);
  auto lit = [&](int action) { return ((live >> action) & 1) != 0; };
  // Control stick: grey octagonal gate, cap with rings.
  dl->AddNgonFilled(stick, 33 * kc, IM_COL32(150, 150, 158, 255), 8);
  dl->AddCircleFilled(stick_cap, 24 * kc, IM_COL32(185, 185, 192, 255), 40);
  dl->AddCircle(stick_cap, 17 * kc, IM_COL32(150, 150, 158, 255), 40, 2.0f * k);
  dl->AddCircle(stick_cap, 9 * kc, IM_COL32(150, 150, 158, 255), 40, 2.0f * k);
  // Start.
  dl->AddCircleFilled(start_c, 9 * kc, lit((int)A::Start) ? IM_COL32(255, 255, 255, 255) : IM_COL32(165, 165, 172, 255), 24);
  // A, B, and the Y and X beans.
  dl->AddCircleFilled(a_c, 26 * kc, lit((int)A::A) ? IM_COL32(90, 230, 120, 255) : IM_COL32(40, 170, 75, 255), 48);
  dl->AddCircleFilled(b_c, 15 * kc, lit((int)A::B) ? IM_COL32(255, 110, 110, 255) : IM_COL32(215, 40, 40, 255), 32);
  auto bean = [&](ImVec2 c, float rx, float ry, float rot, bool on) {
    dl->AddEllipseFilled(c, ImVec2(rx * kc, ry * kc), on ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 200, 206, 255), rot, 32);
  };
  bean(y_c, kYrx, kYry, kYrot, lit((int)A::Y));
  bean(x_c, kXrx, kXry, kXrot, lit((int)A::X));
  auto centred_text = [&](ImVec2 c, const char* t, ImU32 col) {
    const ImVec2 sz = ImGui::CalcTextSize(t);
    dl->AddText(ImVec2(c.x - sz.x / 2, c.y - sz.y / 2), col, t);
  };
  centred_text(a_c, "A", IM_COL32(255, 255, 255, 255));
  centred_text(b_c, "B", IM_COL32(255, 255, 255, 255));
  centred_text(x_c, "X", IM_COL32(60, 60, 66, 255));
  centred_text(y_c, "Y", IM_COL32(60, 60, 66, 255));
  // D-pad: a light plus with rounded arms and arrows, each arm its own part.
  const float arm = 24, half = 9;
  struct Arm { int action; float x0, y0, x1, y1; };
  const Arm arms[] = {{(int)A::DUp, -half, -arm, half, -half}, {(int)A::DDown, -half, half, half, arm},
                      {(int)A::DLeft, -arm, -half, -half, half}, {(int)A::DRight, half, -half, arm, half}};
  dl->AddRectFilled(ImVec2(dpad_c.x - half * kc, dpad_c.y - half * kc), ImVec2(dpad_c.x + half * kc, dpad_c.y + half * kc), IM_COL32(200, 200, 206, 255));
  for (const Arm& a : arms)
    dl->AddRectFilled(ImVec2(dpad_c.x + a.x0 * kc, dpad_c.y + a.y0 * kc), ImVec2(dpad_c.x + a.x1 * kc, dpad_c.y + a.y1 * kc),
                      lit(a.action) ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 200, 206, 255), 3 * kc);
  auto dpad_arrow = [&](float dx, float dy) {
    const ImVec2 tip(dpad_c.x + dx * 20 * kc, dpad_c.y + dy * 20 * kc), base(dpad_c.x + dx * 13 * kc, dpad_c.y + dy * 13 * kc);
    const ImVec2 side(-dy * 5.0f * kc, dx * 5.0f * kc);
    dl->AddTriangleFilled(tip, ImVec2(base.x + side.x, base.y + side.y), ImVec2(base.x - side.x, base.y - side.y), IM_COL32(110, 110, 118, 255));
  };
  dpad_arrow(0, -1); dpad_arrow(0, 1); dpad_arrow(-1, 0); dpad_arrow(1, 0);
  // C-stick: yellow octagon and cap.
  const bool c_on = lit((int)A::CUp) || lit((int)A::CDown) || lit((int)A::CLeft) || lit((int)A::CRight);
  dl->AddNgonFilled(cst_c, 33 * kc, IM_COL32(215, 160, 20, 255), 8);
  dl->AddCircleFilled(c_cap, 22 * kc, c_on ? IM_COL32(255, 230, 120, 255) : IM_COL32(245, 195, 40, 255), 32);
  centred_text(c_cap, "C", IM_COL32(120, 80, 0, 255));
  // Main stick (keyboard digital bindings only): ring lights up while any stick-direction key is held.
  const bool s_on = lit((int)A::SUp) || lit((int)A::SDown) || lit((int)A::SLeft) || lit((int)A::SRight);
  if (s_on) dl->AddCircle(stick, 33 * kc, IM_COL32(255, 255, 255, 255), 8, 3.0f * k);
  draw_deadzone_view(dl, stick, 33 * kc, stick_pos, dz_main, k, false);
  draw_deadzone_view(dl, cst_c, 33 * kc, c_pos, dz_c, k, true);

  // ---- hit testing on the controller ----
  auto in_circle = [&](ImVec2 c, float r) { const float dx = mouse.x - c.x, dy = mouse.y - c.y; return dx * dx + dy * dy <= r * r * kc * kc; };
  auto in_bean = [&](ImVec2 c, float rx, float ry, float rot) {
    const float dx = mouse.x - c.x, dy = mouse.y - c.y, cs = std::cos(-rot), sn = std::sin(-rot);
    const float u = (dx * cs - dy * sn) / ((rx + 3) * kc), v = (dx * sn + dy * cs) / ((ry + 3) * kc);
    return u * u + v * v <= 1.0f;
  };
  auto in_rect = [&](ImVec2 a, ImVec2 z) { return mouse.x >= a.x && mouse.x <= z.x && mouse.y >= a.y && mouse.y <= z.y; };
  auto part_at_mouse = [&]() -> int {
    if (in_circle(a_c, 28)) return (int)A::A;
    if (in_circle(b_c, 17)) return (int)A::B;
    if (in_bean(x_c, kXrx, kXry, kXrot)) return (int)A::X;
    if (in_bean(y_c, kYrx, kYry, kYrot)) return (int)A::Y;
    if (in_circle(start_c, 13)) return (int)A::Start;
    for (const Arm& a : arms)
      if (in_rect(ImVec2(dpad_c.x + (a.x0 - 3) * kc, dpad_c.y + (a.y0 - 3) * kc), ImVec2(dpad_c.x + (a.x1 + 3) * kc, dpad_c.y + (a.y1 + 3) * kc)))
        return a.action;
    if (in_circle(cst_c, 35)) {
      if (analog_c) return -1;   // an analog C-stick is not rebindable
      const float dx = mouse.x - cst_c.x, dy = mouse.y - cst_c.y;
      return std::fabs(dx) > std::fabs(dy) ? (dx > 0 ? (int)A::CRight : (int)A::CLeft) : (dy > 0 ? (int)A::CDown : (int)A::CUp);
    }
    if (in_rect(C(713, 145), C(781, 170))) return (int)A::Z;
    if (in_circle(C(451, 158), 34) && mouse.y < C(0, 176).y) return (int)A::L;
    if (in_circle(C(760, 160), 30) && mouse.y < C(0, 176).y) return (int)A::R;
    return -1;
  };

  // ---- callouts: grouped the way Ultimate groups them ----
  std::vector<GcCallout> boxes = {
    {(int)A::L, 24, 30, 200},
    {(int)A::DUp, 24, 120, 200}, {(int)A::DDown, 24, 160, 200}, {(int)A::DLeft, 24, 200, 200}, {(int)A::DRight, 24, 240, 200},
    {(int)A::R, 736, 30, 200}, {(int)A::Z, 736, 70, 200},
    {(int)A::Y, 736, 142, 200}, {(int)A::X, 736, 182, 200}, {(int)A::A, 736, 222, 200}, {(int)A::B, 736, 262, 200},
    {(int)A::Start, 380, 6, 200},
  };
  const float c_x = cst_c.x / k - o.x / k;   // the C-stick's canvas x, so its box sits right under it
  (void)c_x; (void)c_mode_text;
  if (!analog_c) {
    boxes.push_back({(int)A::CUp, 216, 350, 128}); boxes.push_back({(int)A::CDown, 352, 350, 128});
    boxes.push_back({(int)A::CLeft, 488, 350, 128}); boxes.push_back({(int)A::CRight, 624, 350, 128});
    // Second row: the control stick, on the same condition. A device with a real analog stick
    // feeds it directly and has nothing to bind here.
    boxes.push_back({(int)A::SUp, 216, 392, 128}); boxes.push_back({(int)A::SDown, 352, 392, 128});
    boxes.push_back({(int)A::SLeft, 488, 392, 128}); boxes.push_back({(int)A::SRight, 624, 392, 128});
  }
  constexpr float kBoxH = 32;
  // Grey group panels behind the boxes.
  const ImU32 group = IM_COL32(172, 173, 182, 255);
  dl->AddRectFilled(P(14, 110), P(234, 282), group, 8 * k);
  dl->AddRectFilled(P(726, 20), P(946, 112), group, 8 * k);
  dl->AddRectFilled(P(726, 132), P(946, 304), group, 8 * k);
  if (!analog_c) dl->AddRectFilled(P(206, 342), P(762, 432), group, 8 * k);   // both direction rows

  int hovered = -1;
  if (const char* force = std::getenv("MELEE_TEST_HOVER")) hovered = std::atoi(force);   // screenshot hook
  if (canvas_hovered) {
    for (const GcCallout& b : boxes)
      if (mouse.x >= P(b.x, 0).x && mouse.x <= P(b.x + b.w, 0).x && mouse.y >= P(0, b.y).y && mouse.y <= P(0, b.y + kBoxH).y) { hovered = b.action; break; }
    if (hovered < 0) hovered = part_at_mouse();
  }

  // Where each callout points, and the cyan outline drawn round that part when it is active.
  auto target_of = [&](int action) -> ImVec2 {
    switch (action) {
      case (int)A::L: return C(440, 145);
      case (int)A::DUp: return C(528, 344 - 24);
      case (int)A::DDown: return C(528, 344 + 24);
      case (int)A::DLeft: return C(528 - 24, 344);
      case (int)A::DRight: return C(528 + 24, 344);
      case (int)A::R: case (int)A::Z: return C(790, 160);   // one bracket round R and Z
      case (int)A::A: case (int)A::B: case (int)A::X: case (int)A::Y: return C(800, 240);
      case (int)A::Start: return start_c;
      default: break;
    }
    if (action == kCStickModeBox || (action >= (int)A::CUp && action <= (int)A::CRight)) return ImVec2(cst_c.x, cst_c.y + 37 * kc);
    // The control stick's own boxes point at the stick, not the C-stick.
    if (action >= (int)A::SUp && action <= (int)A::SRight) return ImVec2(stick.x, stick.y + 37 * kc);
    return C(476, 344);   // the D-pad arms: the diamond's left corner
  };
  auto outline_part = [&](int action, ImU32 col, float th) {
    if (action == (int)A::A) dl->AddCircle(a_c, 30 * kc, col, 40, th);
    else if (action == (int)A::B) dl->AddCircle(b_c, 19 * kc, col, 28, th);
    else if (action == (int)A::X) dl->AddEllipse(x_c, ImVec2((kXrx + 4) * kc, (kXry + 4) * kc), col, kXrot, 32, th);
    else if (action == (int)A::Y) dl->AddEllipse(y_c, ImVec2((kYrx + 4) * kc, (kYry + 4) * kc), col, kYrot, 32, th);
    else if (action >= (int)A::DUp && action <= (int)A::DRight) {
      // Just that direction's arm, lit and outlined.
      for (const Arm& a : arms) {
        if (a.action != action) continue;
        const ImVec2 lo(dpad_c.x + (a.x0 - 2) * kc, dpad_c.y + (a.y0 - 2) * kc), hi(dpad_c.x + (a.x1 + 2) * kc, dpad_c.y + (a.y1 + 2) * kc);
        dl->AddRectFilled(lo, hi, IM_COL32(255, 255, 255, 255), 2 * kc);
        dl->AddRect(lo, hi, col, 3 * kc, 0, th);
      }
    } else if (action == kCStickModeBox || (action >= (int)A::CUp && action <= (int)A::CRight))
      dl->AddNgon(cst_c, 37 * kc, col, 8, th);
    else if (action == (int)A::Start) dl->AddCircle(start_c, 14 * kc, col, 24, th);
    else if (action == (int)A::L) dl->AddEllipse(C(451, 166), ImVec2(41 * kc, 25 * kc), col, -0.45f, 40, th);
    else if (action == (int)A::R) dl->AddEllipse(C(747, 166), ImVec2(41 * kc, 25 * kc), col, 0.45f, 40, th);
    else if (action == (int)A::Z) dl->AddEllipse(C(746, 164), ImVec2(38 * kc, 17 * kc), col, 0.38f, 32, th);
  };

  // Leader lines: one per group, from the group's edge to its part, as on Ultimate's screen.
  auto leader = [&](ImVec2 from, ImVec2 to, bool active) {
    const ImU32 col = active ? IM_COL32(255, 255, 255, 255) : cyan;
    const ImVec2 elbow(to.x, from.y);
    dl->AddLine(from, elbow, col, 2.0f * k);
    dl->AddLine(elbow, to, col, 2.0f * k);
    dl->AddCircleFilled(from, 3.5f * k, col, 12);
  };
  auto active = [&](int action) { return action == hovered || action == capturing; };
  auto any_active = [&](std::initializer_list<int> list) { for (int a : list) if (active(a)) return true; return false; };
  const ImU32 white = IM_COL32(255, 255, 255, 255);
  leader(P(224, 46), target_of((int)A::L), active((int)A::L));
  {  // D-pad: across to the pad, then to the arm being pointed at
    int dir = -1;
    for (int a : {(int)A::DUp, (int)A::DDown, (int)A::DLeft, (int)A::DRight}) if (active(a)) dir = a;
    const ImVec2 from = P(234, 196), corner = C(490, 344);
    const ImU32 col = dir >= 0 ? white : cyan;
    dl->AddLine(from, ImVec2(corner.x - 24 * k, from.y), col, 2.0f * k);
    dl->AddLine(ImVec2(corner.x - 24 * k, from.y), corner, col, 2.0f * k);
    dl->AddLine(corner, dir >= 0 ? target_of(dir) : C(528 - 24, 344), col, 2.0f * k);
    dl->AddCircleFilled(from, 3.5f * k, col, 12);
  }
  {  // R and Z share one bracket
    const ImVec2 to = target_of((int)A::R);
    const ImU32 col = any_active({(int)A::R, (int)A::Z}) ? white : cyan;
    dl->AddLine(P(726, 46), ImVec2(P(716, 0).x, P(0, 46).y), col, 2.0f * k);
    dl->AddLine(P(726, 86), ImVec2(P(716, 0).x, P(0, 86).y), col, 2.0f * k);
    dl->AddLine(ImVec2(P(716, 0).x, P(0, 46).y), ImVec2(P(716, 0).x, P(0, 86).y), col, 2.0f * k);
    dl->AddLine(ImVec2(P(716, 0).x, to.y), to, col, 2.0f * k);
    dl->AddLine(ImVec2(P(716, 0).x, to.y), ImVec2(P(716, 0).x, P(0, 46).y), col, 2.0f * k);
    dl->AddCircleFilled(P(726, 46), 3.5f * k, col, 12);
    dl->AddCircleFilled(P(726, 86), 3.5f * k, col, 12);
  }
  {  // face buttons: across, then up to the right side of their frame
    const ImVec2 to = target_of((int)A::A);
    const ImU32 col = any_active({(int)A::A, (int)A::B, (int)A::X, (int)A::Y}) ? white : cyan;
    const float x = P(716, 0).x;
    dl->AddLine(P(726, 218), ImVec2(x, P(0, 218).y), col, 2.0f * k);
    dl->AddLine(ImVec2(x, P(0, 218).y), ImVec2(x, to.y), col, 2.0f * k);
    dl->AddLine(ImVec2(x, to.y), to, col, 2.0f * k);
    dl->AddCircleFilled(P(726, 218), 3.5f * k, col, 12);
  }
  dl->AddLine(P(480, 38), start_c, active((int)A::Start) ? white : cyan, 2.0f * k);
  if (analog_c) {}
  else dl->AddLine(ImVec2(cst_c.x, P(0, 342).y), target_of((int)A::CUp), any_active({(int)A::CUp, (int)A::CDown, (int)A::CLeft, (int)A::CRight}) ? white : cyan, 2.0f * k);
  // Cyan outlines round the hovered or waiting part (drawn over the controller).
  for (int a : {hovered, capturing})
    if (a >= 0) outline_part(a, a == capturing ? IM_COL32(255, 215, 60, 255) : cyan, 3.0f * k);

  // The boxes: an icon circle naming the GameCube input, then what it is bound to.
  auto icon = [&](ImVec2 c, int action) {
    ImU32 bg = IM_COL32(52, 52, 58, 255);
    const char* t = nullptr;
    switch (action) {
      case (int)A::A: bg = IM_COL32(40, 170, 75, 255); t = "A"; break;
      case (int)A::B: bg = IM_COL32(215, 40, 40, 255); t = "B"; break;
      case (int)A::X: t = "X"; break;
      case (int)A::Y: t = "Y"; break;
      case (int)A::Z: bg = IM_COL32(45, 75, 190, 255); t = "Z"; break;
      case (int)A::L: t = "L"; break;
      case (int)A::R: t = "R"; break;
      case (int)A::Start: t = "S"; break;
      default: break;
    }
    const bool is_c = action == kCStickModeBox || (action >= (int)A::CUp && action <= (int)A::CRight);
    if (is_c) bg = IM_COL32(215, 160, 20, 255);
    dl->AddCircleFilled(c, 13 * k, bg, 24);
    if (t) { centred_text(c, t, IM_COL32(255, 255, 255, 255)); return; }
    if (action == kCStickModeBox) { centred_text(c, "C", IM_COL32(90, 60, 0, 255)); return; }
    // Directions: a small plus (D-pad) or nothing (C), and a triangle pointing the way.
    int dir = 0;   // 0 up, 1 down, 2 left, 3 right
    if (action == (int)A::DDown || action == (int)A::CDown) dir = 1;
    else if (action == (int)A::DLeft || action == (int)A::CLeft) dir = 2;
    else if (action == (int)A::DRight || action == (int)A::CRight) dir = 3;
    const float s = 6.5f * k;
    ImVec2 t0, t1, t2;
    switch (dir) {
      case 0: t0 = ImVec2(c.x, c.y - s); t1 = ImVec2(c.x + s, c.y + s * 0.6f); t2 = ImVec2(c.x - s, c.y + s * 0.6f); break;
      case 1: t0 = ImVec2(c.x, c.y + s); t1 = ImVec2(c.x - s, c.y - s * 0.6f); t2 = ImVec2(c.x + s, c.y - s * 0.6f); break;
      case 2: t0 = ImVec2(c.x - s, c.y); t1 = ImVec2(c.x + s * 0.6f, c.y - s); t2 = ImVec2(c.x + s * 0.6f, c.y + s); break;
      default: t0 = ImVec2(c.x + s, c.y); t1 = ImVec2(c.x - s * 0.6f, c.y + s); t2 = ImVec2(c.x - s * 0.6f, c.y - s); break;
    }
    dl->AddTriangleFilled(t0, t1, t2, is_c ? IM_COL32(90, 60, 0, 255) : IM_COL32(230, 230, 235, 255));
  };
  for (const GcCallout& b : boxes) {
    const bool waiting = b.action == capturing, over = b.action == hovered;
    const bool down = b.action < 32 && ((live >> b.action) & 1);
    const ImVec2 a = P(b.x, b.y), z = P(b.x + b.w, b.y + kBoxH);
    const ImU32 fill = waiting ? ImGui::GetColorU32(ImVec4(0.48f, 0.18f, 0.88f, 0.85f + 0.15f * pulse)) : IM_COL32(248, 248, 250, 255);
    dl->AddRectFilled(a, z, fill, 9 * k);
    dl->AddRect(a, z, waiting ? IM_COL32(255, 215, 60, 255) : over ? cyan : IM_COL32(150, 150, 160, 255), 9 * k, 0, waiting || over ? 3.0f * k : 1.0f);
    if (down) dl->AddRectFilled(ImVec2(z.x - 8 * k, a.y + 6 * k), ImVec2(z.x - 4 * k, z.y - 6 * k), IM_COL32(40, 190, 80, 255), 2 * k);
    icon(ImVec2(a.x + 19 * k, (a.y + z.y) / 2), b.action);
    std::string text = waiting ? std::string("Press a button...")
                     : b.action == kCStickModeBox ? std::string(c_mode_text) : label(b.action);
    const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
    const float left = a.x + 38 * k, right = z.x - 10 * k;
    const float tx = std::max(left, (left + right - ts.x) / 2);
    dl->PushClipRect(ImVec2(left, a.y), ImVec2(right, z.y), true);
    dl->AddText(ImVec2(tx, (a.y + z.y - ts.y) / 2), waiting ? IM_COL32(255, 255, 255, 255) : IM_COL32(30, 30, 36, 255), text.c_str());
    dl->PopClipRect();
  }

  if (hovered_out) *hovered_out = hovered;
  if (right_clicked) *right_clicked = hovered >= 0 && hovered != kCStickModeBox && ImGui::IsItemClicked(ImGuiMouseButton_Right) ? hovered : -1;
  return hovered >= 0 && ImGui::IsItemClicked(ImGuiMouseButton_Left) ? hovered : -1;
}

// The Switch Pro Controller, laid out as Smash Ultimate's button settings screen shows it: each
// physical button has a box naming what it does in Melee. Click a button or its box and pick from
// the list; right-click to clear. The right stick carries the C-stick mode, as the C-stick does on the
// GameCube picture. Authored on a 960x432 canvas.
constexpr float kSwCanvasH = 392;
bool g_swpro_gc_picture = false;   // "Use GameCube controller picture" for Switch controllers

static bool draw_swpro_bind_picture(int index, uint16_t raw, ImVec2 lstick, ImVec2 rstick, float dz_main, float dz_c) {
  using A = host::BindAction;
  bool changed = false;
  const float avail = ImGui::GetContentRegionAvail().x;
  const float k = std::clamp(avail / 960.0f, 0.5f, 1.4f);
  const ImVec2 o = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("sw_picture", ImVec2(960.0f * k, kSwCanvasH * k));
  const bool canvas_hovered = ImGui::IsItemHovered();
  const bool left_click = ImGui::IsItemClicked(ImGuiMouseButton_Left), right_click = ImGui::IsItemClicked(ImGuiMouseButton_Right);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  auto P = [&](float x, float y) { return ImVec2(o.x + x * k, o.y + y * k); };
  // Traced, not drawn by eye: the body is the silhouette of the Pro Controller in a 1280x720 capture
  // of Ultimate's button settings screen (left half, mirrored: boxes cover the right grip there), and
  // every part sits where it measures in that capture. Coordinates are that capture's pixels.
  constexpr float kP = 0.85f, kMid = 582.5f;
  auto S = [&](float x, float y) { return P(480 + (x - kMid) * kP, 70 + (y - 222) * kP); };
  const float kp = kP * k;   // a controller-space length in screen pixels
  const ImVec2 mouse = ImGui::GetMousePos();
  const ImU32 cyan = IM_COL32(40, 200, 225, 255), white = IM_COL32(255, 255, 255, 255);
  host::PadBindings& bind = host::g_swpro_bindings[(size_t)index];

  dl->AddRectFilled(P(0, 0), P(960, kSwCanvasH), IM_COL32(222, 223, 229, 255), 12 * k);

  // ---- the controller ----
  static const float left_half[][2] = {
    {582, 229}, {541, 229}, {502, 231}, {494, 228}, {476, 222}, {448, 224}, {424, 228}, {412, 232},
    {391, 245}, {385, 251}, {376, 265}, {365, 278}, {360, 289}, {351, 319}, {337, 393}, {336, 410},
    {333, 416}, {324, 470}, {320, 501}, {319, 523}, {322, 546}, {324, 553}, {330, 563}, {332, 566},
    {336, 571}, {341, 575}, {346, 579}, {357, 584}, {371, 585}, {382, 583}, {389, 579}, {399, 570},
    {413, 548}, {434, 505}, {443, 490}, {449, 484}, {456, 480}, {582, 479}};
  constexpr int kHalf = (int)(sizeof left_half / sizeof left_half[0]);
  ImVec2 pts[kHalf * 2];
  int n = 0;
  for (int i = 0; i < kHalf; ++i) pts[n++] = S(left_half[i][0], left_half[i][1]);
  for (int i = kHalf - 2; i >= 1; --i) pts[n++] = S(2 * kMid - left_half[i][0], left_half[i][1]);
  auto down = [&](uint16_t bit) { return (raw & bit) != 0; };
  const ImU32 shoulder = IM_COL32(70, 70, 76, 255), shoulder_on = IM_COL32(240, 150, 40, 255);
  // Triggers above the bumpers, bumpers peeking over the top edge (their places on the controller's
  // top edge: the bumps at x 424..500).
  dl->AddRectFilled(S(404, 182), S(500, 212), down(host::SWPRO_ZL) ? shoulder_on : IM_COL32(58, 58, 64, 255), 10 * kp);
  dl->AddRectFilled(S(665, 182), S(761, 212), down(host::SWPRO_ZR) ? shoulder_on : IM_COL32(58, 58, 64, 255), 10 * kp);
  dl->AddEllipseFilled(S(452, 228), ImVec2(58 * kp, 14 * kp), down(host::SWPRO_L) ? shoulder_on : shoulder, -0.05f, 32);
  dl->AddEllipseFilled(S(713, 228), ImVec2(58 * kp, 14 * kp), down(host::SWPRO_R) ? shoulder_on : shoulder, 0.05f, 32);
  // Filled a pixel row at a time between the edge crossings, then outlined on top.
  {
    float y0 = pts[0].y, y1 = pts[0].y;
    for (int i = 1; i < n; ++i) { y0 = std::min(y0, pts[i].y); y1 = std::max(y1, pts[i].y); }
    std::vector<float> xs;
    for (float y = std::floor(y0) + 0.5f; y < y1; y += 1.0f) {
      xs.clear();
      for (int i = 0; i < n; ++i) {
        const ImVec2 a = pts[i], b = pts[(i + 1) % n];
        if ((a.y <= y && b.y > y) || (b.y <= y && a.y > y)) xs.push_back(a.x + (y - a.y) / (b.y - a.y) * (b.x - a.x));
      }
      std::sort(xs.begin(), xs.end());
      for (size_t j = 0; j + 1 < xs.size(); j += 2)
        dl->AddRectFilled(ImVec2(xs[j], y - 0.5f), ImVec2(xs[j + 1], y + 0.5f), IM_COL32(60, 60, 64, 255));
    }
  }
  dl->AddPolyline(pts, n, IM_COL32(28, 28, 31, 255), ImDrawFlags_Closed, 2.0f * k);
  auto centred_text = [&](ImVec2 c, const char* t, ImU32 col) {
    const ImVec2 sz = ImGui::CalcTextSize(t);
    dl->AddText(ImVec2(c.x - sz.x / 2, c.y - sz.y / 2), col, t);
  };
  centred_text(S(452, 197), "ZL", IM_COL32(200, 200, 206, 255));
  centred_text(S(713, 197), "ZR", IM_COL32(200, 200, 206, 255));

  // ---- parts, at their measured centres, drawn as Ultimate draws them: black, white letters ----
  const ImVec2 ls = S(437, 330), dpad = S(497, 405), face = S(719, 332), rs = S(650, 406);
  const ImVec2 minus_c = S(518, 293), plus_c = S(646, 293);
  const ImU32 black = IM_COL32(22, 22, 24, 255);
  auto stick = [&](ImVec2 c, ImVec2 pos, float dz, bool c_stick) {
    const ImVec2 game = inside_deadzone(pos, dz) ? ImVec2(0, 0) : pos;
    const ImVec2 cap(c.x + game.x * 8 * kp, c.y - game.y * 8 * kp);
    dl->AddCircleFilled(cap, 29 * kp, black, 40);
    dl->AddCircle(cap, 20 * kp, IM_COL32(52, 52, 56, 255), 40, 2.0f * k);
    draw_deadzone_view(dl, c, 29 * kp, pos, dz, k, c_stick);
  };
  stick(ls, lstick, dz_main, false);
  stick(rs, rstick, dz_c, true);
  // D-pad: a black plus with white arrows.
  const float arm = 30, half = 10;
  struct Arm { uint16_t bit; float x0, y0, x1, y1; };
  const Arm arms[] = {{host::SWPRO_DPAD_UP, -half, -arm, half, -half}, {host::SWPRO_DPAD_DOWN, -half, half, half, arm},
                      {host::SWPRO_DPAD_LEFT, -arm, -half, -half, half}, {host::SWPRO_DPAD_RIGHT, half, -half, arm, half}};
  dl->AddRectFilled(ImVec2(dpad.x - half * kp, dpad.y - half * kp), ImVec2(dpad.x + half * kp, dpad.y + half * kp), black);
  for (const Arm& a : arms)
    dl->AddRectFilled(ImVec2(dpad.x + a.x0 * kp, dpad.y + a.y0 * kp), ImVec2(dpad.x + a.x1 * kp, dpad.y + a.y1 * kp),
                      down(a.bit) ? IM_COL32(120, 120, 128, 255) : black, 3 * kp);
  auto dpad_arrow = [&](float dx, float dy) {
    const ImVec2 tip(dpad.x + dx * 25 * kp, dpad.y + dy * 25 * kp), base(dpad.x + dx * 17 * kp, dpad.y + dy * 17 * kp);
    const ImVec2 side(-dy * 5.5f * kp, dx * 5.5f * kp);
    dl->AddTriangleFilled(tip, ImVec2(base.x + side.x, base.y + side.y), ImVec2(base.x - side.x, base.y - side.y), white);
  };
  dpad_arrow(0, -1); dpad_arrow(0, 1); dpad_arrow(-1, 0); dpad_arrow(1, 0);
  // Face buttons: X top, A right, B bottom, Y left (measured 37 to 39 px from the centre).
  struct Face { uint16_t bit; float dx, dy; const char* t; };
  const Face faces[] = {{host::SWPRO_X, 0, -37, "X"}, {host::SWPRO_A, 40, 0, "A"}, {host::SWPRO_B, 0, 38, "B"}, {host::SWPRO_Y, -39, 0, "Y"}};
  for (const Face& f : faces) {
    const ImVec2 c(face.x + f.dx * kp, face.y + f.dy * kp);
    dl->AddCircleFilled(c, 17 * kp, down(f.bit) ? white : black, 24);
    centred_text(c, f.t, down(f.bit) ? IM_COL32(20, 20, 20, 255) : white);
  }
  // Minus and Plus (grey dots on Ultimate's picture), Capture and Home (not bindable).
  const ImU32 dim_btn = IM_COL32(95, 95, 100, 255);
  dl->AddCircleFilled(minus_c, 11 * kp, down(host::SWPRO_MINUS) ? white : dim_btn, 20);
  dl->AddCircleFilled(plus_c, 11 * kp, down(host::SWPRO_PLUS) ? white : dim_btn, 20);
  dl->AddRectFilled(S(532, 325), S(550, 343), dim_btn, 2 * kp);
  dl->AddCircleFilled(S(616, 334), 11 * kp, dim_btn, 20);

  // ---- which physical button is under the mouse ----
  auto in_circle = [&](ImVec2 c, float r) { const float dx = mouse.x - c.x, dy = mouse.y - c.y; return dx * dx + dy * dy <= r * r * kp * kp; };
  auto in_rect = [&](ImVec2 a, ImVec2 z) { return mouse.x >= a.x && mouse.x <= z.x && mouse.y >= a.y && mouse.y <= z.y; };
  enum { kRStick = 0x10000 };
  auto part_at_mouse = [&]() -> int {
    for (const Face& f : faces) if (in_circle(ImVec2(face.x + f.dx * kp, face.y + f.dy * kp), 20)) return f.bit;
    for (const Arm& a : arms)
      if (in_rect(ImVec2(dpad.x + (a.x0 - 3) * kp, dpad.y + (a.y0 - 3) * kp), ImVec2(dpad.x + (a.x1 + 3) * kp, dpad.y + (a.y1 + 3) * kp))) return a.bit;
    if (in_circle(minus_c, 15)) return host::SWPRO_MINUS;
    if (in_circle(plus_c, 15)) return host::SWPRO_PLUS;
    if (in_rect(S(404, 182), S(500, 212))) return host::SWPRO_ZL;
    if (in_rect(S(665, 182), S(761, 212))) return host::SWPRO_ZR;
    if (in_rect(S(394, 213), S(510, 230))) return host::SWPRO_L;
    if (in_rect(S(655, 213), S(771, 230))) return host::SWPRO_R;
    return -1;
  };

  // ---- the boxes ----
  struct Box { int part; float x, y, w; const char* icon; };
  const Box boxes[] = {
    {host::SWPRO_ZL, 24, 40, 200, "ZL"}, {host::SWPRO_L, 24, 80, 200, "L"},
    {host::SWPRO_DPAD_UP, 24, 160, 200, nullptr}, {host::SWPRO_DPAD_DOWN, 24, 200, 200, nullptr},
    {host::SWPRO_DPAD_LEFT, 24, 240, 200, nullptr}, {host::SWPRO_DPAD_RIGHT, 24, 280, 200, nullptr},
    {host::SWPRO_ZR, 736, 40, 200, "ZR"}, {host::SWPRO_R, 736, 80, 200, "R"},
    {host::SWPRO_X, 736, 160, 200, "X"}, {host::SWPRO_Y, 736, 200, 200, "Y"},
    {host::SWPRO_A, 736, 240, 200, "A"}, {host::SWPRO_B, 736, 280, 200, "B"},
    {host::SWPRO_MINUS, 300, 4, 150, "-"}, {host::SWPRO_PLUS, 510, 4, 150, "+"},
  };
  constexpr float kBoxH = 32;
  const ImU32 group = IM_COL32(172, 173, 182, 255);
  dl->AddRectFilled(P(14, 30), P(234, 122), group, 8 * k);
  dl->AddRectFilled(P(14, 150), P(234, 322), group, 8 * k);
  dl->AddRectFilled(P(726, 30), P(946, 122), group, 8 * k);
  dl->AddRectFilled(P(726, 150), P(946, 322), group, 8 * k);

  int hovered = -1;
  if (canvas_hovered) {
    for (const Box& b : boxes)
      if (in_rect(P(b.x, b.y), P(b.x + b.w, b.y + kBoxH))) { hovered = b.part; break; }
    if (hovered < 0) hovered = part_at_mouse();
  }
  static int menu_part = -1;
  const bool menu_open = ImGui::IsPopupOpen("sw_assign");
  auto active = [&](int part) { return part == hovered || (menu_open && part == menu_part); };

  // What a button does: the Melee action it is bound to.
  auto action_of = [&](uint16_t bit) {
    for (int i = 0; i < (int)A::Count; ++i) if (!host::is_cstick_action(i) && bind.mask[i] == bit) return i;
    return -1;
  };
  auto target_of = [&](int part) -> ImVec2 {
    for (const Face& f : faces) if (f.bit == part) return ImVec2(face.x + f.dx * kp, face.y + f.dy * kp);
    for (const Arm& a : arms)
      if (a.bit == part) return ImVec2(dpad.x + (a.x0 + a.x1) / 2 * kp, dpad.y + (a.y0 + a.y1) / 2 * kp);
    switch (part) {
      case host::SWPRO_ZL: return S(404, 197);
      case host::SWPRO_L: return S(396, 226);
      case host::SWPRO_ZR: return S(761, 197);
      case host::SWPRO_R: return S(769, 226);
      case host::SWPRO_MINUS: return minus_c;
      case host::SWPRO_PLUS: return plus_c;
      case kRStick: return ImVec2(rs.x, rs.y + 32 * k);
      default: return ImVec2(0, 0);
    }
  };
  auto line_col = [&](std::initializer_list<int> parts) { for (int p : parts) if (active(p)) return white; return cyan; };
  auto leader = [&](ImVec2 from, ImVec2 to, ImU32 col) {
    const ImVec2 elbow(to.x, from.y);
    dl->AddLine(from, elbow, col, 2.0f * k);
    dl->AddLine(elbow, to, col, 2.0f * k);
    dl->AddCircleFilled(from, 3.5f * k, col, 12);
  };
  leader(P(224, 56), target_of(host::SWPRO_ZL), line_col({host::SWPRO_ZL}));
  leader(P(224, 96), target_of(host::SWPRO_L), line_col({host::SWPRO_L}));
  leader(P(736, 56), target_of(host::SWPRO_ZR), line_col({host::SWPRO_ZR}));
  leader(P(736, 96), target_of(host::SWPRO_R), line_col({host::SWPRO_R}));
  {  // D-pad group: to the pad, then to the arm being pointed at
    int dir = -1;
    for (const Arm& a : arms) if (active(a.bit)) dir = a.bit;
    const ImU32 col = dir >= 0 ? white : cyan;
    const ImVec2 from = P(234, 236), corner(dpad.x - 50 * kp, from.y);
    dl->AddLine(from, corner, col, 2.0f * k);
    dl->AddLine(corner, dir >= 0 ? target_of(dir) : ImVec2(dpad.x - 30 * kp, dpad.y), col, 2.0f * k);
    dl->AddCircleFilled(from, 3.5f * k, col, 12);
  }
  {  // face buttons: to the cluster, then to the button being pointed at
    int btn = -1;
    for (const Face& f : faces) if (active(f.bit)) btn = f.bit;
    const ImU32 col = btn >= 0 ? white : cyan;
    const ImVec2 from = P(726, 236), corner(face.x + 80 * kp, from.y), edge(face.x + 60 * kp, face.y);
    dl->AddLine(from, corner, col, 2.0f * k);
    dl->AddLine(corner, edge, col, 2.0f * k);
    if (btn >= 0) dl->AddLine(edge, target_of(btn), col, 2.0f * k);
    dl->AddCircleFilled(from, 3.5f * k, col, 12);
  }
  dl->AddLine(P(375, 36), P(375, 60), line_col({host::SWPRO_MINUS}), 2.0f * k);
  dl->AddLine(P(375, 60), minus_c, line_col({host::SWPRO_MINUS}), 2.0f * k);
  dl->AddLine(P(585, 36), P(585, 60), line_col({host::SWPRO_PLUS}), 2.0f * k);
  dl->AddLine(P(585, 60), plus_c, line_col({host::SWPRO_PLUS}), 2.0f * k);

  // Cyan outline round the part being pointed at.
  for (int part : {hovered, menu_open ? menu_part : -1}) {
    if (part < 0) continue;
    const ImU32 col = cyan; const float th = 3.0f * k;
    bool drawn = false;
    for (const Face& f : faces) if (f.bit == part) { dl->AddCircle(ImVec2(face.x + f.dx * kp, face.y + f.dy * kp), 21 * kp, col, 24, th); drawn = true; }
    for (const Arm& a : arms)
      if (a.bit == part) { dl->AddRect(ImVec2(dpad.x + (a.x0 - 2) * kp, dpad.y + (a.y0 - 2) * kp), ImVec2(dpad.x + (a.x1 + 2) * kp, dpad.y + (a.y1 + 2) * kp), col, 3 * kp, 0, th); drawn = true; }
    if (drawn) continue;
    if (part == host::SWPRO_ZL) dl->AddRect(S(400, 178), S(504, 216), col, 10 * kp, 0, th);
    else if (part == host::SWPRO_ZR) dl->AddRect(S(661, 178), S(765, 216), col, 10 * kp, 0, th);
    else if (part == host::SWPRO_L) dl->AddEllipse(S(452, 228), ImVec2(62 * kp, 18 * kp), col, -0.05f, 32, th);
    else if (part == host::SWPRO_R) dl->AddEllipse(S(713, 228), ImVec2(62 * kp, 18 * kp), col, 0.05f, 32, th);
    else if (part == host::SWPRO_MINUS) dl->AddCircle(minus_c, 15 * kp, col, 20, th);
    else if (part == host::SWPRO_PLUS) dl->AddCircle(plus_c, 15 * kp, col, 20, th);
    else if (part == kRStick) dl->AddCircle(rs, 34 * k, col, 32, th);
  }

  for (const Box& b : boxes) {
    const bool over = active(b.part);
    const ImVec2 a = P(b.x, b.y), z = P(b.x + b.w, b.y + kBoxH);
    dl->AddRectFilled(a, z, IM_COL32(248, 248, 250, 255), 9 * k);
    dl->AddRect(a, z, over ? cyan : IM_COL32(150, 150, 160, 255), 9 * k, 0, over ? 3.0f * k : 1.0f);
    const ImVec2 ic(a.x + 19 * k, (a.y + z.y) / 2);
    dl->AddCircleFilled(ic, 13 * k, IM_COL32(52, 52, 58, 255), 24);
    if (b.icon) centred_text(ic, b.icon, white);
    else {   // a D-pad direction: a triangle pointing the way
      const float t = 6.5f * k;
      ImVec2 p0, p1, p2;
      if (b.part == host::SWPRO_DPAD_UP) { p0 = ImVec2(ic.x, ic.y - t); p1 = ImVec2(ic.x + t, ic.y + t * 0.6f); p2 = ImVec2(ic.x - t, ic.y + t * 0.6f); }
      else if (b.part == host::SWPRO_DPAD_DOWN) { p0 = ImVec2(ic.x, ic.y + t); p1 = ImVec2(ic.x - t, ic.y - t * 0.6f); p2 = ImVec2(ic.x + t, ic.y - t * 0.6f); }
      else if (b.part == host::SWPRO_DPAD_LEFT) { p0 = ImVec2(ic.x - t, ic.y); p1 = ImVec2(ic.x + t * 0.6f, ic.y - t); p2 = ImVec2(ic.x + t * 0.6f, ic.y + t); }
      else { p0 = ImVec2(ic.x + t, ic.y); p1 = ImVec2(ic.x - t * 0.6f, ic.y + t); p2 = ImVec2(ic.x - t * 0.6f, ic.y - t); }
      dl->AddTriangleFilled(p0, p1, p2, white);
    }
    std::string text;
    if (b.part == kRStick) text = "Smash Attack";
    else { const int act = action_of((uint16_t)b.part); text = act >= 0 ? kActionTitles[act] : "Nothing"; }
    const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
    const float left = a.x + 38 * k, right = z.x - 10 * k;
    dl->PushClipRect(ImVec2(left, a.y), ImVec2(right, z.y), true);
    dl->AddText(ImVec2(std::max(left, (left + right - ts.x) / 2), (a.y + z.y - ts.y) / 2), IM_COL32(30, 30, 36, 255), text.c_str());
    dl->PopClipRect();
  }

  // ---- clicks: the stick switches the C-stick mode, a button opens the list of Melee buttons ----
  if (hovered == kRStick) {
    ImGui::SetTooltip("Right stick: the C-stick (smash attacks).");
  } else if (hovered >= 0) {
    ImGui::SetTooltip("Click to choose what this button does. Right-click to clear it.");
    if (left_click) { menu_part = hovered; ImGui::OpenPopup("sw_assign"); }
    if (right_click) {
      for (int i = 0; i < (int)A::Count; ++i) if (!host::is_cstick_action(i) && bind.mask[i] == (uint16_t)hovered) { bind.mask[i] = 0; changed = true; }
    }
  }
  if (ImGui::BeginPopup("sw_assign")) {
    const uint16_t bit = (uint16_t)menu_part;
    const int current = action_of(bit);
    ImGui::TextDisabled("This button does");
    ImGui::Separator();
    if (ImGui::Selectable("Nothing", current < 0)) {
      for (int i = 0; i < (int)A::Count; ++i) if (!host::is_cstick_action(i) && bind.mask[i] == bit) bind.mask[i] = 0;
      changed = true;
    }
    for (int i = 0; i < (int)A::Count; ++i) {
      if (host::is_cstick_action(i)) continue;
      if (ImGui::Selectable(kActionTitles[i], i == current)) {
        // One button per Melee button: this button takes the action, and stops doing any other.
        for (int j = 0; j < (int)A::Count; ++j) if (!host::is_cstick_action(j) && bind.mask[j] == bit) bind.mask[j] = 0;
        bind.mask[i] = bit;
        changed = true;
      }
    }
    ImGui::EndPopup();
  }
  return changed;
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
      const OverlayBounds bounds = overlay_bounds();
      ImGui::SetNextWindowPos(ImVec2((bounds.left + bounds.right) * 0.5f, bounds.top + 10), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
      ImGui::SetNextWindowBgAlpha(0.75f);
      ImGui::Begin("LCancelModeNotice", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
      clamp_overlay_window();
      ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                         "You currently have auto L-cancel on, but we have disabled it for %s mode", lower.c_str());
      ImGui::End();
    }
  }
}

// A friend pressed Join on the player's Discord presence. All that does inside the game is put
// their code at the front of the Online > Direct suggestions, which is invisible unless the player
// is already standing on that screen: three players reported pressing Join, seeing nothing happen,
// and concluding it was broken. Say what arrived and where to go with it.
static void draw_discord_invite_overlay() {
  if (!host::discord::enabled()) return;
  const std::string code = host::discord::invite_notice();
  if (code.empty()) return;
  const OverlayBounds bounds = overlay_bounds();
  ImGui::SetNextWindowPos(ImVec2((bounds.left + bounds.right) * 0.5f, bounds.top + 10), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
  ImGui::SetNextWindowBgAlpha(0.85f);
  ImGui::Begin("DiscordInviteNotice", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                                   ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                                                   ImGuiWindowFlags_NoFocusOnAppearing);
  clamp_overlay_window();
  ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "Discord invite from %s", code.c_str());
  ImGui::TextUnformatted("Open Online > Direct. Their code is the first suggestion, and yours is on the clipboard.");
  ImGui::End();
}

// The volume the settings file holds. state.volume is only assigned while the Audio tab is being
// drawn, and ImGui runs a tab's body only when it is the selected one, so saving from any other
// tab used to write whatever that field happened to start as, which is zero. Players saw the
// volume set itself to 0 on a later launch.
int g_volume = 100;

void load_pc_settings(D3D12Options& options, int& volume) {
  capture_default_bindings();   // the built-in buttons, before the saved ones replace them
  std::ifstream file(options.settings_path);
  // First launch (no saved settings yet): open the PC settings panel so nobody has to find it.
  options.settings_open = true;   // opens at every launch unless "startup 0" was saved
  // One setting per line: the key, then everything after it on that line. Reading the value as a
  // single token lost the multi-number "custompreset" line (0.5.5 and later) and shifted every
  // setting after it by one token, so the controller bindings, port choices and profiles saved
  // below it were never read back for anyone who had used the Custom video preset.
  std::string key, value;
  std::vector<std::string> gecko_on;   // the user's Gecko codes saved as on
  bool gecko_chosen = false;           // the panel has saved a choice (even "none")
  while (file >> key) {
    std::getline(file, value);
    const size_t first = value.find_first_not_of(" \t");
    value = first == std::string::npos ? std::string() : value.substr(first);
    while (!value.empty() && (value.back() == '\r' || value.back() == ' ' || value.back() == '\t')) value.pop_back();
    if (value.empty()) continue;
    try {
      if (key == "fps") { double rate = std::stod(value); if (rate == -1 || rate == 0 || (rate >= 30 && rate <= 2000)) options.fps_cap = rate; }
      else if (key == "scale") { int scale = std::stoi(value); if (scale >= 0 && scale <= 8) options.efb_scale = scale; }
      else if (key == "fullscreen") options.fullscreen = value == "1";
      else if (key == "exclusivefullscreen") options.exclusive_fullscreen = value == "1";
      else if (key == "vsync") options.vsync = value == "1";
      else if (key == "widescreen") options.widescreen = value == "1";
      else if (key == "fodreflections") options.fod_reflections = value != "0";
      else if (key == "truewidescreen") options.true_widescreen = value == "1";
      else if (key == "customtextures") options.custom_textures = value == "1";
      else if (key == "dumptextures") options.dump_textures = value == "1";
      else if (key == "prefetchtextures") options.prefetch_textures = value != "0";
      else if (key == "videobackgrounds") options.video_backgrounds = value != "0";
      // One line per pack the player switched off; anything not listed is on, so a pack installed
      // later starts enabled rather than silently doing nothing.
      else if (key == "texpackoff") {
        // A pack is a folder, and a folder name can contain spaces ("HD Textures"); the value is the
        // whole rest of the line.
        auto off = texpack::disabled_packs();
        off.push_back(value);
        texpack::set_disabled_packs(std::move(off));
      }
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
      else if (key == "ssao") options.screen_space_ao = std::clamp(std::stof(value), 0.0f, 1.0f);
      else if (key == "brightness") options.brightness = std::clamp(std::stof(value), 0.5f, 1.5f);
      else if (key == "contrast") options.contrast = std::clamp(std::stof(value), 0.5f, 1.5f);
      else if (key == "vibrance") options.vibrance = std::clamp(std::stof(value), 0.0f, 2.0f);
      else if (key == "anisotropy") { int a = std::stoi(value); if (a == 1 || a == 2 || a == 4 || a == 8 || a == 16) options.anisotropy = a; }
      else if (key == "ssaa") { int a = std::stoi(value); if (a == 1 || a == 2) options.ssaa = a; }
      else if (key == "subframe") options.subframe = value == "0" ? SubFrameMode::Off : value == "2" ? SubFrameMode::AuthoredInterpolate : SubFrameMode::Authored;
      else if (key == "music") slippi::jukebox::set_user_volume(std::stoi(value));
      else if (key == "onlinedelay") { int d = std::atoi(value.c_str()); if (d >= 1 && d <= 9) slippi::online::config().delay = d; }
      else if (key == "performance") options.performance_overlay = value == "1";
      else if (key == "showfps") options.show_fps = value == "1";
      else if (key == "showvram") options.show_vram = value == "1";
      else if (key == "showping") options.show_ping = value == "1";
      // Diagnostic, off unless someone is hunting a one-frame glitch: see D3D12Options::flicker_scan.
      // Settings-file only rather than a control in the panel, because it costs a readback on every
      // presented frame and nobody should switch it on by browsing.
      else if (key == "flickerscan") options.flicker_scan = value == "1";
      else if (key == "settingsreminder") options.settings_hint = value != "0";
      else if (key == "legacymenu") options.legacy_menu_enabled = value == "1";
      else if (key == "legacymenustyle") options.legacy_menu_style = value == "6" ? 6 : 7;
      else if (key == "menusounds") options.settings_menu_sounds = value == "1";
      else if (key == "menucustom") options.settings_custom_color_enabled = value == "1";
      else if (key == "menucolor") {
        std::istringstream color(value);
        color >> options.settings_custom_color[0] >> options.settings_custom_color[1] >> options.settings_custom_color[2];
        for (float& channel : options.settings_custom_color) channel = std::clamp(channel,0.0f,1.0f);
      }
      else if (key == "settingstransparency")
        options.settings_transparency = std::clamp(std::atoi(value.c_str()), 0, 65);
      else if (key == "overlaystyle") {
        int selected = std::atoi(value.c_str());
        options.overlay_style = selected >= 0 && selected <= 7 ? selected : 0;
      }
      else if (key.size() == 15 && key.compare(0, 14, "overlaypalette") == 0 &&
               key[14] >= '0' && key[14] <= '6')
        options.overlay_palettes[key[14] - '0'] = std::clamp(std::atoi(value.c_str()), 0, 3);
      else if (key == "stockhudscale") options.stock_hud_scale = std::clamp(std::atoi(value.c_str()), 75, 175);
      else if (key == "damagehudscale") options.damage_hud_scale = std::clamp(std::atoi(value.c_str()), 75, 175);
      else if (key == "playernicknames") options.show_player_nicknames = value == "1";
      else if (key == "matchmakinghint") options.matchmaking_hint = value != "0";
      else if (key == "effects") { int n = std::atoi(value.c_str()); if (n >= 0 && n <= 2) options.effects_level = n; }
      else if (key == "inputoverlay") options.input_overlay = value == "1";
      // Settings saved before the overlay could show several ports name a single port number.
      else if (key == "inputoverlayport") { int n = std::atoi(value.c_str()); if (n >= 0 && n < 4) options.input_overlay_ports = 1 << n; }
      else if (key == "inputoverlayports") { int n = std::atoi(value.c_str()); if (n >= 0 && n < 16) options.input_overlay_ports = n; }
      else if (key == "inputoverlayhideborder") options.input_overlay_hide_border = value == "1";
      else if (key == "inputoverlayvalues") options.input_overlay_values = value == "1";
      else if (key == "inputoverlaystick") options.input_overlay_stick = std::clamp(std::atoi(value.c_str()), 1, 10);
      else if (key == "lcancelindicator") lcancel::set_indicator(value == "1");
      else if (key == "autolcancel") lcancel::set_automatic(value == "1");
      else if (key == "palstockicons") gecko::option_pal_stock_icons = value == "1";
      else if (key == "noscreenshake") gecko::option_no_screen_shake = value == "1";
      else if (key == "geckocode") gecko_on.push_back(value);
      else if (key == "geckochosen") gecko_chosen = value == "1";
      else if (key == "swpro_gc_picture") g_swpro_gc_picture = value == "1";
      else if (key == "rumble") host::g_rumble_enabled.store(value != "0", std::memory_order_relaxed);
      else if (key == "backgroundinput") host::g_background_input = value != "0";
      else if (key == "editdevice") g_saved_edit_tab = std::atoi(value.c_str());
      // "activeprofile<device> <name>": the profile each controller uses, so it is still the one
      // shown (and the one changes are saved to) after a restart.
      else if (key.rfind("activeprofile", 0) == 0 && key.size() > 13 && std::isdigit((unsigned char)key[13])) {
        const int t = std::atoi(key.c_str() + 13);
        if (t >= 0 && t < kDeviceTabs) { g_active_profile[t] = value; g_named_profile_active[t] = true; }
      }
      else if (key == "custompreset") {
        CustomPreset c; c.set = true;
        if (std::sscanf(value.c_str(), "%d %d %d %d %lf %d", &c.efb, &c.ssaa, &c.aniso, &c.dlss, &c.fps, &c.sub) == 6) g_custom_preset = c;
      }
      else if (load_family_option(key, value)) {}
      else if (key == "startup") options.settings_open = value != "0";
      else if (key == "dlss") { int m = std::stoi(value); if (m >= 0 && m <= 10) options.dlss_mode = m; }
      // Pre-multiplier saves wrote 0 or 1; both still mean what they always meant (off / 2x).
      else if (key == "framegen") options.frame_generation_mode = std::clamp(std::stoi(value), 0, 6);
      else if (key == "reflex") options.reflex_mode = std::clamp(std::atoi(value.c_str()), 0, 2);
      else if (key == "reflexstats") options.reflex_stats = value == "1";
      else if (key == "reflexflash") options.reflex_flash = value == "1";
      else if (key == "pathtracing") options.path_tracing = value == "1";
      else if (key == "rayreconstruction") options.ray_reconstruction = value == "1";
#ifdef GX_DLSS5
      else if (key == "dlss5") options.dlss5 = value == "1";
      else if (key == "dlss5intensity") options.dlss5_tuning.intensity = std::clamp(std::stof(value), 0.0f, 10.0f);
      else if (key == "dlss5detail") options.dlss5_tuning.detail = std::clamp(std::stof(value), 0.0f, 10.0f);
      else if (key == "dlss5tone") options.dlss5_tuning.tone = std::clamp(std::stof(value), 0.0f, 10.0f);
      else if (key == "dlss5skin") options.dlss5_tuning.skin = std::clamp(std::stof(value), -1.0f, 10.0f);
      else if (key == "dlss5style") options.dlss5_tuning.style = std::clamp(std::stoi(value), 0, 3);
      else if (key == "dlss5preset") options.dlss5_tuning.preset = std::clamp(std::stoi(value), 0, 3);
      else if (key == "dlss5automask") options.dlss5_tuning.auto_mask = value == "1";
#endif
      // Low spec: the switch, then what the player had before it was turned on, so turning it off
      // after a restart still restores their own settings rather than the defaults.
      else if (key == "lowspec") options.low_spec = value == "1";
      else if (key == "lowspec_prev_backend") options.low_spec_previous.api = value == "d3d11" ? RenderApi::D3D11 : RenderApi::D3D12;
      else if (key == "lowspec_prev_fps") { double rate = std::stod(value); if (rate == -1 || rate == 0 || (rate >= 30 && rate <= 2000)) options.low_spec_previous.fps_cap = rate; }
      else if (key == "lowspec_prev_scale") { int n = std::stoi(value); if (n >= 0 && n <= 8) options.low_spec_previous.efb_scale = n; }
      else if (key == "lowspec_prev_ssaa") { int n = std::stoi(value); if (n == 1 || n == 2) options.low_spec_previous.ssaa = n; }
      else if (key == "lowspec_prev_anisotropy") { int n = std::stoi(value); if (n == 1 || n == 2 || n == 4 || n == 8 || n == 16) options.low_spec_previous.anisotropy = n; }
      else if (key == "lowspec_prev_effects") { int n = std::stoi(value); if (n >= 0 && n <= 2) options.low_spec_previous.effects_level = n; }
      else if (key == "lowspec_prev_dlss") { int n = std::stoi(value); if (n >= 0 && n <= 10) options.low_spec_previous.dlss_mode = n; }
      else if (key == "lowspec_prev_subframe") options.low_spec_previous.subframe = value == "0" ? SubFrameMode::Off : value == "2" ? SubFrameMode::AuthoredInterpolate : SubFrameMode::Authored;
      else if (key == "discord") options.discord_presence = value == "1";
      // A Discord application id is a snowflake; anything else would only be rejected by Discord.
      else if (key == "discord_app_id") { if (value.find_first_not_of("0123456789") == std::string::npos && value.size() <= 24) options.discord_app_id = value; }
      else if (key == "backend") options.api = value == "d3d11" ? RenderApi::D3D11 : RenderApi::D3D12;
      else if (key == "volume") { volume = std::clamp(std::stoi(value), 0, 100); g_volume = volume; }
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
      // Generic HID pads. The mask is 32 bits, so this parses as unsigned rather than int.
      else if (key.size() > 4 && key.rfind("hid", 0) == 0 && std::isdigit((unsigned char)key[3])) {
        size_t us = key.find('_');
        if (us != std::string::npos) {
          int idx = std::stoi(key.substr(3, us - 3));
          std::string action = key.substr(us + 1);
          if (idx >= 0 && idx < 4)
            for (int i = 0; i < (int)host::BindAction::Count; ++i)
              if (action == kActionNames[i]) host::g_hid_bindings[idx].mask[i] = (uint32_t)std::stoul(value);
        }
      }
      // Generic HID pads. The mask is 32 bits, so this parses as unsigned rather than int.
      else if (key.size() > 4 && key.rfind("hid", 0) == 0 && std::isdigit((unsigned char)key[3])) {
        size_t us = key.find('_');
        if (us != std::string::npos) {
          int idx = std::stoi(key.substr(3, us - 3));
          std::string action = key.substr(us + 1);
          if (idx >= 0 && idx < 4)
            for (int i = 0; i < (int)host::BindAction::Count; ++i)
              if (action == kActionNames[i]) host::g_hid_bindings[idx].mask[i] = (uint32_t)std::stoul(value);
        }
      }
      // "port<n> <comboIndex>" - comboIndex uses the same 0-9 encoding as the UI combo box.
      else if (key.size() == 9 && key.rfind("portname", 0) == 0 && key[8] >= '0' && key[8] <= '3') host::g_port_device_names[key[8] - '0'] = value;
      else if (key.size() > 4 && key.rfind("port", 0) == 0 && std::isdigit((unsigned char)key[4])) {
        int n = std::stoi(key.substr(4));
        if (n >= 0 && n < 4) host::g_port_sources[n] = combo_to_port_source(std::stoi(value));
      }
    } catch (...) { /* Ignore a malformed preference, retaining the safe default. */ }
  }
  // The old Ultra preset (4x supersampling under DLAA) becomes the new one (DLAA alone); see the
  // presets. Only that exact combination is changed.
  if (options.efb_scale == 0 && options.ssaa == 2 && options.anisotropy == 16 && options.dlss_mode == 1) options.ssaa = 1;
  if (options.exclusive_fullscreen) options.fullscreen = false;
  // The player's Gecko codes live beside the settings file.
  std::filesystem::path codes = std::filesystem::path(options.settings_path).parent_path() / "GeckoCodes.ini";
  user_gecko::load(codes.string(), gecko_on, gecko_chosen);
  g_gecko_chosen = gecko_chosen;
#ifdef GX_DLSS5
  dlss5::profile_set_folder(options.settings_path);
#endif
}

// The ImGui context and the Win32 platform backend are the same for every renderer backend.
std::atomic<bool> g_textures_dirty{false};
// "texpackoff <name>" per pack the player switched off, written with the rest of the settings.
// Tooltips are clipped to the OS window, and the standalone settings window is deliberately narrow,
// so a long one ran off the right edge. Wrapping them at a fixed width keeps every line on screen
// whatever the window size.
void wrapped_tooltip(const char* text) {
  if (!ImGui::IsItemHovered()) return;
  ImGui::BeginTooltip();
  ImGui::PushTextWrapPos(380.0f);
  ImGui::TextUnformatted(text);
  ImGui::PopTextWrapPos();
  ImGui::EndTooltip();
}

std::string texpack_disabled_lines() {
  std::string out;
  for (const auto& name : texpack::disabled_packs()) out += std::string("\n") + "texpackoff " + name;
  return out;
}
const bool g_ui_diag = std::getenv("MELEE_UI_DIAG") != nullptr;   // logs settings panel state transitions
ImGuiStyle g_input_overlay_style;   // 0.6.61 look for the controller overlay, set up at context creation
std::atomic<bool> g_fill_window{false};
std::atomic<bool> g_close_requested{false};
bool settings_close_requested() { return g_close_requested.exchange(false, std::memory_order_relaxed); }
void settings_fill_window(bool on) { g_fill_window.store(on, std::memory_order_relaxed); }
bool settings_textures_dirty() { return g_textures_dirty.exchange(false, std::memory_order_relaxed); }

// The appearance layouts share one settings model and page controls; each supplies its own
// navigation, home screen, motion, and color treatment.
static void draw_settings_icon(int index, ImVec2 p, ImU32 color) {
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const float x = p.x, y = p.y;
  switch (index) {
    case 0: // display
      draw->AddRect(ImVec2(x - 11, y - 8), ImVec2(x + 11, y + 6), color, 2.0f, 0, 2.0f);
      draw->AddLine(ImVec2(x, y + 6), ImVec2(x, y + 11), color, 2);
      draw->AddLine(ImVec2(x - 6, y + 11), ImVec2(x + 6, y + 11), color, 2);
      break;
    case 1: // speaker
      draw->AddRectFilled(ImVec2(x - 11, y - 4), ImVec2(x - 5, y + 4), color);
      draw->AddTriangleFilled(ImVec2(x - 5, y - 4), ImVec2(x + 3, y - 9), ImVec2(x + 3, y + 9), color);
      draw->AddLine(ImVec2(x + 7, y - 7), ImVec2(x + 11, y - 3), color, 2);
      draw->AddLine(ImVec2(x + 11, y - 3), ImVec2(x + 11, y + 3), color, 2);
      draw->AddLine(ImVec2(x + 11, y + 3), ImVec2(x + 7, y + 7), color, 2);
      break;
    case 2: // gamepad
      draw->AddRect(ImVec2(x - 13, y - 6), ImVec2(x + 13, y + 8), color, 7.0f, 0, 2.0f);
      draw->AddLine(ImVec2(x - 8, y + 1), ImVec2(x - 2, y + 1), color, 2);
      draw->AddLine(ImVec2(x - 5, y - 2), ImVec2(x - 5, y + 4), color, 2);
      draw->AddCircleFilled(ImVec2(x + 6, y), 2, color);
      draw->AddCircleFilled(ImVec2(x + 10, y + 3), 2, color);
      break;
    case 3: // controls: directional cross
      draw->AddRect(ImVec2(x - 10, y - 10), ImVec2(x + 10, y + 10), color, 4.0f, 0, 2.0f);
      draw->AddLine(ImVec2(x - 6, y), ImVec2(x + 6, y), color, 2);
      draw->AddLine(ImVec2(x, y - 6), ImVec2(x, y + 6), color, 2);
      break;
    case 4: // layers
      draw->AddRect(ImVec2(x - 12, y - 9), ImVec2(x + 5, y + 5), color, 2.0f, 0, 2.0f);
      draw->AddRect(ImVec2(x - 5, y - 3), ImVec2(x + 12, y + 10), color, 2.0f, 0, 2.0f);
      break;
    case 5: // palette
      draw->AddCircle(ImVec2(x, y), 11, color, 0, 2);
      draw->AddCircleFilled(ImVec2(x - 5, y - 4), 2, color);
      draw->AddCircleFilled(ImVec2(x + 3, y - 6), 2, color);
      draw->AddCircleFilled(ImVec2(x + 7, y + 2), 2, color);
      break;
    default: // code brackets
      draw->AddLine(ImVec2(x - 3, y - 8), ImVec2(x - 10, y), color, 2);
      draw->AddLine(ImVec2(x - 10, y), ImVec2(x - 3, y + 8), color, 2);
      draw->AddLine(ImVec2(x + 3, y - 8), ImVec2(x + 10, y), color, 2);
      draw->AddLine(ImVec2(x + 10, y), ImVec2(x + 3, y + 8), color, 2);
      break;
  }
}

#include "ui_sources/gd_melee/text.inl"
#include "pc_settings_visuals.inl"

static void settings_gd_chrome(ImVec2 origin, float scale, const char* title = "SETTINGS") {
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const auto point = [&](float x, float y) {
    return ImVec2(origin.x + (x + (240.0f - y) * 0.25f) * scale, origin.y + y * scale);
  };
  const auto quad = [&](float x0, float y0, float x1, float y1, ImU32 color) {
    const ImVec2 vertices[] = {point(x0,y0), point(x1,y0), point(x1,y1), point(x0,y1)};
    draw->AddConvexPolyFilled(vertices, 4, color);
  };
  // Current GD kit, KS_OPTIONS: exact unsheared chrome geometry and section palette.
  // This replaces fe_draw_frame's old blue bitmap frame with the current kit renderer.
  draw->AddRectFilled(origin, ImVec2(origin.x + 640.0f * scale, origin.y + 480.0f * scale),
                      IM_COL32(25, 33, 42, 252));
  const auto art = [&](const char* name,float x,float y,float w,float h,ImU32 tint) {
    if (auto* tex=cosmetic_preview(ui_source_asset_path("gd_melee",name)))
      draw->AddImageQuad(tex->GetTexRef(),point(x,y),point(x+w,y),point(x+w,y+h),point(x,y+h),
                         ImVec2(0,0),ImVec2(1,0),ImVec2(1,1),ImVec2(0,1),tint);
  };
  const float title_width = gd_kit_text_width("title", title);
  quad(84, 24, 120 + title_width, 52, settings_accent(2));
  quad(116 + title_width, 46, 550, 52, IM_COL32(10, 14, 24, 255));
  gd_kit_text(draw, "title", origin, scale, 100, 45.92f, title, IM_COL32(10, 14, 24, 255));
  art("kit/ico_options.png",96,56,16,16,IM_COL32(149,157,167,255));
  gd_kit_text(draw,"body",origin,scale,118,68.62f,"SETTINGS",IM_COL32(184,194,220,255));
  const float crumb=118+gd_kit_text_width("body","SETTINGS");
  quad(crumb+6,58.62f,crumb+8,68.62f,settings_accent(2));
  gd_kit_text(draw,"body",origin,scale,crumb+14,68.62f,std::strcmp(title,"SETTINGS")==0?"PC SETTINGS":title,IM_COL32(242,239,228,255));
  quad(90, 398, 596, 422, IM_COL32(10, 14, 24, 255));
  quad(90, 430, 596, 456, IM_COL32(10,14,24,255));
  quad(100,437,106,449,settings_accent(2));
  const char* glyphs[]={"kit/glyph_a.png","kit/glyph_dpad.png","kit/glyph_b.png"};
  const char* hints[]={"Select","Change","Back"};
  const ImU32 tints[]={IM_COL32(39,184,138,255),IM_COL32(242,239,228,255),IM_COL32(229,72,59,255)};
  float hint_x=114;
  for(int i=0;i<3;++i) {
    if(i==2)g_settings_gd_back_x=point(hint_x,435).x;
    art(glyphs[i],hint_x,435,16,16,tints[i]);
    gd_kit_text(draw,"body",origin,scale,hint_x+20,447.62f,hints[i],IM_COL32(242,239,228,255));
    hint_x+=32+gd_kit_text_width("body",hints[i]);
  }
}

static void settings_gd_home(SettingsState& state) {
  static constexpr const char* names[] = {
      "VIDEO", "AUDIO", "GAME", "CONTROLS", "OVERLAYS", "CUSTOMIZE", "GECKO CODES"};
  static constexpr const char* help[] = {
      "Adjust graphics and display settings.", "Set music and game sound.",
      "Choose how Melee plays.", "Set up controllers and bindings.",
      "Choose the information shown during play.", "Choose an appearance and menu artwork.",
      "Manage optional game codes."};
  const ImVec2 origin = ImGui::GetWindowPos();
  const float scale = ImGui::GetWindowWidth() / 640.0f;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const auto point = [&](float x, float y) {
    return ImVec2(origin.x + (x + (240.0f - y) * 0.25f) * scale, origin.y + y * scale);
  };
  const auto quad = [&](float x, float y, float w, float h, ImU32 color) {
    const ImVec2 vertices[] = {point(x,y), point(x+w,y), point(x+w,y+h), point(x,y+h)};
    draw->AddConvexPolyFilled(vertices, 4, color);
  };
  const ImVec2 saved_cursor = ImGui::GetCursorPos();
  settings_gd_chrome(origin, scale);
  state.gd_screen_frames += ImGui::GetIO().DeltaTime * 60.0f;
  for (int i = 0; i < 7; ++i) {
    const float t = std::clamp((state.gd_screen_frames - 2.0f * i) / 12.0f, 0.0f, 1.0f);
    const float offset = 48.0f * std::pow(1.0f - t, 3.0f);
    const float x = 96.0f + offset, y = 84.0f + 34.0f * i;
    ImGui::SetCursorScreenPos(point(x - 7.5f, y));
    ImGui::PushID(i);
    bool activated = settings_hit_button("##gd_category", ImVec2(451.5f * scale, 30.0f * scale));
    if (state.home_focus_reset && i == 0) {
      ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
      state.home_focus_reset = false;
    }
    if (i == state.active_tab) ImGui::SetItemDefaultFocus();
    const bool focused = ImGui::IsItemFocused();
    const bool hovered = ImGui::IsItemHovered();
    activated = activated ||
        (focused && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false));
    ImGui::PopID();
    if (focused && state.open && settings_nav_input_pressed()) state.active_tab = i;
    if (hovered && state.open) state.active_tab = i;
    const bool selected = state.active_tab == i;
    if (selected) quad(x, y, 444, 30, settings_palette_tint(2,IM_COL32(169, 118, 26, 255)));
    const float dx = selected ? -5.0f : 0.0f, dy = selected ? -5.0f : 0.0f;
    quad(x+dx, y+dy, 444, 30, selected ? settings_accent(2) : IM_COL32(46, 54, 64, 255));
    gd_kit_text(draw, "row", origin, scale, x+16+dx, y+20.28f+dy,
                names[i], selected ? IM_COL32(10, 14, 24, 255) : IM_COL32(242, 239, 228, 255));
    gd_kit_text(draw, "row", origin, scale, x+414+dx, y+20.28f+dy,
                ">", selected ? IM_COL32(10, 14, 24, 255) : IM_COL32(149, 157, 167, 255));
    if (activated && state.open) {
      state.active_tab = i;
      state.gd_detail_open = true;
      state.gd_screen_frames = 0.0f;
      state.content_anim_frame = 0.0f;
    }
  }
  gd_kit_text(draw, "body", origin, scale, 104, 414.0f, help[state.active_tab], IM_COL32(242, 239, 228, 255), 472);
  ImGui::SetCursorPos(saved_cursor);
}

static void settings_radial_scene() {
  // The wheel supplies its own contrast. Leave the surrounding game untouched.
}

static void settings_radial_nav(SettingsState& state, ImVec2 center, float radius, bool home) {
  static constexpr const char* labels[] = {
      "VIDEO", "AUDIO", "GAME", "CONTROLS", "OVERLAYS", "CUSTOMIZE", "CODES"};
  constexpr float pi = radial_navigation::kPi;
  constexpr float step = radial_navigation::kStep;
  const float inner = radius * 0.44f;
  const float mid = radius * 0.73f;
  const ImVec2 saved_cursor = ImGui::GetCursorPos();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  // The wheel owns the D-pad and stick only on the home screen. On a category page they move
  // through that page's settings; turning the wheel there switched category mid-scroll.
  const bool up = home && (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp,true) || ImGui::IsKeyPressed(ImGuiKey_UpArrow,true));
  const bool right = home && (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight,true) || ImGui::IsKeyPressed(ImGuiKey_RightArrow,true));
  const bool down = home && (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown,true) || ImGui::IsKeyPressed(ImGuiKey_DownArrow,true));
  const bool left = home && (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft,true) || ImGui::IsKeyPressed(ImGuiKey_LeftArrow,true));
  if (home) {
    state.radial_selection = radial_navigation::update(
        state.radial_selection, up, right, down, left,
        state.settings_stick_x, state.settings_stick_y);
    state.active_tab = state.radial_selection;
  }
  draw->AddCircleFilled(ImVec2(center.x+6,center.y+18),radius+15.0f,IM_COL32(0,3,11,82),128);
  draw->AddCircleFilled(ImVec2(center.x+1,center.y+9),radius+11.0f,IM_COL32(2,8,18,255),128);
  draw->AddCircleFilled(ImVec2(center.x,center.y+5),radius+9.0f,IM_COL32(35,48,63,255),128);
  draw->AddCircleFilled(center,radius+3.0f,IM_COL32(14,23,35,255),128);
  draw->AddCircle(center,radius+8.0f,IM_COL32(89,111,130,215),128,1.4f);
  draw->AddCircle(center,radius+3.0f,IM_COL32(28,43,59,255),128,3.0f);
  for (int i = 0; i < 7; ++i) {
    const float angle = -pi * 0.5f + i * step;
    const float start = angle - step * 0.47f;
    const float finish = angle + step * 0.47f;
    const ImVec2 c(center.x + std::cos(angle) * mid,
                   center.y + std::sin(angle) * mid);
    const ImVec2 hit(std::max(64.0f, radius * 0.60f), std::max(46.0f, radius * 0.44f));
    ImGui::SetCursorScreenPos(ImVec2(c.x - hit.x * 0.5f, c.y - hit.y * 0.5f));
    ImGui::PushID(i);
    const bool activated_by_mouse = settings_hit_button("##wheel_category", hit, 0);
    const bool selected = home ? state.radial_selection == i : state.active_tab == i;
    if (selected) ImGui::SetItemDefaultFocus();
    // Mouse hover only: ImGui otherwise reports the nav-focused sector (VIDEO, where focus lands on
    // open) as hovered, so it stayed lit while the D-pad selection moved elsewhere.
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride);
    ImGui::PopID();
    const float target = hovered || selected ? 1.0f : 0.0f;
    state.radial_hover[i] += (target - state.radial_hover[i]) *
        std::min(1.0f, ImGui::GetIO().DeltaTime * 11.0f);
    const float glow=state.radial_hover[i];
    const ImU32 outer=settings_mix_color(IM_COL32(53,72,94,255),settings_accent(3),glow*.94f);
    const ImU32 inner_color=settings_mix_color(IM_COL32(29,43,60,255),settings_accent(3),glow*.62f);
    const ImDrawListFlags saved_flags = draw->Flags;
    draw->Flags &= ~ImDrawListFlags_AntiAliasedFill;
    for (int slice = 0; slice < 16; ++slice) {
      const float a0 = start + (finish - start) * slice / 16.0f;
      const float a1 = start + (finish - start) * (slice + 1) / 16.0f;
      const auto point = [&](float a, float r) {
        return ImVec2(center.x + std::cos(a) * r, center.y + std::sin(a) * r);
      };
      const int first=draw->VtxBuffer.Size;
      draw->AddQuadFilled(point(a0, radius-4.0f), point(a1, radius-4.0f),
                          point(a1, inner+4.0f), point(a0, inner+4.0f), IM_COL32_WHITE);
      draw->VtxBuffer[first].col=outer;
      draw->VtxBuffer[first+1].col=outer;
      draw->VtxBuffer[first+2].col=inner_color;
      draw->VtxBuffer[first+3].col=inner_color;
    }
    draw->Flags = saved_flags;
    draw->PathArcTo(center,radius-7.0f,start+.027f,finish-.027f,16);
    draw->PathStroke(glow>.05f?IM_COL32(181,226,255,235):IM_COL32(124,157,185,112),0,
                     glow>.05f?2.6f:1.4f);
    draw->PathArcTo(center,inner+7.0f,start+.026f,finish-.026f,16);
    draw->PathStroke(IM_COL32(0,5,15,230),0,2.8f);
    draw->AddLine(ImVec2(center.x + std::cos(start) * inner,
                         center.y + std::sin(start) * inner),
                  ImVec2(center.x + std::cos(start) * radius,
                         center.y + std::sin(start) * radius),
                  IM_COL32(4,12,24,230), 3.0f);
    const ImVec2 icon(c.x, c.y - radius*.070f);
    const int icon_start = draw->VtxBuffer.Size;
    if(settings_icon_variant()==0) {
      draw_settings_icon(i,icon,IM_COL32(236,247,255,255));
      const float icon_scale=std::max(1.0f,radius/155.0f);
      for(int v=icon_start;v<draw->VtxBuffer.Size;++v) {
        ImVec2& pos=draw->VtxBuffer[v].pos;
        pos=ImVec2(icon.x+(pos.x-icon.x)*icon_scale,icon.y+(pos.y-icon.y)*icon_scale);
      }
    } else settings_symbol_icon(i,icon,std::max(1.0f,radius/195.0f),IM_COL32(238,247,255,255));
    const float label_font=std::clamp(radius*.095f,14.0f,18.0f);
    const ImVec2 label_size=settings_heading_font()->CalcTextSizeA(label_font,FLT_MAX,0.0f,labels[i]);
    draw->AddText(settings_heading_font(),label_font,
                  ImVec2(c.x-label_size.x*.5f,c.y+radius*.095f),
                  IM_COL32(242,247,252,255),labels[i]);
    const bool activated_by_pad = home && selected && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown,false);
    const bool activated_by_key = home && selected && (ImGui::IsKeyPressed(ImGuiKey_Enter,false) ||
                                                         ImGui::IsKeyPressed(ImGuiKey_Space,false));
    if ((activated_by_mouse || activated_by_pad || activated_by_key) && state.open) {
      state.active_tab = i;
      state.radial_selection = i;
      if (home) state.radial_detail_open = true;
    }
  }
  draw->AddCircleFilled(ImVec2(center.x,center.y+8),inner-1.0f,IM_COL32(0,5,15,255),96);
  draw->AddCircleFilled(center,inner-3.0f,IM_COL32(56,77,93,255),96);
  draw->AddCircleFilled(center,inner-10.0f,IM_COL32(10,19,31,255),96);
  draw->AddCircle(center,inner-4.0f,IM_COL32(124,153,173,210),96,1.4f);
  draw->AddCircle(center,inner-12.0f,IM_COL32(26,48,69,255),96,3.0f);
  draw->PathArcTo(center,inner-7.0f,-.35f,1.36f,32);
  draw->PathStroke(settings_accent(3),0,4.0f);
  const float hub_scale=std::clamp(radius/210.0f,.75f,1.25f);
  draw->AddCircle(center,19.0f*hub_scale,IM_COL32(205,229,241,255),48,2.4f*hub_scale);
  draw->AddLine(center,ImVec2(center.x,center.y-12*hub_scale),IM_COL32_WHITE,2.5f*hub_scale);
  draw->AddLine(center,ImVec2(center.x+9*hub_scale,center.y+5*hub_scale),IM_COL32_WHITE,2.5f*hub_scale);
  draw->AddCircleFilled(center,2.5f*hub_scale,IM_COL32_WHITE,20);
  const char* center_text=home?"SETTINGS":"BACK";
  const float center_size=std::clamp(radius*.071f,11.0f,15.0f);
  const ImVec2 text_size=settings_heading_font()->CalcTextSizeA(center_size,FLT_MAX,0,center_text);
  draw->AddText(settings_heading_font(),center_size,
                ImVec2(center.x-text_size.x*.5f,center.y+25*hub_scale),
                IM_COL32(232,242,248,255),center_text);
  if (!home) {
    const float d = inner * 1.6f;
    ImGui::SetCursorScreenPos(ImVec2(center.x - d * 0.5f, center.y - d * 0.5f));
    if (settings_hit_button("##wheel_home", ImVec2(d, d), 0) && state.open)
      state.radial_detail_open = false;
  }
  ImGui::SetCursorPos(saved_cursor);
}

static bool settings_nav_strip(int index, bool selected, ImVec2 size) {
  static constexpr const char* labels[] = {
      "VIDEO", "AUDIO", "GAME", "CONTROLS", "OVERLAYS", "CUSTOMIZE", "CODES"};
  ImGui::PushID(index);
  const bool clicked = settings_hit_button("##strip_category", size);
  if (selected) ImGui::SetItemDefaultFocus();
  const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  if (selected || hovered) {
    draw->AddRectFilled(a,b,selected ? settings_palette_tint(4,IM_COL32(17,112,224,255)) : IM_COL32(35,57,94,230),size.y*.5f);
    if (selected) draw->AddRect(a,b,settings_palette_tint(4,IM_COL32(105,196,255,255)),size.y*.5f,0,1.5f);
  }
  const ImVec2 text_size = ImGui::CalcTextSize(labels[index]);
  draw->AddText(ImVec2((a.x + b.x - text_size.x) * 0.5f,
                        (a.y + b.y - text_size.y) * 0.5f),
                IM_COL32(245, 249, 255, 255), labels[index]);
  ImGui::PopID();
  return clicked;
}

static void settings_clean_chrome(const char* heading, float slide_offset) {
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
  const ImVec2 end(p.x + s.x, p.y + s.y);
  // The title is anchored to the game's top-left, independently of the category stack
  // on the right. Only these shards and the cards paint over the live game.
  const ImVec2 display=ImGui::GetIO().DisplaySize;
  const float game_left=std::max(0.0f,(display.x-display.y*(73.0f/60.0f))*.5f);
  const float hx=game_left+18.0f-std::abs(slide_offset)*.58f, hy=18.0f;
  const float hw=std::clamp(display.x*.39f,300.0f,420.0f);
  ImDrawList* front=ImGui::GetForegroundDrawList();
  const ImVec2 header[]={ImVec2(hx+24,hy),ImVec2(hx+hw,hy),
      ImVec2(hx+hw-75,hy+94),ImVec2(hx-10,hy+94)};
  front->AddConvexPolyFilled(header,4,settings_palette_tint(0,IM_COL32(226,20,126,245)));
  const ImVec2 slash[]={ImVec2(hx+hw*.40f,hy),ImVec2(hx+hw*.55f,hy),
      ImVec2(hx+hw*.55f-75,hy+94),ImVec2(hx+hw*.40f-75,hy+94)};
  front->AddConvexPolyFilled(slash,4,IM_COL32(11,13,29,250));
  front->AddLine(ImVec2(hx+4,hy+94),ImVec2(hx+hw-78,hy+94),
                 settings_palette_tint(0,IM_COL32(255,134,197,240)),2.0f);
  front->AddText(ImGui::GetFont(),12.0f,ImVec2(hx+24,hy+12),
                 IM_COL32(255,235,248,255),"MELEE UNLOCKED");
  const float heading_size=std::strlen(heading)>9?34.0f:42.0f;
  settings_slanted_text(front,settings_heading_font(),heading_size,
      ImVec2(hx+23,hy+46),IM_COL32_WHITE,heading,.08f);
  const float chip_w=(s.x-78.0f)*.5f;
  for(int chip=0;chip<2;++chip) {
    const float x=p.x+11.0f+chip*(chip_w+5.0f);
    const ImVec2 shadow[]={ImVec2(x+11,end.y-24),ImVec2(x+chip_w+2,end.y-36),
                           ImVec2(x+chip_w-7,end.y-8),ImVec2(x,end.y+2)};
    draw->AddConvexPolyFilled(shadow,4,IM_COL32(0,0,0,110));
    const ImVec2 card[]={ImVec2(x+12,end.y-29),ImVec2(x+chip_w,end.y-41),
                         ImVec2(x+chip_w-10,end.y-11),ImVec2(x,end.y-1)};
    draw->AddConvexPolyFilled(card,4,chip==0?
        settings_palette_tint(0,IM_COL32(145,14,89,248)):IM_COL32(28,17,35,245));
    draw->AddLine(card[0],card[1],settings_palette_tint(0,IM_COL32(255,135,204,220)),1.8f);
  }
}

static void settings_clean_home(SettingsState& state) {
  static constexpr const char* names[] = {
      "VIDEO", "AUDIO", "GAME", "CONTROLS", "OVERLAYS", "CUSTOMIZE", "GECKO CODES"};
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
  const ImVec2 saved = ImGui::GetCursorPos();
  state.clean_home_frames = std::min(24.0f,
      state.clean_home_frames + ImGui::GetIO().DeltaTime * 60.0f);
  const float row_h = std::min(61.0f, (s.y - 210.0f) / 7.0f);
  const float step = row_h - 1.0f;
  for (int i = 0; i < 7; ++i) {
    const float t = std::clamp((state.clean_home_frames - i * 1.25f) / 14.0f, 0.0f, 1.0f);
    const float eased = t * t * (3.0f - 2.0f * t);
    const float entry_x=p.x+19.0f+(1.0f-eased)*90.0f;
    const float y = p.y + 161.0f + i * step;
    const float width = s.x - 39.0f;
    ImGui::SetCursorScreenPos(ImVec2(entry_x, y-width*0.05f));
    ImGui::PushID(i);
    const bool clicked = settings_hit_button("##clean_category", ImVec2(width, row_h));
    if (state.home_focus_reset && i == 0) {
      ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
      state.home_focus_reset = false;
    }
    if (i == state.active_tab) ImGui::SetItemDefaultFocus();
    const bool nav_focused = ImGui::IsItemFocused();
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    if (nav_focused && state.open && settings_nav_input_pressed()) state.active_tab = i;
    if (hovered && state.open) state.active_tab = i;
    const bool selected = state.active_tab == i;
    const float target=selected?1.0f:0.0f;
    state.clean_hover[i]+=(target-state.clean_hover[i])*
        std::min(1.0f,ImGui::GetIO().DeltaTime*13.0f);
    const float x=entry_x-state.clean_hover[i]*10.0f;
    // Only the active category reads as selected. The old
    // alternating accent rows made Gecko Codes look active even when untouched.
    const ImU32 face = selected ? settings_palette_tint(0,IM_COL32(247, 31, 143, 250)) :
                                  IM_COL32(12, 15, 28, 232);
    const float tilt = width * 0.10f;
    const ImVec2 depth[] = {ImVec2(x+19,y+8),ImVec2(x+width+3,y-tilt+8),
                            ImVec2(x+width-14,y+row_h-tilt+10),ImVec2(x-3,y+row_h+10)};
    const ImVec2 shadow[] = {ImVec2(x+17,y+12),ImVec2(x+width+6,y-tilt+12),
                             ImVec2(x+width-12,y+row_h-tilt+14),ImVec2(x-5,y+row_h+14)};
    draw->AddConvexPolyFilled(shadow,4,IM_COL32(0,0,0,70));
    draw->AddConvexPolyFilled(depth,4,settings_palette_tint(0,IM_COL32(55,10,47,235)));
    const ImVec2 left_fold[]={ImVec2(x+18,y),ImVec2(x+2,y+row_h*.38f),
                              ImVec2(x-8,y+row_h+7),ImVec2(x+3,y+row_h)};
    draw->AddConvexPolyFilled(left_fold,4,
        selected?settings_palette_tint(0,IM_COL32(102,13,73,250)):IM_COL32(18,19,37,245));
    const ImVec2 poly[] = {ImVec2(x + 18.0f, y), ImVec2(x + width, y-tilt),
                           ImVec2(x + width - 18.0f, y + row_h-tilt), ImVec2(x, y + row_h)};
    draw->AddConvexPolyFilled(poly, 4, face);
    draw->AddLine(ImVec2(x + 18.0f, y + 1.0f), ImVec2(x + width - 1.0f, y + 1.0f-tilt),
                  selected ? IM_COL32(255, 223, 245, 255) :
                             settings_palette_tint(0,IM_COL32(220,72,142,145)), 1.4f);
    draw->AddLine(poly[3],poly[2],selected?
        settings_palette_tint(0,IM_COL32(104,10,70,220)):IM_COL32(56,35,62,215),1.4f);
    draw_settings_icon(i, ImVec2(x + 38.0f, y + row_h * 0.5f-4.0f),
                       IM_COL32(255, 255, 255, 255));
    settings_slanted_text(draw, settings_heading_font(), 23.0f,
                  ImVec2(x + 69.0f, y + (row_h - 23.0f) * 0.5f-7.0f),
                  IM_COL32(255, 255, 255, 255), names[i], 0.10f);
    if (clicked && state.open) {
      state.active_tab = i;
      state.clean_detail_open = true;
    }
  }
  // The A SELECT / B BACK chips drawn by settings_clean_chrome are real buttons: Select opens
  // the highlighted category, Back closes the panel. Hover lifts and lightens the chip.
  {
    const float chip_w = (s.x - 78.0f) * .5f;
    const float bottom = p.y + s.y;
    for (int chip = 0; chip < 2; ++chip) {
      const float x = p.x + 11.0f + chip * (chip_w + 5.0f);
      ImGui::SetCursorScreenPos(ImVec2(x, bottom - 41.0f));
      ImGui::PushID(chip);
      const bool pressed = settings_hit_button("##clean_home_chip", ImVec2(chip_w, 40.0f));
      const bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
      ImGui::PopID();
      state.footer_hover[chip] += ((hot ? 1.0f : 0.0f) - state.footer_hover[chip]) *
                                  std::min(1.0f, ImGui::GetIO().DeltaTime * 16.0f);
      const float glow = state.footer_hover[chip];
      if (glow > 0.01f) {
        const float lift = glow * 4.0f;
        const ImVec2 card[] = {ImVec2(x + 12, bottom - 29 - lift), ImVec2(x + chip_w, bottom - 41 - lift),
                               ImVec2(x + chip_w - 10, bottom - 11 - lift), ImVec2(x, bottom - 1 - lift)};
        draw->AddConvexPolyFilled(card, 4, chip == 0 ?
            settings_palette_tint(0, IM_COL32(247, 31, 143, (int)(250 * glow))) :
            IM_COL32(70, 44, 88, (int)(245 * glow)));
        draw->AddLine(card[0], card[1], IM_COL32(255, 224, 246, (int)(255 * glow)), 2.0f);
      }
      if (pressed && state.open) {
        if (chip == 0) state.clean_detail_open = true;
        else state.open = false;
      }
    }
  }
  settings_slanted_text(draw,settings_heading_font(),13.0f,
      ImVec2(p.x+34.0f,p.y+s.y-27.0f),IM_COL32(255,239,249,255),
      "A  SELECT",.08f);
  settings_slanted_text(draw,settings_heading_font(),13.0f,
      ImVec2(p.x+39.0f+(s.x-78.0f)*.5f,p.y+s.y-27.0f),
      IM_COL32(255,239,249,255),"B  BACK",.08f);
  ImGui::SetCursorScreenPos(ImVec2(p.x + s.x - 37.0f, p.y + 16.0f));
  if (settings_close_hit("##close_clean", ImVec2(28.0f, 28.0f))) state.open = false;
  draw->AddText(ImVec2(p.x + s.x - 30.0f, p.y + 18.0f),
                IM_COL32(255, 255, 255, 255), "X");
  ImGui::SetCursorPos(saved);
}

static void settings_dashboard_home(SettingsState& state) {
  settings_dashboard_tiles(state);
}

static bool settings_nav_rail(int index, bool selected, ImVec2 size) {
  static constexpr const char* names[] = {
      "Video", "Audio", "Game", "Controls", "Overlays", "Customize", "Gecko Codes"};
  static constexpr const char* short_names[] = {
      "VIDEO", "AUDIO", "GAME", "CONTROLS", "OVERLAYS", "CUSTOMIZE", "CODES"};
  ImGui::PushID(index);
  const bool clicked = settings_hit_button("##rail_category", size);
  if (selected) ImGui::SetItemDefaultFocus();
  const bool hovered = ImGui::IsItemHovered();
  const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(a, b, selected ? IM_COL32(116, 60, 165, 255) :
                             hovered ? IM_COL32(46, 57, 101, 255) : IM_COL32(28, 36, 69, 235), 5.0f);
  if (selected) draw->AddRect(a, b, IM_COL32(229, 197, 255, 255), 5.0f, 0, 2.0f);
  draw_settings_icon(index, ImVec2((a.x + b.x) * 0.5f, a.y + 18.0f),
                     IM_COL32(246, 240, 255, 255));
  const ImVec2 label_size = ImGui::GetFont()->CalcTextSizeA(11.0f, FLT_MAX, 0.0f,
                                                           short_names[index]);
  draw->AddText(ImGui::GetFont(), 11.0f,
                ImVec2((a.x + b.x - label_size.x) * 0.5f, a.y + 37.0f),
                IM_COL32(240, 236, 255, 255), short_names[index]);
  if (hovered) ImGui::SetTooltip("%s", names[index]);
  ImGui::PopID();
  return clicked;
}

static void settings_strip_overview(int index) {
  static constexpr const char* names[] = {
      "VIDEO", "AUDIO", "GAME", "CONTROLS", "OVERLAYS", "CUSTOMIZE", "GECKO CODES"};
  static constexpr const char* descriptions[] = {
      "Adjust graphics and display settings.", "Tune game and music sound.",
      "Choose how Melee plays.", "Set up controllers and bindings.",
      "Choose the information shown during play.", "Pick menu art and cosmetic mods.",
      "Manage your optional game codes."};
  const ImVec2 a = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x;
  const float height = 122.0f;
  const float preview_w = std::min(width * 0.33f, 230.0f);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(ImVec2(a.x + preview_w + 5.0f, a.y),
                      ImVec2(a.x + width, a.y + height), IM_COL32(16, 27, 45, 245), 4.0f);
  draw->AddRect(ImVec2(a.x, a.y), ImVec2(a.x + preview_w, a.y + height),
                IM_COL32(118, 168, 213, 255), 3.0f, 0, 2.0f);
  draw->AddRectFilled(ImVec2(a.x + 6.0f, a.y + height - 27.0f),
                      ImVec2(a.x + preview_w - 6.0f, a.y + height - 6.0f),
                      IM_COL32(5, 13, 26, 190), 2.0f);

  draw->AddText(ImVec2(a.x + preview_w + 20.0f, a.y + 20.0f),
                IM_COL32(250, 252, 255, 255), names[index]);
  draw->AddText(ImVec2(a.x + preview_w + 20.0f, a.y + 47.0f),
                IM_COL32(200, 215, 234, 255), descriptions[index]);
  ImGui::Dummy(ImVec2(width, height));
}

static void settings_wide_home(SettingsState& state) {
  const ImVec2 p=ImGui::GetWindowPos(),s=ImGui::GetWindowSize();
  ImDrawList* d=ImGui::GetWindowDrawList();
  // The compact selector is intentionally only a header and category tabs. Selecting
  // a tab opens the real settings page below; the game stays visible around this strip.
  d->AddRectFilled(p,ImVec2(p.x+s.x,p.y+94),IM_COL32(12,21,31,250),9);
  d->AddRect(p,ImVec2(p.x+s.x,p.y+94),IM_COL32(117,144,165,255),9,0,1.5f);
  d->AddLine(ImVec2(p.x,p.y+94),ImVec2(p.x+s.x,p.y+94),IM_COL32(85,116,145,255));
  d->AddText(settings_heading_font(),28,ImVec2(p.x+20,p.y+14),IM_COL32(247,251,255,255),"SETTINGS");
  ImGui::SetCursorPos(ImVec2(18,54));
  const float gap=5, tab=(s.x-36-gap*6)/7;
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(gap,8));
  for(int i=0;i<7;++i) {
    if(settings_nav_strip(i,state.active_tab==i,ImVec2(tab,30))) {
      state.active_tab=i;
      state.wide_detail_open=true;
      state.content_anim_frame=0.0f;
    }
    if(i!=6)ImGui::SameLine();
  }
  ImGui::PopStyleVar();
}

static bool settings_nav_tile(int index, bool selected, int overlay_style, ImVec2 size) {
  static constexpr const char* labels[] = {
      "Video", "Audio", "Game", "Controls", "Overlays", "Customize", "Gecko Codes"};
  static const ImVec4 colors[] = {
      {0.86f, 0.63f, 0.20f, 1}, {0.84f, 0.39f, 0.63f, 1},
      {0.27f, 0.70f, 0.65f, 1}, {0.37f, 0.54f, 0.87f, 1},
      {0.55f, 0.45f, 0.86f, 1}, {0.89f, 0.51f, 0.32f, 1},
      {0.72f, 0.43f, 0.66f, 1}};
  const bool dashboard = overlay_style == 1;
  const bool gd_kit = overlay_style == 2;
  const ImVec4 base = dashboard ? colors[index] :
      (gd_kit ? ImVec4(0.12f, 0.23f, 0.55f, 1) :
       (selected ? ImVec4(0.53f, 0.27f, 0.72f, 1) : ImVec4(0.12f, 0.13f, 0.27f, 0.96f)));
  ImGui::PushID(index);
  ImGui::PushStyleColor(ImGuiCol_Button, gd_kit ? ImVec4(0, 0, 0, 0) : base);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        dashboard ? ImVec4(base.x + 0.08f, base.y + 0.08f, base.z + 0.08f, 1) :
                                    ImVec4(0.40f, 0.27f, 0.62f, 1));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.68f, 0.47f, 0.90f, 1));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, dashboard ? 14.0f : 3.0f);
  bool clicked = ImGui::Button("##category", size);
  if (selected) ImGui::SetItemDefaultFocus();
  const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  if (gd_kit) {
    // Ported GD Melee hub state: uniform 0.25 shear, six-pixel gutter, flat
    // cobalt faces, and the gold lifted face over a gold-dark rest plate.
    // Source values: menu/pipeline/hub_layout.py, see the import boundary.
    const ImVec2 rest[] = {ImVec2(a.x + 12, a.y), ImVec2(b.x + 12, a.y),
                           ImVec2(b.x, b.y), ImVec2(a.x, b.y)};
    if (selected) draw->AddConvexPolyFilled(rest, 4, settings_palette_tint(2,IM_COL32(169, 118, 26, 255)));
    const float dx = selected ? -5.0f : 0.0f;
    const float dy = selected ? -5.0f : 0.0f;
    ImVec2 face[] = {ImVec2(a.x + 12 + dx, a.y + dy), ImVec2(b.x + 12 + dx, a.y + dy),
                     ImVec2(b.x + dx, b.y + dy), ImVec2(a.x + dx, b.y + dy)};
    const bool hovered = ImGui::IsItemHovered();
    draw->AddConvexPolyFilled(face, 4,
        selected ? settings_accent(2) :
        (hovered ? IM_COL32(47, 85, 184, 255) : IM_COL32(30, 58, 140, 255)));
  } else if (selected) {
    draw->AddRect(a, b, IM_COL32(244, 223, 255, 255), dashboard ? 14.0f : 5.0f, 0, 2.0f);
  }
  ImVec2 icon = dashboard ? ImVec2((a.x + b.x) * 0.5f, a.y + (size.y < 60 ? 14.0f : 25.0f)) :
                            ImVec2(a.x + 23, (a.y + b.y) * 0.5f);
  if (gd_kit && selected) icon = ImVec2(icon.x - 5.0f, icon.y - 5.0f);
  const ImU32 icon_color = gd_kit ?
      (selected ? settings_palette_tint(2,IM_COL32(169, 118, 26, 255)) : IM_COL32(74, 122, 224, 255)) :
      IM_COL32(255, 255, 255, 255);
  draw_settings_icon(index, icon, icon_color);
  ImVec2 text_size = ImGui::CalcTextSize(labels[index]);
  ImVec2 text_pos = dashboard ? ImVec2((a.x + b.x - text_size.x) * 0.5f,
                                      b.y - text_size.y - (size.y < 60 ? 5.0f : 12.0f)) :
                                ImVec2(a.x + 48, (a.y + b.y - text_size.y) * 0.5f);
  if (gd_kit && selected) text_pos = ImVec2(text_pos.x - 5.0f, text_pos.y - 5.0f);
  draw->AddText(text_pos, gd_kit ? (selected ? IM_COL32(10, 14, 24, 255) : IM_COL32(242, 239, 228, 255)) : IM_COL32(255, 255, 255, 255), labels[index]);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", labels[index]);
  ImGui::PopStyleVar(); ImGui::PopStyleColor(3); ImGui::PopID();
  return clicked;
}

void settings_context_create(void* window, bool open_at_startup) {
  (void)open_at_startup;
  IMGUI_CHECKVERSION(); ImGui::CreateContext();
  auto& io = ImGui::GetIO(); io.IniFilename = nullptr;
  g_settings_old_font = io.Fonts->AddFontDefault();
  if (GetFileAttributesW(L"C:\\Windows\\Fonts\\segoeui.ttf") != INVALID_FILE_ATTRIBUTES)
    g_settings_body_font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f);
  if (g_settings_body_font) io.FontDefault = g_settings_body_font;
  if (GetFileAttributesW(L"C:\\Windows\\Fonts\\lucon.ttf") != INVALID_FILE_ATTRIBUTES)
    g_settings_classic_font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\lucon.ttf", 16.0f);
  const std::string heading_font_path = ui_source_asset_path("gd_melee", "kit/SourceSans3-Black.otf");
  if (std::filesystem::exists(heading_font_path))
    g_settings_heading_font = io.Fonts->AddFontFromFileTTF(heading_font_path.c_str(), 28.0f);
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
  // Every input change is handled in the frame it arrives. ImGui otherwise takes one change per key
  // per frame, and with two sources feeding the same gamepad keys (our GameCube pad and ImGui's own
  // XInput polling) the queue grew faster than it drained: holding a stick froze the panel for a while.
  io.ConfigInputTrickleEventQueue = false;
  ImGui::StyleColorsDark();
  auto& style = ImGui::GetStyle();
  style.Colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.063f, 0.14f, 0.97f);
  style.Colors[ImGuiCol_ChildBg] = ImVec4(0.075f, 0.08f, 0.19f, 0.60f);
  style.Colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.16f, 0.30f, 1);
  style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.23f, 0.20f, 0.42f, 1);
  style.Colors[ImGuiCol_Button] = ImVec4(0.22f, 0.18f, 0.38f, 1);
  style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.39f, 0.25f, 0.59f, 1);
  style.Colors[ImGuiCol_CheckMark] = ImVec4(0.85f, 0.57f, 0.98f, 1);
  style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.72f, 0.42f, 0.91f, 1);
  style.WindowRounding = 10.0f; style.ChildRounding = 8.0f;
  style.FrameRounding = 6.0f; style.FramePadding = ImVec2(12, 10);
  style.ItemSpacing = ImVec2(12, 14);
  style.ScaleAllSizes(1.15f);
  // The controller overlay keeps the exact 0.6.61 look (dark style scaled 1.25x, ImGui's own font).
  // The menu redesign above is for the settings panel only; players tune their overlay placement
  // and reading around the old one.
  {
    ImGuiStyle legacy;
    ImGui::StyleColorsDark(&legacy);
    legacy.ScaleAllSizes(1.25f);
    g_input_overlay_style = legacy;
  }
  ImGui_ImplWin32_Init(window);
  host::window_set_message_callback([](void* w, uint32_t m, uintptr_t a, intptr_t b) {
    return ImGui_ImplWin32_WndProcHandler((HWND)w, m, a, b) != 0;
  });
}

void settings_context_destroy() {
  for (auto& preview : g_cosmetic_previews)
    if (preview.second) ImGui::UnregisterUserTexture(preview.second.get());
  g_cosmetic_previews.clear();
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
  state.state.panel_anim_target_open = options.settings_open;
  state.state.panel_anim_initialized = true;
  state.state.panel_anim_frame = options.settings_open ? 17.0f : 12.0f;
  state.state.panel_slide_x = options.settings_open ? 0.0f : -720.0f;
  state.state.panel_slide_start_x = state.state.panel_slide_x;
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

static const char* matchmaking_state_text(int state) {
  switch (state) {
    case 0: return "Starting...";
    case 1: return "Initializing...";
    case 2: return "Searching...";
    case 3: return "Opponent found; connecting...";
    case 4: return "Connected";
    case 5: return "Matchmaking error";
    default: return "Searching...";
  }
}

static void draw_native_practice(SettingsState& state, D3D12Options& options,
                                 const slippi::native_practice::Snapshot& practice,
                                 const host::PadState& pad, bool have_pad) {
  using slippi::native_practice::Phase;
  const ImVec2 screen = ImGui::GetIO().DisplaySize;

  if (practice.phase == Phase::Handoff) {
    ImGui::SetNextWindowPos(ImVec2(screen.x * 0.5f, screen.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::Begin("##native_match_found", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted("Match found");
    ImGui::TextDisabled("Connecting...");
    ImGui::End();
    return;
  }

  if (slippi::native_practice::phase_shows_return_overlay(practice.phase,
                                                          practice.in_practice)) {
    ImGui::SetNextWindowPos(ImVec2(screen.x * 0.5f, screen.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::Begin("##native_practice_return", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs |
                 ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted("Returning to practice...");
    ImGui::End();
    return;
  }

  if (practice.phase == Phase::Failure) {
    const bool a_down = have_pad && (pad.button & 0x100) != 0;
    if (state.practice_pad_armed && a_down && !state.practice_a_was_down) {
      slippi::native_practice::submit_acknowledge_failure();
      state.practice_pad_armed = false;
      state.practice_release_capture = true;
    }
    state.practice_a_was_down = a_down;

    ImGui::SetNextWindowPos(ImVec2(screen.x * 0.5f, screen.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::Begin("##native_practice_failure", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted("Disconnected " "\xE2\x80\x94" " Press A to continue");
    if (!practice.detail.empty()) ImGui::TextDisabled("%s", practice.detail.c_str());
    if (!state.practice_pad_armed) ImGui::TextDisabled("Release the controller, then press A.");
    ImGui::End();
    return;
  }

  if (practice.phase == Phase::Searching && !state.practice_open) {
    ImGui::SetNextWindowPos(ImVec2(screen.x - 12, screen.y - 12), ImGuiCond_Always, ImVec2(1, 1));
    ImGui::SetNextWindowBgAlpha(0.72f);
    ImGui::Begin("##native_search_status", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
    ImGui::Text("%s: %s", slippi::native_practice::match_mode_name(practice.mode),
                matchmaking_state_text(practice.matchmaking_state));
    const std::string elapsed = slippi::native_practice::format_search_duration(practice.search_ticks);
    ImGui::TextDisabled("Searching %s", elapsed.c_str());
    ImGui::TextDisabled("Tab: search controls");
    ImGui::End();
  }

  if (!state.practice_open) {
    if (options.matchmaking_hint && practice.tab_available && practice.phase == Phase::Idle) {
      ImGui::SetNextWindowPos(ImVec2(screen.x - 12, 52), ImGuiCond_Always, ImVec2(1, 0));
      ImGui::SetNextWindowBgAlpha(ImGui::GetTime() < 20.0 ? 0.8f : 0.35f);
      ImGui::Begin("##native_practice_hint", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                   ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
      ImGui::TextUnformatted("Matchmaking: Tab");
      ImGui::End();
    }
    return;
  }

  ImGui::SetNextWindowPos(ImVec2(screen.x * 0.5f, screen.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Practice matchmaking", &state.practice_open,
                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::End();
    return;
  }
  ImGui::TextUnformatted("Tab or Esc: return to the game");
  ImGui::Separator();

  ImGui::BeginDisabled(!practice.can_start);
  const bool start_ranked = ImGui::Button("Ranked", ImVec2(126, 32));
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!practice.can_start);
  const bool start_unranked = ImGui::Button("Unranked", ImVec2(126, 32));
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Direct", ImVec2(126, 32))) state.practice_focus_code = true;
  ImGui::TextDisabled("Ranked and Unranked start immediately; Direct uses a code.");
  ImGui::Separator();

  if (start_ranked && practice.can_start) {
    state.practice_error[0] = 0;
    slippi::native_practice::submit_start_ranked();
    state.practice_open = false;
    state.practice_release_capture = true;
    ImGui::End();
    return;
  }

  if (start_unranked && practice.can_start) {
    state.practice_error[0] = 0;
    slippi::native_practice::submit_start_unranked();
    state.practice_open = false;
    state.practice_release_capture = true;
    ImGui::End();
    return;
  }

  if (practice.phase == Phase::Searching) {
    ImGui::Text("Mode: %s", slippi::native_practice::match_mode_name(practice.mode));
    if (!practice.connect_code.empty()) ImGui::Text("Code: %s", practice.connect_code.c_str());
    ImGui::TextUnformatted(matchmaking_state_text(practice.matchmaking_state));
    const std::string elapsed = slippi::native_practice::format_search_duration(practice.search_ticks);
    ImGui::Text("Search time: %s", elapsed.c_str());
    if (!practice.opponent.empty()) ImGui::Text("Opponent: %s", practice.opponent.c_str());
    ImGui::TextWrapped("Close this popup with Tab to keep practicing while the search continues.");
    if (ImGui::Button("Cancel search", ImVec2(-1, 36))) {
      slippi::native_practice::submit_cancel();
      state.practice_open = false;
      state.practice_release_capture = true;
    }
  } else {
    ImGui::TextUnformatted("Connect code");
    if (state.practice_focus_code) {
      ImGui::SetKeyboardFocusHere();
      state.practice_focus_code = false;
    }
    const bool enter = ImGui::InputText("##direct_code", state.practice_code,
                                        sizeof state.practice_code,
                                        ImGuiInputTextFlags_CharsUppercase |
                                        ImGuiInputTextFlags_CharsNoBlank |
                                        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::TextDisabled("Type or paste a code such as NAME#123.");
    ImGui::BeginDisabled(!practice.can_start);
    const bool start = ImGui::Button("Search Direct", ImVec2(-1, 36)) || enter;
    ImGui::EndDisabled();
    if (start && practice.can_start) {
      std::string code, error;
      if (slippi::native_practice::normalize_direct_code(state.practice_code, &code, &error)) {
        std::snprintf(state.practice_code, sizeof state.practice_code, "%s", code.c_str());
        state.practice_error[0] = 0;
        slippi::native_practice::submit_start_direct(code);
        state.practice_open = false;
        state.practice_release_capture = true;
      } else {
        std::snprintf(state.practice_error, sizeof state.practice_error, "%s", error.c_str());
      }
    }
    if (state.practice_error[0])
      ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.40f, 1.0f), "%s", state.practice_error);
  }
  ImGui::End();
}

bool settings_frame(SettingsState& state, D3D12Options& options) {
  slippi::native_practice::Snapshot practice = slippi::native_practice::snapshot();
  const bool practice_forced = practice.phase == slippi::native_practice::Phase::Handoff ||
                               practice.phase == slippi::native_practice::Phase::Failure ||
                               practice.phase == slippi::native_practice::Phase::ReturningToPractice;
  const bool practice_force_capture =
      slippi::native_practice::phase_forces_input_capture(practice.phase);
  const bool practice_nav = state.practice_open || practice.phase == slippi::native_practice::Phase::Failure;
  // Dear ImGui's Win32 backend polls XInput itself whenever gamepad navigation is enabled, and maps
  // the Xbox X button to its "menu" key, which pops up ImGui's window switcher for as long as the
  // button is held. Players pressing X mid-match got a little window they could not get rid of.
  // Native practice must follow the controller port selected in Training. Disable the backend's
  // hard-wired XInput-pad-1 poll and feed the already-routed GameCube-format port below instead.
  {
    auto& io = ImGui::GetIO();
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
  }
  ImGui_ImplWin32_NewFrame();
  // UI navigation is armed only after the selected pad has returned to neutral. A held A that
  // opened no UI cannot click Start, Cancel, or acknowledge a later disconnect.
  host::PadState pads[4]{};
  const bool have_any_pad = host::window_ui_pads(pads);
  if (!have_any_pad) for (host::PadState& p : pads) p.err = -1;
  if (state.practice_release_capture) {
    bool any_connected = false, all_neutral = true;
    for (const host::PadState& p : pads) {
      if (p.err != 0) continue;
      any_connected = true;
      if (p.button || std::abs(p.stick_x) >= 30 || std::abs(p.stick_y) >= 30 ||
          std::abs(p.sub_x) >= 30 || std::abs(p.sub_y) >= 30 || p.trig_l > 20 || p.trig_r > 20)
        all_neutral = false;
    }
    if (!any_connected || all_neutral) state.practice_release_capture = false;
  }
  const int shortcut_port = host::window_take_settings_controller_port();
  if (shortcut_port >= 0 && shortcut_port < 4) state.settings_controller_port = shortcut_port;
  const bool nav_active = state.open || practice_nav;
  int nav_port = practice_nav ? std::clamp(practice.controller_port, 0, 3) :
                                std::clamp(state.settings_controller_port, 0, 3);
  if (!practice_nav && nav_active) {
    for (int i = 0; i < 4; ++i) {
      const host::PadState& candidate = pads[i];
      const bool active = candidate.err == 0 &&
          (candidate.button || std::abs(candidate.stick_x) >= 30 || std::abs(candidate.stick_y) >= 30);
      if (active) { state.settings_controller_port = i; nav_port = i; break; }
    }
  }
  if (pads[nav_port].err != 0)
    for (int i = 0; i < 4; ++i) if (pads[i].err == 0) { nav_port = i; break; }
  const host::PadState& pad = pads[nav_port];
  const bool have_pad = pad.err == 0;
  state.settings_stick_x = have_pad ? pad.stick_x : 0;
  state.settings_stick_y = have_pad ? pad.stick_y : 0;
  if (nav_active && (!state.practice_nav_active || practice.generation != state.practice_generation)) {
    ImGui::GetIO().ClearEventsQueue();
    ImGui::GetIO().ClearInputKeys();
    state.practice_pad_armed = false;
    state.practice_a_was_down = (pad.button & 0x100) != 0;
  }
  state.practice_nav_active = nav_active;
  state.practice_generation = practice.generation;
  if (!nav_active) state.practice_pad_armed = false;
  else if (!state.practice_pad_armed && have_pad)
    state.practice_pad_armed = pad.button == 0 && std::abs(pad.stick_x) < 30 && std::abs(pad.stick_y) < 30;
  if (g_ui_diag && nav_active) {
    static uint32_t last_sig = 0xFFFFFFFFu;
    const uint32_t sig = have_pad ? ((uint32_t)nav_port << 28) ^ pad.button ^
        ((uint32_t)(uint8_t)(pad.stick_x / 20) << 16) ^ ((uint32_t)(uint8_t)(pad.stick_y / 20) << 20) : 0xFFFFFFFEu;
    if (sig != last_sig) {
      last_sig = sig;
      host::log("ui diag: nav pad port %d present %d armed %d buttons %04X stick %d,%d c %d,%d",
                nav_port, (int)have_pad, (int)state.practice_pad_armed, have_pad ? pad.button : 0,
                have_pad ? pad.stick_x : 0, have_pad ? pad.stick_y : 0, have_pad ? pad.sub_x : 0, have_pad ? pad.sub_y : 0);
    }
  }
  if (have_pad && nav_active && state.practice_pad_armed) {
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
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
  ImGuiStyle launcher_saved_style;
  const bool launcher_old_look = g_fill_window.load(std::memory_order_relaxed);
  if (launcher_old_look) {
    launcher_saved_style = ImGui::GetStyle();
    ImGui::GetStyle() = g_input_overlay_style;
    if (g_settings_old_font) ImGui::PushFont(g_settings_old_font);
  }
  set_hud_scales(options.stock_hud_scale, options.damage_hud_scale, gecko::option_pal_stock_icons);
  static bool test_tab_set = false;
  if (!test_tab_set) {
    test_tab_set = true;
    if (const char* tab = std::getenv("MELEE_TEST_SETTINGS_TAB"))
      state.active_tab = std::clamp(std::atoi(tab), 0, 6);
  }
  const auto reset_settings_home = [&state](const char* why) {
    if (g_ui_diag) host::log("ui diag: settings reset to home (%s), tab %d", why, state.active_tab);
    state.active_tab = 0;
    state.radial_selection = 0;
    state.home_focus_reset = true;
    state.clean_hover.fill(0.0f);
    state.dashboard_hover.fill(0.0f);
    state.radial_hover.fill(0.0f);
    state.clean_detail_open = state.dashboard_detail_open = false;
    state.gd_detail_open = state.radial_detail_open = state.wide_detail_open = false;
  };
  if (g_guest_options_open_request.exchange(false, std::memory_order_acq_rel)) {
    state.open = true;
    state.practice_open = false;
    reset_settings_home("guest Options row");
  }
  if (host::window_take_settings_toggle()) {
    if (state.legacy_presentation) {
      options.overlay_style = state.legacy_saved_appearance;
      state.legacy_presentation = false;
    }
    state.open = !state.open;
    if (state.open) {
      state.practice_open = false;
      reset_settings_home("F1 or controller chord");
    }
  }
  if (host::window_take_legacy_settings_toggle() && options.legacy_menu_enabled) {
    if (state.legacy_presentation) {
      options.overlay_style = state.legacy_saved_appearance;
      state.legacy_presentation = false;
      state.open = false;
    } else {
      state.legacy_saved_appearance = options.overlay_style;
      state.legacy_presentation = true;
      options.overlay_style = options.legacy_menu_style;
      state.open = true;
      state.practice_open = false;
      reset_settings_home("F11 legacy menu");
    }
  }
  // Style 6 is an in-memory-only presentation: a simple header, flat category
  // tabs, and the existing settings controls, with the selected modern appearance
  // held in SettingsState and restored on exit;
  // controls continue editing the same D3D12Options values and persistence file.
  // The launcher's Settings window always shows the old (0.6.61) settings screen, whatever
  // appearance or legacy choice the game uses; the chosen appearance is restored and saved as is.
  const bool launcher_window = g_fill_window.load(std::memory_order_relaxed);
  if (launcher_window && !state.legacy_presentation) {
    state.legacy_saved_appearance = options.overlay_style;
    state.legacy_presentation = true;
  }
  if (state.legacy_presentation) options.overlay_style = launcher_window ? 7 : options.legacy_menu_style;
  const bool tab_pressed = host::window_take_practice_toggle();
  if (tab_pressed && !state.open && !state.menu_open && !state.fill_window && practice.tab_available) {
    const bool was_open = state.practice_open;
    state.practice_open = !state.practice_open;
    if (state.practice_open) state.practice_focus_code = practice.phase == slippi::native_practice::Phase::Idle;
    else if (was_open) state.practice_release_capture = true;
  }
  // F2: write the next ~90 presented frames into capture\. For defects that only show in a real
  // session, where scripted runs reproduce nothing: press it while the problem is happening and
  // the frames themselves can be read afterwards.
  if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) { request_frame_capture(90); host::log("capture: F2, writing the next 90 presented frames into capture\\"); }
  // Start on the controller closes the panel from any page (the open chord is Start + Down + Z,
  // which the input layer swallows whole, so this never fires on the press that opened it).
  if (state.open && ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false) &&
      !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && state.rebind_action < 0)
    state.open = false;
  if (state.open && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) &&
      !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && state.rebind_action < 0) {
    if (options.overlay_style == 0 && state.clean_detail_open) state.clean_detail_open = false;
    else if (options.overlay_style == 1 && state.dashboard_detail_open) state.dashboard_detail_open = false;
    else if (options.overlay_style == 2 && state.gd_detail_open) state.gd_detail_open = false;
    else if (options.overlay_style == 3 && state.radial_detail_open) state.radial_detail_open = false;
    else if (options.overlay_style == 4 && state.wide_detail_open) state.wide_detail_open = false;
    else state.open = false;
  }
  // Esc: closes the panel if it is open; otherwise opens or closes the Esc menu. While a rebind is
  // waiting for a button, Escape means "cancel that", which the capture itself watches for, so it
  // does nothing here then (closing the panel on the same key left the capture running).
  if (host::window_take_escape() && state.rebind_action < 0) {
    if (state.open) state.open = false;
    else if (state.practice_open) { state.practice_open = false; state.practice_release_capture = true; }
    else if (state.menu_open) { state.menu_open = false; state.menu_quit = false; }
    // Esc opens the full settings menu on its category page; quit and restart live in its "..." menu.
    else if (!state.fill_window) { state.open = true; reset_settings_home("Esc"); }
  }
  if (launcher_window && !state.open) {
    g_close_requested.store(true, std::memory_order_relaxed);
    state.open = true;
  }
  if (state.open) { state.menu_open = false; state.practice_open = false; }
  if (state.menu_open) state.practice_open = false;
  if (practice_forced || practice.phase == slippi::native_practice::Phase::OnlineFlow ||
      practice.phase == slippi::native_practice::Phase::InMatch)
    state.practice_open = false;
  // Use the GD Melee source's actual 60 Hz slide keyframes. Sample the current
  // position when direction changes so a quick reopen never snaps.
  if (!state.panel_anim_initialized) {
    state.panel_anim_initialized = true;
    state.panel_anim_target_open = state.open;
    state.panel_anim_frame = state.open ? 17.0f : 12.0f;
    state.panel_slide_x = state.open ? 0.0f : -720.0f;
    state.panel_slide_start_x = state.panel_slide_x;
  } else if (state.panel_anim_target_open != state.open) {
    const bool was_mid_open = state.panel_anim_target_open && state.panel_anim_frame < 17.0f;
    const bool was_mid_close = !state.panel_anim_target_open && state.panel_anim_frame < 12.0f;
    const bool interrupted = was_mid_open || was_mid_close;
    state.panel_anim_initialized = true;
    state.panel_anim_target_open = state.open;
    state.panel_anim_frame = 0.0f;
    state.panel_slide_start_x = interrupted ? state.panel_slide_x : (state.open ? 720.0f : 0.0f);
    if (state.open) {
      state.clean_home_frames = 0.0f;
      state.dashboard_home_frames = 0.0f;
    }
  }
  if (state.open) {
    state.panel_anim_frame = std::min(17.0f, state.panel_anim_frame + ImGui::GetIO().DeltaTime * 60.0f);
    state.panel_slide_x = gx::gd_melee_ui::slide_in(state.panel_slide_start_x, state.panel_anim_frame);
  } else if (state.panel_anim_frame < 12.0f) {
    state.panel_anim_frame = std::min(12.0f, state.panel_anim_frame + ImGui::GetIO().DeltaTime * 60.0f);
    state.panel_slide_x = gx::gd_melee_ui::slide_out(state.panel_slide_start_x, state.panel_anim_frame);
  }
  // The clean side appearance keeps one physical panel and slides its content between pages.
  // The exit track remains visible until it finishes, including when Back is pressed mid motion.
  const float clean_step = ImGui::GetIO().DeltaTime * 60.0f;
  state.clean_detail_frames = std::clamp(state.clean_detail_frames +
      (state.clean_detail_open ? clean_step : -clean_step), 0.0f, 14.0f);
  const bool panel_visible = state.open || state.panel_slide_x > -720.0f;
  const bool practice_capture = state.practice_open || practice_force_capture ||
                                state.practice_release_capture;
  host::window_input_capture(state.open || state.menu_open || practice_capture);
  state.intervals[state.cursor++ % state.intervals.size()] = ImGui::GetIO().DeltaTime*1000.f;
  if (streamline::reflex_available())
    state.latencies[state.latency_cursor++ % state.latencies.size()] = streamline::reflex_latency_ms();
  bool changed = false;
  if (panel_visible) {
    g_settings_palette = options.overlay_palettes[std::clamp(options.overlay_style, 0, 6)];
    g_settings_custom_color_enabled = options.settings_custom_color_enabled;
    g_settings_custom_color = ImVec4(options.settings_custom_color[0],options.settings_custom_color[1],
                                     options.settings_custom_color[2],1.0f);
    const SettingsAppearanceScope appearance_scope(options.overlay_style);
    const bool old_menu = options.overlay_style == 7;
    const bool classic_menu = options.overlay_style >= 6;
    if (options.overlay_style == 6) {
      // Match the compact v0.6.6 settings screen: black canvas, navy controls,
      // purple selected tab, and Lucida Console text. The F11 legacy view is
      // the old settings presentation, not another modern appearance.
      ImGui::PushStyleColor(ImGuiCol_WindowBg,ImVec4(.045f,.048f,.055f,1.0f));
      ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4(.055f,.062f,.075f,1.0f));
      ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(.88f,.90f,.93f,1.0f));
      ImGui::PushStyleColor(ImGuiCol_TextDisabled,ImVec4(.48f,.51f,.57f,1.0f));
      ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(.105f,.16f,.24f,1.0f));
      ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,ImVec4(.24f,.20f,.38f,1.0f));
      ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.10f,.18f,.29f,1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(.30f,.22f,.47f,1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,ImVec4(.34f,.23f,.58f,1.0f));
      ImGui::PushStyleColor(ImGuiCol_Border,ImVec4(.20f,.22f,.26f,1.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,0.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,0.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(4.0f,2.0f));
      if (g_settings_classic_font) ImGui::PushFont(g_settings_classic_font);
    }
    state.fill_window = g_fill_window.load(std::memory_order_relaxed);
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float motion_scale = std::max(0.1f, display.y / 480.0f);
    if (state.fill_window) {
      ImGui::SetNextWindowPos(ImVec2(state.panel_slide_x * motion_scale, 0));
      ImGui::SetNextWindowSize(display);
      ImGui::Begin("PC settings", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
    } else if (old_menu) {
      // Its own window ID: sharing "PC settings" with the modern appearances made it reopen at
      // wherever the modern panel last was, including part-way through its slide (off screen).
      ImGui::SetNextWindowSize(ImVec2(std::min(560.0f, display.x - 20.0f),
                                     std::min(620.0f, display.y - 20.0f)), ImGuiCond_Appearing);
      ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Appearing);
      ImGui::Begin("PC settings###legacy_old_settings", &state.open, ImGuiWindowFlags_NoCollapse |
                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
      ImGui::TextUnformatted("F1 or Esc: return to game");
    } else {
      const float width = options.overlay_style == 0 ? std::min(560.0f, std::max(390.0f, display.x * 0.38f)) :
                          options.overlay_style == 1 ? std::min(1040.0f, display.x - 24.0f) :
                          options.overlay_style == 2 ? std::min(display.x - 12.0f, (display.y - 12.0f) * 4.0f / 3.0f) :
                          options.overlay_style == 3 ? display.x - 24.0f :
                          options.overlay_style == 4 ? std::min(960.0f, display.x - 24.0f) :
                          options.overlay_style == 6 ? std::min(980.0f, display.x - 40.0f) :
                                                       std::min(860.0f, display.x - 24.0f);
      const float w = std::max(300.0f, std::min(width, display.x - 12.0f));
      // Radial fills the height so its header and close button sit at the top of the window, not
      // floating mid-screen when the window is taller than the old 790 px cap.
      const float h = options.overlay_style == 3 ? std::max(320.0f, display.y - 12.0f) :
                      options.overlay_style == 4 ? std::max(360.0f,std::min(710.0f,display.y-40.0f)) :
                      options.overlay_style == 6 ? std::max(360.0f,std::min(710.0f,display.y-40.0f)) :
                      options.overlay_style == 2 ? w * 3.0f / 4.0f :
                      std::max(320.0f, std::min(790.0f, display.y - 12.0f));
      const ImVec2 base_pos = options.overlay_style == 0 ? ImVec2(display.x - w, 0) :
                              ImVec2((display.x - w) * 0.5f, (display.y - h) * 0.5f);
      const float slide_x = options.overlay_style == 0 ? std::abs(state.panel_slide_x) : state.panel_slide_x;
      const float slide_scale = std::max(motion_scale, w / 720.0f);
      const ImVec2 pos(base_pos.x + slide_x * slide_scale, base_pos.y);
      ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
      ImGui::SetNextWindowSize(ImVec2(w, options.overlay_style == 0 ? display.y : h), ImGuiCond_Always);
    const bool scene_chrome = options.overlay_style == 0 || options.overlay_style == 1 ||
                                (options.overlay_style >= 2 && options.overlay_style <= 4);
      ImGui::SetNextWindowBgAlpha(scene_chrome ? 0.0f : classic_menu ? 1.0f :
                                  options.overlay_style == 0 ? 0.90f : 0.97f);
      ImGui::Begin("PC settings", &state.open, ImGuiWindowFlags_NoTitleBar |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                   (scene_chrome ? ImGuiWindowFlags_NoBackground : 0));
    }
    static bool screenshot_tab_set = false;
    if (!screenshot_tab_set) {
      screenshot_tab_set = true;
      const char* requested = std::getenv("MELEE_SETTINGS_TAB");
      static constexpr const char* tabs[] = {
          "Video", "Audio", "Game", "Controls", "Overlays", "Customize", "Gecko Codes"};
      if (requested) for (int i = 0; i < 7; ++i)
        if (std::strcmp(requested, tabs[i]) == 0 ||
            (i == 5 && std::strcmp(requested, "Mods") == 0)) {
          state.active_tab = i;
          if (options.overlay_style == 0) state.clean_detail_open = true;
          if (options.overlay_style == 1) state.dashboard_detail_open = true;
          if (options.overlay_style == 3) state.radial_detail_open = true;
          if (options.overlay_style == 2) state.gd_detail_open = true;
          if (options.overlay_style == 4) state.wide_detail_open = true;
        }
    }
    const bool clean_home = !state.fill_window && options.overlay_style == 0 &&
                            !state.clean_detail_open && state.clean_detail_frames <= 0.0f;
    const bool dashboard_home = !state.fill_window && options.overlay_style == 1 &&
                                !state.dashboard_detail_open;
    const bool gd_home = !state.fill_window && options.overlay_style == 2 &&
                         !state.gd_detail_open;
    const bool radial_home = !state.fill_window && options.overlay_style == 3 &&
                             !state.radial_detail_open;
    if (!state.fill_window && options.overlay_style == 0) {
      static constexpr const char* clean_titles[]={
          "VIDEO","AUDIO","GAME","CONTROLS","OVERLAYS","CUSTOMIZE","GECKO CODES"};
      // Clean side draws its title in the foreground so it can extend beyond
      // the narrow panel. Hide it while the style gallery modal is visible.
      if (!g_menu_style_gallery_open)
        settings_clean_chrome(state.clean_detail_open?
            clean_titles[std::clamp(state.active_tab,0,6)]:"SETTINGS",state.panel_slide_x);
    }
    if (!state.fill_window && options.overlay_style == 3)
      settings_radial_scene();
    if (!state.fill_window && options.overlay_style == 4)
      settings_wide_home(state);
    if (clean_home) {
      settings_clean_home(state);
    } else if (dashboard_home) {
      settings_dashboard_home(state);
    } else if (gd_home) {
      settings_gd_home(state);
    } else if (radial_home) {
      ImDrawList* draw = ImGui::GetWindowDrawList();
      const ImVec2 window_pos = ImGui::GetWindowPos();
      const ImVec2 window_size = ImGui::GetWindowSize();
      const ImVec2 center(window_pos.x + window_size.x * 0.5f,
                          window_pos.y + window_size.y * 0.51f);
      const float radius = std::min(224.0f, std::min(window_size.x, window_size.y) * 0.315f);
      draw->AddText(ImVec2(window_pos.x + 24.0f, window_pos.y + 18.0f),
                    IM_COL32(240, 248, 255, 255), "MELEE UNLOCKED  /  SETTINGS");
      const ImVec2 hint_min(window_pos.x + window_size.x - 238.0f,
                            window_pos.y + 46.0f);
      const ImVec2 hint_max(window_pos.x + window_size.x - 18.0f,
                            window_pos.y + 96.0f);
      draw->AddRectFilled(ImVec2(hint_min.x+2,hint_min.y+3),
                          ImVec2(hint_max.x+2,hint_max.y+3),IM_COL32(0,3,10,96),12.0f);
      draw->AddRectFilled(hint_min,hint_max,IM_COL32(15,27,42,235),12.0f);
      draw->AddRect(hint_min,hint_max,IM_COL32(95,145,184,205),12.0f,0,1.2f);
      draw->AddText(ImVec2(hint_min.x + 12.0f, hint_min.y + 6.0f),
                    IM_COL32(186, 204, 222, 255), "MOVE  /  D-PAD OR STICK");
      draw->AddText(ImVec2(hint_min.x + 12.0f, hint_min.y + 25.0f),
                    IM_COL32(239, 246, 252, 255), "A  SELECT     B  BACK");
      settings_radial_nav(state, center, radius, true);
      ImGui::SetCursorScreenPos(ImVec2(window_pos.x + window_size.x - 40.0f,
                                       window_pos.y + 10.0f));
      if (settings_close_hit("##close_radial", ImVec2(28.0f, 28.0f))) state.open = false;
      draw->AddLine(ImVec2(window_pos.x + window_size.x - 33.0f, window_pos.y + 17.0f),
                    ImVec2(window_pos.x + window_size.x - 19.0f, window_pos.y + 31.0f),
                    IM_COL32(235, 243, 255, 255), 2.0f);
      draw->AddLine(ImVec2(window_pos.x + window_size.x - 19.0f, window_pos.y + 17.0f),
                    ImVec2(window_pos.x + window_size.x - 33.0f, window_pos.y + 31.0f),
                    IM_COL32(235, 243, 255, 255), 2.0f);
    } else {
    const bool gd_detail_theme = options.overlay_style == 2 && !state.fill_window;
    const bool clean_detail_theme = options.overlay_style == 0 && !state.fill_window;
    const bool dashboard_detail_theme = options.overlay_style == 1 && !state.fill_window;
    const bool wide_detail_theme = options.overlay_style == 4 && !state.fill_window;
    if (dashboard_detail_theme) {
      ImDrawList* draw = ImGui::GetWindowDrawList();
      const ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
      draw->AddRectFilled(p, ImVec2(p.x + s.x, p.y + s.y), IM_COL32(25, 34, 49, 246), 13.0f);
      draw->AddRectFilled(p, ImVec2(p.x + s.x, p.y + 5.0f), IM_COL32(246, 195, 55, 255), 2.0f);
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.31f, 0.55f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.49f, 0.82f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.60f, 0.94f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.18f, 0.34f, 1.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    }
    if (clean_detail_theme) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.23f, 0.06f, 0.19f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.91f, 0.12f, 0.48f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.24f, 0.58f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.11f, 0.07f, 0.14f, 1.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
    }
    if (gd_detail_theme) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.21f, 0.25f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.94f, 0.69f, 0.15f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.79f, 0.23f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.10f, 0.13f, 1.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    }
    if (options.overlay_style == 2 && !state.fill_window) {
      static constexpr const char* page_names[] = {
          "VIDEO", "AUDIO", "GAME", "CONTROLS", "OVERLAYS", "CUSTOMIZE", "GECKO CODES"};
      const ImVec2 origin = ImGui::GetWindowPos();
      const float scale = ImGui::GetWindowWidth() / 640.0f;
      settings_gd_chrome(origin, scale, page_names[state.active_tab]);
      gd_kit_text(ImGui::GetWindowDrawList(), "body", origin, scale, 104, 414.0f,
                  g_settings_gd_help.empty() ? "Choose a setting. Changes save automatically." : g_settings_gd_help.c_str(),
                  IM_COL32(242,239,228,255), 472);
    } else if (clean_detail_theme) {
      const ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
      ImDrawList* draw = ImGui::GetWindowDrawList();
      ImGui::SetCursorScreenPos(ImVec2(p.x + s.x - 37.0f, p.y + 15.0f));
      if (settings_close_hit("##close_clean_detail", ImVec2(28.0f, 28.0f))) state.open = false;
      draw->AddText(ImVec2(p.x + s.x - 30.0f, p.y + 18.0f),
                    IM_COL32(255, 255, 255, 255), "X");
      ImGui::SetCursorScreenPos(ImVec2(p.x + 20.0f, p.y + 115.0f));
      const bool categories_clicked=settings_hit_button("##clean_categories",ImVec2(s.x-40.0f,34.0f));
      const bool categories_hovered=ImGui::IsItemHovered()||ImGui::IsItemFocused();
      const ImVec2 ca=ImGui::GetItemRectMin(), cb=ImGui::GetItemRectMax();
      const ImVec2 category_shadow[]={ImVec2(ca.x+15,ca.y+5),ImVec2(cb.x,ca.y-2),
                                      ImVec2(cb.x-9,cb.y+5),ImVec2(ca.x+2,cb.y+5)};
      draw->AddConvexPolyFilled(category_shadow,4,IM_COL32(0,0,0,105));
      const ImVec2 category_card[]={ImVec2(ca.x+13,ca.y),ImVec2(cb.x-2,ca.y-7),
                                    ImVec2(cb.x-13,cb.y-7),ImVec2(ca.x,cb.y)};
      draw->AddConvexPolyFilled(category_card,4,categories_hovered?
          settings_accent(0):IM_COL32(26,17,35,235));
      draw->AddLine(category_card[0],category_card[1],
          settings_palette_tint(0,IM_COL32(255,108,189,245)),2.0f);
      settings_slanted_text(draw,settings_heading_font(),16.0f,
          ImVec2(ca.x+23,ca.y+7),IM_COL32_WHITE,"<  CATEGORIES",.08f);
      if (categories_clicked) {
        state.clean_detail_open = false;
        state.clean_home_frames = 0.0f;
      }
      ImGui::SetCursorScreenPos(ImVec2(p.x + 18.0f, p.y + 153.0f));
    } else if (dashboard_detail_theme) {
      settings_detail_header(state,1);
      ImGui::SetCursorPos(ImVec2(24, 112));
    } else if (options.overlay_style == 3) {
      settings_detail_header(state,3);
      const ImVec2 window_pos = ImGui::GetWindowPos(), window_size = ImGui::GetWindowSize();
      const float nav_width = std::min(320.0f,
          std::max(248.0f, (window_size.x - 52.0f) * 0.31f));
      const ImVec2 center(window_pos.x + 26.0f + nav_width * 0.5f,
                          window_pos.y + 112.0f + (window_size.y - 160.0f) * 0.45f);
      const float radius = std::min(122.0f, std::min(nav_width * 0.43f,
                                                    (window_size.y - 200.0f) * 0.34f));
      settings_radial_nav(state, center, radius, false);
      ImGui::SetCursorScreenPos(ImVec2(window_pos.x + 26.0f + nav_width + 22.0f,
                                       window_pos.y + 112.0f));
    } else if (old_menu) {
      // Native v0.6.6 title and tab positioning.
    } else {
        ImGui::SetCursorPos(ImVec2(20, 14));
        if (state.legacy_presentation || options.overlay_style == 6) {
          // The v0.6.6 layout began with the category tabs, not the modern
          // oversized SETTINGS banner.
          ImGui::SetCursorPos(ImVec2(8, 12));
        } else {
          ImGui::PushFont(settings_heading_font());
          ImGui::TextUnformatted("SETTINGS");
          ImGui::PopFont();
          ImGui::SetCursorPos(ImVec2(20, 56));
        }
    }
    ImGui::BeginDisabled(!state.open);
    if ((options.overlay_style != 2 || state.fill_window) && !clean_detail_theme &&
        !(options.overlay_style == 3 && !state.fill_window) &&
        !state.legacy_presentation && options.overlay_style != 6) ImGui::Separator();
    const bool icon_dashboard = options.overlay_style == 1;
    if (options.overlay_style == 2 && !state.fill_window) {
      const float scale = ImGui::GetWindowWidth() / 640.0f;
      ImGui::SetCursorPos(ImVec2(72.0f * scale, 84.0f * scale));
    } else if (clean_detail_theme || dashboard_detail_theme || options.overlay_style == 3) {
      // Wheel categories open a complete page; labels never compete with a miniature wheel.
    } else if (old_menu) {
      static constexpr const char* tabs[] = {"Video", "Audio", "Game", "Controls", "Overlays", "Customize", "Gecko Codes"};
      static bool old_tabs_initialized = false;
      const int requested_old_tab = state.active_tab;
      if (ImGui::BeginTabBar("settings_tabs")) {
        for (int i = 0; i < 7; ++i) {
          if (ImGui::BeginTabItem(tabs[i], nullptr, !old_tabs_initialized && requested_old_tab == i ? ImGuiTabItemFlags_SetSelected : 0)) {
            state.active_tab = i;
            ImGui::EndTabItem();
          }
        }
        ImGui::EndTabBar();
        old_tabs_initialized = true;
      }
    } else if (state.legacy_presentation || options.overlay_style == 6) {
      static constexpr const char* legacy_tabs[] = {
          "Video", "Audio", "Game", "Controls", "Overlays", "Customize", "Gecko Codes"};
      ImGui::SetCursorPos(ImVec2(8, 12));
      const float spacing = 4.0f;
      for (int i = 0; i < 7; ++i) {
        const bool selected = state.active_tab == i;
        ImGui::PushID(i);
        ImGui::PushStyleColor(ImGuiCol_Button, selected ? ImVec4(.34f,.23f,.58f,1.0f) :
                                                        ImVec4(.10f,.18f,.29f,1.0f));
        const ImVec2 label_size=ImGui::CalcTextSize(legacy_tabs[i]);
        if (ImGui::Button(legacy_tabs[i], ImVec2(label_size.x + 18.0f, 23.0f)))
          state.active_tab = i;
        ImGui::PopStyleColor();
        ImGui::PopID();
        if (i != 6) ImGui::SameLine(0.0f,spacing);
      }
      ImGui::Separator();
      ImGui::Spacing();
    } else if (options.overlay_style == 4) {
      // Leave breathing room below the tab strip; the panel's rounded outline is its only top edge.
      ImGui::SetCursorPos(ImVec2(18.0f,120.0f));
    } else if (icon_dashboard) {
      const float spacing = ImGui::GetStyle().ItemSpacing.x;
      const float tile_width = std::max(82.0f, (ImGui::GetContentRegionAvail().x - 3 * spacing) / 4);
      const float tile_height = display.y < 600.0f ? 48.0f : 70.0f;
      for (int i = 0; i < 7; ++i) {
        if (settings_nav_tile(i, state.active_tab == i, options.overlay_style,
                              ImVec2(tile_width, tile_height))) state.active_tab = i;
        if (i % 4 != 3 && i != 6) ImGui::SameLine();
      }
      ImGui::Spacing();
    } else if (options.overlay_style == 3) {
      const float nav_width = std::min(320.0f,
          std::max(248.0f, (ImGui::GetWindowWidth() - 52.0f) * 0.31f));
      const ImVec2 window_pos = ImGui::GetWindowPos(), window_size = ImGui::GetWindowSize();
      const ImVec2 center(window_pos.x + 26.0f + nav_width * 0.5f,
                          window_pos.y + 112.0f + (window_size.y - 160.0f) * 0.45f);
      const float radius = std::min(122.0f, std::min(nav_width * 0.43f,
                                                    (window_size.y - 200.0f) * 0.34f));
      ImGui::SetCursorScreenPos(ImVec2(window_pos.x + 26.0f + nav_width + 22.0f,
                                       window_pos.y + 112.0f));
    } else {
      const float nav_width = options.overlay_style == 0 ? 78.0f :
                              151.0f;
      ImGui::BeginChild("##settings_navigation", ImVec2(nav_width, -80), false);
      for (int i = 0; i < 7; ++i) {
        const bool clicked = options.overlay_style == 0 ?
            settings_nav_rail(i, state.active_tab == i, ImVec2(nav_width - 8.0f, 58.0f)) :
            settings_nav_tile(i, state.active_tab == i, options.overlay_style,
                              ImVec2(nav_width - 8.0f, 48.0f));
        if (clicked) state.active_tab = i;
      }
      ImGui::EndChild();
      ImGui::SameLine();
    }
    bool content_tab_changed = false;
    if (state.content_anim_tab < 0) {
      state.content_anim_tab = state.active_tab;
      state.content_anim_frame = 12.0f;
      content_tab_changed = true;
    } else if (state.content_anim_tab != state.active_tab) {
      state.content_anim_tab = state.active_tab;
      state.content_anim_frame = 0.0f;
      content_tab_changed = true;
    }
    g_settings_focus_next_gd_row = content_tab_changed && options.overlay_style == 2 &&
                                   !state.fill_window;
    constexpr float kContentTransitionFrames = 8.0f;
    state.content_anim_frame = std::min(kContentTransitionFrames,
        state.content_anim_frame + ImGui::GetIO().DeltaTime * 60.0f);
    const float page_t = std::clamp(state.content_anim_frame / kContentTransitionFrames, 0.0f, 1.0f);
    const float page_ease = page_t * page_t * (3.0f - 2.0f * page_t);
    ImVec2 page_offset(0.0f, 0.0f);
    if (options.overlay_style == 0) {
      const float t = std::clamp(state.clean_detail_frames / 14.0f, 0.0f, 1.0f);
      const float slide = t * t * (3.0f - 2.0f * t);
      page_offset.x = ImGui::GetWindowWidth() * 0.65f * (1.0f - slide);
    }
    else if (options.overlay_style == 1) page_offset.y = 14.0f * (1.0f - page_ease);
    else page_offset.x = 12.0f * (1.0f - page_ease);
    const ImVec2 content_origin = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(content_origin.x + page_offset.x,
                               content_origin.y + page_offset.y));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
        options.overlay_style == 0 ? 0.35f + 0.65f * std::clamp(state.clean_detail_frames / 14.0f, 0.0f, 1.0f) :
                                     0.72f + 0.28f * page_ease);
    const bool gd_page = options.overlay_style == 2 && !state.fill_window;
    if (wide_detail_theme) {
      const ImVec2 window = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
      const ImVec2 a(window.x + 24.0f, window.y + 108.0f);
      const ImVec2 b(window.x + size.x - 24.0f, window.y + size.y - 50.0f);
      ImDrawList* draw = ImGui::GetWindowDrawList();
      draw->AddRectFilled(ImVec2(a.x, a.y + 4.0f), ImVec2(b.x, b.y + 4.0f),
                          IM_COL32(0, 0, 0, 55), 14.0f);
      draw->AddRectFilled(a, b, IM_COL32(12, 21, 32, 246), 14.0f);
      draw->AddRect(a, b, IM_COL32(76, 105, 132, 150), 14.0f, 0, 1.0f);
    }
    if (clean_detail_theme || dashboard_detail_theme ||
        (options.overlay_style >= 2 && options.overlay_style <= 4))
      ImGui::PushStyleColor(ImGuiCol_ChildBg,
                            gd_page ? ImVec4(0, 0, 0, 0) :
                            clean_detail_theme ? ImVec4(0.035f, 0.025f, 0.055f, 0.78f) :
                            dashboard_detail_theme ? ImVec4(0.055f, 0.12f, 0.22f, 0.96f) :
                                                 ImVec4(0, 0, 0, 0));
    const float gd_scale = ImGui::GetWindowWidth() / 640.0f;
    g_settings_form_style = gd_page ? 2 : (options.overlay_style == 2 ? 4 : options.overlay_style);
    g_settings_gd_origin = ImGui::GetWindowPos();
    g_settings_gd_scale = gd_scale;
    if (!state.fill_window && (options.overlay_style == 3 || options.overlay_style == 4)) {
      const ImVec2 window = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
      ImVec2 a = ImGui::GetCursorScreenPos();
      ImVec2 b(a.x + ImGui::GetContentRegionAvail().x,
               window.y + size.y - 54.0f);
      if (options.overlay_style == 3) {
        // The radial detail page is a two-column composition. Its content glass must
        // begin to the right of the wheel; painting the normal full-width card here
        // covered the wheel and made it look like the page was frozen underneath it.
        constexpr float kRadialDetailSideInset = 64.0f;
        constexpr float kRadialDetailColumnGap = 36.0f;
        const float nav_width = std::min(320.0f,
            std::max(248.0f, (size.x - 52.0f) * 0.31f));
        a = ImVec2(window.x + 26.0f + nav_width + kRadialDetailColumnGap, window.y + 112.0f);
        b = ImVec2(window.x + size.x - kRadialDetailSideInset, window.y + size.y - 54.0f);
      }
      ImDrawList* draw = ImGui::GetWindowDrawList();
      draw->AddRectFilled(ImVec2(a.x-5,a.y-7),ImVec2(b.x+5,b.y+4),
                          options.overlay_style==3?IM_COL32(13,22,35,250):IM_COL32(7,19,35,204),18.0f);
      draw->AddRect(ImVec2(a.x-5,a.y-7),ImVec2(b.x+5,b.y+4),
                    options.overlay_style==3?IM_COL32(99,119,139,112):
                        settings_palette_tint(options.overlay_style,IM_COL32(85,150,208,142)),
                    18.0f,0,1.5f);
    }
    const bool radial_detail_page = options.overlay_style == 3 && !state.fill_window;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
        gd_page ? ImVec2(0,0) : classic_menu ? ImVec2(8.0f, 5.0f) :
        radial_detail_page ? ImVec2(24.0f, 20.0f) :
        wide_detail_theme ? ImVec2(26.0f, 22.0f) :
        ImVec2(clean_detail_theme ? 18.0f : 24.0f, 20.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
        old_menu ? ImVec2(5.0f, 3.75f) : classic_menu ? ImVec2(4.0f, 2.0f) :
        radial_detail_page ? ImVec2(14.0f, 9.0f) : ImVec2(12.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, old_menu ? ImVec2(10.0f, 5.0f) : classic_menu ? ImVec2(5.0f, 4.0f) :
        gd_page ? ImVec2(4.0f * gd_scale, 4.0f * gd_scale) : ImVec2(12.0f, 12.0f));
    ImVec2 settings_content_size = gd_page ? ImVec2(532.0f * gd_scale, 306.0f * gd_scale) : ImVec2(0, -48);
    if (options.overlay_style == 3) {
      const ImVec2 window_pos = ImGui::GetWindowPos(), window_size = ImGui::GetWindowSize();
      constexpr float kRadialDetailSideInset = 64.0f;
      constexpr float kRadialDetailColumnGap = 36.0f;
      const float nav_width = std::min(320.0f, std::max(248.0f, (window_size.x - 52.0f) * 0.31f));
      const float content_x = 26.0f + nav_width + kRadialDetailColumnGap;
      ImGui::SetCursorScreenPos(ImVec2(window_pos.x + content_x, window_pos.y + 112.0f));
      settings_content_size.x = std::max(300.0f, window_size.x - content_x - kRadialDetailSideInset);
    }
    ImGui::BeginChild("##settings_content", settings_content_size,
                      (radial_detail_page || wide_detail_theme) ?
                          ImGuiChildFlags_AlwaysUseWindowPadding : ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollWithMouse);
    {
      // Smooth wheel scrolling at the presented frame rate. ImGui's own wheel scroll jumps a whole
      // notch in one frame, which reads as 60 Hz stepping on a high-refresh display. The target
      // follows any other scroll change (scrollbar drag, keyboard or controller navigation).
      ImGuiWindow* content = ImGui::GetCurrentWindow();
      ImGuiStorage* storage = ImGui::GetStateStorage();
      const ImGuiID target_id = ImGui::GetID("##smooth_scroll_target");
      const ImGuiID applied_id = ImGui::GetID("##smooth_scroll_applied");
      const float current = content->Scroll.y;
      float target = storage->GetFloat(target_id, current);
      const float applied = storage->GetFloat(applied_id, current);
      if (std::fabs(current - applied) > 0.5f) target = current;
      const ImGuiIO& scroll_io = ImGui::GetIO();
      if (GImGui->HoveredWindow == content && scroll_io.MouseWheel != 0.0f)
        target -= scroll_io.MouseWheel * std::max(60.0f, ImGui::GetTextLineHeightWithSpacing() * 4.0f);
      target = std::clamp(target, 0.0f, content->ScrollMax.y);
      float next = current + (target - current) * std::min(1.0f, scroll_io.DeltaTime * 16.0f);
      if (std::fabs(target - next) < 0.5f) next = target;
      if (next != current) ImGui::SetScrollY(next);
      storage->SetFloat(target_id, target);
      storage->SetFloat(applied_id, next);
    }
    // The radial details glass is positioned as a separate right-hand column. Give its actual
    // controls their own inset instead of relying on inherited child padding, which can be zero
    // under the legacy child flags used by some builds. This keeps rows away from the glass edge.
    ImGui::PushTextWrapPos(0.0f);
    static constexpr const char* titles[] = {
        "VIDEO", "AUDIO", "GAME", "CONTROLS", "OVERLAYS", "CUSTOMIZE", "GECKO CODES"};
    if (wide_detail_theme) {
      static constexpr const char* wide_titles[] = {
          "Video", "Audio", "Game", "Controls", "Overlays", "Customize", "Gecko Codes"};
      const ImVec2 header = ImGui::GetCursorScreenPos();
      const ImVec2 icon(header.x + 19.0f, header.y + 17.0f);
      settings_symbol_icon(state.active_tab, icon, 0.52f,
          settings_palette_tint(4, IM_COL32(226, 238, 249, 255)));
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 40.0f);
      ImGui::PushFont(settings_heading_font());
      ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(settings_accent(4)), "%s",
                         wide_titles[state.active_tab]);
      ImGui::PopFont();
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);
      ImGui::Spacing();
    } else if (!gd_page && !clean_detail_theme && !dashboard_detail_theme &&
               options.overlay_style != 3 && !classic_menu) {
    ImGui::PushFont(settings_heading_font());
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(settings_accent(options.overlay_style)),
                       "%s", titles[state.active_tab]);
    ImGui::PopFont();
    ImGui::Separator();
    }
    if (state.active_tab == 0) {
    // One width for every combo and slider on this tab, so their labels all begin at the same x.
    // Left to itself ImGui sizes each control from the space its own label needs, which staggered
    // the labels down the column.
    ImGui::PushItemWidth(std::clamp(ImGui::GetContentRegionAvail().x - 135.0f, 145.0f, 330.0f));
    // ---- Quality presets: one click for people who do not want to learn the settings below ----
    // Each sets the image settings and the frame rate; the controls below still show and change
    // every value, and the row says "Custom" once any of them differs from all four.
    {
      struct Preset { const char* name; const char* tip; int efb; int ssaa; int aniso; int dlss; double fps; SubFrameMode sub; bool dlss5; };
      const bool dlaa_ok = streamline::available() && !state.running_d3d11;
#ifdef GX_DLSS5
      const bool dlss5_ok = dlaa_ok && !state.running_d3d11;
#endif
      const Preset presets[] = {
        {"Low", "For laptops and integrated graphics: native resolution, no anti-aliasing,\n60 frames per second, no sub-frame animation.", 1, 1, 1, 0, 60, SubFrameMode::Off, false},
        {"Medium", "For most older PCs: 2x resolution, 4x filtering,\nyour monitor's refresh rate with smooth in-between frames.", 2, 1, 4, 0, -1, SubFrameMode::Authored, false},
        {"High", "Recommended, and the default: resolution matched to your window, 16x filtering,\nyour monitor's refresh rate with smooth in-between frames.", 0, 1, 16, 0, -1, SubFrameMode::Authored, false},
        // DLAA already smooths every edge, so Ultra uses it alone where it exists. Stacked on 4x
        // supersampling it ran DLAA over an image several times 4K: a large drop in frame rate, and
        // out of video memory on 8 GB cards once a recorder was running too.
        {"Ultra", dlaa_ok ? "High, plus NVIDIA DLAA for the smoothest edges."
                          : "High, plus 4x supersampling for the smoothest edges.",
         0, dlaa_ok ? 1 : 2, 16, dlaa_ok ? 1 : 0, -1, SubFrameMode::Authored, false},
#ifdef GX_DLSS5
        // Resolution matched to your window (the same "native for your machine" Auto that High and
        // Ultra use) plus DLAA plus DLSS 5 on top of that. Costs real frame rate -- see the DLSS 5
        // panel below for what it is actually adding once this is on.
        {"Insane", "Ultra, plus DLSS 5 Neural Rendering (experimental). Heavy: watch the render\nlatency line below Upscaling after picking this.",
         0, 1, 16, dlaa_ok ? 1 : 0, -1, SubFrameMode::Authored, dlss5_ok},
#endif
      };
      constexpr int kPresetCount = sizeof(presets) / sizeof(presets[0]);
      auto matches = [&](const Preset& pr) {
        return options.efb_scale == pr.efb && options.ssaa == pr.ssaa && options.anisotropy == pr.aniso &&
               options.dlss_mode == pr.dlss && options.fps_cap == pr.fps && options.subframe == pr.sub
#ifdef GX_DLSS5
               && options.dlss5 == pr.dlss5
#endif
               ;
      };
      int current = -1;
      for (int i = kPresetCount - 1; i >= 0 && current < 0; --i) if (matches(presets[i])) current = i;
      // Settings that match no preset are the player's own: keep them as Custom.
      if (current < 0)
        g_custom_preset = {true, options.efb_scale, options.ssaa, options.anisotropy, options.dlss_mode, options.fps_cap, (int)options.subframe,
#ifdef GX_DLSS5
                           options.dlss5
#else
                           false
#endif
        };
      const char* preset_labels[kPresetCount + 1];
      for (int i=0;i<kPresetCount;++i) preset_labels[i]=presets[i].name;
      preset_labels[kPresetCount]="Custom";
      int picked=current<0?kPresetCount:current;
      bool preset_changed=false;
      if (classic_menu) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Quality");
        for (int i=0;i<=kPresetCount;++i) {
          if (ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + 94.0f < ImGui::GetWindowContentRegionMax().x)
            ImGui::SameLine(0.0f,6.0f);
          ImGui::PushID(i);
          const bool selected=picked==i;
          if (selected) ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.34f,.23f,.58f,1.0f));
          if (ImGui::Button(preset_labels[i],ImVec2(82.0f,22.0f))) {
            picked=i;
            preset_changed=true;
          }
          if (selected) ImGui::PopStyleColor();
          if (ImGui::IsItemHovered() && i<kPresetCount) ImGui::SetTooltip("%s",presets[i].tip);
          ImGui::PopID();
        }
      } else preset_changed=settings_combo("Quality",&picked,preset_labels,kPresetCount+1);
      if (preset_changed) {
        if (picked<kPresetCount) {
          const Preset& pr=presets[picked];
          options.efb_scale=pr.efb; options.ssaa=pr.ssaa; options.anisotropy=pr.aniso;
          options.dlss_mode=pr.dlss; options.fps_cap=pr.fps; options.subframe=pr.sub;
#ifdef GX_DLSS5
          options.dlss5=pr.dlss5;
#endif
          options.low_spec=false; changed=true;
        } else if (g_custom_preset.set) {
          const CustomPreset& c=g_custom_preset;
          options.efb_scale=c.efb;options.ssaa=c.ssaa;options.anisotropy=c.aniso;
          options.dlss_mode=c.dlss;options.fps_cap=c.fps;options.subframe=(SubFrameMode)c.sub;
#ifdef GX_DLSS5
          options.dlss5=c.dlss5;
#endif
          options.low_spec=false;changed=true;
        }
      }
      if (!classic_menu && ImGui::IsItemHovered() && picked<kPresetCount)
        ImGui::SetTooltip("%s",presets[picked].tip);
    }
    // Frame rate first: it is the setting this port exists for, and the one people look for.
    const double rates[] = {-1, 0, 60, 120, 144, 165, 200, 240, 360, 480};
    const char* names[] = {"Match monitor", "Unlocked", "60", "120", "144", "165", "200", "240", "360", "480"};
    int selected = -1; for (int i = 0; i < 10; ++i) if (options.fps_cap == rates[i]) selected = i;
    if (settings_combo("Frame rate", &selected, names, 10)) { options.fps_cap = rates[selected]; changed = true; }
    // Two checkboxes per row. The second column is measured from the widest label in the first one
    // rather than guessed: a fixed offset put "True 16:9" hard against the bracket of "Widescreen
    // 16:9 (Slippi)" and would break again the moment a label or the font changed.
    const float kCol2 = ImGui::GetCursorPosX() + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
                        ImGui::CalcTextSize("Widescreen 16:9 (Slippi)").x + ImGui::GetStyle().ItemSpacing.x * 3.0f;
    changed |= settings_toggle("VSync", &options.vsync);
    if (ImGui::GetContentRegionAvail().x >= 650.0f) ImGui::SameLine(kCol2);
    if (settings_toggle("Borderless fullscreen", &options.fullscreen)) {
      if (options.fullscreen) options.exclusive_fullscreen = false;
      changed = true;
    }
    if (settings_toggle("Exclusive fullscreen (experimental)", &options.exclusive_fullscreen)) {
      if (options.exclusive_fullscreen) options.fullscreen = false;
      changed = true;
    }
    if (settings_toggle("Widescreen 16:9 (Slippi)", &options.widescreen)) {
      if (options.widescreen) options.true_widescreen = false;   // one or the other, never both
      changed = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The Slippi widescreen Gecko code. Online safe.");
    if (ImGui::GetContentRegionAvail().x >= 650.0f) ImGui::SameLine(kCol2);
    // True 16:9 widens the frustum here in the renderer instead of running the Gecko code, so
    // nothing is written to guest memory and it cannot desync. Experimental because the game still
    // lays out and culls for 73:60: geometry can be missing or pop in at the new edges.
    if (settings_toggle("True 16:9 (experimental)", &options.true_widescreen)) {
      if (options.true_widescreen) options.widescreen = false;
      changed = true;
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Widens the camera in the renderer instead of running game code, so it cannot desync.\n"
                        "Watch the edges for missing or popping scenery: the game still culls for 73:60.");
    float win_w = ImGui::GetIO().DisplaySize.x, win_h = ImGui::GetIO().DisplaySize.y;

    // Aspect ratio and window size are presentation only: they change nothing the game computes,
    // so they cannot desync and the two players in a match may each pick their own.
    {
      const char* aspects[] = {"Auto (Melee's own: 73:60, or 16:9 with widescreen on)",
                               "73:60 (Melee's native)", "4:3", "16:9", "Stretch to window (no black bars)"};
      int index = std::clamp((int)options.aspect, 0, 4);
      if (settings_combo("Aspect ratio", &index, aspects, 5)) { options.aspect = (AspectMode)index; changed = true; }
      if (options.aspect == AspectMode::Stretch)
        settings_hint("Fills the whole window or screen, so the picture is stretched. Pick a 4:3 window\n"
                            "size below and a wider screen to get the stretched resolution players use.");
      else
        settings_hint("Melee's camera asks for 73:60, not 4:3; the Slippi widescreen code widens it to 16:9.\n"
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
      const bool full = options.fullscreen || options.exclusive_fullscreen || host::window_is_fullscreen();
      if (full) ImGui::BeginDisabled();
      if (settings_combo("Window size", &size_index, items, count)) {
        if (size_index == 0) options.window_pinned = false;
        else if (size_index < kPresets) {
          options.window_pinned = true;
          options.window_w = kSizes[size_index].w; options.window_h = kSizes[size_index].h;
          host::window_set_client_size(options.window_w, options.window_h);
        }
        changed = true;
      }
      if (full) { ImGui::EndDisabled(); settings_hint("Fullscreen uses the whole screen. Stretch above fills it; the other aspects add bars."); }
      // A window bigger than the desktop cannot be shown with its title bar on screen, so Windows
      // (and the clamp in window_set_client_size) gives back a smaller one. The player can also just
      // have dragged the edge since. Either way, say what the window actually is.
      else if (options.window_pinned && ((int)win_w != options.window_w || (int)win_h != options.window_h))
        settings_hint("Picked %dx%d, window is %dx%d (dragged, or capped to your desktop).\nFullscreen is never capped.",
                            options.window_w, options.window_h, (int)win_w, (int)win_h);
      settings_hint("Aspect ratio and window size only change how the picture is fitted to your screen.\n"
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
    const bool dlss_picks_resolution = options.dlss_mode >= 2 && options.dlss_mode != 6;   // not DLAA / XeSS AA
    if (dlss_picks_resolution) ImGui::BeginDisabled();
    changed |= settings_combo("Internal resolution", &options.efb_scale, scales, 9);
    // DLSS (DLAA included) does its own anti-aliasing, so supersampling on top would render larger
    // for a second pass over the same edges: grey it out rather than let the two stack.
    const char* aa[] = {"None", "4x SSAA (supersampling)"};
    int aa_index = options.ssaa == 2 ? 1 : 0;
    if (options.dlss_mode && !dlss_picks_resolution) ImGui::BeginDisabled();
    if (settings_combo("Anti-aliasing", &aa_index, aa, 2)) { options.ssaa = aa_index ? 2 : 1; changed = true; }
    if (options.dlss_mode && !dlss_picks_resolution) ImGui::EndDisabled();
    if (dlss_picks_resolution) ImGui::EndDisabled();
    const char* anis[] = {"1x", "2x", "4x", "8x", "16x"};
    int an_index = options.anisotropy >= 16 ? 4 : options.anisotropy >= 8 ? 3 : options.anisotropy >= 4 ? 2 : options.anisotropy >= 2 ? 1 : 0;
    if (settings_combo("Anisotropic filtering", &an_index, anis, 5)) { options.anisotropy = 1 << an_index; changed = true; }
    // Live, right where the settings that move it are, rather than only in an on-screen overlay
    // during play: raise Internal resolution, Anti-aliasing or a DLSS mode and watch this move.
    {
      float used = 0, total = 0;
      if (vram_usage(&used, &total)) settings_hint("Video memory in use: %.1f / %.1f GB", used, total);
    }
    {
      const char* levels[] = {"Full", "Reduced (no sparks or glow)", "Minimal (no screen overlays)"};
      if (settings_combo("Visual effects", &options.effects_level, levels, 3)) changed = true;
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Skips decorative effects during matches to help slower PCs: glow, sparks and\n"
                          "flashes (Reduced), plus full-screen overlays (Minimal). Menus, fighters, the\n"
                          "stage and the HUD always draw. Display only: safe online.");
    }
    // Creating a device and a swapchain on another API means restarting; the choice is saved and
    // read again at the next launch (see load_pc_settings and --backend).
    const char* backends[] = {"Direct3D 12 (default)", "Direct3D 11 (older GPUs and drivers)"};
    int api_index = options.api == RenderApi::D3D11 ? 1 : 0;
    if (settings_combo("Graphics backend", &api_index, backends, 2)) { options.api = api_index ? RenderApi::D3D11 : RenderApi::D3D12; changed = true; }
    settings_hint("Takes effect at the next launch: save settings, then restart.");
    const bool d3d11 = options.api == RenderApi::D3D11;
    const char* upscalers[] = {"Native", "DLAA", "DLSS Quality", "DLSS Balanced", "DLSS Performance", "DLSS Ultra Performance",
                               "XeSS AA", "XeSS Ultra Quality", "XeSS Quality", "XeSS Balanced", "XeSS Performance"};
    if (d3d11) ImGui::BeginDisabled();
    if (settings_combo("Upscaling (DLSS / XeSS)", &options.dlss_mode, upscalers, 11)) changed = true;
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("DLSS: NVIDIA RTX cards. XeSS: Intel's upscaler, works on Intel, NVIDIA and AMD cards.\n"
                        "AA modes (DLAA, XeSS AA) keep the full resolution and only smooth edges.\n"
                        "DLSS Ultra Performance would render below the GameCube's own resolution on\n"
                        "anything short of an 8K display, so there it switches to Performance.");
    // What each of these is actually costing, right where the settings that cost it are, instead of
    // only in the separate performance graph. Render latency is Reflex's measured total (works at
    // Native too, see gx_streamline.h); the two "adds" numbers are GPU-timestamp measured costs of
    // each pass (see gx_d3d12.cpp read_gpu_timers), and "Native (estimated)" is the total with both
    // subtracted out -- an estimate, since it assumes the two costs are simply additive to the total,
    // which is usually close but not exact (queueing and driver overhead do not scale perfectly linearly).
    if (streamline::reflex_available()) {
      float dlaa_ms = 0, dlss5_ms = 0;
      gpu_pass_cost(&dlaa_ms, &dlss5_ms);
      const float total = streamline::reflex_latency_ms();
      const float native_est = std::max(0.0f, total - dlaa_ms - dlss5_ms);
      char line[192];
      int n = std::snprintf(line, sizeof line, "Render latency: %.1f ms", total);
      if (dlaa_ms > 0.0f || dlss5_ms > 0.0f) {
        n += std::snprintf(line + n, sizeof(line) - n, "  (Native est. %.1f ms", native_est);
        if (dlaa_ms > 0.0f) n += std::snprintf(line + n, sizeof(line) - n, ", DLAA/DLSS +%.1f ms", dlaa_ms);
#ifdef GX_DLSS5
        if (dlss5_ms > 0.0f) n += std::snprintf(line + n, sizeof(line) - n, ", DLSS 5 +%.1f ms", dlss5_ms);
#endif
        std::snprintf(line + n, sizeof(line) - n, ")");
      }
      settings_hint("%s", line);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("An average of the last measured frames, so it lags a change by a second or\n"
                          "two, not instant. \"Native (estimated)\" is the total with the two measured\n"
                          "GPU costs subtracted out -- an estimate, not a separate measurement.");
    }
    // Frame generation reuses DLSS's depth and motion vectors, so it needs a DLSS mode. Build the
    // list from Streamline's present-thread capability query: RTX 40 usually reports 2x, while newer
    // RTX 50 drivers can expose fixed multipliers through 6x and Dynamic.
    const bool fg_capabilities_known = streamline::frame_generation_capabilities_queried();
    ImGui::BeginDisabled(options.dlss_mode == 0 || options.dlss_mode >= 6 || !fg_capabilities_known);
    {
      const uint32_t max_mult = std::min<uint32_t>(5, streamline::frame_generation_max_multiplier());
      const bool dynamic_ok = streamline::frame_generation_dynamic_supported();
      static const char* labels[] = {"Off", "2x", "3x", "4x", "5x", "6x", "Dynamic"};
      const int values[] = {0, 1, 2, 3, 5, 6, 4};
      const char* modes[7]; int mode_values[7]; int n = 0;
      for (int i = 0; i <= (int)max_mult; ++i) {
        modes[n] = labels[i];
        mode_values[n++] = values[i];
      }
      if (dynamic_ok) { modes[n] = labels[6]; mode_values[n++] = values[6]; }
      int selected = 0; bool found_mode = false;
      for (int i = 0; i < n; ++i) {
        if (mode_values[i] == options.frame_generation_mode) { selected = i; found_mode = true; break; }
      }
      if (!found_mode) selected = (int)max_mult;  // safest supported fixed multiplier
      if (fg_capabilities_known && mode_values[selected] != options.frame_generation_mode) {
        options.frame_generation_mode = mode_values[selected];
      }
      ImGui::SetNextItemWidth(160.0f);
      if (settings_combo("Frame generation", &selected, modes, n)) {
        options.frame_generation_mode = mode_values[selected];
        changed = true;
      }
      if (!fg_capabilities_known) settings_hint("Checking supported modes…");
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("NVIDIA DLSS Frame Generation (RTX 40 series and newer): generated frames between\n"
                        "rendered frames. RTX 50 Multi Frame Generation can offer up to 6x when the\n"
                        "driver reports support. It adds input delay, so use it for solo play rather\n"
                        "than competitive online matches. Needs DLSS upscaling; Reflex turns on with it.");
    // NVIDIA Reflex, laid out the way games offer it.
    {
      const char* modes[] = {"Off", "On", "On + Boost"};
      ImGui::SetNextItemWidth(160.0f);
      if (settings_combo("NVIDIA Reflex Low Latency", &options.reflex_mode, modes, 3)) changed = true;
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("On: the GPU stops queueing frames ahead of the game, so what you press shows sooner.\n"
                          "On + Boost: also keeps the GPU clocks up, trading power for a little more.\n"
                          "Frame generation always runs with Reflex at least On. NVIDIA cards only.");
      if (settings_toggle("Reflex flash indicator", &options.reflex_flash)) changed = true;
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Flashes a white square in the top left corner when A is pressed on port 1,\n"
                          "for monitors with the NVIDIA Reflex Latency Analyzer and for LDAT.");
    }
    if (d3d11) { ImGui::EndDisabled(); settings_hint("DLSS needs Direct3D 12 and an NVIDIA GPU."); }
    {
      const bool path_available = !d3d11 && options.dlss_mode < 6 && dxr_path_tracing_available();
      if (!path_available && options.path_tracing) { options.path_tracing = false; changed = true; }
      if (!path_available) ImGui::BeginDisabled();
      if (settings_toggle("DXR diffuse path tracing (experimental)", &options.path_tracing)) changed = true;
      if (options.path_tracing)
        settings_hint("Adds three low-sample diffuse bounces to the raster image. High GPU cost; match scenes only.");
      if (!path_available) {
        ImGui::EndDisabled();
        settings_hint("DXR needs Direct3D 12, a ray-tracing capable GPU, and Native, DLAA, or DLSS selected.");
      }
      const bool rr_available = path_available && options.path_tracing && options.dlss_mode > 0 &&
                                streamline::ray_reconstruction_available();
      if (!rr_available && options.ray_reconstruction) { options.ray_reconstruction = false; changed = true; }
      if (!rr_available) ImGui::BeginDisabled();
      if (settings_toggle("NVIDIA Ray Reconstruction", &options.ray_reconstruction)) changed = true;
      if (!rr_available) {
        ImGui::EndDisabled();
        settings_hint("Needs DXR path tracing, an NVIDIA DLSS mode, and the signed DLSS Ray Reconstruction runtime.");
      }
    }
#ifdef GX_DLSS5
    {
      // EXPERIMENTAL: DLSS 5 rides on the DLSS/DLAA pass (it needs its depth and motion vectors).
      const bool nr_blocked = d3d11 || options.dlss_mode == 0 || options.dlss_mode >= 6;
      // Greying the checkbox only stops it being clicked; it leaves whatever value was already
      // there. Someone who turned DLSS 5 on and then set Upscaling to Off kept a setting that
      // reads as on, saves as on and is still acted on by the renderer (gx_d3d12 tests opts_.dlss5
      // directly), while the panel shows a disabled box they cannot use to switch it back off.
      // Clear it when it cannot apply, so what the panel shows and what the renderer does agree.
      if (nr_blocked && options.dlss5) { options.dlss5 = false; changed = true; }
      if (nr_blocked) ImGui::BeginDisabled();
      if (settings_toggle("DLSS 5 Neural Rendering (experimental)", &options.dlss5)) changed = true;
      if (options.dlss5) {
        // Sliders apply when released: every change rebuilds the model's feature, and doing that on
        // each pixel of a drag would stall the frame repeatedly.
        dlss5::Tuning& t = options.dlss5_tuning;
        auto percent = [&](const char* label, float& v) {
          static std::unordered_map<const float*, int> held;
          auto it = held.find(&v);
          int shown = it != held.end() ? it->second : (int)std::lround(v * 100.0f);
          const ImVec4 accent = shown > 200 ? ImVec4(1.0f, 0.22f, 0.26f, 1.0f) :
                                shown > 150 ? ImVec4(1.0f, 0.57f, 0.16f, 1.0f) :
                                              ImVec4(0.56f, 0.69f, 1.0f, 1.0f);
          ImGui::PushStyleColor(ImGuiCol_SliderGrab, accent);
          ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, accent);
          settings_slider(label, &shown, 0, 1000, "%d%%");
          ImGui::PopStyleColor(2);
          if (ImGui::IsItemActive()) held[&v] = shown;
          else if (it != held.end()) { held.erase(it); v = shown / 100.0f; changed = true; }
          return shown;
        };
        const int intensity_pct = percent("Intensity", t.intensity);
        const int detail_pct = percent("Surface detail", t.detail);
        const int tone_pct = percent("Lighting and tone", t.tone);
        bool skin_auto = t.skin < 0.0f;
        if (settings_toggle("Skin detail: automatic", &skin_auto)) { t.skin = skin_auto ? -1.0f : 1.0f; changed = true; }
        const int skin_pct = skin_auto ? 0 : percent("Skin detail", t.skin);
        const int extreme_pct = std::max({intensity_pct, detail_pct, tone_pct, skin_pct});
        if (extreme_pct > 150) {
          const bool red = extreme_pct > 200;
          const float pulse = red ? .5f + .5f * std::sin((float)ImGui::GetTime() * 3.2f) : .25f;
          const ImVec2 origin = ImGui::GetCursorScreenPos();
          const float badge_width = std::max(0.0f, std::min(ImGui::GetContentRegionAvail().x, 440.0f));
          constexpr float badge_height = 38.0f;
          const ImU32 fill = red ? IM_COL32(45, 21, 33, 250) : IM_COL32(46, 34, 23, 248);
          const ImU32 edge = red ? IM_COL32(255, (uint8_t)(75+55*pulse), (uint8_t)(86+50*pulse), 255) :
                                   IM_COL32(230, 145, 48, 255);
          const ImVec2 a(origin.x, origin.y + 4.0f);
          const ImVec2 b(a.x + badge_width, a.y + badge_height);
          ImDrawList* draw = ImGui::GetWindowDrawList();
          draw->AddRectFilled(ImVec2(a.x,a.y+3),ImVec2(b.x,b.y+3),IM_COL32(0,0,0,68),8.0f);
          draw->AddRectFilled(a,b,fill,8.0f);
          draw->AddRect(a,b,edge,8.0f,0,red ? 1.5f+0.7f*pulse : 1.2f);
          draw->AddRectFilled(a,ImVec2(a.x+4,b.y),edge,2.0f);
          draw->AddTriangleFilled(ImVec2(a.x+18,a.y+26),ImVec2(a.x+27,a.y+10),
                                  ImVec2(a.x+36,a.y+26),edge);
          draw->AddLine(ImVec2(a.x+27,a.y+15),ImVec2(a.x+27,a.y+21),fill,1.8f);
          draw->AddCircleFilled(ImVec2(a.x+27,a.y+24),1.0f,fill,12);
          const char* badge = red ? "EXTREME" : "HIGH";
          draw->AddText(settings_heading_font(),16.0f,ImVec2(a.x+47,a.y+6),
                        IM_COL32(255,247,240,255),badge);
          char percent[24];
          std::snprintf(percent,sizeof(percent),"%d%%",extreme_pct);
          const ImVec2 value_size=ImGui::CalcTextSize(percent);
          draw->AddText(ImVec2(b.x-value_size.x-15,a.y+7),IM_COL32(255,247,240,255),percent);
          ImGui::Dummy(ImVec2(0,badge_height+12.0f));
          if (extreme_pct >= 1000)
            ImGui::TextWrapped("i'm not sure youre ready for that, but you can try");
          else if (red)
            ImGui::TextWrapped("Above 200%% is experimental. NVIDIA has not published a supported numeric range; visual artifacts or feature failure are possible.");
        }
        if (tone_pct > 150)
          ImGui::TextWrapped("Lighting above 150%% can flicker on stage backgrounds.");
        const char* styles[] = {"Style 0 (default)", "Style 1", "Style 2", "Style 3"};
        ImGui::SetNextItemWidth(160.0f);
        if (settings_combo("Style", &t.style, styles, 4)) changed = true;
        const char* presets[] = {"Model default", "Preset 1", "Preset 2", "Preset 3"};
        ImGui::SetNextItemWidth(160.0f);
        if (settings_combo("Model preset", &t.preset, presets, 4)) changed = true;
        if (settings_toggle("Protect HUD and flat areas (auto mask)", &t.auto_mask)) changed = true;
        if (ImGui::Button("Reset DLSS 5 controls")) { t = dlss5::Tuning{}; changed = true; }
        // Named tuning profiles: a saved sliders-and-all setup under a name, one text file per name
        // in Dlss5Profiles beside port-settings.ini. Loading one applies it immediately.
        {
          static std::string active_name;
          static char new_name[48] = "";
          static std::string status;
          const std::vector<std::string> list = dlss5::profile_list();
          ImGui::AlignTextToFramePadding();
          ImGui::TextUnformatted("Profile");
          ImGui::SameLine();
          ImGui::SetNextItemWidth(200.0f);
          if (ImGui::BeginCombo("##dlss5_profile", active_name.empty() ? "(unsaved)" : active_name.c_str())) {
            for (const std::string& name : list) {
              if (ImGui::Selectable(name.c_str(), name == active_name)) {
                if (dlss5::profile_load(name, t)) { active_name = name; status = "Loaded " + name + "."; changed = true; }
                else status = "Could not read " + name + ".";
              }
            }
            ImGui::EndCombo();
          }
          ImGui::SameLine();
          if (ImGui::Button("Save as...")) { new_name[0] = 0; status.clear(); ImGui::OpenPopup("dlss5_save"); }
          if (ImGui::BeginPopup("dlss5_save")) {
            ImGui::TextUnformatted("Name");
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            const bool enter = ImGui::InputText("##dlss5_name", new_name, sizeof new_name, ImGuiInputTextFlags_EnterReturnsTrue);
            if ((ImGui::Button("Save") || enter) && new_name[0]) {
              if (dlss5::profile_save(new_name, t)) { active_name = new_name; status = "Saved " + active_name + "."; }
              else status = "Could not save. Check the name.";
              ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
          }
          ImGui::SameLine();
          ImGui::BeginDisabled(active_name.empty());
          if (ImGui::Button("Delete")) {
            status = dlss5::profile_delete(active_name) ? "Deleted " + active_name + "." : "Could not delete " + active_name + ".";
            active_name.clear();
          }
          ImGui::EndDisabled();
          if (!status.empty()) { ImGui::SameLine(); settings_hint("%s", status.c_str()); }
        }
      }
      if (nr_blocked) ImGui::EndDisabled();
      if (nr_blocked) settings_hint("DLSS 5 needs Direct3D 12 and Upscaling set to DLAA or a DLSS mode.");
      else if (options.dlss5) {
        ImGui::TextWrapped("DLSS 5: %s. Experimental; intended for RTX 50 series or newer. It runs on every frame shown, so expect a lower frame rate.", dlss5::status());
        float dlaa_ms = 0, dlss5_ms = 0;
        gpu_pass_cost(&dlaa_ms, &dlss5_ms);
        if (dlss5_ms > 0.0f) settings_hint("Costing about %.1f ms of GPU time per frame right now.", dlss5_ms);
      }
    }
#endif
    if (options.dlss_mode == 1 || options.dlss_mode == 6) {
      ImGui::TextWrapped("DLAA anti-aliases the game at the Internal resolution above without changing it, then the picture is fitted to the window as usual. Internal resolution and Anti-aliasing keep working, so DLAA stacks with 4x SSAA if you want both.");
    } else if (dlss_picks_resolution) {
      static const char* ratios[] = {"", "", "67% (Quality)", "58% (Balanced)", "50% (Performance)", "33% (Ultra Performance)",
                                     "", "77% (Ultra Quality)", "67% (Quality)", "59% (Balanced)", "50% (Performance)"};
      ImGui::TextWrapped("DLSS renders the game at %s of the window size (at 1080p about 1280x960) and upscales it. That is what DLSS is for in heavy games; Melee is cheap to render, so here it is a downgrade in sharpness, and Internal resolution and Anti-aliasing above are ignored while it is on. For the sharpest image choose Native, set Internal resolution to 3x or higher and Anti-aliasing to 4x SSAA (the Dolphin look), or choose DLAA (full resolution, DLSS used only as anti-aliasing).", ratios[options.dlss_mode]);
    }
    int sharp = (int)std::lround(options.sharpness * 100.0f);
    if (settings_slider("Sharpening", &sharp, 0, 100, "%d%%")) { options.sharpness = sharp / 100.0f; changed = true; }
    int ao = (int)std::lround(options.screen_space_ao * 100.0f);
    if (settings_slider("Screen-space ambient occlusion", &ao, 0, 100, "%d%%")) { options.screen_space_ao = ao / 100.0f; changed = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Experimental depth-based contact shading. It uses the game's depth buffer and works on D3D11 and D3D12; it is not RTX ray tracing.");
    // Display adjustment over the finished picture, HUD included; 100% is neutral on all three.
    int bright = (int)std::lround(options.brightness * 100.0f);
    if (settings_slider("Brightness", &bright, 50, 150, "%d%%")) { options.brightness = bright / 100.0f; changed = true; }
    int contrast = (int)std::lround(options.contrast * 100.0f);
    if (settings_slider("Contrast", &contrast, 50, 150, "%d%%")) { options.contrast = contrast / 100.0f; changed = true; }
    int vibrance = (int)std::lround(options.vibrance * 100.0f);
    if (settings_slider("Vibrance", &vibrance, 0, 200, "%d%%")) { options.vibrance = vibrance / 100.0f; changed = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("0%% is greyscale, 100%% is native, above that is more saturated.");
    const char* subframe_modes[] = {"Off (60 Hz poses only)", "Predict ahead (no delay, can overshoot on speed changes)", "Interpolate (exact, one frame of delay)"};
    int sf = options.subframe == SubFrameMode::Off ? 0 : options.subframe == SubFrameMode::AuthoredInterpolate ? 2 : 1;
    if (settings_combo("Sub-frame animation", &sf, subframe_modes, 3)) { options.subframe = sf == 0 ? SubFrameMode::Off : sf == 2 ? SubFrameMode::AuthoredInterpolate : SubFrameMode::Authored; changed = true; }
    if (sf == 1) ImGui::TextWrapped("Samples supported animation beyond the latest pose. Sudden stops can require correction.");
    if (sf == 2) ImGui::TextWrapped("Samples between completed poses. This adds up to one simulation tick of visual delay; unsupported motion may hold.");
    // Say it rather than quietly ignoring the setting: a player who picked a mode and sees no
    // difference should be told why, and this pairing is what several stage glitch reports were.
    // Judged on what is actually being presented: "follow the monitor" on a 60 Hz display is the
    // same one-frame-per-tick case as an explicit cap of 60.
    const double shown_rate = options.fps_cap > 0 ? options.fps_cap
                            : options.fps_cap < 0 ? (double)host::window_refresh_rate() : 0.0;
    // Warnings wrap like every other explanation here. TextColored does not wrap, so both of these
    // ran off the right edge of the panel and were cut mid-word, which is how a warning ends up
    // reading as a rendering fault.
    auto warning = [](const char* fmt, ...) {
      va_list args; va_start(args, fmt);
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.25f, 1.0f));
      ImGui::TextWrappedV(fmt, args);
      ImGui::PopStyleColor();
      va_end(args);
    };
    if (sf != 0 && shown_rate > 0.0 && shown_rate <= 60.5)
      warning("Not in use: only %.0f frames a second are being shown. There is one frame "
              "per tick either way, so this would cost delay and accuracy and buy no "
              "smoothness. Raise the frame rate above 60, or leave this off.", shown_rate);
    if (options.fps_cap == 0)
      warning("Uncapped draws frames no display can show, and on light scenes it takes "
              "enough of the machine to slow the game down.");
    // The "Visual effects" control was removed: the filter it drove deleted the stage select
    // pointer and menu text, and nothing in a draw separates a hit spark from a cursor.

    // ---- Texture packs ----
    // The list is always here, whether or not replacement is switched on: someone who has already
    // put a pack in the folder should see it without having to find a checkbox first. Scanning is
    // filenames only, so showing it costs nothing; decoding the PNGs is the expensive part and
    // still happens only when packs are on.
    ImGui::Separator();
    texpack::refresh_packs();
    const auto installed = texpack::packs();
    ImGui::TextUnformatted("Texture packs");
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Add")) texpack::open_packs_folder();
    wrapped_tooltip("Opens the TexturePacks folder. Put a pack folder in there and it appears in this list.");
    if (installed.empty()) {
      ImGui::TextWrapped("None installed. Press + Add and drop a pack folder in.");
    } else {
      changed |= settings_toggle("Use texture packs", &options.custom_textures);
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        texpack::configure(options.custom_textures, options.dump_textures);
        g_textures_dirty.store(true, std::memory_order_relaxed);
      }
      ImGui::SameLine();
      changed |= settings_toggle("Load them at startup", &options.prefetch_textures);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Decodes every replacement once when the game starts instead of the first time each\n"
                          "texture appears. One wait up front rather than stutters through the first minutes.");
      // A box per pack: green while it is on, red while it is off, so the state reads at a glance.
      for (const auto& pack : installed) {
        const ImVec4 on(0.25f, 0.62f, 0.35f, 1.0f), off(0.62f, 0.24f, 0.22f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, pack.enabled ? on : off);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, pack.enabled ? on : off);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, pack.enabled ? on : off);
        ImGui::PushID(pack.name.c_str());
        if (ImGui::Button(pack.enabled ? "ON" : "OFF", ImVec2(44, 0))) {
          texpack::set_pack_enabled(pack.name, !pack.enabled);
          g_textures_dirty.store(true, std::memory_order_relaxed);
          changed = true;
        }
        ImGui::PopID();
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
        ImGui::Text("%s", pack.name.c_str());
        ImGui::SameLine();
        settings_hint("(%llu textures)", (unsigned long long)pack.files);
      }
      if (texpack::prefetching()) {
        uint64_t done = 0, total = 0;
        texpack::prefetch_progress(&done, &total);
        ImGui::Text("Loading textures %llu / %llu", (unsigned long long)done, (unsigned long long)total);
      }
      // For pack authors, not for playing: writes what the game drew, named the way a pack must.
      changed |= settings_toggle("Dump textures (for making a pack)", &options.dump_textures);
      if (ImGui::IsItemDeactivatedAfterEdit()) texpack::configure(options.custom_textures, options.dump_textures);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Saves every texture the game draws into Dump\\Textures\\GALE01 with the exact\n"
                          "filenames a replacement has to use. Only useful if you are making a pack.");
    }

    // ---- Looping menu backgrounds ----
    // Fixed filenames make the first test hard to misconfigure: replace css.mp4 or sss.mp4 in
    // place, and one checkbox restores the unmodified game backdrop.
    ImGui::Separator();
    ImGui::TextUnformatted("Animated menu backgrounds");
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Open folder")) video_bg::open_folder();
    bool video_on = options.video_backgrounds;
    if (settings_toggle("Use looping MP4 backgrounds", &video_on)) {
      options.video_backgrounds = video_on;
      video_bg::set_enabled(video_on);
      changed = true;
    }
    ImGui::TextWrapped("Drop css.mp4 and/or sss.mp4 into VideoBackgrounds. Audio is ignored. "
                       "The first visit learns the vanilla backdrop without changing the ISO.");
    const std::string css_status = video_bg::status(0);
    const std::string sss_status = video_bg::status(1);
    auto target_picker = [](int slot, const char* label) {
      const auto observed = video_bg::observed_textures(slot);
      if (observed.empty()) return;
      const std::string current = video_bg::target(slot);
      const std::string preview = current.empty() ? "Choose observed texture..." : current;
      ImGui::PushID(slot == 0 ? "css-video-target" : "sss-video-target");
      ImGui::SetNextItemWidth(-1.0f);
      if (ImGui::BeginCombo(label, preview.c_str())) {
        for (const auto& texture : observed) {
          const std::string item = texture.name + " — " + std::to_string(texture.width) + "x" +
              std::to_string(texture.height) + ", " +
              std::to_string(texture.distinct_frames) + " frames / " +
              std::to_string(texture.observations) + " observations";
          if (ImGui::Selectable(item.c_str(), texture.name == current))
            video_bg::choose_target(slot, texture.name);
          if (texture.name == current) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
      ImGui::PopID();
    };
    ImGui::Text("Character select: %s", css_status.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Relearn CSS")) video_bg::relearn(0);
    target_picker(0, "CSS texture target");
    ImGui::Text("Stage select: %s", sss_status.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Relearn SSS")) video_bg::relearn(1);
    target_picker(1, "SSS texture target");

    // ---- Low spec ----
    // One switch for every setting above that costs frames. Turning it on remembers what the player
    // had; turning it off puts exactly that back, not a hardcoded default. Both halves are saved, so
    // the switch and the remembered settings survive a restart. Everything it changes takes effect
    // immediately except the graphics backend, which needs a new device and so a new launch.
    {
      bool low = options.low_spec;
      if (settings_toggle("Low spec", &low)) {
        if (low) {
          options.low_spec_previous = {options.api, options.fps_cap, options.efb_scale, options.ssaa,
                                       options.anisotropy, options.effects_level, options.dlss_mode, options.subframe};
          if (d3d11_available()) options.api = RenderApi::D3D11;   // the better exercised driver path on old integrated GPUs
          options.fps_cap = 60;
          options.efb_scale = 1;                  // native 640x528, the floor
          options.ssaa = 1;                       // no supersampling
          options.anisotropy = 1;                 // no anisotropic filtering

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
      settings_hint("For integrated graphics and older laptops.");
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
        ImGui::PopItemWidth();
      }
      if (state.active_tab == 1) {
    ImGui::PushItemWidth(330.0f);
    int music = slippi::jukebox::user_volume();
    if (settings_slider("Music", &music, 0, 100, "%d%%")) slippi::jukebox::set_user_volume(music);
    // With a game running the device holds the live value. Without one, which is the settings
    // window the launcher opens, the saved value is all there is: reading back from an audio module
    // that was never opened returned zero every frame and dragged the slider back to it.
    if (host::audio_running()) g_volume = host::audio_volume();
    if (settings_slider("Volume", &g_volume, 0, 100, "%d%%")) host::audio_set_volume(g_volume);
    state.volume = g_volume;
        ImGui::PopItemWidth();
        if (settings_toggle("Menu sounds", &options.settings_menu_sounds)) {
          changed = true;
          if (options.settings_menu_sounds) host::audio_ui_sound(2);   // a sample, so the change is heard
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A short tick when you move through the settings, and when a page opens or closes.");
      }
      if (state.active_tab == 5) {
        static std::string mod_message;
        ImGui::TextUnformatted("Customize");
        ImGui::TextDisabled("Choose the menu layout, colors, and transparency.");
        ImGui::Spacing();
        changed |= settings_toggle("Enable Legacy Menu (F11)", &options.legacy_menu_enabled);
        ImGui::TextWrapped("F11 and launcher Settings open the selected legacy menu. Both menus edit the same settings.");
        const char* legacy_names[] = {"Legacy Old (v0.6.6)", "Legacy New"};
        int legacy_choice = options.legacy_menu_style == 6 ? 1 : 0;
        if (settings_combo("Legacy menu", &legacy_choice, legacy_names, 2)) {
          options.legacy_menu_style = legacy_choice == 1 ? 6 : 7;
          changed = true;
        }
        if (state.legacy_presentation && !options.legacy_menu_enabled) {
          options.overlay_style = state.legacy_saved_appearance;
          state.legacy_presentation = false;
          reset_settings_home("legacy menu switched off");
        }
        ImGui::Spacing();
        static constexpr const char* appearance_names[] = {
            "Clean side", "Icon tiles", "GD Melee", "Radial", "Wide tabs", "Simple"};
        static constexpr const char* appearance_images[] = {
            "clean-side.png", "icon-tiles.png", "gd-melee.png", "radial.png", "wide-tabs.png", "simple.png"};
        static constexpr const char* appearance_hints[] = {
            "Pink side panel; settings slide in beside the live game",
            "Rounded, dimensional settings tiles",
            "GD Melee's slanted navigation and matching setting rows",
            "Controller-first radial category navigation",
            "Wide category tabs with live settings pages",
            "Compact settings layout"};
        static const char* const palette_names[7][4] = {
          {"Original pink", "Electric cyan", "Sunset amber", "Arcade violet"},
          {"Original candy", "Arcade brights", "Soft pastels", "Night lights"},
          {"Classic gold", "Cobalt", "Emerald", "Crimson"},
          {"Original blue", "Plasma violet", "Emerald", "Amber"},
          {"Original blue", "Magenta", "Mint", "Amber"},
          {"Simple blue", "Graphite", "Forest", "Warm gray"},
          {"Windows blue", "Steel", "Pine", "Sepia"}};
        static const char* const palette_short[7][4] = {
          {"Pink","Cyan","Amber","Violet"},
          {"Candy","Arcade","Pastel","Night"},
          {"Gold","Cobalt","Emerald","Crimson"},
          {"Blue","Violet","Emerald","Amber"},
          {"Blue","Magenta","Mint","Amber"},
          {"Blue","Graphite","Forest","Warm"},
          {"Blue","Steel","Pine","Sepia"}};
        if (false) { // Superseded by the compact appearance controls below.
        ImGui::TextUnformatted("Color palette for this appearance");
        ImGui::TextDisabled(options.overlay_style==2 ?
            "Classic Gold is GD Melee's original palette." :
            "Each layout remembers its own color choice.");
        int& palette = options.overlay_palettes[std::clamp(options.overlay_style, 0, 5)];
        const float palette_gap=ImGui::GetStyle().ItemSpacing.x;
        const float palette_w=(ImGui::GetContentRegionAvail().x-3*palette_gap)/4.0f;
        for (int variant=0;variant<4;++variant) {
          if (variant) ImGui::SameLine();
          ImGui::PushID(variant);
          const bool palette_pressed=settings_hit_button("##palette",ImVec2(palette_w,52));
          if (palette_pressed) {
            if (palette!=variant) { palette=variant; changed=true; }
          }
          const ImVec2 a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
          const bool hover=ImGui::IsItemHovered()||ImGui::IsItemFocused();
          ImDrawList* draw=ImGui::GetWindowDrawList();
          const ImU32 swatch=settings_accent_for(options.overlay_style,variant);
          draw->AddRectFilled(a,b,IM_COL32(18,28,42,242),8.0f);
          draw->AddRectFilled(ImVec2(a.x+5,a.y+5),ImVec2(b.x-5,a.y+23),swatch,6.0f);
          draw->AddLine(ImVec2(a.x+13,a.y+8),ImVec2(b.x-13,a.y+8),
                        IM_COL32(255,255,255,125),2.0f);
          draw->AddText(ImGui::GetFont(),12.0f,ImVec2(a.x+7,a.y+29),
                        IM_COL32(245,248,253,255),palette_short[std::clamp(options.overlay_style, 0, 5)][variant]);
          if (palette==variant||hover)
            draw->AddRect(a,b,palette==variant?swatch:IM_COL32(230,238,252,150),
                          8.0f,0,palette==variant?2.5f:1.5f);
          if (hover) ImGui::SetTooltip("%s",palette_names[std::clamp(options.overlay_style, 0, 5)][variant]);
          ImGui::PopID();
        }
        ImGui::Spacing();
        }
        if (false) { // Retained as design reference only; selectable options below are live styles, not painted mockup screenshots.
        const float available = ImGui::GetContentRegionAvail().x;
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const int per_row = std::clamp((int)((available + gap) / (145.0f + gap)), 1, 5);
        const float card_w = (available - (per_row - 1) * gap) / per_row;
        const ImVec2 card_size(card_w, 140.0f);
        for (int style = 0; style < 5; ++style) {
          if (style % per_row != 0) ImGui::SameLine();
          ImGui::PushID(style);
          const bool selected = options.overlay_style == style;
          settings_hit_button("##appearance_preview", card_size);
          const ImVec2 a = ImGui::GetItemRectMin();
          const ImVec2 b = ImGui::GetItemRectMax();
          const bool hovered = ImGui::IsItemHovered();
          ImDrawList* draw = ImGui::GetWindowDrawList();
          const ImU32 bg = ImGui::GetColorU32(selected ? ImGuiCol_FrameBgActive :
                                               hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
          draw->AddRectFilled(a, b, bg, 5.0f);
          // Both 5x4 reference sheets have 334x235 cells. Preserve their proportions instead
          // of stretching each mockup to whichever card width a window happens to allow.
          const float preview_h = 105.0f;
          const float preview_w = std::min(card_w - 10.0f, preview_h * 334.0f / 235.0f);
          const ImVec2 p(a.x + (card_w - preview_w) * 0.5f, a.y + 5.0f);
          const ImVec2 q(p.x + preview_w, p.y + preview_h);
          // The source sheet for #02 contains Japanese labels. Draw the implemented English
          // layout here so the appearance picker previews what the player will actually see.
          if (style == 0) {
            draw->AddRectFilled(p,q,IM_COL32(13,20,43,255));
            draw->AddRectFilled(ImVec2(p.x+3,p.y+8),ImVec2(p.x+preview_w*.46f,q.y-7),
                                IM_COL32(24,39,75,255));
            const float left_w=preview_w*.46f;
            for(int line=0;line<7;++line)
              draw->AddLine(ImVec2(p.x+6+line*5,p.y+6),ImVec2(p.x+6+line*5,q.y-5),
                            IM_COL32(46,71,126,115));
            draw->AddRectFilled(ImVec2(p.x+8,p.y+preview_h*.43f),
                                ImVec2(p.x+left_w-3,p.y+preview_h*.57f),
                                IM_COL32(255,196,25,255),2.0f);
            draw->AddText(ImGui::GetFont(),9.0f,ImVec2(p.x+12,p.y+preview_h*.455f),
                          IM_COL32(20,19,16,255),"Unranked");
            const float x=p.x+left_w;
            draw->AddRectFilled(ImVec2(x,p.y),q,IM_COL32(17,18,29,250));
            const ImVec2 banner[]={ImVec2(x,p.y),ImVec2(q.x,p.y),
                ImVec2(q.x,p.y+20),ImVec2(x,p.y+20)};
            draw->AddConvexPolyFilled(banner,4,IM_COL32(184,17,104,255));
            draw->AddText(ImGui::GetFont(),10.0f,ImVec2(x+5,p.y+4),
                          IM_COL32(255,255,255,255),"SETTINGS");
            static const char* labels[]={"VIDEO","AUDIO","GAME","CONTROLS",
                                          "OVERLAYS","CUSTOMIZE","CODES"};
            for(int line=0;line<7;++line) {
              const float y=p.y+23+line*11.0f;
              const ImVec2 quad[]={ImVec2(x+4,y),ImVec2(q.x-4,y-1),
                                   ImVec2(q.x-7,y+9),ImVec2(x+1,y+10)};
              draw->AddConvexPolyFilled(quad,4,line==0?IM_COL32(244,20,137,255):
                                                       IM_COL32(28,30,43,255));
              draw->AddText(ImGui::GetFont(),7.0f,ImVec2(x+10,y+1),
                            IM_COL32(255,255,255,255),labels[line]);
            }
          } else if (style == 1) {
            draw->AddRectFilled(p,q,IM_COL32(18,38,68,255));
            for(int col=0;col<8;++col)
              draw->AddLine(ImVec2(p.x+col*21.0f,p.y),ImVec2(p.x+col*21.0f,q.y),
                            IM_COL32(64,98,145,65));
            draw->AddText(ImGui::GetFont(),11.0f,ImVec2(p.x+6,p.y+5),
                          IM_COL32(255,255,255,255),"Settings");
            const float gap=3.0f,tile=(preview_w-12.0f-3.0f*gap)/4.0f;
            static const char* tiny[]={"V","A","G","C","O","M","<>"};
            for(int i=0;i<7;++i) {
              const int row=i<4?0:1,col=i<4?i:i-4;
              const ImVec2 lo(p.x+6.0f+col*(tile+gap),p.y+23.0f+row*35.0f);
              const ImVec2 hi(lo.x+tile,lo.y+31.0f);
              draw->AddRectFilled(ImVec2(lo.x+1,lo.y+4),ImVec2(hi.x+1,hi.y+4),
                                  IM_COL32(3,8,25,150),7.0f);
              draw->AddRectFilled(lo,hi,
                  settings_dashboard_tile_color(i,options.overlay_palettes[1]),7.0f);
              draw->AddRect(ImVec2(lo.x+1,lo.y+1),ImVec2(hi.x-1,hi.y-1),
                            IM_COL32(255,255,255,175),6.0f);
              const ImVec2 size=ImGui::GetFont()->CalcTextSizeA(10.0f,FLT_MAX,0,tiny[i]);
              draw->AddText(ImGui::GetFont(),10.0f,
                  ImVec2((lo.x+hi.x-size.x)*.5f,(lo.y+hi.y-size.y)*.5f),
                  IM_COL32(20,35,58,255),tiny[i]);
            }
          } else if (style == 2) {
            draw->AddRectFilled(p,q,IM_COL32(25,33,42,255));
            const float k=preview_w/640.0f;
            auto pt=[&](float x,float y) {
              return ImVec2(p.x+(x+(240-y)*.25f)*k,p.y+y*(preview_h/480.0f));
            };
            auto quad=[&](float x0,float y0,float x1,float y1,ImU32 color) {
              const ImVec2 v[]={pt(x0,y0),pt(x1,y0),pt(x1,y1),pt(x0,y1)};
              draw->AddConvexPolyFilled(v,4,color);
            };
            quad(84,24,343,52,style==options.overlay_style?settings_accent(2):IM_COL32(240,180,41,255));
            quad(340,46,550,52,IM_COL32(10,14,24,255));
            for(int row=0;row<5;++row)
              quad(96.0f,84.0f+34.0f*row,540.0f,114.0f+34.0f*row,
                   row==0&&style==options.overlay_style?settings_accent(2):
                   row==0?IM_COL32(240,180,41,255):IM_COL32(46,54,64,255));
            draw->AddText(ImVec2(p.x+15,p.y+7),IM_COL32(10,14,24,255),"SETTINGS");
            draw->AddText(ImVec2(p.x+27,p.y+35),IM_COL32(10,14,24,255),"VIDEO");
          } else if (style == 3) {
            draw->AddRectFilled(p,q,IM_COL32(12,24,43,255));
            const ImVec2 c((p.x+q.x)*.5f,p.y+preview_h*.56f);
            const float outer=std::min(43.0f,preview_h*.42f), hub=outer*.39f;
            draw->AddCircleFilled(ImVec2(c.x+2,c.y+4),outer+3,IM_COL32(0,4,13,125),72);
            draw->AddCircleFilled(c,outer+1,IM_COL32(8,15,25,255),72);
            for(int i=0;i<7;++i) {
              const float center=-1.57079632679f+i*6.28318530718f/7.0f;
              const float half=3.14159265359f/7.0f*.93f;
              const ImVec2 a(c.x+std::cos(center-half)*outer,c.y+std::sin(center-half)*outer);
              const ImVec2 b(c.x+std::cos(center+half)*outer,c.y+std::sin(center+half)*outer);
              draw->AddTriangleFilled(c,a,b,i==0?settings_accent_for(3,options.overlay_palettes[3]):
                                                   IM_COL32(31,45,62,255));
              draw->AddLine(a,c,IM_COL32(3,9,17,225),1.1f);
              const ImVec2 icon(c.x+std::cos(center)*outer*.67f,
                                c.y+std::sin(center)*outer*.67f);
              settings_symbol_icon(i,icon,.38f,IM_COL32(238,246,253,255));
            }
            draw->AddCircleFilled(c,hub,IM_COL32(11,22,36,255),48);
            draw->AddCircle(c,hub,IM_COL32(119,153,179,220),48,1.8f);
            draw->AddCircleFilled(ImVec2(c.x,c.y-2),5,IM_COL32(195,224,242,255),24);
            draw->AddText(ImGui::GetFont(),7.0f,ImVec2(p.x+6,p.y+5),
                          IM_COL32(238,245,252,255),"SETTINGS  /  A SELECT  ·  B BACK");
          } else if (style == 4) {
            draw->AddRectFilled(p,q,IM_COL32(11,23,37,255));
            draw->AddText(ImGui::GetFont(),10.0f,ImVec2(p.x+7,p.y+4),
                          IM_COL32(239,245,252,255),"SETTINGS");
            static const char* tabs[]={"VIDEO","AUDIO","GAME","CTRL","HUD","MODS","CODES"};
            const float tab_top=p.y+21.0f, tab_h=13.0f, tab_gap=2.0f;
            const float tab_w=(preview_w-12.0f-6.0f*tab_gap)/7.0f;
            for(int i=0;i<7;++i) {
              const float x=p.x+6.0f+i*(tab_w+tab_gap);
              draw->AddRectFilled(ImVec2(x,tab_top),ImVec2(x+tab_w,tab_top+tab_h),
                  i==0?settings_accent_for(4,options.overlay_palettes[4]):IM_COL32(24,40,57,255),3.0f);
              draw->AddText(ImGui::GetFont(),5.5f,ImVec2(x+2,tab_top+3),
                            IM_COL32(244,248,253,255),tabs[i]);
            }
            const ImVec2 panel_min(p.x+6,p.y+39), panel_max(q.x-6,q.y-5);
            draw->AddRectFilled(panel_min,panel_max,IM_COL32(17,31,46,245),4.0f);
            draw->AddText(ImGui::GetFont(),8.0f,ImVec2(panel_min.x+6,panel_min.y+3),
                          settings_accent_for(4,options.overlay_palettes[4]),"VIDEO");
            static const char* rows[]={"Quality","Frame rate","VSync","Window size"};
            for(int i=0;i<4;++i) {
              const float y=panel_min.y+16+i*10.0f;
              draw->AddText(ImGui::GetFont(),6.5f,ImVec2(panel_min.x+6,y+1),
                            IM_COL32(217,228,238,255),rows[i]);
              draw->AddRectFilled(ImVec2(panel_min.x+64,y),ImVec2(panel_max.x-5,y+8),
                                  IM_COL32(32,54,77,255),2.0f);
              if(i<2) draw->AddText(ImGui::GetFont(),5.0f,
                  ImVec2(panel_min.x+69,y+1),IM_COL32(239,245,252,255),i==0?"High":"Match monitor");
              else draw->AddCircleFilled(ImVec2(panel_max.x-12,y+4),2.4f,
                  i==2?settings_accent_for(4,options.overlay_palettes[4]):IM_COL32(172,186,200,255),12);
            }
          } else {
            draw->AddRectFilled(p, q, IM_COL32(17, 29, 61, 255));
          }
          draw->AddText(ImVec2(a.x + 6.0f, a.y + 115.0f),
                        ImGui::GetColorU32(selected ? ImGuiCol_Text : ImGuiCol_TextDisabled),
                        appearance_names[style]);
          if (selected) {
          draw->AddRect(a, b, style == 2 ? settings_accent(2) :
                                    IM_COL32(133, 184, 255, 255), 5.0f, 0, 2.0f);
            draw->AddText(ImVec2(b.x - 48.0f, a.y + 8.0f),
                          IM_COL32(255, 255, 255, 255), "ACTIVE");
          }
          if (hovered) ImGui::SetTooltip("%s", appearance_hints[style]);
          if (ImGui::IsItemClicked()) {
            if (options.overlay_style != style) {
              options.overlay_style = style;
              state.clean_detail_open=false; state.clean_home_frames=0;
              state.dashboard_detail_open=false; state.dashboard_home_frames=0;
              state.gd_detail_open=false; state.gd_screen_frames=0;
              state.radial_detail_open=false; state.wide_detail_open=false;
              state.content_anim_tab=-1;
              changed = true;
            }
          }
          ImGui::PopID();
        }
        }
        {
          ImGui::TextUnformatted("Menu style");
          ImGui::TextDisabled("Choose a picture to change the menu layout.");
          if (ImGui::Button("Choose menu style...",ImVec2(-1.0f,30.0f))) {
            ImGui::OpenPopup("Menu styles");
            g_menu_style_gallery_open=true;
          }
          if (ImTextureData* current_preview=cosmetic_preview(ui_source_asset_path(
                  "menu_previews",appearance_images[std::clamp(options.overlay_style,0,5)]))) {
            const ImVec2 preview_size(160.0f,120.0f);
            if (ImGui::ImageButton("##current_menu_style",current_preview->GetTexRef(),preview_size)) {
              ImGui::OpenPopup("Menu styles");
              g_menu_style_gallery_open=true;
            }
          }
          static bool test_gallery_opened=false;
          if (!test_gallery_opened && std::getenv("MELEE_TEST_STYLE_GALLERY")) {
            ImGui::OpenPopup("Menu styles");
            g_menu_style_gallery_open=true;
            test_gallery_opened=true;
          }
          const ImVec2 display_size=ImGui::GetIO().DisplaySize;
          ImGui::SetNextWindowSize(ImVec2(std::min(820.0f,display_size.x-24.0f),
                                           std::min(570.0f,display_size.y-20.0f)),ImGuiCond_Appearing);
          ImGui::SetNextWindowPos(ImVec2(display_size.x*.5f,display_size.y*.5f),
                                  ImGuiCond_Appearing,ImVec2(.5f,.5f));
          if (ImGui::BeginPopupModal("Menu styles",nullptr,
                  ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings)) {
          ImGui::TextUnformatted("Choose a menu style");
          ImGui::TextDisabled("Each picture shows the current in-game layout.");
          ImGui::Spacing();
          const float available=ImGui::GetContentRegionAvail().x;
          const float gap=ImGui::GetStyle().ItemSpacing.x;
          const int per_row=std::clamp((int)((available+gap)/(204.0f+gap)),1,4);
          const float card_w=(available-gap*(per_row-1))/per_row;
          const float image_w=std::min(card_w-12.0f,195.0f);
          const float image_h=image_w*0.75f;
          const float card_h=image_h+34.0f;
          ImGui::BeginDisabled(state.legacy_presentation);
          for(int style=0;style<6;++style) {
            if(style%per_row) ImGui::SameLine(0.0f,gap);
            ImGui::PushID(style);
            const bool pressed=settings_hit_button("##appearance_picture",ImVec2(card_w,card_h));
            const bool hovered=ImGui::IsItemHovered()||ImGui::IsItemFocused();
            const ImVec2 a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
            ImDrawList* draw=ImGui::GetWindowDrawList();
            draw->AddRectFilled(a,b,IM_COL32(15,24,38,250),7.0f);
            const ImVec2 image_min(a.x+(card_w-image_w)*0.5f,a.y+6.0f);
            const ImVec2 image_max(image_min.x+image_w,image_min.y+image_h);
            if(ImTextureData* preview=cosmetic_preview(
                ui_source_asset_path("menu_previews",appearance_images[style]))) {
              draw->AddImage(preview->GetTexRef(),image_min,image_max);
            } else {
              draw->AddRectFilled(image_min,image_max,IM_COL32(9,16,28,255));
              draw->AddText(ImVec2(image_min.x+8,image_min.y+8),IM_COL32(220,228,239,255),
                            "Preview unavailable");
            }
            const ImU32 accent=settings_accent_for(style,options.overlay_palettes[style]);
            draw->AddText(ImVec2(a.x+9,image_max.y+7),IM_COL32(239,245,252,255),appearance_names[style]);
            if(options.overlay_style==style||hovered)
              draw->AddRect(a,b,options.overlay_style==style?accent:IM_COL32(232,239,248,180),
                            7.0f,0,options.overlay_style==style?2.0f:1.2f);
            if(pressed&&options.overlay_style!=style) {
              options.overlay_style=style;
              state.clean_detail_open=false; state.clean_home_frames=0;
              state.dashboard_detail_open=false; state.dashboard_home_frames=0;
              state.gd_detail_open=false; state.gd_screen_frames=0;
              state.radial_detail_open=false; state.wide_detail_open=false;
              state.content_anim_tab=-1;
              changed=true;
              ImGui::CloseCurrentPopup();
              g_menu_style_gallery_open=false;
            }
            ImGui::PopID();
          }
          ImGui::EndDisabled();
          ImGui::Spacing();
          if (ImGui::Button("Close",ImVec2(110.0f,26.0f))) {
            ImGui::CloseCurrentPopup();
            g_menu_style_gallery_open=false;
          }
          ImGui::EndPopup();
          } else g_menu_style_gallery_open=false;
          ImGui::Spacing();
          ImGui::TextUnformatted("Color palette");
          int& palette = options.overlay_palettes[std::clamp(options.overlay_style, 0, 6)];
          for (int i=0;i<4;++i) {
            if (i) ImGui::SameLine(0.0f,10.0f);
            ImGui::PushID(i);
            const ImVec2 swatch_size(40.0f,24.0f);
            const bool clicked=ImGui::ColorButton("##palette_swatch",
                ImGui::ColorConvertU32ToFloat4(settings_accent_for(options.overlay_style,i)),
                ImGuiColorEditFlags_NoTooltip,swatch_size);
            const ImVec2 a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
            if (palette==i)
              ImGui::GetWindowDrawList()->AddRect(a,b,IM_COL32(242,246,252,220),4.0f,0,1.5f);
            if (clicked && palette!=i) { palette=i; changed=true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Palette %d",i+1);
            ImGui::PopID();
          }
          ImGui::Spacing();
          int transparency = options.settings_transparency;
          if (settings_slider("Window transparency", &transparency, 0, 65, "%d%%")) {
            options.settings_transparency=transparency;
            changed=true;
          }
          ImGui::Spacing();
          ImGui::TextUnformatted("Custom menu accent");
          ImGui::Checkbox("Use custom accent",&options.settings_custom_color_enabled);
          if (ImGui::IsItemDeactivatedAfterEdit()) changed=true;
          // Collapsed by default: the full wheel is large and most players never open it.
          if (ImGui::TreeNode("Show colour picker")) {
            ImGui::SetNextItemWidth(std::min(220.0f, ImGui::GetContentRegionAvail().x));
            if (ImGui::ColorPicker3("##menu_accent_wheel",options.settings_custom_color.data(),
                                    ImGuiColorEditFlags_PickerHueWheel|ImGuiColorEditFlags_NoSidePreview|
                                    ImGuiColorEditFlags_NoInputs)) {
              options.settings_custom_color_enabled=true;
              changed=true;
            }
            ImGui::TreePop();
          }
        }
        if (false) { // Superseded by the screenshot picker above.
          const float available = ImGui::GetContentRegionAvail().x;
          const float gap = ImGui::GetStyle().ItemSpacing.x;
          const int per_row = std::clamp((int)((available + gap) / (145.0f + gap)), 1, 4);
          const float card_w = (available - (per_row - 1) * gap) / per_row;
          static constexpr const char* style_names[] = {
            "Clean side", "Icon tiles", "GD Melee", "Radial", "Wide tabs", "Simple", "Classic"};
          for (int style = 0; style < 7; ++style) {
            if (style % per_row != 0) ImGui::SameLine();
            ImGui::PushID(style);
            const bool active = options.overlay_style == style;
            const bool pressed = settings_hit_button("##appearance_style", ImVec2(card_w, 76.0f));
            const bool hovered = ImGui::IsItemHovered() || ImGui::IsItemFocused();
            const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(a, b, IM_COL32(15, 24, 38, 248), 8.0f);
            const ImU32 accent = settings_accent_for(style, options.overlay_palettes[style]);
            draw->AddRectFilled(ImVec2(a.x + 5, a.y + 5), ImVec2(b.x - 5, a.y + 9), accent, 2.0f);
            const ImVec2 mark(a.x + 12, a.y + 19), mark_end(b.x - 12, a.y + 45);
            if (style == 0) {
              draw->AddRectFilled(mark, ImVec2(mark.x + 12, mark_end.y), IM_COL32(36, 53, 78, 255), 2.0f);
              draw->AddRectFilled(ImVec2(mark.x + 17, mark.y), mark_end, IM_COL32(31, 42, 59, 255), 2.0f);
              draw->AddRectFilled(ImVec2(mark.x + 17, mark.y + 7), ImVec2(mark_end.x, mark.y + 11), accent, 1.0f);
            } else if (style == 1) {
              const float tile = std::min(21.0f, (card_w - 39.0f) / 4.0f);
              for (int i = 0; i < 4; ++i)
                draw->AddRectFilled(ImVec2(mark.x + i * (tile + 3), mark.y),
                                    ImVec2(mark.x + i * (tile + 3) + tile, mark.y + tile),
                                    settings_dashboard_tile_color(i, options.overlay_palettes[1]), 5.0f);
            } else if (style == 2) {
              const ImVec2 row1[] = {mark, ImVec2(mark_end.x, mark.y - 3), ImVec2(mark_end.x - 5, mark.y + 7), ImVec2(mark.x - 4, mark.y + 10)};
              const ImVec2 row2[] = {ImVec2(mark.x + 3, mark.y + 14), ImVec2(mark_end.x - 3, mark.y + 12), ImVec2(mark_end.x - 7, mark.y + 23), ImVec2(mark.x - 1, mark.y + 25)};
              draw->AddConvexPolyFilled(row1, 4, accent);
              draw->AddConvexPolyFilled(row2, 4, IM_COL32(48, 58, 72, 255));
            } else if (style == 3) {
              const ImVec2 center((a.x + b.x) * 0.5f, mark.y + 13.0f);
              draw->AddCircleFilled(center, 16.0f, IM_COL32(29, 43, 60, 255), 48);
              draw->AddCircle(center, 16.0f, IM_COL32(107, 131, 154, 220), 48, 1.3f);
              for (int i = 0; i < 7; ++i) {
                const float angle = -1.5707963f + 6.2831853f * i / 7.0f;
                draw->AddCircleFilled(ImVec2(center.x + std::cos(angle) * 10.0f,
                                             center.y + std::sin(angle) * 10.0f), 3.1f,
                                      i == 0 ? accent : IM_COL32(188, 204, 220, 255), 16);
              }
              draw->AddCircleFilled(center, 4.0f, IM_COL32(230, 240, 248, 255), 20);
            } else if (style == 4) {
              draw->AddRectFilled(mark, ImVec2(mark_end.x, mark.y + 7), IM_COL32(31, 48, 67, 255), 3.0f);
              draw->AddRectFilled(mark, ImVec2(mark.x + (mark_end.x - mark.x) / 4.0f, mark.y + 7), accent, 3.0f);
              draw->AddRectFilled(ImVec2(mark.x, mark.y + 12), mark_end, IM_COL32(25, 37, 53, 255), 3.0f);
              for (int i = 1; i < 7; ++i)
                draw->AddLine(ImVec2(mark.x + (mark_end.x - mark.x) * i / 7.0f, mark.y + 13),
                              ImVec2(mark.x + (mark_end.x - mark.x) * i / 7.0f, mark_end.y),
                              IM_COL32(90, 113, 136, 180), 1.0f);
            } else if (style == 5) {
              draw->AddRectFilled(mark, ImVec2(mark_end.x, mark_end.y), IM_COL32(23, 31, 41, 255));
              draw->AddRectFilled(mark, ImVec2(mark_end.x, mark.y + 7), IM_COL32(42, 57, 72, 255));
              draw->AddText(ImGui::GetFont(), 8.0f, ImVec2(mark.x + 5, mark.y + 1),
                            IM_COL32(239, 245, 252, 255), "VIDEO");
              for (int i = 0; i < 3; ++i)
                draw->AddRectFilled(ImVec2(mark.x + 4, mark.y + 11 + i * 5),
                                    ImVec2(mark_end.x - 4, mark.y + 14 + i * 5),
                                    i == 0 ? accent : IM_COL32(54, 67, 81, 255), 1.0f);
            } else {
              draw->AddRectFilled(mark, ImVec2(mark_end.x, mark_end.y), IM_COL32(192, 192, 192, 255));
              draw->AddRectFilled(mark, ImVec2(mark_end.x, mark.y + 7), IM_COL32(0, 0, 128, 255));
              draw->AddText(ImGui::GetFont(), 7.0f, ImVec2(mark.x + 4, mark.y + 1),
                            IM_COL32(255, 255, 255, 255), "Settings");
              draw->AddRectFilled(ImVec2(mark.x + 5, mark.y + 11),
                                  ImVec2(mark_end.x - 5, mark_end.y - 4),
                                  IM_COL32(226, 226, 226, 255));
              draw->AddRect(ImVec2(mark.x + 5, mark.y + 11),
                            ImVec2(mark_end.x - 5, mark_end.y - 4),
                            IM_COL32(90, 90, 90, 255), 0.0f, 0, 1.0f);
              draw->AddRectFilled(ImVec2(mark.x + 8, mark.y + 14),
                                  ImVec2(mark.x + 17, mark.y + 17), accent);
            }
            draw->AddText(ImVec2(a.x + 9, a.y + 51), IM_COL32(239, 245, 252, 255), style_names[style]);
            if (active || hovered)
              draw->AddRect(a, b, active ? accent : IM_COL32(226, 236, 247, 155), 8.0f, 0, active ? 2.3f : 1.3f);
            if (active) {
              const ImVec2 badge(b.x - 15.0f, a.y + 21.0f);
              draw->AddCircleFilled(ImVec2(badge.x + 1.0f, badge.y + 1.0f), 7.0f,
                                    IM_COL32(0, 4, 10, 150), 24);
              draw->AddCircleFilled(badge, 6.0f, accent, 24);
              draw->AddLine(ImVec2(badge.x - 2.5f, badge.y), ImVec2(badge.x - 0.5f, badge.y + 2.0f),
                            IM_COL32(255,255,255,255), 1.6f);
              draw->AddLine(ImVec2(badge.x - 0.5f, badge.y + 2.0f), ImVec2(badge.x + 3.0f, badge.y - 2.5f),
                            IM_COL32(255,255,255,255), 1.6f);
            }
            if (hovered) ImGui::SetTooltip("%s", appearance_hints[style]);
            if (pressed && !active) {
              options.overlay_style = style;
              state.clean_detail_open = false; state.clean_home_frames = 0;
              state.dashboard_detail_open = false; state.dashboard_home_frames = 0;
              state.gd_detail_open = false; state.gd_screen_frames = 0;
              state.radial_detail_open = false; state.wide_detail_open = false;
              state.content_anim_tab = -1;
              changed = true;
            }
            ImGui::PopID();
          }
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextUnformatted("Cosmetic mods");
        ImGui::TextWrapped("Imports use one shared native profile. Stage DATs that fail the exact-ISO "
                           "visual check use the clean disc resource online. Changes take effect after restart.");
        if (ImGui::Button("Import / Refresh...")) {
          std::string path = host::cosmetics::choose_import_file();
          if (!path.empty()) mod_message = host::cosmetics::import_file(path).message;
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Import a Nucleus project ZIP, costume DAT/ZIP, or stage DAT/ZIP.\n"
                            "Stage DATs use only their matching disc file. Unsafe stage changes stay offline.");
        ImGui::SameLine();
        if (ImGui::Button("Refresh catalog")) {
          std::string error;
          if (!host::cosmetics::refresh_catalog(&error)) mod_message = error;
          else mod_message = host::cosmetics::last_message();
        }
        bool use_mods = host::cosmetics::profile_enabled();
        ImGui::NewLine();
        if (settings_toggle("Use cosmetic profile", &use_mods)) {
          std::string error;
          if (!host::cosmetics::set_profile_enabled(use_mods, &error)) mod_message = error;
          else { mod_message = host::cosmetics::last_message(); changed = true; }
        }

        const auto installed_mods = host::cosmetics::assets();
        if (installed_mods.empty()) {
          ImGui::Spacing();
          settings_hint("No supported cosmetic assets imported.");
        } else {
          using CosmeticAsset = host::cosmetics::AssetInfo;
          std::map<std::string, std::map<std::string, std::vector<const CosmeticAsset*>>> characters;
          std::map<std::string, std::vector<const CosmeticAsset*>> stages;
          std::map<std::string, std::map<std::string, std::vector<const CosmeticAsset*>>> effects;
          for (const auto& asset : installed_mods) {
            if (asset.kind == "character_costume") characters[asset.character][asset.costume].push_back(&asset);
            else if (asset.kind == "stage_visual" && asset.available) stages[asset.costume].push_back(&asset);
            else if (asset.kind == "effect_visual") effects[asset.character][asset.costume].push_back(&asset);
          }
          static std::string rename_id;
          static std::array<char, 97> rename_text{};
          if (ImGui::CollapsingHeader("Characters")) {
            for (const auto& character : characters) {
              if (!ImGui::TreeNode(character.first.c_str())) continue;
              for (const auto& costume : character.second) {
                const auto& variants = costume.second;
                const std::string& target = variants.front()->target_path;
                ImGui::PushID(target.c_str());
                ImGui::TextUnformatted(costume.first.c_str());
                int current = 0;
                std::vector<std::string> option_storage{"Vanilla"};
                for (size_t i = 0; i < variants.size(); ++i) {
                  option_storage.push_back(variants[i]->name +
                      (variants[i]->available ? "" : " (unavailable)"));
                  if (variants[i]->selected && variants[i]->available) current = (int)i + 1;
                }
                std::vector<const char*> option_names;
                for (const auto& option : option_storage) option_names.push_back(option.c_str());
                int selected = current;
                ImGui::SetNextItemWidth(280.0f);
                if (settings_combo("##variant", &selected, option_names.data(), (int)option_names.size())) {
                  std::string error;
                  bool ok = selected == 0 ? host::cosmetics::disable_target(target, &error) :
                      host::cosmetics::select_variant(target, variants[(size_t)selected - 1]->id, &error);
                  if (!ok) mod_message = error;
                  else { mod_message = host::cosmetics::last_message(); changed = true; }
                }
                if (current != 0) {
                  ImGui::SameLine();
                  if (ImGui::SmallButton("Use Vanilla")) {
                    std::string error;
                    if (!host::cosmetics::disable_target(target, &error)) mod_message = error;
                    else { mod_message = host::cosmetics::last_message(); changed = true; }
                  }
                }
                if (variants.size() > 1)
                  ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
                                     "%zu variants; the saved selection wins.", variants.size());
                const CosmeticAsset* details = current > 0 ? variants[(size_t)current - 1] : variants.front();
                std::string details_label = "Details — " + details->name;
                if (ImGui::TreeNode(details_label.c_str())) {
                  draw_cosmetic_preview(*details);
                  ImGui::Text("Target: %s", details->target_path.c_str());
                  ImGui::TextWrapped("Content ID: %s", details->id.c_str());
                  ImGui::TextWrapped("SHA-256: %s", details->sha256.c_str());
                  if (!details->availability_message.empty())
                    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s",
                                       details->availability_message.c_str());
                  if (!details->dependencies.empty()) {
                    std::string deps;
                    for (const auto& dep : details->dependencies) deps += (deps.empty() ? "" : ", ") + dep;
                    ImGui::TextWrapped("Dependencies: %s", deps.c_str());
                  }
                  for (const auto& companion : details->unsupported_companions)
                    ImGui::TextWrapped("Companion: %s", companion.c_str());
                  if (ImGui::SmallButton("Rename...")) {
                    rename_id = details->id;
                    rename_text.fill(0);
                    std::copy_n(details->name.data(), std::min(details->name.size(), rename_text.size() - 1),
                                rename_text.data());
                    ImGui::OpenPopup("Rename variant");
                  }
                  if (ImGui::BeginPopupModal("Rename variant", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::InputText("Display name", rename_text.data(), rename_text.size());
                    if (ImGui::Button("Save", ImVec2(100, 0))) {
                      std::string error;
                      if (!host::cosmetics::rename_asset(rename_id, rename_text.data(), &error)) mod_message = error;
                      else { mod_message = host::cosmetics::last_message(); changed = true; ImGui::CloseCurrentPopup(); }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                  }
                  ImGui::TreePop();
                }
                ImGui::PopID();
              }
              ImGui::TreePop();
            }
          }
          auto draw_resource_slot = [&](const std::string& label,
                                        const std::vector<const CosmeticAsset*>& variants) {
            const std::string target = variants.front()->target_path +
                (variants.front()->scope.empty() ? "" : "#" + variants.front()->scope);
            ImGui::PushID(target.c_str());
            ImGui::TextUnformatted(label.c_str());
            int current = 0;
            std::vector<std::string> option_storage{"Vanilla"};
            for (size_t i = 0; i < variants.size(); ++i) {
              option_storage.push_back(variants[i]->name +
                  (variants[i]->available ? "" : " (unavailable)"));
              if (variants[i]->selected && variants[i]->available) current = (int)i + 1;
            }
            std::vector<const char*> option_names;
            for (const auto& option : option_storage) option_names.push_back(option.c_str());
            int selected = current;
            ImGui::SetNextItemWidth(280.0f);
            if (settings_combo("##variant", &selected, option_names.data(), (int)option_names.size())) {
              std::string error;
              bool ok = selected == 0 ? host::cosmetics::disable_target(target, &error) :
                  host::cosmetics::select_variant(target, variants[(size_t)selected - 1]->id, &error);
              if (!ok) mod_message = error;
              else { mod_message = host::cosmetics::last_message(); changed = true; }
            }
            const CosmeticAsset* details = current > 0 ? variants[(size_t)current - 1] : variants.front();
            draw_cosmetic_preview(*details);
            if (!details->availability_message.empty())
              ImGui::TextWrapped("%s", details->availability_message.c_str());
            ImGui::PopID();
          };
          if (!stages.empty() && ImGui::CollapsingHeader("Stages", ImGuiTreeNodeFlags_DefaultOpen))
            for (const auto& stage : stages) draw_resource_slot(stage.first, stage.second);
          if (!effects.empty() && ImGui::CollapsingHeader("Effects")) {
            if (ImGui::Button("Enable project effects")) {
              std::string error;
              if (!host::cosmetics::enable_project_effects(&error)) mod_message = error;
              else { mod_message = host::cosmetics::last_message(); changed = true; }
            }
            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("Validates each move against this clean ISO and combines compatible changes.\n"
                                "Conflicting changes are reported. Character and stage choices are preserved.");
            for (const auto& group : effects) {
              if (!ImGui::TreeNode(group.first.c_str())) continue;
              for (const auto& slot : group.second) draw_resource_slot(slot.first, slot.second);
              ImGui::TreePop();
            }
          }
        }
        ImGui::Separator();
        if (ImGui::Button("Restore Vanilla")) ImGui::OpenPopup("restore_vanilla_cosmetics");
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Disables every asset override. No backup is needed: the clean ISO is never modified.");
        if (ImGui::BeginPopupModal("restore_vanilla_cosmetics", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
          ImGui::TextWrapped("Disable all cosmetic overrides and clear every slot selection?\n"
                             "Imported files stay in the catalog so they can be selected again.");
          if (ImGui::Button("Restore Vanilla", ImVec2(150, 0))) {
            std::string error;
            if (!host::cosmetics::restore_vanilla(&error)) mod_message = error;
            else { mod_message = host::cosmetics::last_message(); changed = true; }
            ImGui::CloseCurrentPopup();
          }
          ImGui::SameLine();
          if (ImGui::Button("Cancel", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
          ImGui::EndPopup();
        }
        if (host::cosmetics::pending_restart()) {
          ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
                             "Profile changed. Restart to apply it and clear affected guest/render caches.");
          if (!state.fill_window) {
            if (ImGui::Button("Restart to apply mods")) state.confirm = SettingsState::Confirm::Restart;
          } else settings_hint("Close settings, then launch the game to apply this profile.");
        }
        if (mod_message.empty()) mod_message = host::cosmetics::last_message();
        if (!mod_message.empty()) ImGui::TextWrapped("%s", mod_message.c_str());
      }
      if (state.active_tab == 2) {

    // ---- Input ----
    ImGui::TextUnformatted("Input");
    if (settings_toggle("Background input", &host::g_background_input)) changed = true;
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("On: controllers keep playing while another window is in front\n"
                        "(a stream, Discord, a second monitor).\n"
                        "Off: the game ignores all input until you click back into its window.");

    // Slippi's Lagless FoD code is a real game patch, so expose it as an offline/direct setting
    // instead of silently forcing the performance-oriented variant on every player.
    ImGui::TextUnformatted("Stage effects");
    if (settings_toggle("Fountain of Dreams reflections", &options.fod_reflections)) changed = true;
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("On: keep Fountain of Dreams' water reflection and particles.\n"
                        "Off: use the Lagless FoD code for lower GPU cost. Takes effect at the next retrace.");

    // ---- L-cancel helpers ----
    // The indicator reads the fighter's action state and never writes anything, so it is display
    // only and safe in every mode. The automatic press is a real analog trigger press injected into
    // the local pad before the game reads it, so it is transmitted like any other input and both
    // clients compute the same landing lag: it cannot desync. It is still gated to offline and
    // Direct because it is a fairness question, not a safety one.
    ImGui::TextUnformatted("L-cancel");
    {
      bool indicator = lcancel::indicator_enabled();
      if (settings_toggle("Flash red on missed L-cancel", &indicator)) lcancel::set_indicator(indicator);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Flashes the fighter red when an aerial lands without the landing lag halved.\n"
                          "Display only: the tint is applied by the renderer and never written into the\n"
                          "game, so it is safe in every mode. It is skipped while auto L-cancel is doing\n"
                          "the press for you, since there is then nothing to report.");
      bool automatic = lcancel::automatic_enabled();
      if (settings_toggle("Auto L-cancel", &automatic)) lcancel::set_automatic(automatic);
      ImGui::SameLine();
      settings_hint("(NOTE: Will not work in Unranked or Ranked, only offline and direct)");
      if (automatic) {
        ImGui::TextWrapped("Presses the analog trigger for you during an aerial. It is a real input, sent over the "
                           "network like any other, so it cannot desync. In a Direct match both players should agree "
                           "to use it: it is a fairness question, not a safety one.");
        if (const char* mode = lcancel::auto_suppressed_mode())
          ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "Disabled right now: this is %s.", mode);
      }
    }

    // ---- HUD ----
    // A port code compiled into the game (recomp/gecko.py PORT_CODES): the stock row drawn at PAL's
    // size and height. Display only, read when the HUD is built, so it applies from the next match.
    ImGui::TextUnformatted("HUD");
    ImGui::SetNextItemWidth(170.0f);
    if (settings_slider("Stock icon size", &options.stock_hud_scale, 75, 175, "%d%%")) {
      gecko::option_pal_stock_icons = false;
      changed = true;
    }
    ImGui::SetNextItemWidth(170.0f);
    changed |= settings_slider("Damage number size", &options.damage_hud_scale, 75, 175, "%d%%");
    settings_hint("Display only; updates during play. Larger values may overlap in four-player matches.");
    if (settings_toggle("PAL style stock icons", &gecko::option_pal_stock_icons)) {
      if (gecko::option_pal_stock_icons) options.stock_hud_scale = 85;
      changed = true;
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Smaller stock icons, set a little higher, as in the PAL version.\n"
                        "Display only. Applies from the next match.");
    // Also a port code: zeroes the camera's shake offset before the game applies it.
    if (settings_toggle("Disable screen shake", &gecko::option_no_screen_shake)) changed = true;
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The camera no longer shakes on hard hits, explosions and stage effects.\n"
                        "Camera only: fighters and hits are unchanged. Takes effect immediately.");


    // ---- Online ----
    // Slippi's local input delay: frames of your own input held back so there is less to roll back.
    // The game asks for it when an online match is set up, so a change applies from the next match.
    ImGui::TextUnformatted("Online");
    {
      int delay = slippi::online::config().delay;
      ImGui::SetNextItemWidth(160.0f);
      if (settings_slider("Frame delay", &delay, 1, 9)) { slippi::online::config().delay = delay; changed = true; }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Frames of your own input held back before the game uses it, as in Slippi Dolphin.\n"
                          "Higher means fewer rollbacks on a bad connection and more input lag.\n"
                          "2 is Slippi's default. Takes effect from the next online match.");
      if (slippi::online::is_online_match()) {
        ImGui::SameLine();
        settings_hint("(applies from the next match)");
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
    settings_toggle("Discord presence (show what you are playing; friends can press Join)", &options.discord_presence);
    if (options.discord_presence != discord_was) {
      host::discord::configure(options.discord_app_id);
      host::discord::enable(options.discord_presence);   // starts or stops one background thread
    }
    if (options.discord_presence) {
      ImGui::TextWrapped("%s", host::discord::status().c_str());
      settings_hint("A new Application ID is picked up the next time you switch this off and on.");
      ImGui::TextWrapped("Discord Join sends the other player's code to Online > Direct; selecting Direct automatically uses it for that attempt. Your Slippi code is copied to the clipboard for the other player. Your IP address is never published. Who can see or join depends on your Discord activity privacy settings.");
    } else {
      settings_hint("Off. Nothing is sent to Discord. Needs an Application ID from discord.com/developers/applications.");
    }

    changed |= settings_toggle("Open this panel at startup", &options.settings_open);
      }
      if (state.active_tab == 3) {

    bool rumble_enabled = host::g_rumble_enabled.load(std::memory_order_relaxed);
    if (settings_toggle("Controller rumble", &rumble_enabled)) {
      host::g_rumble_enabled.store(rumble_enabled, std::memory_order_relaxed);
      if (!rumble_enabled) host::input_stop_all_rumble();
      changed = true;
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Turn off vibration for GameCube adapters and Xbox controllers, including online play.");
    ImGui::Spacing();

    host::InputDebugSnapshot snap;
    host::input_debug_snapshot(snap);

    // Device numbers used by the profile row and the saved settings: 0 keyboard, 1-4 Xbox, 5-8
    // PlayStation, 9-12 adapter, 13-16 Switch, 17-20 box/HID.
    struct Family { const char* name; int fam; int first_tab, count; const char* device; };
    static const Family kFamilies[] = {
      {"GameCube", (int)host::PadFamily::GameCube, 9, 4, "Adapter port"},
      {"Xbox", (int)host::PadFamily::Xbox, 1, 4, "Xbox"},
      {"PlayStation", (int)host::PadFamily::PlayStation, 5, 4, "PlayStation"},
      {"Switch", (int)host::PadFamily::Switch, 13, 4, "Switch"},
      {"B0XX / Frame1 / Box", (int)host::PadFamily::Box, 17, 4, "Box"},
      {"Keyboard", -1, 0, 1, "Keyboard"},
    };
    constexpr int kFamilyCount = (int)(sizeof kFamilies / sizeof kFamilies[0]);
    auto tab_connected = [&](int t) {
      if (t == 0) return true;
      if (t <= 4) return snap.xinput_connected[t - 1];
      if (t <= 8) return snap.ds4_connected[t - 5];
      if (t <= 12) return (snap.gc_mask & (1u << (t - 9))) != 0;
      if (t <= 16) return snap.swpro_connected[t - 13];
      return snap.hid_connected[t - 17];
    };
    auto family_connected = [&](const Family& f) {
      int n = 0;
      if (f.fam < 0) return 0;
      for (int i = 0; i < f.count; ++i) n += tab_connected(f.first_tab + i) ? 1 : 0;
      return n;
    };
    auto tab_of_source = [](const host::PortSource& s) {
      switch (s.kind) {
        case host::DeviceKind::Keyboard: return 0;
        case host::DeviceKind::XInputPad: return 1 + std::clamp(s.index, 0, 3);
        case host::DeviceKind::DS4Pad: return 5 + std::clamp(s.index, 0, 3);
        case host::DeviceKind::GCAdapter: return 9 + std::clamp(s.index, 0, 3);
        case host::DeviceKind::SwitchPro: return 13 + std::clamp(s.index, 0, 3);
        case host::DeviceKind::HidPad: return 17 + std::clamp(s.index, 0, 3);
        default: return -1;
      }
    };
    static int family_sel = -1;
    static int device_sel[kFamilyCount] = {};
    auto select_tab = [&](int t) {
      for (int f = 0; f < kFamilyCount; ++f)
        if (t >= kFamilies[f].first_tab && t < kFamilies[f].first_tab + kFamilies[f].count) { family_sel = f; device_sel[f] = t - kFamilies[f].first_tab; }
    };
    if (family_sel < 0) {   // open on whatever plays as port 1, else the first family with something connected
      family_sel = 0;
      for (int f = 0; f < kFamilyCount; ++f) if (family_connected(kFamilies[f]) > 0) { family_sel = f; break; }
      for (int f = 0; f < kFamilyCount; ++f)
        for (int i = 0; i < kFamilies[f].count; ++i) if (tab_connected(kFamilies[f].first_tab + i)) { device_sel[f] = i; break; }
      const int t = tab_of_source(host::g_port_sources[0]);
      if (t >= 0 && tab_connected(t)) select_tab(t);
      if (g_saved_edit_tab >= 0 && g_saved_edit_tab < kDeviceTabs) select_tab(g_saved_edit_tab);
      // Test hook for panel screenshots: MELEE_SETTINGS_EDIT=<device number> opens on that device.
      if (const char* e = std::getenv("MELEE_SETTINGS_EDIT")) select_tab(std::atoi(e));
    }
    auto light = [&](ImVec2 c, int count, bool show_count) {
      ImDrawList* d = ImGui::GetWindowDrawList();
      if (count > 0) {
        d->AddCircleFilled(c, 8, IM_COL32(60, 205, 95, 255), 20);
        if (show_count) {
          char n[8]; std::snprintf(n, sizeof n, "%d", count);
          const ImVec2 ts = ImGui::CalcTextSize(n);
          d->AddText(ImVec2(c.x - ts.x / 2, c.y - ts.y / 2), IM_COL32(10, 40, 15, 255), n);
        }
      } else {
        d->AddCircle(c, 7, IM_COL32(110, 110, 120, 255), 20, 1.5f);
      }
    };
    auto chip = [&](const char* text, bool selected, bool show_light, int connected, bool show_count, float width) {
      ImGui::PushStyleColor(ImGuiCol_Button, selected ? ImVec4(0.45f, 0.22f, 0.75f, 1.0f) : ImVec4(0.16f, 0.16f, 0.2f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selected ? ImVec4(0.52f, 0.28f, 0.82f, 1.0f) : ImVec4(0.24f, 0.24f, 0.3f, 1.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
      const std::string padded = std::string("  ") + text;
      const bool pressed = ImGui::Button(padded.c_str(), ImVec2(width, 28));
      ImGui::PopStyleVar(2);
      ImGui::PopStyleColor(2);
      if (show_light) {
        const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        light(ImVec2(mx.x - 15, (mn.y + mx.y) / 2), connected, show_count);
      }
      return pressed;
    };

    // A press on any controller (not the keyboard) selects it, so a player never has to work out
    // which adapter socket or pad number theirs is.
    {
      // A stick pushed well off centre counts as well as a button, so moving a stick to watch it
      // in the picture shows that controller, whichever socket or pad number it is on.
      static uint32_t last_live[kDeviceTabs] = {};
      static bool last_pushed[kDeviceTabs] = {};
      for (int t = 1; t < kDeviceTabs; ++t) {
        int idx; const host::CaptureDevice kind = tab_kind_of(t, &idx);
        const uint32_t now_live = tab_connected(t) ? live_actions(snap, kind, idx) : 0;
        const host::PadState& ps = t <= 4 ? snap.xinput_pad[t - 1] : t <= 8 ? snap.ds4_pad[t - 5] : t <= 12 ? snap.gc_pad[t - 9]
                                 : t <= 16 ? snap.swpro_pad[t - 13] : snap.hid_pad[t - 17];
        auto far_out = [](int8_t v) { return v > 50 || v < -50; };
        const bool pushed = tab_connected(t) && (far_out(ps.stick_x) || far_out(ps.stick_y) || far_out(ps.sub_x) || far_out(ps.sub_y));
        if (((now_live & ~last_live[t]) || (pushed && !last_pushed[t])) && state.rebind_action < 0) select_tab(t);
        last_live[t] = now_live;
        last_pushed[t] = pushed;
      }
    }
    auto port_of_tab = [&](int t) {
      for (int port = 0; port < 4; ++port) if (tab_of_source(host::g_port_sources[port]) == t) return port + 1;
      return 0;
    };
    // What a player calls each device: the kind of controller, then where it is plugged in.
    auto device_title = [&](int t) -> std::string {
      if (t < 0) return "Nobody";
      if (t == 0) return "Keyboard";
      if (t <= 4) return "Xbox controller";
      if (t <= 8) return "PlayStation controller";
      if (t <= 12) return "GameCube controller";
      if (t <= 16) return "Switch controller";
      return "B0XX / Frame1 / box";
    };
    auto device_where = [&](int t) -> std::string {
      char w[96];
      if (t <= 0) return t == 0 ? "" : "No controller";
      if (t <= 4) std::snprintf(w, sizeof w, "Pad %d", t);
      else if (t <= 8) std::snprintf(w, sizeof w, "Pad %d", t - 4);
      else if (t <= 12) std::snprintf(w, sizeof w, "Adapter socket %d", t - 8);
      else if (t <= 16) std::snprintf(w, sizeof w, "Pad %d", t - 12);
      else { const std::string n = host::hidpad_name(t - 17); if (n.empty()) std::snprintf(w, sizeof w, "Box %d", t - 16); else std::snprintf(w, sizeof w, "%s", n.c_str()); }
      return w;
    };
    auto device_short = [&](int t) -> std::string {
      if (t < 0) return "Nobody";
      if (t == 0) return "Keyboard";
      if (t <= 4) return "Xbox";
      if (t <= 8) return "PlayStation";
      if (t <= 12) return "GameCube";
      if (t <= 16) return "Switch";
      return "Box";
    };
    const int edit_tab = kFamilies[family_sel].first_tab + device_sel[family_sel];
    if (edit_tab != g_saved_edit_tab) { g_saved_edit_tab = edit_tab; changed = true; }   // kept in the settings file

    // ---- Players: the game's four ports. Each card says what plays as that port; clicking a card
    // edits that controller, and its arrow changes what plays there. ----
    {
      const float gap = ImGui::GetStyle().ItemSpacing.x;
      const int card_columns = ImGui::GetContentRegionAvail().x < 600.0f ? 2 : 4;
      const float card_w = (ImGui::GetContentRegionAvail().x - (card_columns - 1) * gap) / card_columns;
      constexpr float card_h = 70.0f;
      for (int port = 0; port < 4; ++port) {
        if (port % card_columns) ImGui::SameLine();
        ImGui::PushID(100 + port);
        int t = tab_of_source(host::g_port_sources[port]);
        // A port left on the keyboard is also played by the first spare pad (window.cpp); show that
        // pad, so someone with only an Xbox controller sees it on P1 without having to pick it.
        if (host::g_port_sources[port].kind == host::DeviceKind::Keyboard) {
          const int ft = tab_of_source(host::g_port_feeding[port]);
          if (ft > 0 && tab_connected(ft)) t = ft;
        }
        const bool connected = t >= 0 && tab_connected(t);
        const bool editing = t >= 0 && t == edit_tab;
        const ImVec2 o = ImGui::GetCursorScreenPos();
        ImGui::SetNextItemAllowOverlap();   // the arrow drawn on top of the card takes its own clicks
        if (settings_hit_button("card", ImVec2(card_w, card_h)) && t >= 0) select_tab(t);
        const bool hovered_card = ImGui::IsItemHovered();
        ImDrawList* d = ImGui::GetWindowDrawList();
        d->AddRectFilled(o, ImVec2(o.x + card_w, o.y + card_h), hovered_card ? IM_COL32(40, 40, 52, 255) : IM_COL32(30, 30, 40, 255), 8.0f);
        d->AddRect(o, ImVec2(o.x + card_w, o.y + card_h), editing ? IM_COL32(150, 90, 240, 255) : IM_COL32(60, 60, 74, 255), 8.0f, 0, editing ? 2.5f : 1.0f);
        // Port badge: green when its controller is plugged in, grey when not.
        const ImU32 col = connected ? IM_COL32(52, 176, 82, 255) : IM_COL32(80, 80, 90, 255);
        // Stacked so a narrow panel (the default size in game) still shows every word: the badge and
        // the arrow on the top row, then the kind of controller and where it is plugged in.
        d->AddRectFilled(ImVec2(o.x + 8, o.y + 8), ImVec2(o.x + 46, o.y + 30), col, 5.0f);
        char badge[4]; std::snprintf(badge, sizeof badge, "P%d", port + 1);
        const ImVec2 bs = ImGui::CalcTextSize(badge);
        d->AddText(ImVec2(o.x + 27 - bs.x / 2, o.y + 19 - bs.y / 2), IM_COL32(255, 255, 255, 255), badge);
        d->PushClipRect(ImVec2(o.x + 6, o.y), ImVec2(o.x + card_w - 6, o.y + card_h), true);
        d->AddText(ImVec2(o.x + 10, o.y + 34), connected ? IM_COL32(235, 235, 240, 255) : IM_COL32(140, 140, 150, 255), device_short(t).c_str());
        std::string where = t < 0 ? std::string("Empty") : device_where(t);
        if (where.rfind("Adapter socket", 0) == 0) where = "Socket" + where.substr(14);
        d->AddText(ImVec2(o.x + 10, o.y + 51), IM_COL32(140, 140, 155, 255), where.c_str());
        d->PopClipRect();
        // The arrow: what plays as this port.
        ImGui::SetCursorScreenPos(ImVec2(o.x + card_w - 30, o.y + 8));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(4.0f,3.0f));
        if (ImGui::ArrowButton("pick", ImGuiDir_Down)) ImGui::OpenPopup("port_source");
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Choose what plays as port %d.", port + 1);
        else if (hovered_card)
          ImGui::SetTooltip("%s%s%s\nClick to set up this controller's buttons.", device_title(t).c_str(), t > 0 ? ", " : "",
                            t > 0 ? (device_where(t) + (connected ? "" : " (unplugged)")).c_str() : "");
        if (ImGui::BeginPopup("port_source")) {
          settings_hint("Plays as port %d", port + 1);
          ImGui::Separator();
          const int cur = port_source_to_combo(host::g_port_sources[port]);
          // Plugged in first; the rest under "Not plugged in", so it can be set up ahead of time.
          auto source_item = [&](int c) {
            const host::PortSource src = combo_to_port_source(c);
            const int st = tab_of_source(src);
            std::string item = c == 0 ? std::string("Nobody") : st == 0 ? std::string("Keyboard") : device_title(st) + "  |  " + device_where(st);
            if (st > 0 && !tab_connected(st)) item += "  (not plugged in)";
            item += "##src" + std::to_string(c);   // unnamed boxes share a label
            if (ImGui::Selectable(item.c_str(), c == cur)) {
              host::g_port_sources[port] = src;
              host::g_port_device_names[port] = src.kind == host::DeviceKind::HidPad ? host::port_device_key(host::hidpad_name(src.index)) : std::string();
              changed = true;
              if (st >= 0) select_tab(st);
            }
          };
          for (int c = 0; c < kPortSourceCount; ++c) {
            const int st = tab_of_source(combo_to_port_source(c));
            if (c == 0 || st == 0 || tab_connected(st)) source_item(c);
          }
          if (ImGui::BeginMenu("Not plugged in")) {
            for (int c = 0; c < kPortSourceCount; ++c) {
              const int st = tab_of_source(combo_to_port_source(c));
              if (st > 0 && !tab_connected(st)) source_item(c);
            }
            ImGui::EndMenu();
          }
          ImGui::EndPopup();
        }
        ImGui::SetCursorScreenPos(o);   // the card as one item, so the next sits beside it
        ImGui::Dummy(ImVec2(card_w, card_h));
        ImGui::PopID();
      }
    }
    ImGui::Spacing();

    // ---- The controller being edited: normally picked by a card or a button press; this list
    // also reaches a plugged-in controller that is not playing as any port. ----
    {
      auto label_of = [&](int t) {
        std::string l = device_title(t);
        const std::string w = device_where(t);
        if (!w.empty()) l += "  |  " + w;
        if (const int p = port_of_tab(t)) l += "  (P" + std::to_string(p) + ")";
        return l;
      };
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted("Editing");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(std::max(100.0f, ImGui::GetContentRegionAvail().x - 40.0f));
      if (ImGui::BeginCombo("##editing", label_of(edit_tab).c_str())) {
        static const int kOrder[] = {9, 10, 11, 12, 1, 2, 3, 4, 5, 6, 7, 8, 13, 14, 15, 16, 17, 18, 19, 20, 0};
        for (int t : kOrder)
          if (t == 0 || tab_connected(t))
            if (ImGui::Selectable((label_of(t) + "##dev" + std::to_string(t)).c_str(), t == edit_tab)) select_tab(t);
        // Every other controller, to set one up before plugging it in.
        if (ImGui::BeginMenu("Not plugged in")) {
          for (int t : kOrder)
            if (t != 0 && !tab_connected(t))
              if (ImGui::Selectable((label_of(t) + "  (not plugged in)##dev" + std::to_string(t)).c_str(), t == edit_tab)) select_tab(t);
          ImGui::EndMenu();
        }
        ImGui::EndCombo();
      }
      ImGui::SameLine();
      {
        const ImVec2 c = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetFrameHeight();
        light(ImVec2(c.x + 9, c.y + h / 2), tab_connected(edit_tab) ? 1 : 0, false);
        ImGui::Dummy(ImVec2(20, h));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(tab_connected(edit_tab) ? "Plugged in." : "Not plugged in.");
      }
      if (room_after_last_item() > ImGui::CalcTextSize("Press any button on a controller to jump to it.").x + 16) {
        ImGui::SameLine();
        settings_hint("Press any button on a controller to jump to it.");
      }
    }
    const Family& cur_family = kFamilies[family_sel];
    ImGui::Spacing();
    if (cur_family.fam == (int)host::PadFamily::GameCube) {
      const double rate = host::gcadapter_poll_rate_hz();
      if (rate > 0.0) ImGui::Text("Adapter polling rate: %.0f Hz", rate);
      else settings_hint("Adapter polling rate: --");
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Incoming USB reports per second, shared by all four adapter sockets.\nThe game reads controller state once per frame.");
      ImGui::Spacing();
    }

    {
      const int tab = cur_family.first_tab + device_sel[family_sel];
      {
        host::CaptureDevice tab_kind;
        int tab_index = 0;
        if (tab == 0) { tab_kind = host::CaptureDevice::Keyboard; }
        else if (tab <= 4) { tab_kind = host::CaptureDevice::XInputPad; tab_index = tab - 1; }
        else if (tab <= 8) { tab_kind = host::CaptureDevice::DS4Pad; tab_index = tab - 5; }
        else if (tab <= 12) { tab_kind = host::CaptureDevice::GCAdapter; tab_index = tab - 9; }
        else if (tab <= 16) { tab_kind = host::CaptureDevice::SwitchPro; tab_index = tab - 13; }
        else { tab_kind = host::CaptureDevice::HidPad; tab_index = tab - 17; }

        // A rebind in progress for this device: wait for its next press.
        const bool capturing_here = state.rebind_action >= 0 && state.rebind_kind == tab_kind && state.rebind_index == tab_index;
        if (capturing_here) {
          host::CaptureDevice dev; int value; int device_index;
          if (host::input_poll_capture(dev, value, device_index)) {
            if (dev == host::CaptureDevice::None) {
              state.rebind_action = -1;   // Escape cancelled; binding unchanged.
            } else if (dev == tab_kind) {
              // Whichever controller of this kind answered is the one being set up: the player
              // need not know its adapter socket or pad number.
              const int idx = tab_kind == host::CaptureDevice::Keyboard ? 0 : device_index;
              binding_set(tab_kind, idx, state.rebind_action, (uint32_t)value);
              state.rebind_action = -1;
              changed = true;
              if (idx != tab_index) select_tab(tab - tab_index + idx);
            } else {
              host::input_begin_capture(tab_kind, tab_index);
            }
          }
        }
        auto begin_rebind = [&](int action) {
          host::input_begin_capture(tab_kind, tab_index);
          state.rebind_action = action; state.rebind_kind = tab_kind; state.rebind_index = tab_index;
        };
        auto unbind = [&](int action) { binding_set(tab_kind, tab_index, action, 0); changed = true; };

        host::profiles_set_folder(options.settings_path);
        changed |= draw_profile_row(tab_kind, tab_index, tab);
        ImGui::Spacing();

        // The picture: one C-stick box, as Smash Ultimate has it.
        // Boxes and pads send their C-stick as a stick (a box computes it, modifier angles included,
        // in its own firmware), so only the keyboard binds the four directions; two keys held
        // together give the diagonals. A pad that really sends C directions as buttons can still
        // bind them in the list below.
        const bool analog_c = tab_kind != host::CaptureDevice::Keyboard;
        const int waiting = state.rebind_kind == tab_kind && state.rebind_index == tab_index ? state.rebind_action : -1;
        const uint32_t live = live_actions(snap, tab_kind, tab_index);
        int right_clicked = -1, hovered = -1;
        ImVec2 stick_pos(0, 0), c_pos(0, 0);
        {
          const host::PadState& ps = tab == 0 ? snap.keyboard_pad : tab <= 4 ? snap.xinput_pad[tab - 1] : tab <= 8 ? snap.ds4_pad[tab - 5]
                                   : tab <= 12 ? snap.gc_pad[tab - 9] : tab <= 16 ? snap.swpro_pad[tab - 13] : snap.hid_pad[tab - 17];
          stick_pos = ImVec2(std::clamp(ps.stick_x / 80.0f, -1.0f, 1.0f), std::clamp(ps.stick_y / 80.0f, -1.0f, 1.0f));
          c_pos = ImVec2(std::clamp(ps.sub_x / 80.0f, -1.0f, 1.0f), std::clamp(ps.sub_y / 80.0f, -1.0f, 1.0f));
        }
        const bool switch_picture = tab_kind == host::CaptureDevice::SwitchPro && !g_swpro_gc_picture;
        // This family's deadzones, over 80 like the stick positions (the keyboard has none).
        const float dz_main = cur_family.fam >= 0 ? host::g_deadzones[(size_t)cur_family.fam].main / 80.0f : 0.0f;
        const float dz_c = cur_family.fam >= 0 ? host::g_deadzones[(size_t)cur_family.fam].c / 80.0f : 0.0f;
        if (tab_kind == host::CaptureDevice::SwitchPro) {
          if (settings_toggle("Use GameCube controller picture", &g_swpro_gc_picture)) changed = true;
          if (ImGui::IsItemHovered()) ImGui::SetTooltip("Set up this Switch controller on the GameCube layout instead:\nclick a GameCube button, then press the Switch button for it.");
        }
        if (switch_picture) {
          changed |= draw_swpro_bind_picture(tab_index, snap.swpro_buttons[tab_index], stick_pos, c_pos, dz_main, dz_c);
          if (tab_connected(tab)) settings_hint("Click a button or its box to choose what it does. Right-click to clear.");
        }
        const int clicked = switch_picture ? -1 : draw_gc_bind_picture(live, waiting, &right_clicked, &hovered,
                                                 [&](int action) { return binding_label(tab_kind, tab_index, action); },
                                                 analog_c, "Smash Attack", stick_pos, c_pos, dz_main, dz_c);
        if (clicked >= 0 && clicked != kCStickModeBox) begin_rebind(clicked);
        if (right_clicked >= 0) unbind(right_clicked);
        if (hovered == kCStickModeBox)
          ImGui::SetTooltip("C-Stick: smash attacks.");
        else if (hovered >= 0)
          ImGui::SetTooltip("%s: %s\nClick to rebind, right-click to clear.", kActionTitles[hovered], binding_label(tab_kind, tab_index, hovered).c_str());
        if (waiting >= 0)
          ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Press a key or button for %s (Esc to cancel)", kActionTitles[waiting]);
        else if (!tab_connected(tab))
          settings_hint("Not connected. Plug it in to rebind it; its saved buttons are shown.");
        else if (!switch_picture)
          settings_hint("Click a button or its box to rebind it. Right-click to clear.");

        // Any change to this controller's buttons (a rebind, a clear, a box layout, a loaded
        // profile) is kept in its profile straight away.
        {
          static host::ProfileBindings last_seen[kDeviceTabs];
          static bool seen[kDeviceTabs] = {};
          host::profiles_set_folder(options.settings_path);
          const host::ProfileBindings now_b = bindings_of(tab);
          if (seen[tab] && now_b != last_seen[tab]) autosave_profile(tab);
          last_seen[tab] = now_b;
          seen[tab] = true;
        }

        // ---- the rest, folded away ----
        if (cur_family.fam >= 0) {
          ImGui::Spacing();
          ImGui::SeparatorText("Sticks");
          host::Deadzone& dz = host::g_deadzones[(size_t)cur_family.fam];
          ImGui::SetNextItemWidth(200.0f);
          changed |= settings_slider("Control stick deadzone", &dz.main, 0, 60, dz.main ? "%d" : "Off");
          if (ImGui::IsItemActive()) g_show_dz_main_until = ImGui::GetTime() + 1.5;
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Inside this distance from centre the stick reads as centred; outside it the stick\n"
                              "is passed through exactly as the controller sends it (out of 127). Melee already\n"
                              "ignores about 23, so only a worn stick drifting past that needs it.");
          ImGui::SetNextItemWidth(200.0f);
          changed |= settings_slider("C-stick deadzone", &dz.c, 0, 60, dz.c ? "%d" : "Off");
          if (ImGui::IsItemActive()) g_show_dz_c_until = ImGui::GetTime() + 1.5;
          settings_hint("Applies to every %s controller.", cur_family.name);
        }
        if (tab_kind == host::CaptureDevice::HidPad && ImGui::CollapsingHeader("Box layouts")) {
          if (ImGui::Button("B0XX (vJoy / b0xx-ahk)")) { host::g_hid_bindings[tab_index] = host::vjoy_b0xx_bindings(); changed = true; }
          ImGui::SameLine();
          if (ImGui::Button("B0XX-layout box (HayBox)")) { host::g_hid_bindings[tab_index] = host::haybox_dinput_bindings(); changed = true; }
          ImGui::SameLine();
          if (ImGui::Button("Generic")) { host::g_hid_bindings[tab_index] = host::default_hid_bindings()[0]; changed = true; }
          settings_hint("vJoy and HayBox boxes in DInput mode get their layout automatically.");
          const host::HidPadAxes axes = host::hidpad_axes(tab_index);
          if (axes.count) {
            std::string line = "Axes:";
            for (int a = 0; a < axes.count; ++a) line += "  " + std::string(axes.name[a]) + " " + std::to_string(axes.value[a]);
            settings_hint("%s", line.c_str());
          }
        }
        if (ImGui::CollapsingHeader("All buttons as a list")) {
          if (ImGui::BeginTable("bindings", 2, ImGuiTableFlags_SizingStretchSame)) {
            for (int i = 0; i < (int)host::BindAction::Count; ++i) {
              if (analog_c && host::is_cstick_action(i) && tab_kind != host::CaptureDevice::HidPad) continue;
              ImGui::TableNextColumn();
              ImGui::PushID(i);
              const bool pressed_now = (live >> i) & 1;
              char row[64];
              std::snprintf(row, sizeof row, "%-12s %s", kActionTitles[i], i == waiting ? "(press...)" : binding_label(tab_kind, tab_index, i).c_str());
              if (pressed_now) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.95f, 0.55f, 1.0f));
              if (ImGui::Selectable(row, i == waiting || i == hovered, ImGuiSelectableFlags_None, ImVec2(ImGui::GetContentRegionAvail().x - 58.0f, 0))) begin_rebind(i);
              if (pressed_now) ImGui::PopStyleColor();
              if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) unbind(i);
              ImGui::SameLine();
              if (ImGui::SmallButton("Clear")) unbind(i);
              ImGui::PopID();
            }
            ImGui::EndTable();
          }
        }
      }
    }
      }
      if (state.active_tab == 4) {
    changed |= settings_toggle("Show player nicknames above fighters", &options.show_player_nicknames);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shows each online player's name at Melee's player-tag anchor. Local display only.");
    changed |= settings_toggle("Show the \"Settings: F1\" reminder", &options.settings_hint);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("F1, or Start + D-pad Down + Z on a GameCube controller, opens settings.");
    changed |= settings_toggle("Show the \"Matchmaking: Tab\" reminder", &options.matchmaking_hint);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tab still opens matchmaking with it off.");
    settings_toggle("Performance overlay", &options.performance_overlay);
    changed |= settings_toggle("FPS counter (top left)", &options.show_fps);
    // VRAM has its own line in the Video tab, right by the settings that move it; no overlay needed.
    changed |= settings_toggle("Ping while online (under the FPS)", &options.show_ping);
    changed |= settings_toggle("Render latency (under the FPS)", &options.reflex_stats);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Shows the measured render latency under the FPS counter. Works whether or\n"
                        "not NVIDIA Reflex Low Latency (Video tab) is On, so Off has a number too --\n"
                        "the full breakdown by stage is on the performance graph.");
    changed |= settings_toggle("Controller overlay", &options.input_overlay);
    if (options.input_overlay) {
      ImGui::SameLine();
      changed |= settings_toggle("Show values", &options.input_overlay_values);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Each stick's position as the game reads it. A full press is 1.0000.");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(110.0f);
      changed |= settings_slider("Stick size", &options.input_overlay_stick, 1, 10);
      // Several ports can be shown at once (doubles and crew streams want every player visible);
      // they stack upward from the bottom left corner.
      for (int i = 0; i < 4; ++i) {
        ImGui::SameLine();
        char label[16];
        std::snprintf(label, sizeof label, "P%d", i + 1);
        bool on = (options.input_overlay_ports & (1 << i)) != 0;
        if (settings_toggle(label, &on)) {
          options.input_overlay_ports = on ? (options.input_overlay_ports | (1 << i)) : (options.input_overlay_ports & ~(1 << i));
          changed = true;
        }
      }
      ImGui::SameLine();
      changed |= settings_toggle("Hide background", &options.input_overlay_hide_border);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Removes the panel behind the overlay, leaving only the buttons and sticks.");
      settings_hint("  Drag an overlay to move it, and its edges to resize, while this panel is open.");
    }
      }
      if (state.active_tab == 6) {
    // ---- Gecko codes (the player's own, from GeckoCodes.ini beside the settings file) ----
    // Always shown: a code the other player does not have desyncs the match.
    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "WARNING: Gecko codes can cause DESYNCS online.");
    ImGui::TextWrapped("Codes change the game itself. Online, both players need exactly the same codes switched on, "
                       "or the match falls out of sync. Switch codes off before playing online unless your opponent has them too.");
    ImGui::Separator();
    ImGui::TextUnformatted("Your codes");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Your own codes, in Dolphin's format, from:\n%s\n"
                        "Codes that write game data work. Codes that patch the game's code (C2 and\n"
                        "writes into the code) cannot run in this build and are shown greyed out.",
                        user_gecko::path().c_str());
    if (user_gecko::codes().empty()) {
      settings_hint("No codes yet. Paste one below, or put a GeckoCodes.ini next to port-settings.ini.");
    } else {
      std::string to_remove;
      for (user_gecko::Code& c : user_gecko::codes()) {
        ImGui::PushID(&c);
        if (ImGui::SmallButton("-")) to_remove = c.name;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove this code.");
        ImGui::SameLine();
        ImGui::BeginDisabled(!c.supported);
        if (settings_toggle(c.name.c_str(), &c.enabled)) { changed = true; g_gecko_chosen = true; }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
          std::string tip;
          for (const std::string& n : c.notes) tip += n + "\n";
          if (!c.supported) tip += "Cannot run here: this code " + c.reason + ".";
          else tip += "Desyncs online unless your opponent runs it too.";
          ImGui::SetTooltip("%s", tip.c_str());
        }
        ImGui::PopID();
      }
      if (!to_remove.empty()) { user_gecko::remove(to_remove); user_gecko::save(); changed = true; }
    }
    ImGui::Separator();
    // Paste in a code without needing to find and edit GeckoCodes.ini by hand. A pasted block may
    // carry its own "$Name" line (Dolphin's format, what most sites hand out); if it does, that name
    // is used and the typed one below is just what is offered until then.
    {
      static char add_name[64] = "";
      static char add_body[2048] = "";
      static std::string add_error;
      if (ImGui::Button("+ Add Gecko code")) { add_name[0] = 0; add_body[0] = 0; add_error.clear(); ImGui::OpenPopup("add_gecko_code"); }
      if (ImGui::BeginPopup("add_gecko_code")) {
        ImGui::TextUnformatted("Name");
        ImGui::SetNextItemWidth(300.0f);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputText("##gecko_name", add_name, sizeof add_name);
        ImGui::TextUnformatted("Code (XXXXXXXX YYYYYYYY, one pair per line -- paste the whole thing, name line and all, and it wins)");
        ImGui::InputTextMultiline("##gecko_body", add_body, sizeof add_body, ImVec2(400.0f, 140.0f));
        if (!add_error.empty()) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", add_error.c_str());
        if (ImGui::Button("Add")) {
          add_error = user_gecko::add(add_name, add_body);
          if (add_error.empty()) { user_gecko::save(); changed = true; ImGui::CloseCurrentPopup(); }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
      }
    }
    }
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    if (clean_detail_theme || dashboard_detail_theme ||
        (options.overlay_style >= 2 && options.overlay_style <= 4))
      ImGui::PopStyleColor();
    const ImVec2 content_size = ImGui::GetItemRectSize();
    // Keep the footer anchored while the page itself slides in. BeginChild advances the parent
    // cursor by the displaced child position, so restore its undisturbed layout position here.
    if (gd_page)
      ImGui::SetCursorPos(ImVec2(44.0f * gd_scale, 396.0f * gd_scale));
    else
      ImGui::SetCursorPos(ImVec2(content_origin.x, content_origin.y + content_size.y));
    ImGui::PopStyleVar();

    if (!state.fill_window) settings_page_footer(state,options.overlay_style);
    else ImGui::Separator();
    // Every change is saved on its own, once the control that changed it is released (a slider
    // being dragged writes once, at the end, not sixty times a second). The button stays for
    // anyone who wants to be sure, and for the few controls that do not report a change.
    if (changed) state.dirty = true;
    const bool autosave = state.dirty && !ImGui::IsAnyItemActive();
    if ((state.fill_window && ImGui::Button("Save settings")) || autosave) {
      state.dirty = false;
      std::filesystem::path path(options.settings_path), temporary = path; temporary += ".tmp";
      std::ofstream file(temporary);
      file << "fps " << options.fps_cap << "\nscale " << options.efb_scale << "\nfullscreen " << options.fullscreen
           << "\nexclusivefullscreen " << options.exclusive_fullscreen
           << "\nvsync " << options.vsync << "\nwidescreen " << options.widescreen
           << "\nfodreflections " << options.fod_reflections
           << "\ntruewidescreen " << options.true_widescreen << "\naspect " << (int)options.aspect
           << "\nwindow " << (options.window_pinned ? std::to_string(options.window_w) + "x" + std::to_string(options.window_h) : std::string("follow"))
           << "\nvolume " << g_volume << "\nperformance " << options.performance_overlay
           << "\nshowfps " << options.show_fps << "\nshowvram " << (options.show_vram ? 1 : 0) << "\nshowping " << options.show_ping
           << "\ndlss " << options.dlss_mode << "\nframegen " << options.frame_generation_mode
           << "\nreflex " << options.reflex_mode << "\nreflexstats " << (options.reflex_stats ? 1 : 0)
           << "\nreflexflash " << (options.reflex_flash ? 1 : 0)
           << "\npathtracing " << (options.path_tracing ? 1 : 0)
           << "\nrayreconstruction " << (options.ray_reconstruction ? 1 : 0)
#ifdef GX_DLSS5
           << "\ndlss5 " << (options.dlss5 ? 1 : 0) << "\ndlss5intensity " << options.dlss5_tuning.intensity
           << "\ndlss5detail " << options.dlss5_tuning.detail << "\ndlss5tone " << options.dlss5_tuning.tone
           << "\ndlss5skin " << options.dlss5_tuning.skin << "\ndlss5style " << options.dlss5_tuning.style
           << "\ndlss5preset " << options.dlss5_tuning.preset << "\ndlss5automask " << (options.dlss5_tuning.auto_mask ? 1 : 0)
#endif
           << "\nbackend " << (options.api == RenderApi::D3D11 ? "d3d11" : "d3d12")
           << "\nsharpness " << options.sharpness << "\nssao " << options.screen_space_ao << "\nbrightness " << options.brightness
           << "\ncontrast " << options.contrast << "\nvibrance " << options.vibrance
           << "\nanisotropy " << options.anisotropy << "\nssaa " << options.ssaa
           << "\nsubframe " << (options.subframe == SubFrameMode::Off ? 0 : options.subframe == SubFrameMode::AuthoredInterpolate ? 2 : 1) << "\nmusic " << slippi::jukebox::user_volume()
           << "\nonlinedelay " << slippi::online::config().delay
           << "\nstartup " << (options.settings_open ? 1 : 0)
           // Read since it was added and never written, so hiding the reminder lasted one session.
           << "\nsettingsreminder " << (options.settings_hint ? 1 : 0)
           << "\nlegacymenu " << (options.legacy_menu_enabled ? 1 : 0)
           << "\nlegacymenustyle " << options.legacy_menu_style
           << "\nmenusounds " << (options.settings_menu_sounds ? 1 : 0)
           << "\nmenucustom " << (options.settings_custom_color_enabled ? 1 : 0)
           << "\nmenucolor " << options.settings_custom_color[0] << ' '
           << options.settings_custom_color[1] << ' ' << options.settings_custom_color[2]
           << "\noverlaystyle " << (state.legacy_presentation ? state.legacy_saved_appearance : options.overlay_style)
           << "\nsettingstransparency " << options.settings_transparency
           << "\noverlaypalette0 " << options.overlay_palettes[0]
           << "\noverlaypalette1 " << options.overlay_palettes[1]
           << "\noverlaypalette2 " << options.overlay_palettes[2]
           << "\noverlaypalette3 " << options.overlay_palettes[3]
           << "\noverlaypalette4 " << options.overlay_palettes[4]
           << "\noverlaypalette5 " << options.overlay_palettes[5]
           << "\noverlaypalette6 " << options.overlay_palettes[6]
           << "\nstockhudscale " << options.stock_hud_scale
           << "\ndamagehudscale " << options.damage_hud_scale
           << "\nplayernicknames " << (options.show_player_nicknames ? 1 : 0)
           << "\nmatchmakinghint " << (options.matchmaking_hint ? 1 : 0)
           << "\ninputoverlay " << options.input_overlay << "\ninputoverlayports " << options.input_overlay_ports
           << "\ninputoverlayhideborder " << options.input_overlay_hide_border
           << "\ninputoverlayvalues " << options.input_overlay_values
           << "\ninputoverlaystick " << options.input_overlay_stick
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
           << "\npalstockicons " << (gecko::option_pal_stock_icons ? 1 : 0)
           << "\nnoscreenshake " << (gecko::option_no_screen_shake ? 1 : 0)
           << "\nswpro_gc_picture " << (g_swpro_gc_picture ? 1 : 0)
           << family_options_text()
           << "\nrumble " << (host::g_rumble_enabled.load(std::memory_order_relaxed) ? 1 : 0)
           << "\nbackgroundinput " << (host::g_background_input ? 1 : 0)
           << "\neditdevice " << g_saved_edit_tab
           << (g_custom_preset.set ? "\ncustompreset " + std::to_string(g_custom_preset.efb) + " " + std::to_string(g_custom_preset.ssaa) + " " +
                                         std::to_string(g_custom_preset.aniso) + " " + std::to_string(g_custom_preset.dlss) + " " +
                                         std::to_string(g_custom_preset.fps) + " " + std::to_string(g_custom_preset.sub)
                                   : std::string())
           << "\ndiscord " << (options.discord_presence ? 1 : 0);
      // Only when set: "key value" parsing would swallow the next line on an empty value.
      if (!options.discord_app_id.empty()) file << "\ndiscord_app_id " << options.discord_app_id;
      // One line per pack that is switched off. Without this the loader parsed "texpackoff" but
      // nothing ever wrote it, so switching a pack off lasted only until the next launch. The
      // loader reads the name to end of line, so a name with spaces in it round-trips.
      // The texture settings themselves. The loader has always parsed these three, but nothing ever
      // wrote them, so "Use texture packs" came back unchecked at every launch and a player who had
      // set it up correctly was told their packs were off. Same omission that hid texpackoff.
      file << "\ncustomtextures " << (options.custom_textures ? 1 : 0)
           << "\ndumptextures " << (options.dump_textures ? 1 : 0)
           << "\nprefetchtextures " << (options.prefetch_textures ? 1 : 0)
           << "\nvideobackgrounds " << (options.video_backgrounds ? 1 : 0);
      file << texpack_disabled_lines();
      // The player's Gecko codes switched on, by name (names can hold spaces; read to end of line).
      if (g_gecko_chosen) {
        file << "\ngeckochosen 1";
        for (const user_gecko::Code& c : user_gecko::codes())
          if (c.enabled) file << "\ngeckocode " << c.name;
      }
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
      for (int idx = 0; idx < 4; ++idx)
        for (int i = 0; i < (int)host::BindAction::Count; ++i)
          file << "\nhid" << idx << "_" << kActionNames[i] << " " << host::g_hid_bindings[idx].mask[i];
      for (int n = 0; n < 4; ++n)
        file << "\nport" << n << " " << port_source_to_combo(host::g_port_sources[n]);
      for (int n = 0; n < 4; ++n)
        if (!host::g_port_device_names[n].empty()) file << "\nportname" << n << " " << host::g_port_device_names[n];
      // Only while a named profile (not "Default") is actually active: g_active_profile also holds a
      // pre-allocated name while Default is selected (so an unprompted rebind has somewhere to save
      // to), and writing that out would read back next launch as if that profile were chosen.
      for (int t = 0; t < kDeviceTabs; ++t)
        if (g_named_profile_active[t] && !g_active_profile[t].empty()) file << "\nactiveprofile" << t << " " << g_active_profile[t];
      file << '\n';
      file.close();
      state.saved = file.good() && MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
    // Standalone: there is no game behind this window, so "return", "restart" and "quit the game"
    // are all the same thing, closing it. Restart in particular relaunched this process with the
    // command line it was started with, which is --settings-window, so it reopened the settings.
    if (state.fill_window) {
      ImGui::SameLine();
      // The window itself has to be told: state.open is the panel's own flag and the standalone
      // window loop cannot see it, so pressing Close left the window sitting there open.
      if (ImGui::Button("Close")) { state.open = false; g_close_requested.store(true, std::memory_order_relaxed); }
    }
    if (state.saved && ImGui::IsItemHovered()) ImGui::SetTooltip("Settings saved");

    if (state.confirm != SettingsState::Confirm::None) {
      const bool restart = state.confirm == SettingsState::Confirm::Restart;
      ImGui::OpenPopup(restart ? "Restart game?" : "Quit game?");
      const ImVec2 screen = ImGui::GetIO().DisplaySize;
      ImGui::SetNextWindowPos(ImVec2(screen.x * 0.5f, screen.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      if (ImGui::BeginPopupModal(restart ? "Restart game?" : "Quit game?", nullptr,
                                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextUnformatted(restart ? "Restart now? The current match will end."
                                       : "Quit now? The current match will end.");
        if (!state.saved) ImGui::TextDisabled("Unsaved changes in this panel will be lost.");
        ImGui::Separator();
        if (ImGui::Button(restart ? "Restart" : "Quit", ImVec2(110, 0))) {
          state.confirm = SettingsState::Confirm::None;
          ImGui::CloseCurrentPopup();
          if (restart) host::request_restart(); else host::request_exit(0);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110, 0))) {
          state.confirm = SettingsState::Confirm::None;
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }
    }
    ImGui::EndDisabled();
    if (gd_detail_theme) {
      ImGui::PopStyleVar();
      ImGui::PopStyleColor(4);
    }
    if (clean_detail_theme) {
      ImGui::PopStyleVar();
      ImGui::PopStyleColor(4);
    }
    if (dashboard_detail_theme) {
      ImGui::PopStyleVar();
      ImGui::PopStyleColor(4);
    }
    }
    ImGui::End();
    if (!old_menu && classic_menu) {
      if (g_settings_classic_font) ImGui::PopFont();
      ImGui::PopStyleVar(3);
      ImGui::PopStyleColor(10);
    }
  }
  // ---- the Esc menu ----
  if (state.menu_open && !state.open) {
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const float width = std::min(760.0f, screen.x - 24.0f);
    const float height = 172.0f;
    ImGui::SetNextWindowPos(ImVec2((screen.x - width) * 0.5f, screen.y - height - 18.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("##esc_menu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                         ImGuiWindowFlags_NoBackground);
    const ImVec2 p = ImGui::GetWindowPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    g_settings_palette = options.overlay_palettes[0];
    g_settings_custom_color_enabled = options.settings_custom_color_enabled;
    g_settings_custom_color = ImVec4(options.settings_custom_color[0], options.settings_custom_color[1],
                                     options.settings_custom_color[2], 1.0f);
    const ImU32 accent = settings_accent(0);
    const ImVec4 accent_f = ImGui::ColorConvertU32ToFloat4(accent);
    const ImU32 accent_dark = ImGui::ColorConvertFloat4ToU32(
        ImVec4(accent_f.x * .48f, accent_f.y * .48f, accent_f.z * .48f, 1.0f));
    draw->AddRectFilled(p, ImVec2(p.x + width, p.y + height), IM_COL32(8, 10, 20, 226), 8.0f);
    draw->AddRectFilled(p, ImVec2(p.x + width, p.y + 4), accent);
    draw->AddText(ImVec2(p.x + 20, p.y + 17), IM_COL32(250, 244, 248, 255),
                  state.menu_quit ? "QUIT MELEE UNLOCKED?" : "MELEE UNLOCKED");
    if (state.menu_quit)
      draw->AddText(ImVec2(p.x + 20, p.y + 41), IM_COL32(183, 174, 190, 255), "The current match will end.");
    const float gap = 10.0f;
    const int count = state.menu_quit ? 2 : 3;
    const float button_w = (width - 40.0f - gap * (count - 1)) / count;
    auto action = [&](const char* id, const char* label, int index) {
      const float x = 20.0f + index * (button_w + gap), y = 66.0f;
      ImGui::SetCursorPos(ImVec2(x, y));
      const bool pressed = ImGui::InvisibleButton(id, ImVec2(button_w, 58.0f), ImGuiButtonFlags_EnableNav);
      const bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
      const ImVec2 q(p.x + x, p.y + y - (hot ? 4.0f : 0.0f));
      const ImVec2 shadow[] = {ImVec2(q.x + 9, q.y + 7), ImVec2(q.x + button_w, q.y + 7),
                               ImVec2(q.x + button_w - 9, q.y + 62), ImVec2(q.x, q.y + 62)};
      const ImVec2 face[] = {ImVec2(q.x + 9, q.y), ImVec2(q.x + button_w, q.y),
                             ImVec2(q.x + button_w - 9, q.y + 54), ImVec2(q.x, q.y + 54)};
      draw->AddConvexPolyFilled(shadow, 4, hot ? accent_dark : IM_COL32(24, 13, 34, 245));
      draw->AddConvexPolyFilled(face, 4, hot ? accent : IM_COL32(31, 20, 42, 245));
      const ImVec2 text_size = ImGui::CalcTextSize(label);
      draw->AddText(ImVec2(q.x + (button_w - text_size.x) * .5f, q.y + (54.0f - text_size.y) * .5f),
                    IM_COL32(255, 250, 253, 255), label);
      return pressed;
    };
    if (!state.menu_quit) {
      if (action("##resume", "BACK TO GAME", 0)) state.menu_open = false;
      if (action("##settings", "SETTINGS", 1)) {
        state.menu_open = false;
        state.open = true;
        reset_settings_home("Esc menu SETTINGS");
      }
      if (action("##quit", "QUIT GAME", 2)) state.menu_quit = true;
      draw->AddText(ImVec2(p.x + 20, p.y + 144), IM_COL32(167, 162, 178, 255),
                    "ESC  BACK TO GAME     F1  SETTINGS");
    } else {
      if (action("##confirm_quit", "QUIT", 0)) { state.menu_open = false; host::request_exit(0); }
      if (action("##cancel_quit", "CANCEL", 1)) state.menu_quit = false;
    }
    ImGui::End();
  }
  const bool practice_was_open = state.practice_open;
  if (!state.fill_window && !state.open && !state.menu_open)
    draw_native_practice(state, options, practice, pad, have_pad);
  if (practice_was_open && !state.practice_open) state.practice_release_capture = true;
  if (!panel_visible) {
    if (!state.fill_window && !state.menu_open &&
        g_guest_options_selection.load(std::memory_order_relaxed)) {
      const OverlayBounds bounds = overlay_bounds();
      ImGui::SetNextWindowPos(ImVec2(bounds.right - 24.0f, bounds.bottom - 24.0f),
                              ImGuiCond_Always, ImVec2(1.0f, 1.0f));
      ImGui::SetNextWindowBgAlpha(0.88f);
      ImGui::Begin("GameOptionsPCSettings", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                   ImGuiWindowFlags_NoSavedSettings);
      ImGui::TextUnformatted("OPTIONS");
      if (ImGui::Button("PC Settings", ImVec2(190.0f, 36.0f))) {
        state.open = true;
        reset_settings_home("Options overlay PC Settings button");
      }
      ImGui::TextDisabled("Select the fourth option with A");
      ImGui::End();
    }
    // The closed state is passive except for F1 and the explicit GameCube Start + Down + Z chord.
    // Hidden on request; F1 still opens the panel. This must not return early: ImGui::Render() is
    // at the end of this function, and skipping it left draw() handing the renderer draw data that
    // was never built for this frame, which crashed on the next F1.
    if (options.settings_hint) {
      const OverlayBounds bounds = overlay_bounds();
      ImGui::SetNextWindowPos(ImVec2(bounds.right - 12, bounds.top + 12), ImGuiCond_Always, ImVec2(1, 0));
      ImGui::SetNextWindowBgAlpha(ImGui::GetTime() < 20.0 ? 0.8f : 0.35f);
      ImGui::Begin("SettingsButton", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
      clamp_overlay_window();
      ImGui::TextUnformatted("Settings: F1  /  START + DOWN + Z");
    ImGui::End();
    }
  }
  // Test hook for screenshots: MELEE_TEST_OVERLAY=1 shows the controller overlay with its values.
  static const bool test_overlay = std::getenv("MELEE_TEST_OVERLAY") != nullptr;
  if (test_overlay) { options.input_overlay = true; options.input_overlay_values = true; if (const char* k = std::getenv("MELEE_TEST_KNOB")) options.input_overlay_stick = std::atoi(k); }
  if (options.input_overlay) {
    const int mask = options.input_overlay_ports ? options.input_overlay_ports : 1;
    const bool lone = (mask & (mask - 1)) == 0;   // exactly one port selected
    int row = 0;
    // Draw with the 0.6.61 style and font; restore the menu style afterwards.
    ImGuiStyle& live_style = ImGui::GetStyle();
    const ImGuiStyle menu_style = live_style;
    live_style = g_input_overlay_style;
    if (g_settings_old_font) ImGui::PushFont(g_settings_old_font);
    for (int i = 0; i < 4; ++i)
      if (mask & (1 << i)) draw_input_overlay(i, row++, lone, state.open, options.input_overlay_hide_border, options.input_overlay_values, options.input_overlay_stick);
    if (g_settings_old_font) ImGui::PopFont();
    live_style = menu_style;
  }
  // Below here are the two overlays that only make sense with a match behind them: the L-cancel
  // notice, which is about a mode the player is queuing for, and the frame time graph, which in the
  // standalone settings window would report that window's own frame rate on top of the panel.
  // The controller display above is deliberately left on: it shows live pad input, which is exactly
  // what someone checking their bindings in this window wants to see.
  // Skipped as a block, never by returning: ImGui::Render() is below, and a frame that leaves this
  // function without it hands the renderer draw data that was never built, which is an access
  // violation on the next present. 0.3.2 shipped exactly that as an early return here and crashed
  // the launcher's settings window. The warning was already in this file, twenty lines up.
  if (!state.fill_window) {
    draw_lcancel_overlays();
    draw_discord_invite_overlay();
    // The plain readouts: frame rate and, while online, the ping. Small, top left, no window
    // chrome, the way a Dolphin OSD line looks, and separate from the performance graph.
    // Reflex flash indicator: a white square on the frame A goes down on port 1, and the matching
    // marker, which a latency analyzer monitor or LDAT times against the photons.
    if (options.reflex_flash && options.reflex_mode > 0) {
      static bool a_was_down = false;
      host::PadState pads[4]{};
      host::input_last_pads(pads);
      const bool a_down = (pads[0].button & 0x0100) != 0;
      if (a_down && !a_was_down) {
        const OverlayBounds bounds = overlay_bounds();
        ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(bounds.left, bounds.top), ImVec2(bounds.left + 64, bounds.top + 64), IM_COL32(255, 255, 255, 255));
        streamline::pcl_marker(7);   // eTriggerFlash
      }
      a_was_down = a_down;
    }
    // Drawn as lines of text like Dolphin's: the frame rate, and the ping on the line under it.
    // The performance graph opens below them; if it is dragged over them, they move below it.
    static ImVec2 perf_min(0, 0), perf_max(0, 0);   // the graph's rectangle last frame, if shown
    // Test hook for screenshots: MELEE_TEST_READOUT=1 shows both lines and the graph.
    static const bool test_readout = std::getenv("MELEE_TEST_READOUT") != nullptr;
    if (test_readout) { options.show_fps = true; options.show_ping = true; options.performance_overlay = true; }
    const bool ping_line = options.show_ping && (slippi::online::is_online_match() || test_readout);
    const float line_h = ImGui::GetTextLineHeight() + 2.0f;
    const float readout_h = 8.0f + line_h * 4;       // room for every line, so the graph never moves
    const bool latency_line = options.reflex_stats && streamline::reflex_latency_ms() > 0.0f;
    float vram_used = 0, vram_total = 0;
    const bool vram_line = options.show_vram && vram_usage(&vram_used, &vram_total);
    if (options.show_fps || ping_line || latency_line || vram_line) {
      char lines[4][40];
      int n = 0;
      if (options.show_fps) std::snprintf(lines[n++], sizeof lines[0], "FPS: %.0f", ImGui::GetIO().Framerate);
      if (ping_line) std::snprintf(lines[n++], sizeof lines[0], "Ping: %d ms", slippi::online::ping_ms());
      if (options.reflex_stats && streamline::reflex_latency_ms() > 0.0f && n < 3)
        std::snprintf(lines[n++], sizeof lines[0], "Render latency: %.1f ms", streamline::reflex_latency_ms());
      if (vram_line) std::snprintf(lines[n++], sizeof lines[0], "VRAM: %.1f / %.1f GB", vram_used, vram_total);
      const OverlayBounds bounds = overlay_bounds();
      ImVec2 at(bounds.left + 10, bounds.top + 8);
      float wide = 0;
      for (int i = 0; i < n; ++i) wide = std::max(wide, ImGui::CalcTextSize(lines[i]).x);
      const bool graph_there = options.performance_overlay && perf_max.x > perf_min.x &&
                               at.x < perf_max.x && at.x + wide > perf_min.x && at.y < perf_max.y && at.y + line_h * n > perf_min.y;
      if (graph_there) at.y = perf_max.y + 6;
      ImDrawList* fg = ImGui::GetForegroundDrawList();
      for (int i = 0; i < n; ++i) {
        const ImVec2 p(at.x, at.y + line_h * i);
        fg->AddText(ImVec2(p.x + 1, p.y + 1), IM_COL32(0, 0, 0, 200), lines[i]);
        fg->AddText(p, IM_COL32(0, 255, 255, 255), lines[i]);
      }
    }
    if (options.performance_overlay) {
      // Draggable, and resizable by its bottom-right corner (drag it smaller if it is in the way);
      // NoTitleBar keeps it out of the way otherwise. Opens under the FPS and ping lines.
      const OverlayBounds bounds = overlay_bounds();
      ImGui::SetNextWindowPos(ImVec2(bounds.left + 12, bounds.top + ((options.show_fps || options.show_ping) ? 8.0f + readout_h : 12.0f)), ImGuiCond_FirstUseEver);
      ImGui::SetNextWindowSize(ImVec2(266, 0), ImGuiCond_FirstUseEver);
      ImGui::SetNextWindowSizeConstraints(ImVec2(120, 40), ImVec2(FLT_MAX, FLT_MAX));
      ImGui::SetNextWindowBgAlpha(0.75f);
      ImGui::Begin("Performance", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse);
      clamp_overlay_window();
      const float plot_w = ImGui::GetContentRegionAvail().x;
      ImGui::Text("%.0f presentations/s | %.2f ms", ImGui::GetIO().Framerate, 1000.f/std::max(1.f, ImGui::GetIO().Framerate));
      ImGui::PlotLines("##frametimes", state.intervals.data(), (int)state.intervals.size(), state.cursor % state.intervals.size(), nullptr, 0, 33.4f, ImVec2(plot_w, 60));
      // Render latency: works whether or not Reflex's low-latency mode is on, so Native has a number
      // to show too (and something to compare "On" against). The breakdown adds up to roughly the
      // total; osRenderQueue and driver overlap the other stages a little, per NVIDIA's own report.
      if (streamline::reflex_available()) {
        const float lat = streamline::reflex_latency_ms();
        ImGui::Text("Render latency: %.2f ms%s", lat, options.reflex_mode == 0 ? " (Reflex off)" : "");
        ImGui::PlotLines("##latency", state.latencies.data(), (int)state.latencies.size(),
                         state.latency_cursor % state.latencies.size(), nullptr, 0, std::max(8.0f, lat * 2.0f), ImVec2(plot_w, 60));
        const streamline::ReflexBreakdown b = streamline::reflex_breakdown();
        ImGui::TextWrapped("Sim %.2f | Submit %.2f | Driver %.2f | OS queue %.2f | GPU %.2f ms",
                           b.sim, b.render_submit, b.driver, b.os_queue, b.gpu_render);
      }
      perf_min = ImGui::GetWindowPos();
      perf_max = ImVec2(perf_min.x + ImGui::GetWindowSize().x, perf_min.y + ImGui::GetWindowSize().y);
      ImGui::End();
    } else {
      perf_min = perf_max = ImVec2(0, 0);
    }
  }
  // When the panel closes, keep the game's controller input neutral until every button is up.
  // Otherwise the A (or B/Start) that closed it is still held and Melee reads it as a new press.
  {
    static bool capture_prev_open = false;
    if (capture_prev_open && !state.open && !state.fill_window) state.practice_release_capture = true;
    capture_prev_open = state.open;
  }
  // Same rule as at the top of the frame. With only the in-game (Esc) menu open, this used to say
  // "not captured" while the top said "captured", so the pointer was shown and hidden every frame.
  host::window_input_capture(state.open || state.menu_open || practice_capture ||
                             state.practice_release_capture);
  if (!state.fill_window && !state.open && !state.menu_open && options.show_player_nicknames)
    draw_player_nicknames();
  {
    // Menu sounds: a tick when the highlighted category or control changes, a brighter one when
    // a page opens or the panel opens, a lower one when a page or the panel closes.
    static bool snd_init = false, snd_open = false;
    static int snd_tab = 0, snd_detail = 0;
    static ImGuiID snd_focus = 0;
    const int detail_now = (state.clean_detail_open ? 1 : 0) | (state.dashboard_detail_open ? 2 : 0) |
                           (state.gd_detail_open ? 4 : 0) | (state.radial_detail_open ? 8 : 0) |
                           (state.wide_detail_open ? 16 : 0);
    const ImGuiID focus_now = state.open ? ImGui::GetFocusID() : 0;
    int sound = 0;
    if (snd_init && options.settings_menu_sounds && !state.fill_window) {
      if (state.open != snd_open) sound = state.open ? 2 : 3;
      else if (state.open && detail_now != snd_detail) sound = detail_now ? 2 : 3;
      else if (state.open && (state.active_tab != snd_tab || (focus_now && focus_now != snd_focus))) sound = 1;
    }
    if (sound) host::audio_ui_sound(sound);
    snd_init = true; snd_open = state.open; snd_tab = state.active_tab;
    snd_detail = detail_now; snd_focus = focus_now;
  }
  if (g_ui_diag) {
    // One line per change, so a report of "the menu jumped" can be read back from the log.
    static int last_open = -1, last_tab = -1, last_detail = -1, last_style = -1, last_legacy = -1;
    const int detail = (state.clean_detail_open ? 1 : 0) | (state.dashboard_detail_open ? 2 : 0) |
                       (state.gd_detail_open ? 4 : 0) | (state.radial_detail_open ? 8 : 0) |
                       (state.wide_detail_open ? 16 : 0);
    if (last_open != (int)state.open || last_tab != state.active_tab || last_detail != detail ||
        last_style != options.overlay_style || last_legacy != (int)state.legacy_presentation) {
      host::log("ui diag: open %d tab %d detail %d style %d legacy %d", (int)state.open, state.active_tab,
                detail, options.overlay_style, (int)state.legacy_presentation);
      last_open = state.open; last_tab = state.active_tab; last_detail = detail;
      last_style = options.overlay_style; last_legacy = state.legacy_presentation;
    }
    const ImGuiIO& diag_io = ImGui::GetIO();
    if (diag_io.MouseClicked[0] || diag_io.MouseReleased[0])
      host::log("ui diag: mouse %s at %.0f,%.0f hovered id %08X active id %08X nav id %08X",
                diag_io.MouseClicked[0] ? "down" : "up", diag_io.MousePos.x, diag_io.MousePos.y,
                ImGui::GetHoveredID(), ImGui::GetActiveID(), ImGui::GetFocusID());
  }
  if (launcher_old_look) {
    if (g_settings_old_font) ImGui::PopFont();
    ImGui::GetStyle() = launcher_saved_style;
  }
  ImGui::Render();
  // Apply opacity after ImGui has built the overlay: this includes the game-facing custom
  // geometry and images as well as standard widgets, leaving the emulated frame visible beneath.
  if (options.settings_transparency > 0) {
    const float alpha_scale = 1.0f - options.settings_transparency / 100.0f;
    ImDrawData* data = ImGui::GetDrawData();
    for (int list_index = 0; list_index < data->CmdListsCount; ++list_index) {
      ImDrawList* list = data->CmdLists[list_index];
      for (ImDrawVert& vertex : list->VtxBuffer) {
        const uint32_t alpha = (vertex.col >> IM_COL32_A_SHIFT) & 0xff;
        const uint32_t scaled = (uint32_t)std::lround(alpha * alpha_scale);
        vertex.col = (vertex.col & ~IM_COL32_A_MASK) | (scaled << IM_COL32_A_SHIFT);
      }
    }
  }
  if (state.legacy_presentation) {
    options.overlay_style = state.legacy_saved_appearance;
    if (!state.open) {
      state.legacy_presentation = false;
      // The legacy view has no slide-out. Finish the close now, or the restored modern
      // appearance (pink Clean side) is drawn sliding out for a few frames behind it.
      state.panel_anim_target_open = false;
      state.panel_anim_frame = 12.0f;
      state.panel_slide_x = -720.0f;
      state.panel_slide_start_x = -720.0f;
    }
  }
  return changed;
}

void PcSettingsUI::draw(ID3D12GraphicsCommandList* list) {
  ID3D12DescriptorHeap* heap = impl_->heap.Get(); list->SetDescriptorHeaps(1, &heap);
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), list);
}
}
