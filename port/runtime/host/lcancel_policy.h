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

// agreed: the opponent said yes to it for this match (YouGame Unranked, controls.h). It counts only in
// Unranked; everywhere else the answer is exactly what it was before.
constexpr bool automatic_allowed(int mode, bool saved, bool vanilla_only = false, bool agreed = false) {
  return saved && !vanilla_only && (auto_suppressed_mode(mode) == nullptr || (mode == 1 && agreed));
}
// The notice's "disabled for this mode", with the same agreement taken into account.
constexpr const char* auto_suppressed_mode_agreed(int mode, bool agreed) {
  return mode == 1 && agreed ? nullptr : auto_suppressed_mode(mode);
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
