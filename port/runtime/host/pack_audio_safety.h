// Bounded HPS block-chain validation for local music from ISO overlays.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace source_port::skins {
inline bool is_music_file(const std::string& path) {
  return path.rfind("/audio/", 0) == 0 && path.size() > 11 &&
         path.compare(path.size() - 4, 4, ".hps") == 0;
}
inline bool is_stage_file(const std::string& path) {
  return path.rfind("/gr", 0) == 0 && path.find('/', 1) == std::string::npos &&
         path.size() > 7 && path.compare(path.size() - 4, 4, ".dat") == 0;
}
inline bool music_is_valid(const std::vector<uint8_t>& bytes, std::string* reason) {
  auto fail = [&](const char* text) { if (reason) *reason = text; return false; };
  if (bytes.size() < 0xb0 || bytes.size() > 64u * 1024 * 1024 ||
      std::memcmp(bytes.data(), " HALPST\0", 8)) return fail("invalid HPS header or size");
  auto word = [&](uint32_t at) {
    const uint8_t* p = bytes.data() + at;
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
  };
  const uint32_t rate = word(8) ? word(8) : 32000;
  if (rate < 8000 || rate > 192000 || word(12) != 2) return fail("unsupported HPS format");
  std::set<uint32_t> blocks;
  uint32_t offset = 0x80;
  uint64_t frames = 0;
  while (uint64_t(offset) + 0x20 <= bytes.size()) {
    if (!blocks.insert(offset).second) return fail("invalid HPS loop");
    const uint32_t length = word(offset), next = word(offset + 8);
    const uint64_t end = uint64_t(offset) + 0x20 + length;
    if (!length || length % 16 || end > bytes.size()) return fail("invalid HPS block");
    frames += uint64_t(length / 16) * 14;
    if (frames * 32000 / rate > 32000ull * 60 * 20) return fail("HPS track exceeds 20 minutes");
    if (next == 0xffffffffu || blocks.count(next)) {
      if (reason) *reason = "local music; valid HPS blocks";
      return true;
    }
    if (next < end || uint64_t(next) + 0x20 > bytes.size()) return fail("invalid HPS next block");
    offset = next;
  }
  return fail("truncated HPS blocks");
}
}  // namespace source_port::skins
