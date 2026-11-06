// The Vulkan renderer's pipeline recipe files (gx/vulkan_recipes.h): the player's own file and the
// one a release ships are read together, each pipeline once, and damaged files are ignored.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "vulkan_recipes.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

static int failures = 0;
static void check(bool ok, const char* what) {
  if (!ok) { std::printf("FAIL %s\n", what); ++failures; }
}

int main() {
  namespace fs = std::filesystem;
  using namespace gx::vk_recipes;
  std::error_code ec;
  const fs::path dir = fs::temp_directory_path(ec) / ("mu-vk-recipes-" + std::to_string((unsigned long long)fs::file_time_type::clock::now().time_since_epoch().count()));
  fs::create_directories(dir, ec);
  const fs::path own = dir / "vk_recipes.bin", shipped = dir / "vk_recipes.shipped.bin";

  const std::vector<Recipe> a = {{1, 2, 11, 12, 0x80000u, 0}, {3, 4, 13, 14, 0x80001u, 0}, {5, 6, 15, 16, 0x100000u, 0}};
  const std::vector<Recipe> b = {{3, 4, 99, 98, 0x80001u, 0}, {7, 8, 17, 18, 0x80000u, 0}, {1, 2, 11, 12, 0x100000u, 0}};
  check(write(own, a) && write(shipped, b), "files written");
  check(read(own).size() == 3, "a file reads back whole");
  {
    const std::vector<Recipe> m = merge({own, shipped});
    // (3,4,0x80001) is in both: the player's copy is kept; (1,2) with another sample count is another pipeline.
    check(m.size() == 5, "merged, each pipeline once");
    check(m.size() >= 4 && m[3].vs == 7, "the shipped file's new pipeline follows the player's");
    bool own_copy = false;
    for (const Recipe& r : m) if (r.vs == 3 && r.vs_spv == 13) own_copy = true;
    check(own_copy, "the first file's copy of a shared pipeline wins");
  }
  check(merge({dir / "absent.bin", shipped}).size() == 3, "a missing file is skipped");
  {
    std::ofstream(own, std::ios::binary | std::ios::trunc) << "not a recipe file at all";
    check(read(own).empty(), "another magic reads as empty");
    check(merge({own, shipped}).size() == 3, "a damaged file does not stop the other");
  }
  {
    std::vector<Recipe> big(kMax + 10, Recipe{0, 0, 0, 0, 0, 0});
    for (size_t i = 0; i < big.size(); ++i) big[i].vs = i + 1;
    check(write(own, big), "a large set is written");
    check(read(own).size() == kMax, "at most kMax recipes are kept");
  }
  {
    write(own, a);
    fs::resize_file(own, fs::file_size(own) - 8, ec);
    check(read(own).empty(), "a file cut short reads as empty");
  }
  fs::remove_all(dir, ec);
  if (failures) return 1;
  std::printf("vulkan recipes: all checks passed\n");
  return 0;
}
