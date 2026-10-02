// SPDX-License-Identifier: GPL-2.0-or-later
#include "pack_skin_rule.h"

namespace source_port::skins {

bool is_costume_file(const std::string& lower_path) {
  const size_t slash = lower_path.find_last_of("/\\");
  const std::string name = slash == std::string::npos ? lower_path : lower_path.substr(slash + 1);
  // "pl" + fighter + color + ".dat" is exactly ten characters.
  if (name.size() != 10 || name.compare(0, 2, "pl") != 0) return false;
  const std::string extension = name.substr(6);
  if (extension != ".dat" && extension != ".usd") return false;
  for (size_t i = 2; i < 6; ++i)
    if (name[i] < 'a' || name[i] > 'z') return false;
  // A fighter's animation bank has the same length as a costume name ("plfxaj.dat").
  if (name.compare(4, 2, "aj") == 0) return false;
  return true;
}

std::string display_name(const std::string& lower_path) {
  const size_t slash = lower_path.find_last_of("/\\");
  std::string name = slash == std::string::npos ? lower_path : lower_path.substr(slash + 1);
  if (!is_costume_file(name)) return name;
  // Three two-letter codes, each written with a capital first letter on the disc.
  for (size_t i = 0; i < 6; i += 2) name[i] = static_cast<char>(name[i] - 'a' + 'A');
  return name;
}

int32_t resolve_open(bool retail_view, int32_t entry,
                     const std::unordered_map<int32_t, int32_t>& view_alias,
                     const std::unordered_set<int32_t>& served_online) {
  if (!retail_view) return entry;
  if (served_online.count(entry)) return entry;
  const auto alias = view_alias.find(entry);
  return alias == view_alias.end() ? entry : alias->second;
}

}  // namespace source_port::skins
