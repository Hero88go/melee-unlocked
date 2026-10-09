// SPDX-License-Identifier: GPL-2.0-or-later
#include "dolphin_profile.h"
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

namespace host {
namespace {

// Dolphin's setting for each action, in BindAction order (controller_profiles.cpp kActions).
constexpr const char* kDolphinKeys[kProfileActions] = {
    "Buttons/A", "Buttons/B", "Buttons/X", "Buttons/Y", "Buttons/Z", "Buttons/Start", "Triggers/L", "Triggers/R",
    "D-Pad/Up", "D-Pad/Down", "D-Pad/Left", "D-Pad/Right",
    "C-Stick/Up", "C-Stick/Down", "C-Stick/Left", "C-Stick/Right",
    "Main Stick/Up", "Main Stick/Down", "Main Stick/Left", "Main Stick/Right",
    "Triggers/L-Analog", "Triggers/R-Analog"};

struct Name { const char* name; uint32_t value; };

// XInput: the wButtons bit, with the two bits this project reserves for the triggers
// (input_bindings.h kXInputBindLT / kXInputBindRT).
constexpr Name kXInput[] = {
    {"Button A", 0x1000}, {"Button B", 0x2000}, {"Button X", 0x4000}, {"Button Y", 0x8000},
    {"Pad N", 0x0001}, {"Pad S", 0x0002}, {"Pad W", 0x0004}, {"Pad E", 0x0008},
    {"Start", 0x0010}, {"Back", 0x0020}, {"Thumb L", 0x0040}, {"Thumb R", 0x0080},
    {"Shoulder L", 0x0100}, {"Shoulder R", 0x0200}, {"Trigger L", 0x0400}, {"Trigger R", 0x0800}};

// The keyboard: Dolphin's DirectInput key names and their Windows virtual-key codes.
constexpr Name kKeys[] = {
    {"ESCAPE", 0x1B}, {"MINUS", 0xBD}, {"EQUALS", 0xBB}, {"BACK", 0x08}, {"TAB", 0x09},
    {"LBRACKET", 0xDB}, {"RBRACKET", 0xDD}, {"RETURN", 0x0D}, {"LCONTROL", 0xA2}, {"RCONTROL", 0xA3},
    {"SEMICOLON", 0xBA}, {"APOSTROPHE", 0xDE}, {"GRAVE", 0xC0}, {"LSHIFT", 0xA0}, {"RSHIFT", 0xA1},
    {"BACKSLASH", 0xDC}, {"COMMA", 0xBC}, {"PERIOD", 0xBE}, {"SLASH", 0xBF}, {"MULTIPLY", 0x6A},
    {"LMENU", 0xA4}, {"RMENU", 0xA5}, {"SPACE", 0x20}, {"CAPITAL", 0x14}, {"NUMLOCK", 0x90}, {"SCROLL", 0x91},
    {"SUBTRACT", 0x6D}, {"ADD", 0x6B}, {"DECIMAL", 0x6E}, {"DIVIDE", 0x6F}, {"NUMPADENTER", 0x0D},
    {"HOME", 0x24}, {"UP", 0x26}, {"PRIOR", 0x21}, {"LEFT", 0x25}, {"RIGHT", 0x27}, {"END", 0x23},
    {"DOWN", 0x28}, {"NEXT", 0x22}, {"INSERT", 0x2D}, {"DELETE", 0x2E}};

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace((unsigned char)s[a])) ++a;
  while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
  return s.substr(a, b - a);
}

// One control's name out of a Dolphin expression, or empty when it is more than one control.
// `Button A`, Start, and `XInput/0/Gamepad:Button A` are single controls; `A | B` and !`A` are not.
std::string single_control(std::string value) {
  value = trim(value);
  if (value.size() >= 2 && value.front() == '`' && value.back() == '`') {
    value = value.substr(1, value.size() - 2);
    if (value.find('`') != std::string::npos) return {};
  } else {
    for (unsigned char c : value)
      if (!std::isalnum(c) && c != '_' && c != '+' && c != '-') return {};
  }
  const size_t device = value.rfind(':');
  if (device != std::string::npos) value = value.substr(device + 1);
  return trim(value);
}

bool key_code(const std::string& name, uint32_t* code) {
  if (name.size() == 1 && std::isalnum((unsigned char)name[0])) { *code = (uint32_t)std::toupper((unsigned char)name[0]); return true; }
  if (name.size() >= 2 && name.size() <= 3 && name[0] == 'F' && std::isdigit((unsigned char)name[1])) {
    const int n = std::atoi(name.c_str() + 1);
    if (n >= 1 && n <= 24) { *code = 0x70u + (uint32_t)(n - 1); return true; }
  }
  if (name.rfind("NUMPAD", 0) == 0 && name.size() == 7 && std::isdigit((unsigned char)name[6])) { *code = 0x60u + (uint32_t)(name[6] - '0'); return true; }
  for (const Name& key : kKeys)
    if (name == key.name) { *code = key.value; return true; }
  return false;
}

}  // namespace

DolphinProfile dolphin_profile_read(const std::string& text) {
  DolphinProfile out;
  std::string device, values[kProfileActions];
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
    if (key == "Device") device = value;
    for (int i = 0; i < kProfileActions; ++i)
      if (key == kDolphinKeys[i]) values[i] = value;
  }
  if (device.empty()) { out.message = "This is not a Dolphin controller profile (no Device line)."; return out; }
  const bool keyboard = device.find("Keyboard") != std::string::npos;
  const bool xinput = device.rfind("XInput/", 0) == 0;
  if (!keyboard && !xinput) {
    out.message = "Only Dolphin keyboard and XInput profiles can be read. This one is for " + device + ".";
    return out;
  }
  out.device = keyboard ? ProfileDevice::Keyboard : ProfileDevice::XInput;
  for (int i = 0; i < kProfileActions; ++i) {
    if (values[i].empty()) continue;
    const std::string name = single_control(values[i]);
    uint32_t value = 0;
    bool known = false;
    if (keyboard) {
      known = !name.empty() && key_code(name, &value);
    } else {
      for (const Name& button : kXInput)
        if (name == button.name) { value = button.value; known = true; }
      // An analog stick axis ("Left Y+", "Right X-"): this project reads a pad's sticks itself.
      if (!known && (name.rfind("Left ", 0) == 0 || name.rfind("Right ", 0) == 0)) continue;
    }
    if (!known) { ++out.skipped; continue; }
    out.bindings[i] = value;
    ++out.bound;
  }
  out.ok = out.bound > 0;
  if (!out.ok) out.message = "No button in this profile could be read.";
  return out;
}

DolphinProfile dolphin_profile_read_file(const std::string& path, std::string* name) {
  const std::filesystem::path file = std::filesystem::u8path(path);
  if (name) *name = file.stem().u8string();
  std::ifstream in(file, std::ios::binary);
  if (!in) { DolphinProfile out; out.message = "The file could not be opened."; return out; }
  // A profile is a few hundred bytes; anything large is not one.
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (text.size() > 256 * 1024) { DolphinProfile out; out.message = "This is not a Dolphin controller profile."; return out; }
  return dolphin_profile_read(text);
}

std::string dolphin_profile_choose_file() {
#ifdef _WIN32
  wchar_t file[32768]{};
  OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof dialog;
  dialog.hwndOwner = GetActiveWindow();
  dialog.lpstrFilter = L"Dolphin controller profile (*.ini)\0*.ini\0";
  dialog.lpstrTitle = L"Dolphin controller profile (User\\Config\\Profiles\\GCPad)";
  dialog.lpstrFile = file; dialog.nMaxFile = (DWORD)std::size(file);
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&dialog)) return {};
  char utf8[32768 * 3];
  const int n = WideCharToMultiByte(CP_UTF8, 0, file, -1, utf8, (int)sizeof utf8, nullptr, nullptr);
  return n > 0 ? std::string(utf8) : std::string();
#else
  return {};
#endif
}

}  // namespace host
