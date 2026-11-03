// Pure room compatibility key policy shared by matchmaking and host-only tests.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_online_policy.h"
#include <cctype>
#include <string>

namespace yougame {
inline std::string compatibility_key(const std::string& version, bool mod_view,
                                     const std::string& fingerprint, bool static_engine = false,
                                     const std::string& rules_digest = {}) {
  if (mod_view && !slippi::online::native_mod_fingerprint_valid(fingerprint)) return {};
  if (!rules_digest.empty() && !slippi::online::native_mod_fingerprint_valid(rules_digest)) return {};
  std::string key = "mu-" + version;
  for (char& c : key) if (!std::isalnum((unsigned char)c) && c != '.' && c != '-') c = '-';
  std::string suffix = static_engine ? "-st" : "";
  if (mod_view) suffix += "-mod-" + fingerprint.substr(0, 16);
  if (!rules_digest.empty()) suffix += "-r" + rules_digest.substr(0, 8);
  // Reserve engine, mod and rules identity before truncating the version.
  const std::string result = key.substr(0, 64 - suffix.size()) + suffix;
  // A version can itself contain "-st" at the truncation boundary. Keep the legacy Source key
  // byte-identical, and shorten only Static's version once more if that would alias it.
  if (static_engine && result == compatibility_key(version, mod_view, fingerprint, false, rules_digest))
    return key.substr(0, 63 - suffix.size()) + suffix;
  return result;
}
} // namespace yougame
