// Native gameplay profile restrictions for Slippi matchmaking.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <string>

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
  // Source content views switch those modes to retail files. A Static mod session has no such
  // switch, so a non-Vanilla profile reaching here outside Direct is refused.
  if (profile == NativeGameplayProfile::Akaneia && mode != 2)
    return "Akaneia is available only in Direct mode; Unranked, Teams and Party require vanilla gameplay";
  if (profile == NativeGameplayProfile::OtherMod && mode != 2)
    return "Mods are available only in Direct mode; Unranked, Teams and Party require vanilla gameplay";
  return nullptr;
}
// A missing, truncated or malformed content identity cannot establish a same-mod match.
inline bool native_mod_fingerprint_valid(const std::string& fingerprint) {
  if (fingerprint.size() != 64) return false;
  for (const char ch : fingerprint)
    if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
  return true;
}

inline bool native_direct_builds_match(bool local_mod, const std::string& local_fingerprint,
                                      bool remote_mod, const std::string& remote_fingerprint) {
  if (local_mod != remote_mod) return false;
  if (!local_mod) return true;
  return native_mod_fingerprint_valid(local_fingerprint) &&
         native_mod_fingerprint_valid(remote_fingerprint) && local_fingerprint == remote_fingerprint;
}

// Called only after the Direct build check succeeds. Static mod code supports its own extended
// roster/stages; the native Source game and a retail session keep their original limits.
inline bool native_direct_selection_supported(bool mod_view, bool extended_content,
                                              uint8_t character, uint16_t stage) {
  return (mod_view && extended_content) || (character < 26 && stage < 0x56);
}
} // namespace slippi::online
