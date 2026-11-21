// The disc a game tells the YouGame app it opened: the local channel's `disc` message, the one YouGame's
// native SDK sends for yg_report_disc. The app takes only a plain .iso or .gcm on a local drive, by the
// rules below (YouGame's engines/native/yougame.h, yg__disc_path_ok): absolute (a drive letter or /), at
// most 1,024 bytes, no control characters, never a network path. The game (yougame_link.cpp) sends it when
// it is on the app's channel; a launcher start on the launcher's bridge has the launcher send it.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cctype>
#include <string>

namespace yougame::disc {

inline bool path_ok(const std::string& p) {
  if (p.empty() || p.size() > 1024) return false;
  for (unsigned char c : p)
    if (c < 0x20 || c == 0x7f) return false;
  if (p.size() >= 2 && (p[0] == '\\' || p[0] == '/') && (p[1] == '\\' || p[1] == '/')) return false;
  if (p[0] == '/') return true;
  return p.size() >= 3 && std::isalpha((unsigned char)p[0]) && p[1] == ':' && (p[2] == '\\' || p[2] == '/');
}

// A plain disc image: .iso or .gcm, any case (never .ciso, .rvz or a mod's patch).
inline bool plain_image(const std::string& p) {
  if (p.size() < 4) return false;
  std::string ext = p.substr(p.size() - 4);
  for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
  return ext == ".iso" || ext == ".gcm";
}

}  // namespace yougame::disc
