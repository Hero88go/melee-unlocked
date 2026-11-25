// The disc for a start from Finder, which opens the app with no arguments.
// SPDX-License-Identifier: GPL-2.0-or-later
//
// As a no-argument start on Windows (port/app/standalone_launch_win.h): the disc chosen last time,
// else an open panel that takes only a Melee NTSC 1.02 disc image, and the choice is remembered in
// melee-unlocked/last-iso under the data folder. Any argument at all skips this.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace mac_host {
// Melee NTSC 1.02: game ID GALE01 at the start of the disc, disc revision 2 at offset 7.
inline bool is_melee_102(const std::filesystem::path& iso) {
  std::error_code ec;
  if (iso.empty() || !std::filesystem::is_regular_file(iso, ec)) return false;
  FILE* f = std::fopen(iso.c_str(), "rb");
  if (!f) return false;
  unsigned char head[8] = {};
  const size_t got = std::fread(head, 1, sizeof head, f);
  std::fclose(f);
  return got == sizeof head && std::memcmp(head, "GALE01", 6) == 0 && head[7] == 2;
}

// The open panel, with what the game needs when the player cancels it (disc_choice_mac.mm).
// Returns a Melee NTSC 1.02 disc, or an empty path when the player quits.
std::filesystem::path choose_disc_panel();

// Finder passes no arguments (an older macOS adds -psn_<number>).
inline bool started_from_finder(const std::vector<std::string>& args) {
  return args.size() == 1 || (args.size() == 2 && args[1].rfind("-psn_", 0) == 0);
}

// `data` is the XDG data folder (~/.local/share unless XDG_DATA_HOME says otherwise).
inline std::filesystem::path finder_disc(const std::filesystem::path& data) {
  namespace fs = std::filesystem;
  const fs::path memory = data / "melee-unlocked/last-iso";
  std::string line;
  if (std::ifstream in{memory}; in) std::getline(in, line);
  if (is_melee_102(fs::u8path(line))) return fs::u8path(line);
  const fs::path chosen = choose_disc_panel();
  if (chosen.empty()) return {};
  std::error_code ec;
  fs::create_directories(memory.parent_path(), ec);
  std::ofstream(memory) << chosen.u8string() << '\n';
  return chosen;
}
}  // namespace mac_host
