// Shared, read-only account discovery for the launcher and both game engines.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace slippi::account {
struct Profile {
  std::filesystem::path file;
  nlohmann::json data;
  explicit operator bool() const { return !file.empty(); }
};

inline std::string text(const nlohmann::json& data, const char* key) {
  if (!data.is_object()) return {};
  const auto value = data.find(key);
  return value != data.end() && value->is_string() ? value->get<std::string>() : std::string();
}

inline Profile read(const std::filesystem::path& file) {
  std::ifstream input(file, std::ios::binary);
  if (!input) return {};
  try {
    auto data = nlohmann::json::parse(input, nullptr, false);
    if (text(data, "uid").empty() || text(data, "playKey").empty() ||
        text(data, "connectCode").empty()) return {};
    return {file, std::move(data)};
  } catch (const nlohmann::json::exception&) {
    return {};
  }
}

inline Profile search(const std::filesystem::path& root, int depth, size_t& remaining) {
  if (depth < 0 || remaining == 0) return {};
  --remaining;
  if (auto profile = read(root / "User" / "Slippi" / "user.json")) return profile;
  std::error_code ec;
  std::vector<std::filesystem::path> directories;
  std::filesystem::directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec);
  for (std::filesystem::directory_iterator end; !ec && it != end; it.increment(ec)) {
    std::error_code kind;
    if (!it->is_symlink(kind) && !kind && it->is_directory(kind) && !kind)
      directories.push_back(it->path());
  }
  std::sort(directories.begin(), directories.end());
  for (const auto& directory : directories)
    if (auto profile = search(directory, depth - 1, remaining)) return profile;
  return {};
}

inline std::vector<std::filesystem::path> launcher_roots() {
  std::vector<std::filesystem::path> roots;
  for (const char* name : {"APPDATA", "LOCALAPPDATA"}) {
#ifdef _WIN32
    char* value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, name) == 0 && value && *value)
      roots.emplace_back(std::filesystem::path(value) / "Slippi Launcher");
    free(value);
#else
    if (const char* value = std::getenv(name))
      if (*value) roots.emplace_back(std::filesystem::path(value) / "Slippi Launcher");
#endif
  }
  return roots;
}

// The folders the Slippi Launcher's own settings file names for its Dolphin builds. They are the
// default netplay and playback folders unless the player moved them, and a moved one can be on
// another drive, outside anything searched from the launcher's folder.
inline std::vector<std::filesystem::path> settings_builds(const std::filesystem::path& root) {
  std::vector<std::filesystem::path> builds;
  std::ifstream input(root / "Settings", std::ios::binary);
  if (!input) return builds;
  const auto data = nlohmann::json::parse(input, nullptr, false);
  if (!data.is_object()) return builds;
  const auto inner = data.find("settings");
  const nlohmann::json& settings = inner != data.end() && inner->is_object() ? *inner : data;
  for (const char* key : {"netplayDolphinPath", "playbackDolphinPath"})
    if (const auto value = text(settings, key); !value.empty()) builds.push_back(std::filesystem::u8path(value));
  return builds;
}

inline Profile resolve(const std::filesystem::path& local_user_dir,
                       const std::vector<std::filesystem::path>& roots = launcher_roots()) {
  if (auto profile = read(local_user_dir / "user.json")) return profile;
  for (const auto& root : roots) {
    for (const char* build : {"netplay", "playback"})
      if (auto profile = read(root / build / "User" / "Slippi" / "user.json")) return profile;
    for (const auto& build : settings_builds(root))
      if (auto profile = read(build / "User" / "Slippi" / "user.json")) return profile;
    size_t remaining = 1024;
    if (auto profile = search(root, 4, remaining)) return profile;
  }
  return {};
}

// Why resolve() found nothing, so the launcher can say what to do instead of "log in" to a player
// who has: no Slippi Launcher folder at all, a launcher with no sign-in file, or a sign-in file
// without a connect code (an account that was created but never finished).
enum class Missing { Launcher, SignIn, ConnectCode };
inline Missing why_missing(const std::filesystem::path& local_user_dir,
                           const std::vector<std::filesystem::path>& roots = launcher_roots()) {
  std::error_code ec;
  std::vector<std::filesystem::path> files{local_user_dir / "user.json"};
  bool launcher = false;
  for (const auto& root : roots) {
    if (!std::filesystem::is_directory(root, ec)) continue;
    launcher = true;
    for (const char* build : {"netplay", "netplay-beta", "playback"}) files.push_back(root / build / "User" / "Slippi" / "user.json");
    for (const auto& build : settings_builds(root)) files.push_back(build / "User" / "Slippi" / "user.json");
  }
  for (const auto& file : files)
    if (std::filesystem::is_regular_file(file, ec)) return Missing::ConnectCode;
  return launcher ? Missing::SignIn : Missing::Launcher;
}
}  // namespace slippi::account
