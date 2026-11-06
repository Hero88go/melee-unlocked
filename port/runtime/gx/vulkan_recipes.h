// The Vulkan renderer's pipeline recipes (gx_vulkan.cpp prewarm_pipelines): which pipelines to build
// before the first frame, as the two shader keys, the hashes naming their SPIR-V files in the cache and
// the packed blend/depth/cull/sample state. The player's own file and the one a release ships
// (vk_recipes.shipped.bin) are read together, each pipeline once. Header-only and free of Vulkan, so
// it is unit-tested (tests/vulkan_recipes_test.cpp).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <vector>

namespace gx::vk_recipes {

struct Recipe { uint64_t vs, ps, vs_spv, ps_spv; uint32_t state, pad; };
static_assert(sizeof(Recipe) == 40, "the file layout is the struct's");

constexpr size_t kMax = 16384;
constexpr uint32_t kMagic = 0x564B5231u;   // "VKR1"

// A file's recipes; empty when it is missing, has another magic, claims too many, or is cut short.
inline std::vector<Recipe> read(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary);
  uint32_t head[2]{};
  if (!f.read(reinterpret_cast<char*>(head), sizeof head) || head[0] != kMagic || head[1] > kMax) return {};
  std::vector<Recipe> out(head[1]);
  if (!out.empty() && !f.read(reinterpret_cast<char*>(out.data()), (std::streamsize)(out.size() * sizeof(Recipe)))) return {};
  return out;
}

// Writes at most kMax recipes. False when the file could not be written.
inline bool write(const std::filesystem::path& path, const std::vector<Recipe>& recipes) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  const uint32_t head[2] = {kMagic, (uint32_t)(recipes.size() < kMax ? recipes.size() : kMax)};
  f.write(reinterpret_cast<const char*>(head), sizeof head);
  f.write(reinterpret_cast<const char*>(recipes.data()), (std::streamsize)(head[1] * sizeof(Recipe)));
  return (bool)f;
}

// One pipeline: its two shaders and its state (the SPIR-V hashes follow from the shaders).
struct Key {
  uint64_t vs, ps; uint32_t state;
  bool operator==(const Key& o) const { return vs == o.vs && ps == o.ps && state == o.state; }
};
struct KeyHash { size_t operator()(const Key& k) const { return size_t(k.vs ^ (k.ps * 31) ^ (uint64_t(k.state) << 7)); } };

// The files in order, each pipeline once (the first file's copy of it), at most kMax.
inline std::vector<Recipe> merge(const std::vector<std::filesystem::path>& files) {
  std::vector<Recipe> out;
  std::unordered_set<Key, KeyHash> seen;
  for (const auto& file : files)
    for (const Recipe& r : read(file))
      if (out.size() < kMax && seen.insert({r.vs, r.ps, r.state}).second) out.push_back(r);
  return out;
}

}  // namespace gx::vk_recipes
