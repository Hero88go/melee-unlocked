// Window-free policy shared by input injection, settings and ranked notices.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace lcancel::policy {
// Slippi mode ids: Ranked=0, Unranked=1, Direct=2, Teams=3, Party=4; offline < 0.
constexpr const char* auto_suppressed_mode(int mode) {
  if (mode < 0 || mode == 2) return nullptr;
  switch (mode) {
    case 0: return "Ranked";
    case 1: return "Unranked";
    case 3: return "Teams";
    case 4: return "Party";
    default: return "this online mode";
  }
}

constexpr bool automatic_allowed(int mode, bool saved, bool vanilla_only = false) {
  return saved && !vanilla_only && auto_suppressed_mode(mode) == nullptr;
}

constexpr bool automatic_setting_locked(int mode) { return mode == 0; }
constexpr bool automatic_after_toggle(int mode, bool saved, bool requested) {
  return automatic_setting_locked(mode) ? saved : requested;
}

constexpr const char* ranked_start_log(bool saved) {
  return saved ? "ranked: auto L-cancel off (enforced; saved setting on)"
               : "ranked: auto L-cancel off (enforced; saved setting off)";
}
inline constexpr const char* ranked_hint = "Auto L-cancel: off (enforced in Ranked).";
}  // namespace lcancel::policy
