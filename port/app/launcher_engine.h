// Where the launcher finds the Source Port and how it reads the saved engine choice, as pure
// functions of the install folder's contents, so they can be tested without the window (port/tests).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdlib>
#include <functional>
#include <string>

namespace launcher {

enum Engine { ENGINE_LEGACY = 0, ENGINE_SOURCE = 1 };
enum CpuBuild { CPU_AUTO = 0, CPU_STANDARD = 1, CPU_COMPAT = 2 };

struct EngineInputs {
  std::string dir;         // the launcher's own folder (a release folder, or somewhere in a checkout)
  std::string repo_root;   // the source checkout above it, empty for a release
  int engine = ENGINE_LEGACY;
  std::function<bool(const std::string&)> exists;
};

// The source build is two files: the host and the game library beside it. It is only offered when
// both are there, next to the launcher or in a checkout's build-sourceport output.
inline std::string source_exe_dir(const EngineInputs& in) {
  if (in.exists(in.dir + "\\melee_source.exe") && in.exists(in.dir + "\\melee_game.dll")) return in.dir;
  if (!in.repo_root.empty()) {
    const std::string d = in.repo_root + "\\build-sourceport\\port\\Release";
    if (in.exists(d + "\\melee_source.exe") && in.exists(d + "\\melee_game.dll")) return d;
  }
  return "";
}

// launcher.ini's engine= line; anything missing or out of range keeps Legacy, the default.
inline int parse_engine_line(const std::string& line, int current) {
  if (line.rfind("engine=", 0) != 0) return current;
  const int v = std::atoi(line.c_str() + 7);
  return v >= ENGINE_LEGACY && v <= ENGINE_SOURCE ? v : current;
}

}  // namespace launcher
