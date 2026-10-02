// Which retail texture a costume's portrait or stock icon is, and which costume a texture the
// game draws belongs to. Header-only and free of renderer state so the unit tests can use it.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <string>
#include <vector>

#include "companion_texture_map.h"

namespace gx::texpack::native_companions {

// The table row a costume file's picture lives in. Nana's costume suffixes differ from Popo's but
// each is the same Ice Climbers selector ordinal. Sheik shares Zelda's portrait at the same
// ordinal; her stock icon is her own, so only the portrait is redirected.
inline std::string selector_target(std::string target, bool stock) {
  if (!stock && target.rfind("PlSk", 0) == 0) target.replace(2, 2, "Zd");
  static constexpr const char* nana[] = {"PlNnNr.dat", "PlNnYe.dat", "PlNnAq.dat", "PlNnWh.dat"};
  static constexpr const char* popo[] = {"PlPpNr.dat", "PlPpGr.dat", "PlPpOr.dat", "PlPpRe.dat"};
  for (size_t i = 0; i < std::size(nana); ++i) if (target == nana[i]) return popo[i];
  return target;
}

// One retail texture. A stock icon is a 16 color image plus a palette, and a few costumes of one
// fighter use the same image with different palettes: those are told apart by the palette
// (needs_tlut). Everything else is identified by its image alone.
struct Identity {
  uint64_t tex = 0, tlut = 0;
  bool needs_tlut = false;
};

inline bool stock_image_shared(const Slot& slot) {
  for (const auto& other : kSlots)
    if (other.stock_hash == slot.stock_hash && other.stock_tlut_hash != slot.stock_tlut_hash) return true;
  return false;
}

// Every retail texture that shows `kind` ("csp" or "stock") for the costume file `target_path`.
// More than one for Mr. Game & Watch, whose four selector cells are one costume file.
inline std::vector<Identity> identities(const std::string& kind, const std::string& target_path) {
  std::vector<Identity> out;
  const bool stock = kind == "stock";
  if (!stock && kind != "csp") return out;
  const std::string target = selector_target(target_path, stock);
  for (const auto& slot : kSlots) {
    if (target != slot.target) continue;
    if (!stock) { if (slot.csp_hash) out.push_back({slot.csp_hash, 0, false}); continue; }
    out.push_back({slot.stock_hash, slot.stock_tlut_hash, stock_image_shared(slot)});
  }
  return out;
}

// The parts of a Dolphin texture name that identify a portrait or a stock icon:
//   tex1_136x188[_m]_<image hash>[_<palette hash>]_<format>
//   tex1_24x24[_m]_<image hash>[_<palette hash>]_<format>
struct TextureName {
  bool ok = false, stock = false, has_tlut = false;
  uint64_t tex = 0, tlut = 0;
};

inline bool companion_hex64(const std::string& text, size_t at, uint64_t* out) {
  if (at + 16 > text.size()) return false;
  for (size_t i = 0; i < 16; ++i) {
    const char c = text[at + i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
  }
  *out = std::strtoull(text.substr(at, 16).c_str(), nullptr, 16);
  return true;
}

inline TextureName parse_texture_name(const std::string& base) {
  TextureName name;
  size_t at = 0;
  // The mipmapped spelling first: the plain prefix is also a prefix of it.
  static constexpr const char* portrait[] = {"tex1_136x188_m_", "tex1_136x188_"};
  static constexpr const char* icon[] = {"tex1_24x24_m_", "tex1_24x24_"};
  for (const char* prefix : portrait)
    if (!at && base.rfind(prefix, 0) == 0) at = std::string(prefix).size();
  if (!at) {
    for (const char* prefix : icon)
      if (!at && base.rfind(prefix, 0) == 0) at = std::string(prefix).size();
    if (!at) return name;
    name.stock = true;
  }
  if (!companion_hex64(base, at, &name.tex) || at + 16 >= base.size() || base[at + 16] != '_') return name;
  name.ok = true;
  const size_t second = at + 17;
  name.has_tlut = companion_hex64(base, second, &name.tlut) && second + 16 < base.size() && base[second + 16] == '_';
  if (!name.has_tlut) name.tlut = 0;
  return name;
}

}  // namespace gx::texpack::native_companions
