// Package-owned YouGame ranked rules. Pure parsing/policy for both engines and host tests.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "native_online_policy.h"
#include "../host/hackpack_ai.h" // existing portable, tested SHA-256
#include <nlohmann/json.hpp>
#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace yougame::ranked_rules {
constexpr size_t kMaxBytes = 16 * 1024;
constexpr const char* kInvalid = "This remix's ranked rules are invalid";
struct Rules {
  std::vector<uint16_t> stages;
  std::vector<uint8_t> characters;
  bool all_characters = true;
  std::string canonical, digest;
  bool character_allowed(uint8_t id) const {
    return all_characters || std::binary_search(characters.begin(), characters.end(), id);
  }
};

// Extended builds follow native_direct_selection_supported: the entire uint8/uint16 wire range.
// Lists are sets: sorting also makes random draws use the same order on both peers.
inline bool parse(const std::string& text, bool extended_content, Rules& out) {
  out = {};
  if (text.size() > kMaxBytes) return false;
  try {
    using json = nlohmann::json;
    bool duplicate = false;
    std::set<std::string> keys;
    const auto j = json::parse(text, [&](int depth, json::parse_event_t event, json& value) {
      if (depth > 3) throw std::runtime_error("rules nesting");
      if (event == json::parse_event_t::key && depth == 1 && !keys.insert(value.get<std::string>()).second)
        duplicate = true;
      return true;
    });
    if (duplicate || !j.is_object() || j.size() != 3 || !j.count("version") ||
        !j.count("stages") || !j.count("characters") || !j["version"].is_number_integer() ||
        j["version"] != 1 || !j["stages"].is_array() || j["stages"].empty()) return false;
    Rules rules;
    const auto ids = [](const json& list, unsigned max, auto& result) {
      if (!list.is_array()) return false;
      for (const auto& id : list) {
        if (!id.is_number_integer() || id < 0 || id > max) return false;
        result.push_back(static_cast<typename std::decay_t<decltype(result)>::value_type>(id.get<unsigned>()));
      }
      std::sort(result.begin(), result.end());
      return std::adjacent_find(result.begin(), result.end()) == result.end();
    };
    if (!ids(j["stages"], extended_content ? 65535 : 0x55, rules.stages)) return false;
    rules.all_characters = j["characters"].is_string() && j["characters"] == "all";
    if (!rules.all_characters && !ids(j["characters"], extended_content ? 255 : 25, rules.characters)) return false;
    // A list nobody can pick from would only fail at the character screen: refuse it with the file.
    if (!rules.all_characters && rules.characters.empty()) return false;
    json canonical = {{"version", 1}, {"stages", rules.stages}, {"characters", "all"}};
    if (!rules.all_characters) canonical["characters"] = rules.characters;
    rules.canonical = canonical.dump();
    rules.digest = host::hackpack_ai::rules::sha256_hex(
        reinterpret_cast<const uint8_t*>(rules.canonical.data()), rules.canonical.size());
    out = std::move(rules);
    return true;
  } catch (const std::exception&) { return false; }
}

inline bool applies(bool yougame, int mode, bool mod_view, const std::string& fingerprint) {
  return yougame && mode == 0 && mod_view && slippi::online::native_mod_fingerprint_valid(fingerprint);
}

inline const std::vector<uint16_t>& stage_pool(const Rules* rules, bool remix_ranked,
                                              const std::vector<uint16_t>& matchmaking) {
  if (rules && remix_ranked) return rules->stages;
  if (!matchmaking.empty()) return matchmaking;
  static const std::vector<uint16_t> vanilla = {0x2, 0x3, 0x8, 0x1C, 0x1F, 0x20};
  return vanilla;
}

// Missing build messages wait only when local rules require verification. A full digest compares
// every bit even if two short room suffixes collide; Direct/Teams never call this policy.
inline int peer_verdict(const std::string& local, bool received, const std::string& remote, bool expired) {
  if (!received) return local.empty() ? 1 : expired ? -1 : 0;
  return local == remote ? 1 : -1;
}
} // namespace yougame::ranked_rules
