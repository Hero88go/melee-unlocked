// Immutable source bytes for deferred GX draws. No guest pointers escape capture.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gx_texture.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <unordered_map>

namespace gx {
struct TextureSnapshot {
  std::vector<uint8_t> image, palette;
  uint64_t hash;
};

class TextureSnapshotCache {
  std::unordered_multimap<uint64_t, std::shared_ptr<const TextureSnapshot>> entries;
  std::unordered_map<const uint8_t*, std::shared_ptr<const TextureSnapshot>> last_source;
  static bool equal(const TextureSnapshot& s, const uint8_t* image, size_t image_size,
                    const uint8_t* palette, size_t palette_size) {
    return s.image.size() == image_size && s.palette.size() == palette_size &&
        !std::memcmp(s.image.data(), image, image_size) &&
        (!palette_size || !std::memcmp(s.palette.data(), palette, palette_size));
  }
public:
  void clear() { entries.clear(); last_source.clear(); }
  std::shared_ptr<const TextureSnapshot> capture(const uint8_t* image, size_t image_size,
                                                const uint8_t* palette, size_t palette_size) {
    // Most adjacent draws reuse their source. Vectorized memcmp avoids rehashing
    // every byte with a serial hash recurrence; changes still receive a new copy.
    auto previous = last_source.find(image);
    if (previous != last_source.end() && equal(*previous->second, image, image_size, palette, palette_size))
      return previous->second;
    uint64_t hash = hash_bytes(image, image_size) ^ (hash_bytes(palette, palette_size) * 31);
    auto range = entries.equal_range(hash);
    for (auto i = range.first; i != range.second; ++i) {
      const auto& s = i->second;
      if (equal(*s, image, image_size, palette, palette_size)) {
        last_source[image] = s;
        return s;
      }
    }
    auto s = std::make_shared<TextureSnapshot>();
    s->image.assign(image, image + image_size);
    if (palette_size) s->palette.assign(palette, palette + palette_size);
    s->hash = hash;
    entries.emplace(hash, s);
    last_source[image] = s;
    return s;
  }
};

inline uint32_t texture_mip_count(uint32_t width, uint32_t height, uint32_t requested) {
  uint32_t maximum = 1;
  for (uint32_t d = std::max(width, height); d > 1; d >>= 1) ++maximum;
  return std::max(1u, std::min(requested, maximum));
}
inline uint32_t texture_chain_bytes(uint32_t w, uint32_t h, uint32_t format, uint32_t levels) {
  uint32_t total = 0;
  for (uint32_t i = 0; i < levels; ++i) {
    total += texture_level_bytes(w, h, format);
    w = std::max(1u, w / 2); h = std::max(1u, h / 2);
  }
  return total;
}
} // namespace gx
