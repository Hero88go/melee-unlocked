// Regenerate the recorded shader seed for a different EFB scale, without SDL/GL or the game.
// Never relabel scale-1 GLSL: the fragment-coordinate correction depends on EFB height.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "../linux/shader_translation.h"
#include "gx_shader.h"
#include "gx_texture.h"
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unordered_set>
struct ProgramRecipe { uint32_t components = 0; gx::BPMemory bp{}; uint32_t xf[0x58]{}; };
int main(int argc, char** argv) {
  try {
    if (argc != 5) throw std::runtime_error("usage: seed-build <source-seed> <output-seed> <scale> <tool-dir>");
    const std::filesystem::path source = argv[1], output = argv[2], tools = argv[4];
    const int scale = std::stoi(argv[3]);
    if (scale < 1 || scale > 6) throw std::runtime_error("scale must be 1..6");
    const auto data = shader_translation::read_text(source / "gx-gl410-v2-s1/recipes.bin");
    uint64_t header[3]{};
    if (data.size() < sizeof header) throw std::runtime_error("missing seed recipes");
    std::memcpy(header, data.data(), sizeof header);
    if (header[0] != 0x3150524C47505847ull || header[1] > 16384 ||
        data.size() != sizeof header + header[1] * sizeof(ProgramRecipe) ||
        gx::hash_bytes(data.data() + sizeof header, data.size() - sizeof header) != header[2])
      throw std::runtime_error("invalid seed recipes/ABI/hash");
    const auto cache = output / ("gx-gl410-v2-s" + std::to_string(scale));
    std::filesystem::create_directories(cache);
    shader_translation::Queue queue;
    std::vector<std::shared_ptr<shader_translation::Result>> results;
    std::unordered_set<std::string> names;
    size_t source_hits = 0;
    for (size_t i = 0; i < header[1]; ++i) {
      ProgramRecipe recipe{};
      std::memcpy(&recipe, data.data() + sizeof header + i * sizeof recipe, sizeof recipe);
      gx::DrawCall dc{}; dc.components = recipe.components; dc.bp = recipe.bp;
      std::memcpy(dc.xf_regs, recipe.xf, sizeof recipe.xf);
      for (bool vertex : {true, false}) {
        const auto hlsl = vertex ? gx::generate_vertex_shader(gx::make_vs_uid(dc)) : gx::generate_pixel_shader(gx::make_ps_uid(dc));
        std::ostringstream name; name << (vertex ? "vs-" : "ps-") << std::hex << gx::hash_bytes(hlsl.data(), hlsl.size());
        if (!names.insert(name.str()).second) continue;
        if (std::filesystem::is_regular_file(source / "gx-gl410-v2-s1" / (name.str() + ".glsl"))) ++source_hits;
        results.push_back(queue.request(cache / (name.str() + ".glsl"), [=, shader_name = name.str()] {
          return shader_translation::translate(cache, shader_name, vertex, gx::EFB_HEIGHT * scale, hlsl, tools);
        }));
      }
    }
    for (const auto& result : results) {
      while (result->state.load(std::memory_order_acquire) == shader_translation::Result::Pending)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      if (result->state.load(std::memory_order_acquire) != shader_translation::Result::Ready)
        throw std::runtime_error(result->error);
    }
    queue.stop();
    shader_translation::write_text(cache / "recipes.bin", data);
    std::cout << "Seed scale " << scale << ": " << results.size() << " shaders, " << header[1]
              << " recipes; " << source_hits << " generated shader hashes found in source seed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
