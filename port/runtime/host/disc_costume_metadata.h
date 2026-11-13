// Read-only costume and portrait metadata from m-ex archives. No mod code is executed.
// Layouts: akaneia/mexTool Core/MEX.cs and Ploaj/HSDLib HSDRaw/MEX.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace host::cosmetics::disc_metadata {

class Archive {
  const std::vector<uint8_t>& bytes;
  uint32_t length = 0;
  std::set<uint32_t> pointers;
  std::map<std::string, uint32_t> symbols;
public:
  explicit Archive(const std::vector<uint8_t>& source) : bytes(source) {}
  bool bounds(uint64_t at, uint64_t count) const { return at <= length && count <= length - at; }
  uint32_t word(uint64_t at) const {
    const uint8_t* p = bytes.data() + 32 + at;
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
  }
  uint8_t byte(uint64_t at) const { return bytes[32 + at]; }
  bool pointer(uint64_t field, uint32_t* target) const {
    if (!bounds(field, 4) || !pointers.count(uint32_t(field))) return false;
    *target = word(field);
    return bounds(*target, 1);
  }
  bool root(const char* name, uint32_t* target) const {
    const auto it = symbols.find(name);
    if (it == symbols.end()) return false;
    *target = it->second;
    return true;
  }
  bool string(uint32_t at, std::string* out) const {
    out->clear();
    while (bounds(at, 1) && out->size() <= 255) {
      const uint8_t c = byte(at++);
      if (!c) return !out->empty();
      if (c < 32 || c > 126) return false;
      out->push_back(char(c));
    }
    return false;
  }
  bool parse() {
    if (bytes.size() < 32 || bytes.size() > 64ull * 1024 * 1024) return false;
    auto raw = [&](uint64_t at) {
      const uint8_t* p = bytes.data() + at;
      return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
    };
    if (raw(0) != bytes.size()) return false;
    length = raw(4);
    const uint64_t relocations = raw(8), roots = raw(12), references = raw(16);
    const uint64_t table = 32ull + length + relocations * 4;
    const uint64_t names = table + (roots + references) * 8;
    if (relocations > length / 4 || roots > 1024 || references > 1024 || names > bytes.size()) return false;
    for (uint64_t i = 0; i < relocations; ++i) {
      const uint32_t field = raw(32ull + length + i * 4);
      if ((field & 3) || !bounds(field, 4) || !bounds(word(field), 1) || !pointers.insert(field).second) return false;
    }
    for (uint64_t i = 0; i < roots; ++i) {
      const uint32_t target = raw(table + i * 8);
      const uint64_t at = names + raw(table + i * 8 + 4);
      if (!bounds(target, 1) || at >= bytes.size()) return false;
      const char* name = reinterpret_cast<const char*>(bytes.data() + at);
      const size_t n = strnlen(name, bytes.size() - size_t(at));
      if (!n || n > 255 || n == bytes.size() - at || !symbols.emplace(std::string(name, n), target).second) return false;
    }
    return true;
  }
};

struct MexCostume {
  std::string family;
  uint32_t external = 0, costume = 0;
  uint32_t internal = 0;   // m-ex internal fighter id (its stock icon column)
  std::string joint, matanim;
  uint32_t visibility = 0;
};
using MexCostumes = std::map<std::string, MexCostume>;  // lower-case file name -> explicit costume row

inline bool mex_costumes(const std::vector<uint8_t>& bytes, MexCostumes* out) {
  out->clear();
  Archive dat(bytes);
  uint32_t root, meta, fighter, mapping, info, files;
  if (!dat.parse() || !dat.root("mexData", &root) || !dat.bounds(root, 12) ||
      !dat.pointer(root, &meta) || !dat.bounds(meta, 12) || dat.byte(meta) != 1 ||
      !dat.pointer(root + 8, &fighter) || !dat.bounds(fighter, 24) ||
      !dat.pointer(fighter + 12, &mapping) || !dat.pointer(fighter + 16, &info) ||
      !dat.pointer(fighter + 20, &files)) return false;
  const uint32_t internal_count = dat.word(meta + 4), external_count = dat.word(meta + 8);
  if (!internal_count || internal_count > 512 || !external_count || external_count > 512 ||
      !dat.bounds(mapping, uint64_t(external_count) * 3) || !dat.bounds(info, uint64_t(external_count) * 4) ||
      !dat.bounds(files, uint64_t(internal_count) * 4)) return false;
  // m-ex external IDs include Sheik at 19; the retail portrait animation omits her column.
  static constexpr const char* codes[] = {"Ca", "Dk", "Fx", "Gw", "Kb", "Kp", "Lk", "Lg", "Mr", "Ms", "Mt", "Ns", "Pe",
                                        "Pk", "Pp", "Pr", "Ss", "Ys", "Zd", "Sk", "Fc", "Cl", "Dr", "Fe", "Pc", "Gn"};
  static constexpr uint8_t kinds[] = {2, 3, 1, 24, 4, 5, 6, 17, 0, 18, 16, 8, 9, 12, 10, 15, 13, 14, 19, 7, 22, 20, 21, 26, 23, 25};
  MexCostumes found;
  std::set<std::string> ambiguous;
  for (uint32_t ext = 0; ext < external_count && ext < 26; ++ext) {
    const uint32_t kind = dat.byte(mapping + ext * 3), count = dat.byte(info + ext * 4);
    if (kind != kinds[ext] || kind >= internal_count || !count) continue;  // added/replaced fighters need their own code
    uint32_t list;
    if (count > 64 || !dat.pointer(files + kind * 4, &list) || !dat.bounds(list, uint64_t(count) * 16)) return false;
    for (uint32_t costume = 0; costume < count; ++costume) {
      uint32_t name;
      std::string file;
      if (!dat.pointer(list + costume * 16, &name) || !dat.string(name, &file)) return false;
      // Names select a file already found in this pack; never open a path supplied by the archive.
      if (file.size() < 7 || file.find_first_of("/\\:") != std::string::npos || file.find("..") != std::string::npos) continue;
      for (char& c : file) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
      MexCostume row{codes[ext], ext, costume};
      row.internal = kind;
      row.visibility = dat.word(list + costume * 16 + 12);
      for (const auto field : {std::make_pair(4u, &row.joint), std::make_pair(8u, &row.matanim)}) {
        uint32_t symbol;
        if (dat.pointer(list + costume * 16 + field.first, &symbol) && !dat.string(symbol, field.second)) return false;
      }
      const auto inserted = found.emplace(file, std::move(row));
      if (!inserted.second && (inserted.first->second.external != ext || inserted.first->second.costume != costume)) ambiguous.insert(file);
    }
  }
  for (const auto& file : ambiguous) found.erase(file);
  *out = std::move(found);
  return true;
}

// Only the archive's exported names change. Data, model objects, relocations and external
// references keep their original values; the source ISO is never modified.
inline bool alias_roots(const std::vector<uint8_t>& bytes, const std::map<std::string, std::string>& aliases,
                        std::vector<uint8_t>* out) {
  out->clear();
  Archive dat(bytes);
  if (aliases.empty() || aliases.size() > 2 || !dat.parse()) return false;
  auto raw = [&](size_t at) {
    const uint8_t* p = bytes.data() + at;
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
  };
  const uint64_t roots = raw(12), references = raw(16);
  const uint64_t table = 32ull + raw(4) + uint64_t(raw(8)) * 4, names = table + (roots + references) * 8;
  std::vector<uint8_t> rewritten(bytes.begin(), bytes.begin() + size_t(names));
  auto put = [&](size_t at, uint32_t value) {
    for (int i = 0; i < 4; ++i) rewritten[at + i] = uint8_t(value >> (24 - i * 8));
  };
  std::set<std::string> exports, used;
  for (uint64_t i = 0; i < roots + references; ++i) {
    const uint64_t at = names + raw(size_t(table + i * 8 + 4));
    if (at >= bytes.size()) return false;
    const char* name = reinterpret_cast<const char*>(bytes.data() + at);
    const size_t length = strnlen(name, bytes.size() - size_t(at));
    if (!length || length > 255 || length == bytes.size() - at) return false;
    std::string symbol(name, length);
    const auto alias = aliases.find(symbol);
    if (i < roots && alias != aliases.end()) { used.insert(symbol); symbol = alias->second; }
    if (symbol.empty() || symbol.size() > 255 || (i < roots && !exports.insert(symbol).second)) return false;
    for (unsigned char c : symbol) if (c < 32 || c > 126) return false;
    put(size_t(table + i * 8 + 4), uint32_t(rewritten.size() - names));
    rewritten.insert(rewritten.end(), symbol.begin(), symbol.end()); rewritten.push_back(0);
  }
  if (used.size() != aliases.size() || rewritten.size() > 64ull * 1024 * 1024) return false;
  put(0, uint32_t(rewritten.size()));
  *out = std::move(rewritten);
  return true;
}

}  // namespace host::cosmetics::disc_metadata
