// SPDX-License-Identifier: GPL-2.0-or-later
#include "controller_profiles.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace host {
namespace {

// Action names as the settings file already spells them (pc_settings.cpp kActionNames).
constexpr const char* kActions[kProfileActions] = {"A", "B", "X", "Y", "Z", "Start", "L", "R", "DUp", "DDown", "DLeft", "DRight",
                                                    "CUp", "CDown", "CLeft", "CRight"};
constexpr const char* kDeviceKeys[(int)ProfileDevice::Count] = {"keyboard", "xinput", "playstation", "gcadapter", "switchpro", "hid"};
constexpr const char* kHeader = "# Melee Unlocked controller profile";
constexpr const char* kExtension = ".profile";

std::filesystem::path g_folder = "ControllerProfiles";

std::filesystem::path file_for(const std::string& name) { return g_folder / std::filesystem::u8path(name + kExtension); }

// Reads the device line only, cheaply, for listing.
bool device_of(const std::filesystem::path& file, std::string& device) {
  std::ifstream in(file);
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream words(line);
    std::string key, value;
    if (!(words >> key) || key[0] == '#') continue;
    if (key == "device" && (words >> value)) { device = value; return true; }
  }
  return false;
}

}  // namespace

const char* profile_device_key(ProfileDevice device) {
  const int i = (int)device;
  return i >= 0 && i < (int)ProfileDevice::Count ? kDeviceKeys[i] : "";
}

void profiles_set_folder(const std::string& settings_path) {
  std::filesystem::path settings = std::filesystem::u8path(settings_path);
  g_folder = settings.has_parent_path() ? settings.parent_path() / "ControllerProfiles" : std::filesystem::path("ControllerProfiles");
}

std::string profiles_folder() { return g_folder.u8string(); }

std::string profile_clean_name(const std::string& name) {
  std::string out;
  for (unsigned char c : name)
    if (std::isalnum(c) || c == ' ' || c == '-' || c == '_' || c == '.') out += (char)c;
  // No leading dots (hidden files, "..") and no surrounding spaces.
  while (!out.empty() && (out.front() == ' ' || out.front() == '.')) out.erase(out.begin());
  while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
  if (out.size() > 40) out.resize(40);
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

std::vector<std::string> profile_list(ProfileDevice device) {
  std::vector<std::string> names;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(g_folder, ec)) {
    if (ec) break;
    if (!entry.is_regular_file(ec) || entry.path().extension() != kExtension) continue;
    std::string kind;
    if (device_of(entry.path(), kind) && kind == profile_device_key(device)) names.push_back(entry.path().stem().u8string());
  }
  std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                        [](char x, char y) { return std::tolower((unsigned char)x) < std::tolower((unsigned char)y); });
  });
  return names;
}

bool profile_save(ProfileDevice device, const std::string& name, const ProfileBindings& bindings) {
  const std::string clean = profile_clean_name(name);
  if (clean.empty() || clean != name) return false;
  std::error_code ec;
  std::filesystem::create_directories(g_folder, ec);
  // Written beside and renamed over, so a crash mid-write cannot leave half a profile.
  const std::filesystem::path path = file_for(clean), temporary = path.string() + ".tmp";
  {
    std::ofstream out(temporary, std::ios::trunc);
    if (!out) return false;
    out << kHeader << "\ndevice " << profile_device_key(device) << "\n";
    for (int i = 0; i < kProfileActions; ++i) out << kActions[i] << " " << bindings[i] << "\n";
    if (!out) return false;
  }
  std::filesystem::rename(temporary, path, ec);
  if (ec) { std::filesystem::remove(temporary, ec); return false; }
  return true;
}

bool profile_load(ProfileDevice device, const std::string& name, ProfileBindings& bindings) {
  const std::string clean = profile_clean_name(name);
  if (clean.empty() || clean != name) return false;
  std::ifstream in(file_for(clean));
  if (!in) return false;
  ProfileBindings read{};   // an action a profile does not mention is left unbound
  bool device_ok = false;
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream words(line);
    std::string key, value;
    if (!(words >> key) || key[0] == '#' || !(words >> value)) continue;
    if (key == "device") { device_ok = value == profile_device_key(device); continue; }
    for (int i = 0; i < kProfileActions; ++i) {
      if (key != kActions[i]) continue;
      char* end = nullptr;
      const unsigned long v = std::strtoul(value.c_str(), &end, 10);
      if (end && *end == '\0') read[i] = (uint32_t)v;
    }
  }
  if (!device_ok) return false;
  bindings = read;
  return true;
}

bool profile_delete(ProfileDevice device, const std::string& name) {
  const std::string clean = profile_clean_name(name);
  if (clean.empty() || clean != name) return false;
  std::string kind;
  const std::filesystem::path path = file_for(clean);
  if (!device_of(path, kind) || kind != profile_device_key(device)) return false;   // never another device's file
  std::error_code ec;
  return std::filesystem::remove(path, ec) && !ec;
}

}  // namespace host
