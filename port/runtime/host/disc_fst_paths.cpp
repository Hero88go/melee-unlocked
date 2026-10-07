#include "disc_fst_paths.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace host::disc_fst {
namespace {
uint32_t be32(const std::vector<uint8_t>& bytes, size_t at) {
  if (at + 4 > bytes.size()) return 0;
  return ((uint32_t)bytes[at] << 24) | ((uint32_t)bytes[at + 1] << 16) |
         ((uint32_t)bytes[at + 2] << 8) | bytes[at + 3];
}

template <typename File>
bool visit_files(const std::vector<uint8_t>& fst, File visit) {
  if (fst.size() < 12) return false;
  const uint32_t entries = be32(fst, 8);
  const uint64_t string_offset = (uint64_t)entries * 12;
  if (!entries || string_offset > fst.size()) return false;
  std::vector<std::pair<uint32_t, std::string>> dirs{{entries, ""}};
  for (uint32_t i = 1; i < entries; ++i) {
    while (dirs.size() > 1 && i >= dirs.back().first) dirs.pop_back();
    const size_t entry_at = (size_t)i * 12;
    const uint32_t entry = be32(fst, entry_at);
    const uint32_t name_offset = entry & 0xFFFFFFu;
    const uint64_t at = string_offset + name_offset;
    if (at >= fst.size()) return false;
    const char* name = (const char*)fst.data() + at;
    const size_t available = fst.size() - (size_t)at;
    const auto* end = (const char*)std::memchr(name, 0, available);
    if (!end) return false;
    std::string full = dirs.back().second + "/" + std::string(name, (size_t)(end - name));
    std::transform(full.begin(), full.end(), full.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    if (entry >> 24) {
      const uint32_t end_entry = be32(fst, entry_at + 8);
      if (end_entry <= i || end_entry > entries) return false;
      dirs.push_back({end_entry, std::move(full)});
    } else if (!visit(full, be32(fst, entry_at + 4))) {
      return true;
    }
  }
  return true;
}
}  // namespace

bool path_by_offset(const std::vector<uint8_t>& fst, uint32_t offset, std::string* path) {
  bool found = false;
  const bool valid = visit_files(fst, [&](const std::string& name, uint32_t start) {
    if (start != offset) return true;
    if (path) *path = name;
    found = true;
    return false;
  });
  return valid && found;
}

bool music_paths(const std::vector<uint8_t>& fst, std::vector<std::string>* paths) {
  if (!paths) return false;
  paths->clear();
  const bool valid = visit_files(fst, [&](const std::string& name, uint32_t) {
    if (name.rfind("/audio/", 0) == 0 && name.size() >= 4 &&
        name.compare(name.size() - 4, 4, ".hps") == 0)
      paths->push_back(name);
    return true;
  });
  if (!valid) { paths->clear(); return false; }
  std::sort(paths->begin(), paths->end());
  return true;
}
}  // namespace host::disc_fst
