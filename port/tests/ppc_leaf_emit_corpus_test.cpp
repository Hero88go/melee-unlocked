// Translated native code versus the recompiler's own emitted C++ for the same guest words.
// The corpus header is written at build time by tools/ppc_stencils/generate_emit_corpus.py, which
// runs the unmodified port/recomp/emit.py. Each function also runs through the hand-written
// reference, so the reference used by the larger random tests is itself checked against emit.py.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "ppc_leaf_reference.h"
#include "ppc_leaf_stencils.generated.h"
#include "ppc_emit_corpus.generated.h"
#include <array>

namespace {
using namespace reference;
struct Polls { uint64_t count, digest; };
void reset_polls() { ppc::g_poll_count = 0; ppc::g_poll_digest = 1469598103934665603ull; }
Polls polls() { return {ppc::g_poll_count, ppc::g_poll_digest}; }
bool same(const Polls& a, const Polls& b) { return a.count == b.count && a.digest == b.digest; }
} // namespace

int main() {
  Random random(0x2026100100000003ull);
  static std::array<uint8_t, 8192> ram{};
  uint64_t comparisons = 0, polled = 0, total_polls = 0, words = 0;
  size_t functions = 0;
  for (const auto& function : emit_corpus::functions) {
    const std::vector<uint32_t> guest(function.words, function.words + function.count);
    const auto code = bytes(guest);
    const size_t at = function.address - ppc::RAM_BASE;
    if (at + code.size() > ram.size()) { std::printf("FAIL: corpus function outside the test RAM\n"); return 1; }
    ram.fill(0);
    std::memcpy(ram.data() + at, code.data(), code.size());
    ppc::stencil::CompiledLeaf leaf;
    std::string error;
    if (!ppc::stencil::translate_leaf(code.data(), code.size(), function.address,
                                      ppc::stencil::generated::table, leaf, error)) {
      std::printf("FAIL: %s function %zu does not translate: %s\n", function.kind, functions, error.c_str());
      for (uint32_t word : guest) std::printf("  %08X\n", word);
      return 1;
    }
    const bool branching = std::strcmp(function.kind, "branching") == 0;
    for (int sample = 0; sample < 48; ++sample) {
      ppc::Context native, emitted, stepped;
      randomize(native, random);
      // Half of the branching samples start just below the poll interval, so short loops poll too.
      if (branching && (sample & 1)) native.backedges = (random.u32() & ~0x3FFu) | (0x3FFu - random.below(6));
      std::memcpy(&emitted, &native, sizeof native);
      std::memcpy(&stepped, &native, sizeof native);
      const auto before_ram = ram;
      reset_polls();
      function.fn(emitted, ram.data());
      const Polls emitted_polls = polls();
      reset_polls();
      const bool stepped_ok = run(stepped, guest, function.address);
      const Polls stepped_polls = polls();
      reset_polls();
      const bool native_ok = leaf.run(native, ram.data());
      const Polls native_polls = polls();
      ++comparisons;
      if (emitted_polls.count) { ++polled; total_polls += emitted_polls.count; }
      if (!native_ok || !stepped_ok || !same(native, emitted) || !same(stepped, emitted) ||
          !same(native_polls, emitted_polls) || !same(stepped_polls, emitted_polls) || ram != before_ram) {
        std::printf("FAIL: %s function %zu sample %d (native %d, reference %d, polls %llu/%llu/%llu)\n",
                    function.kind, functions, sample, native_ok, stepped_ok,
                    (unsigned long long)native_polls.count, (unsigned long long)stepped_polls.count,
                    (unsigned long long)emitted_polls.count);
        for (uint32_t word : guest) std::printf("  %08X\n", word);
        std::printf(" native versus emit.py:\n"); describe(native, emitted);
        std::printf(" reference versus emit.py:\n"); describe(stepped, emitted);
        return 1;
      }
    }
    ++functions;
    words += function.count;
  }
  std::printf("leaf emit corpus: %zu functions (%llu guest words) emitted by emit.py, %llu three-way "
              "full-context comparisons passed; %llu runs polled, %llu polls in all; corpus %s\n",
              functions, (unsigned long long)words, (unsigned long long)comparisons,
              (unsigned long long)polled, (unsigned long long)total_polls, emit_corpus::sha256);
  return 0;
}
