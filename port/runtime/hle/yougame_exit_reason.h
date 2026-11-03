// Window-free policy for app-owned mod compatibility messages.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../host/mod_scan.h"
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace yougame::mode_policy {

enum class ModRefusal { None, UnsupportedMod, UnsupportedVersion, ModdedDol, NativeFighters };

inline ModRefusal mod_refusal(const source_port::mods::Detected& mod) {
  using namespace source_port::mods;
  // Unrecognized, unreadable, compressed, wrong-region and retail discs are not mod updates.
  if (mod.kind == Kind::Unsupported || mod.kind == Kind::Vanilla) return ModRefusal::None;
  if (mod.status != Support::Supported)
    return mod.version.empty() ? ModRefusal::UnsupportedMod : ModRefusal::UnsupportedVersion;
  if (mod.needs == Engine::Recomp) return ModRefusal::UnsupportedMod;
  return ModRefusal::None;
}

inline std::string exit_reason_field(std::string_view value) {
  std::string clean;
  for (unsigned char c : value) if (c >= 0x20 && c <= 0x7e) clean += char(c);
  std::string lower = clean;
  for (char& c : lower) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  // Check after stripping too: removing a control byte must not assemble a link.
  if (lower.find("://") != std::string::npos || lower.find("www.") != std::string::npos) return {};
  clean.resize(clean.size() > 60 ? 60 : clean.size());
  const auto first = clean.find_first_not_of(' ');
  if (first == std::string::npos) return {};
  return clean.substr(first, clean.find_last_not_of(' ') - first + 1);
}

// Null means no notification. Unknown metadata stays absent; filenames and log prose are not names.
inline nlohmann::json mod_exit_request(bool active, bool stdio, bool explicit_layer, ModRefusal refusal,
                                       std::string_view name = {}, std::string_view version = {}) {
  if (!active || !stdio || !explicit_layer || refusal == ModRefusal::None) return nullptr;
  nlohmann::json request = {{"type", "exitReason"}, {"reason", "mod_outdated"}};
  const auto mod = exit_reason_field(name), mod_version = exit_reason_field(version);
  if (!mod.empty()) request["mod"] = mod;
  if (!mod_version.empty()) request["modVersion"] = mod_version;
  return request;
}

}  // namespace yougame::mode_policy
