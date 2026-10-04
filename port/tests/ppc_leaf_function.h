// One translated guest function and its comparison against the hand-written reference, in the
// worlds of ppc_leaf_worlds.h: world 0 runs the translated native code, world 1 the reference.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ppc_leaf_worlds.h"
#include "ppc_leaf_stencils.generated.h"

namespace worlds {
inline int failures = 0;
inline uint64_t comparisons = 0, set_aside = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++worlds::failures; } } while (0)
struct Function {
  ppc::stencil::CompiledLeaf leaf;
  std::vector<uint32_t> words;
  std::vector<uint8_t> code;
  uint32_t address = ppc::RAM_BASE + 0x3000;
  // What the planner and the reference are told about callees: (address, computed-return delta).
  std::vector<std::pair<uint32_t, uint32_t>> resumes;
  void dump() const { for (uint32_t word : words) std::printf("  %08X\n", word); }
  bool translate(std::vector<uint32_t> guest, uint32_t at = ppc::RAM_BASE + 0x3000) {
    words = std::move(guest);
    address = at;
    code = reference::bytes(words);
    std::string error;
    std::vector<ppc::stencil::Callee> callees;
    for (const auto& item : resumes) callees.push_back({item.first, {item.second}});
    if (!ppc::stencil::translate_leaf(code.data(), code.size(), address, ppc::stencil::generated::table, leaf, error,
                                      callees.empty() ? nullptr : &callees)) {
      std::printf("FAIL: function does not translate: %s\n", error.c_str());
      dump();
      ++failures;
      return false;
    }
    return true;
  }
  // Native code in world 0 and nothing else; for known answers.
  bool run_native(ppc::Context& c) {
    install(address, code);
    run(0, c, [&](ppc::Context& context, uint8_t* ram) { return leaf.run(context, ram); });
    if (g_count > 1) {
      run(1, c, [&](ppc::Context& context, uint8_t* ram) { return reference::run(context, ram, words, address, 1u << 22, &resumes); });
      const char* what = difference();
      if (what) { std::printf("FAIL: known answer differs from the reference in %s\n", what); dump(); ++failures; }
    }
    std::memcpy(&c, &g_world[0].result.context, sizeof c);
    return g_world[0].result.ok;
  }
  // `skip_open_nan`: for functions of several instructions, a sample in which the reference met a
  // NaN result that C++ leaves to the compiler is set aside (and counted) instead of compared.
  bool compare(const ppc::Context& start, const char* name, bool skip_open_nan = false) {
    install(address, code);
    run(0, start, [&](ppc::Context& context, uint8_t* ram) { return leaf.run(context, ram); });
    reference::g_nan_order_open = false;
    run(1, start, [&](ppc::Context& context, uint8_t* ram) { return reference::run(context, ram, words, address, 1u << 22, &resumes); });
    if (reference::g_nan_order_open && skip_open_nan) {
      // The worlds may have written different NaNs to RAM: bring world 1 back in step.
      ++set_aside;
      resync();
      return true;
    }
    ++comparisons;
    g_accept_either_nan = reference::g_nan_order_open;
    const char* what = difference();
    g_accept_either_nan = false;
    if (!what) return true;
    std::printf("FAIL: %s differs in %s (native %d, reference %d)\n", name, what,
                g_world[0].result.ok, g_world[1].result.ok);
    dump();
    reference::describe(g_world[0].result.context, g_world[1].result.context);
    ++failures;
    return false;
  }
};
} // namespace worlds
