// Full host bundle defaults and output isolation. Caller-supplied paths take precedence.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "platform_paths.h"
#include <cstdlib>
#include <string>
#include <vector>
namespace mac_host {
inline std::vector<std::string> full_launch_arguments(int argc, char** argv) {
  namespace fs = std::filesystem;
  std::vector<std::string> args(argv, argv + argc);
  const std::vector<std::string> paths = {"--iso", "--settings-path", "--sys-dir", "--user-dir", "--replay-dir",
      "--card-dir", "--log-file", "--shader-cache", "--script", "--state-digest", "--state-trace", "--replay",
      "--p2p-identity", "--p2p-result", "--capture", "--capture-frames"};
  auto value = [&](const std::string& name) -> std::string {
    for (size_t i = 1; i + 1 < args.size(); ++i) if (args[i] == name) return args[i+1];
    return {};
  };
  for (size_t i = 1; i + 1 < args.size(); ++i)
    for (const auto& flag : paths) if (args[i] == flag) { args[i+1] = fs::absolute(fs::u8path(args[i+1])).string(); ++i; break; }
  const auto seed = resources() / "shader-seed";
  if (!std::getenv("MELEE_SHADER_SEED") && fs::is_directory(seed))
    setenv("MELEE_SHADER_SEED", seed.c_str(), 0);
  fs::path user = value("--user-dir");
  if (user.empty()) {
    const char* data = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    user = data && *data ? fs::path(data) : fs::path(home && *home ? home : ".") / ".local/share";
    user = fs::absolute(user / "melee-unlocked/User/Slippi");
  }
  auto default_path = [&](const std::string& flag, const fs::path& path) {
    if (value(flag).empty()) { args.push_back(flag); args.push_back(path.string()); }
  };
  default_path("--user-dir", user);
  default_path("--settings-path", user / "port-settings.ini");
  default_path("--card-dir", user / "GC/CardA");
  default_path("--replay-dir", user / "Replays");
  default_path("--log-file", user / "melee_port.log");
  default_path("--shader-cache", user / "shaders");
  default_path("--sys-dir", resources() / "Sys");
  fs::create_directories(user);
  for (const auto& flag : {"--card-dir", "--replay-dir", "--shader-cache"}) fs::create_directories(value(flag));
  for (const auto& flag : {"--settings-path", "--log-file"}) fs::create_directories(fs::path(value(flag)).parent_path());
  // Relative diagnostic and auxiliary writes (including crash reports) belong to this profile.
  fs::create_directories(user / "tmp");
  setenv("TMPDIR", (user / "tmp").c_str(), 1);
  fs::current_path(user);
  return args;
}
}
