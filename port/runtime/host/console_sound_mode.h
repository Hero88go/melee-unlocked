// The console's persistent Mono (0) / Stereo (1) bit, stored beside this user's memory cards.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <filesystem>
#include <fstream>

namespace host {
class ConsoleSoundMode {
  std::filesystem::path path;
  int mode = 1;
public:
  explicit ConsoleSoundMode(const std::filesystem::path& card_dir) : path(card_dir / "sound-mode.txt") {
    int saved = 1;
    std::ifstream file(path);
    if (file >> saved && (saved == 0 || saved == 1)) mode = saved;
  }
  int get() const { return mode; }
  bool set(int value) {
    mode = value ? 1 : 0;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return false;
    std::ofstream file(path, std::ios::trunc);
    file << mode << '\n';
    file.close();
    return file.good();
  }
};
}  // namespace host
