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

// The Vanilla segment of the GAME BUILD row: the Source Port started as the retail game. The online
// menu layer is never loaded (the disc's own boot: logos, opening, title, the normal main menu, the
// game's own music), gameplay mods, codes and training packs are suspended for the session, and
// saves go to their own card folder. launcher.ini keeps vanilla=1 beside engine=1; it means
// nothing for the Static Recomp, whose online code set is compiled in. vanillaowncode=1 also drops
// the built-in UCF and unlock codes, so the disc runs exactly as shipped.
struct VanillaChoice { bool vanilla = false; bool own_code = false; };
inline bool parse_vanilla_line(const std::string& line, VanillaChoice& choice) {
  if (line == "vanilla=1") { choice.vanilla = true; return true; }
  if (line == "vanillaowncode=1") { choice.own_code = true; return true; }
  return false;
}
// The arguments PLAY adds to a normal Source Port launch when Vanilla is picked. A lobby match or a
// mod launch never gets them: those are online or modded by definition.
inline std::string vanilla_launch_args(const VanillaChoice& choice) {
  if (!choice.vanilla) return "";
  return std::string(" --slippi-menus off --vanilla-only") + (choice.own_code ? " --vanilla-game" : "");
}

}  // namespace launcher
