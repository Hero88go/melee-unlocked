// Actual copied MSVC stencils versus the recompiler's expressions, including full Context bytes.
// No game, ISO, window, device or interpreter dependency.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "leaf_translator.h"
#include "ppc_leaf_host.h"
#include "leaf_translation_plan.h"
#include "ppc_leaf_stencils.generated.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <random>
#include <utility>
#include <vector>

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)
uint32_t dform(uint32_t opcode, uint32_t first, uint32_t second, uint32_t immediate) {
  return (opcode << 26) | (first << 21) | (second << 16) | immediate;
}
std::vector<uint8_t> bytes(const std::vector<uint32_t>& words) {
  std::vector<uint8_t> out;
  for (auto word : words) for (int shift : {24, 16, 8, 0}) out.push_back(uint8_t(word >> shift));
  return out;
}
// Fixed expressions match emit.py, independent of the prototype planner's decoded operand cells.
void reference(ppc::Context& c) {
  ppc::enter(c, ppc::RAM_BASE);
  c.r[3] = 0xFFFFFFFFu;
  c.r[4] = c.r[3] + 0x80000000u;
  c.r[5] = c.r[0] | 0x8000u;
  c.r[5] = c.r[5] | 0xFFFF0000u;
  c.r[6] = c.r[5] ^ 0xFFFFu;
  c.r[6] = c.r[6] ^ 0x80000000u;
  c.r[7] = c.r[6] & 0u; ppc::cr0(c, c.r[7]);
  c.r[8] = c.r[0] & 0x80000000u; ppc::cr0(c, c.r[8]);
  c.r[0] = 0xFFFF8000u;
  c.r[0] = c.r[0] | 1u;
}
} // namespace

int main() {
  using namespace ppc::stencil;
  std::string error;
  std::vector<Instruction> plan;
  const auto code = bytes({dform(14,3,0,0xFFFF), dform(15,4,3,0x8000),
                           dform(24,0,5,0x8000), dform(25,5,5,0xFFFF),
                           dform(26,5,6,0xFFFF), dform(27,6,6,0x8000),
                           dform(28,6,7,0), dform(29,0,8,0x8000),
                           dform(14,0,0,0x8000), dform(24,0,0,1), 0x4E800020});
  CHECK(plan_leaf(code.data(), code.size(), ppc::RAM_BASE, plan, error));
  CHECK(plan.size() == 11 && plan[0].immediate == 0xFFFFFFFFu && plan[1].immediate == 0x80000000u);
  CHECK(plan[2].source == 0 && plan[2].destination == 5 && plan[8].source == 0);
  // Primary opcodes with no accepted form at all; the extended ones are probed in the other tests.
  for (uint32_t op = 0; op < 64; ++op) {
    // With every field zero: update forms name r0 as their base and lmw loads its own base, so
    // those are refused; op 4 is ps_cmpu0 and op 63 fcmpu.
    const bool accepted = (op >= 7 && op <= 8) || (op >= 10 && op <= 16) || (op >= 18 && op <= 21) ||
                          (op >= 23 && op <= 29) || op == 31 || op == 4 || op == 63 || op == 47 || op == 56 ||
                          op == 60 || (op >= 32 && op <= 54 && !(op & 1) && op != 46);
    if (accepted) continue;
    const auto unsupported = bytes({op << 26, 0x4E800020});
    CHECK(!plan_leaf(unsupported.data(), unsupported.size(), ppc::RAM_BASE, plan, error));
    CHECK(plan.empty());
  }
  // A word no path reaches is not planned: here the nop after the return.
  const auto early = bytes({0x4E800020, 0x60000000});
  CHECK(plan_leaf(early.data(), early.size(), ppc::RAM_BASE, plan, error) && plan.size() == 1);
  CHECK(!plan_leaf(code.data(), code.size()-4, ppc::RAM_BASE, plan, error));
  CHECK(!plan_leaf(nullptr, 4, ppc::RAM_BASE, plan, error));
  CHECK(!plan_leaf(code.data(), 0, ppc::RAM_BASE, plan, error));
  CHECK(!plan_leaf(code.data(), code.size(), ppc::RAM_BASE+1, plan, error));
  CHECK(!plan_leaf(code.data(), code.size(), ppc::RAM_BASE+ppc::RAM_SIZE-4, plan, error));
  CHECK(!plan_leaf(code.data(), kMaxFunctionBytes + 4, ppc::RAM_BASE, plan, error));

  std::array<uint8_t,4096> ram{};
  std::memcpy(ram.data(), code.data(), code.size());
  CompiledLeaf compiled;
  CHECK(translate_leaf(code.data(), code.size(), ppc::RAM_BASE, generated::table, compiled, error));
  if (!compiled.ready()) { std::printf("translation error: %s\n", error.c_str()); return 1; }
  std::mt19937 random(0x20261001);
  for (int sample = 0; sample < 10000; ++sample) {
    ppc::Context actual{};
    // memcpy avoids writing Context's aliasing unions through an incompatible pointer.
    auto* raw = reinterpret_cast<uint8_t*>(&actual);
    for (size_t i = 0; i < sizeof actual; ++i) raw[i] = uint8_t(random());
    actual.entry = 0; actual.so &= 1;
    ppc::Context expected;
    std::memcpy(&expected, &actual, sizeof actual);
    const auto before_ram = ram;
    reference(expected);
    CHECK(compiled.run(actual, ram.data()));
    CHECK(std::memcmp(&actual, &expected, sizeof actual) == 0);
    CHECK(ram == before_ram);
    if (failures) break;
  }
  // Each CR0 outcome survives to the comparison, with RS=RA=31 and both XER.SO values.
  // The longer chain above overwrites its first record result before the final comparison.
  unsigned record_comparisons = 0;
  for (uint32_t op : {28u, 29u}) {
    for (uint32_t input : {0u, 1u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu}) {
      for (uint32_t so : {0u, 1u}) {
        const auto record_code = bytes({dform(op,31,31,0xFFFF), 0x4E800020});
        CompiledLeaf record_leaf;
        CHECK(translate_leaf(record_code.data(), record_code.size(), ppc::RAM_BASE,
                             generated::table, record_leaf, error));
        if (!record_leaf.ready()) return 1;
        auto record_ram = ram;
        std::memcpy(record_ram.data(), record_code.data(), record_code.size());
        ppc::Context actual{};
        auto* raw = reinterpret_cast<uint8_t*>(&actual);
        for (size_t i = 0; i < sizeof actual; ++i) raw[i] = uint8_t(random());
        actual.entry = 0; actual.so = uint8_t(so); actual.r[31] = input;
        ppc::Context expected;
        std::memcpy(&expected, &actual, sizeof actual);
        ppc::enter(expected, ppc::RAM_BASE);
        if (op == 28) expected.r[31] &= 0xFFFFu;
        else expected.r[31] &= 0xFFFF0000u;
        ppc::cr0(expected, expected.r[31]);
        const auto before_ram = record_ram;
        CHECK(record_leaf.run(actual, record_ram.data()));
        CHECK(std::memcmp(&actual, &expected, sizeof actual) == 0);
        CHECK(record_ram == before_ram);
        ++record_comparisons;
      }
    }
  }
  ppc::Context untouched{};
  untouched.entry = ppc::RAM_BASE+4;
  auto original = untouched;
  CHECK(!compiled.run(untouched, ram.data()));
  CHECK(std::memcmp(&untouched, &original, sizeof original) == 0);
  untouched.entry = 0; original = untouched;
  ram[0] ^= 1;
  CHECK(!compiled.run(untouched, ram.data()));
  CHECK(std::memcmp(&untouched, &original, sizeof original) == 0);
  ram[0] ^= 1;
  CompiledLeaf moved = std::move(compiled);
  CHECK(moved.ready() && !compiled.ready());

  auto stencils = std::vector<Stencil>(generated::table.stencils, generated::table.stencils+generated::table.count);
  auto table = generated::table;
  table.stencils = stencils.data();
  table.version += 1;
  CHECK(!translate_leaf(code.data(), code.size(), ppc::RAM_BASE, table, compiled, error));
  CHECK(!compiled.ready());
  table.version = kFormatVersion;
  stencils[1].operation = stencils[0].operation;
  CHECK(!translate_leaf(code.data(), code.size(), ppc::RAM_BASE, table, compiled, error));
  stencils[1] = generated::table.stencils[1];
  auto relocations = std::vector<Relocation>(stencils[0].relocations, stencils[0].relocations+stencils[0].relocation_count);
  stencils[0].relocations = relocations.data();
  relocations[0].offset = uint32_t(stencils[0].size);
  CHECK(!translate_leaf(code.data(), code.size(), ppc::RAM_BASE, table, compiled, error));
  relocations[0] = generated::table.stencils[0].relocations[0];
  relocations[0].hole = static_cast<Hole>(255);
  CHECK(!translate_leaf(code.data(), code.size(), ppc::RAM_BASE, table, compiled, error));
  CHECK(!compiled.ready());
  stencils[0] = generated::table.stencils[0];
  // The return stencil is xor eax,eax then RET or RET imm16=0, and nothing else.
  const uint8_t plain_return[] = {0x33,0xC0,0xC3};
  const uint8_t zero_pop_return[] = {0x33,0xC0,0xC2,0,0};
  const uint8_t pop_return[] = {0x33,0xC0,0xC2,8,0};
  const uint8_t high_pop_return[] = {0x33,0xC0,0xC2,0,1};
  const uint8_t trailing_return[] = {0x33,0xC0,0xC2,0,0,0x90};
  const uint8_t no_result_return[] = {0xC3};
  const uint8_t other_result_return[] = {0x33,0xC9,0xC3};
  for (const auto& return_shape : {
         std::pair<const uint8_t*,size_t>{plain_return,sizeof plain_return},
         {zero_pop_return,sizeof zero_pop_return}}) {
    stencils[4] = {Operation::Return, return_shape.first, return_shape.second, nullptr, 0};
    CHECK(translate_leaf(code.data(), code.size(), ppc::RAM_BASE, table, compiled, error));
    CHECK(compiled.ready());
  }
  for (const auto& return_shape : {
         std::pair<const uint8_t*,size_t>{pop_return,sizeof pop_return},
         {high_pop_return,sizeof high_pop_return}, {trailing_return,sizeof trailing_return},
         {no_result_return,sizeof no_result_return}, {other_result_return,sizeof other_result_return}}) {
    stencils[4] = {Operation::Return, return_shape.first, return_shape.second, nullptr, 0};
    CHECK(!translate_leaf(code.data(), code.size(), ppc::RAM_BASE, table, compiled, error));
    CHECK(!compiled.ready());
  }
  const auto ret = bytes({0x4E800020});
  CHECK(translate_leaf(ret.data(), ret.size(), ppc::RAM_BASE, generated::table, compiled, error));
  std::memcpy(ram.data(), ret.data(), ret.size());
  original = {}; untouched = original;
  ppc::enter(original, ppc::RAM_BASE);
  CHECK(compiled.run(untouched, ram.data()));
  CHECK(std::memcmp(&untouched, &original, sizeof original) == 0);
  if (!failures) std::printf("leaf translator: decoder rejection, stale-code rejection, 10000 full-context comparisons and %u CR0/SO boundary comparisons passed\n", record_comparisons);
  return failures ? 1 : 0;
}
