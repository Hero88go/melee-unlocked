// Translated native code versus the recompiler's own emitted C++ for the same guest words.
// The corpus header is written at build time by tools/ppc_stencils/generate_emit_corpus.py, which
// runs the unmodified port/recomp/emit.py. Each function also runs through the hand-written
// reference, so the reference used by the larger random tests is itself checked against emit.py.
// Three worlds (ppc_leaf_worlds.h): 0 native, 1 emit.py's C++, 2 the reference.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "ppc_leaf_worlds.h"
#include "ppc_leaf_stencils.generated.h"
#include "ppc_emit_corpus.generated.h"

int main() {
  using namespace reference;
  Random random(0x2026100100000003ull);
  if (!worlds::init(3, random)) return 1;
  static const unsigned modes[] = {0x1F80, 0x7F80, 0x5F80, 0x3F80, 0x9FC0};
  uint64_t comparisons = 0, polled = 0, total_polls = 0, words = 0, set_aside = 0, resumed = 0, local_sites = 0;
  size_t functions = 0;
  // What analyze.py would know about the stand-in callees with a computed return.
  std::vector<ppc::stencil::Callee> callees;
  std::vector<std::pair<uint32_t, uint32_t>> resumes;
  for (uint32_t i = 0; i < host::kResumeCount; ++i) {
    const uint32_t address = host::kResumeBase + 4 * i;
    callees.push_back({address, {host::resume_delta(address)}});
    resumes.push_back({address, host::resume_delta(address)});
  }
  for (const auto& function : emit_corpus::functions) {
    const std::vector<uint32_t> guest(function.words, function.words + function.count);
    const auto code = bytes(guest);
    ppc::stencil::CompiledLeaf leaf;
    std::string error;
    if (!ppc::stencil::translate_leaf(code.data(), code.size(), function.address,
                                      ppc::stencil::generated::table, leaf, error, &callees)) {
      std::printf("FAIL: %s function %zu does not translate: %s\n", function.kind, functions, error.c_str());
      for (uint32_t word : guest) std::printf("  %08X\n", word);
      return 1;
    }
    const bool branching = std::strcmp(function.kind, "branching") == 0;
    if ((functions & 31) == 0) worlds::watch(random, 3);
    worlds::g_start_mxcsr = modes[functions % 5];
    for (int sample = 0; sample < 32; ++sample) {
      ppc::Context start;
      randomize_rich(start, random);
      // Half of the branching samples start just below the poll interval, so short loops poll too.
      if (branching && (sample & 1)) start.backedges = (random.u32() & ~0x3FFu) | (0x3FFu - random.below(6));
      worlds::install(function.address, code);
      worlds::run(0, start, [&](ppc::Context& c, uint8_t* m) { return leaf.run(c, m); });
      worlds::run(1, start, [&](ppc::Context& c, uint8_t* m) { function.fn(c, m); return true; });
      g_nan_order_open = false;
      worlds::run(2, start, [&](ppc::Context& c, uint8_t* m) { return run(c, m, guest, function.address, 1u << 22, &resumes); });
      // Two NaN operands of an addition, a multiplication or a fused multiply-add: C++ leaves the
      // resulting NaN to the compiler (ppc_leaf_reference.h). One instruction: either NaN is
      // accepted in the float registers. A longer function: the sample is set aside.
      if (g_nan_order_open && function.count > 2) { ++set_aside; worlds::resync(); continue; }
      worlds::g_accept_either_nan = g_nan_order_open;
      const char* what = worlds::difference();
      worlds::g_accept_either_nan = false;
      ++comparisons;
      const auto& emitted = worlds::g_world[1].result;
      if (emitted.poll_count) { ++polled; total_polls += emitted.poll_count; }
      resumed += emitted.resumed;
      if (emitted.fatals || worlds::g_world[0].result.fatals) what = "a return into inline data";
      if (what) {
        std::printf("FAIL: %s function %zu sample %d differs in %s (native %d, reference %d)\n",
                    function.kind, functions, sample, what, worlds::g_world[0].result.ok, worlds::g_world[2].result.ok);
        for (uint32_t word : guest) std::printf("  %08X\n", word);
        std::printf(" native versus emit.py:\n"); describe(worlds::g_world[0].result.context, emitted.context);
        std::printf(" reference versus emit.py:\n"); describe(worlds::g_world[2].result.context, emitted.context);
        return 1;
      }
    }
    ++functions;
    words += function.count;
    for (uint32_t word : guest) local_sites += ((word >> 26) == 18 || (word >> 26) == 16) && (word & 1) &&
        std::strcmp(function.kind, "local") == 0;
  }
  std::printf("leaf emit corpus: %zu functions (%llu guest words) emitted by emit.py, %llu three-way "
              "full-context comparisons passed; %llu runs polled, %llu polls in all; %llu host calls compared; "
              "%llu call sites in the functions with local calls, %llu returns resumed past their call; "
              "%llu samples set aside and either NaN accepted in %llu for a NaN result the compiler chooses; corpus %s\n",
              functions, (unsigned long long)words, (unsigned long long)comparisons,
              (unsigned long long)polled, (unsigned long long)total_polls, (unsigned long long)worlds::g_host_events,
              (unsigned long long)local_sites, (unsigned long long)resumed,
              (unsigned long long)set_aside, (unsigned long long)worlds::g_either_nan_accepted, emit_corpus::sha256);
  return 0;
}
