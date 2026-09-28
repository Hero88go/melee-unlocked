// Native gameplay profile restrictions for Slippi matchmaking.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace slippi::online {
enum class NativeGameplayProfile { Vanilla, Akaneia, OtherMod };

// Slippi's mode IDs are Ranked=0, Unranked=1, Direct=2, Teams=3, Party=4.
// Melee Unlocked does not offer Ranked, on either engine; every matchmaking entry (the game's own
// online menus, practice matchmaking, Discord joins) goes through this check.
// Direct is the only mode in which both clients can deliberately run Akaneia.
inline const char* native_profile_mode_error(NativeGameplayProfile profile, int mode) {
  if (mode < 0 || mode > 4) return "Unsupported matchmaking mode";
  if (mode == 0) return "Ranked is not available in Melee Unlocked. Play Unranked, Direct or Teams.";
  // A mod's own gameplay (its fighters, stages and files) only in Direct, against the same build.
  // Unranked, Teams and Party switch to the retail files by themselves (the content view), so a
  // profile other than Vanilla reaching here in those modes is refused.
  if (profile == NativeGameplayProfile::Akaneia && mode != 2)
    return "Akaneia is available only in Direct mode; Unranked, Teams and Party require vanilla gameplay";
  if (profile == NativeGameplayProfile::OtherMod && mode != 2)
    return "Mods are available only in Direct mode; Unranked, Teams and Party require vanilla gameplay";
  return nullptr;
}
} // namespace slippi::online
