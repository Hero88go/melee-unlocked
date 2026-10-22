// Session restrictions for launchers that expose vanilla and mods as separate listings.
// These affect execution only: saved mod choices and the native implementations stay intact.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string_view>

namespace host::gameplay {
// Set during the first argument pass, before settings or worker threads start. Never saved.
inline bool vanilla_only_session = false;
inline bool mod_autodetect_disabled = false;
inline bool vanilla_only() { return vanilla_only_session; }

inline bool conflicts_with_vanilla(std::string_view arg) {
  return arg == "--mod-base-iso" || arg == "--mod-profile" || arg == "--mod-iso" ||
         arg == "--mod-dir" || arg == "--mod-gci" || arg == "--scan-mods" ||
         arg == "--mod-opponent-unverified" || arg == "--te-options" ||
         arg == "--te-options2" || arg == "--gecko-codes" || arg == "--ram-translator" ||
         arg == "--lag-test";
}
}  // namespace host::gameplay
