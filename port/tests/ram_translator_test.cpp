// Runtime translations versus the actual RAM interpreter, including writes and continuations.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "ram_translator.h"
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
#include <stdexcept>
namespace ppc { void init_dispatch(); }

namespace { 
constexpr uint32_t base = ppc::RAM_BASE + 0x1700100;
constexpr uint32_t host_target = ppc::RAM_BASE + 0x1710000;
int failures = 0, cases = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (0)
uint32_t d(uint32_t op, uint32_t rd, uint32_t ra, uint32_t imm) { return (op << 26) | (rd << 21) | (ra << 16) | (imm & 0xFFFF); }
uint32_t b(int32_t offset, bool link = false) { return 0x48000000u | (uint32_t(offset) & 0x03FFFFFCu) | uint32_t(link); }
constexpr uint32_t blr = 0x4E800020u, mflr0 = 0x7C0802A6u, mtlr0 = 0x7C0803A6u;
void install(std::vector<uint8_t>& ram, uint32_t address, const std::vector<uint32_t>& words) {
  size_t off = address - ppc::RAM_BASE;
  for (uint32_t word : words) for (int s : {24, 16, 8, 0}) ram[off++] = uint8_t(word >> s);
  ppc::mark_ram_write(address, uint32_t(words.size() * 4));
}
ppc::Context context() {
  ppc::Context c{};
  c.lr = 0xDEAD0000; c.r[1] = base + 0x7000; c.r[4] = base;
  return c;
}
void host_call(ppc::Context& c, uint8_t* m) { ppc::st32(c, m, base + 0x6000, c.r[3]); c.r[3] += 5; }
void throws(ppc::Context&, uint8_t*) { throw std::runtime_error("guest unwind"); }
void host_patch(ppc::Context& c, uint8_t* m) { ppc::st32(c, m, base + 8, d(14, 3, 0, 17)); }
void host_nested(ppc::Context& c, uint8_t* m) {
  const uint32_t lr = c.lr;
  c.lr = 0xDEAD1000;
  ppc::call(c, m, base + 0x1000);
  c.lr = lr;
  mu_ram_translation_invalidate(base, 4);
}

void differential(const char* name, const std::vector<uint32_t>& words, ppc::Context start = context(),
                  const std::vector<uint32_t>& extra = {}) {
  ppc::reset_ram_translator();
  std::vector<uint8_t> reference(ppc::RAM_SIZE + 64), native(ppc::RAM_SIZE + 64);
  install(reference, base, words); install(native, base, words);
  if (!extra.empty()) { install(reference, base + 0x1000, extra); install(native, base + 0x1000, extra); }
  auto a = start, c = start;
  ppc::interpret(a, reference.data(), base);
  const bool translated = ppc::try_translate_ram(c, native.data(), base);
  if (!translated) ppc::interpret(c, native.data(), base);
  if (std::memcmp(&a, &c, sizeof c) || reference != native) {
    std::printf("FAIL %s: context or RAM differs (native=%d)\n", name, translated); ++failures;
    for (size_t i = 0; i < sizeof c; ++i)
      if (reinterpret_cast<uint8_t*>(&a)[i] != reinterpret_cast<uint8_t*>(&c)[i]) {
        std::printf("context byte %zu: %02X / %02X\n", i, reinterpret_cast<uint8_t*>(&a)[i], reinterpret_cast<uint8_t*>(&c)[i]); break;
      }
  }
  ++cases;
}
}

int main() {
  ppc::init_dispatch(); // declared locally below by the runtime's public dispatch entry
  ppc::set_hook(host_target, host_call);
  std::vector<uint8_t> memory(ppc::RAM_SIZE + 64);
  install(memory, base, {d(14, 3, 0, 7), blr});
  auto c = context();
  ppc::configure_ram_translator(false);
  CHECK(!ppc::try_translate_ram(c, memory.data(), base));
  CHECK(ppc::ram_translator_stats().translated == 0);
  ppc::configure_ram_translator(true);
  CHECK(ppc::try_translate_ram(c, memory.data(), base)); CHECK(c.r[3] == 7);
  c = context(); CHECK(ppc::try_translate_ram(c, memory.data(), base));
  CHECK(ppc::ram_translator_stats().translated == 1 && ppc::ram_translator_stats().hits == 1);
  install(memory, base, {d(14, 3, 0, 9), blr});
  c = context(); CHECK(ppc::try_translate_ram(c, memory.data(), base)); CHECK(c.r[3] == 9);
  CHECK(ppc::ram_translator_stats().translated == 2 && ppc::ram_translator_stats().invalidated == 1);
  mu_ram_translation_invalidate(base, 4);
  CHECK(ppc::ram_translator_stats().entries == 0);
  c = context(); CHECK(ppc::try_translate_ram(c, memory.data(), base));
  c = context(); c.entry = base + 4;
  CHECK(!ppc::try_translate_ram(c, memory.data(), base));
  CHECK(!ppc::try_translate_ram(c, memory.data(), base + 1));
  CHECK(!ppc::try_translate_ram(c, memory.data(), ppc::RAM_BASE + ppc::RAM_SIZE));

  differential("data store in watched code block", {d(14, 3, 0, 8), d(36, 3, 4, 0x6000), blr});
  CHECK(ppc::ram_translator_stats().resumed == 0);
  differential("patch current invocation", {d(15, 5, 0, 0x3860), d(24, 5, 5, 9), d(36, 5, 4, 16), d(14, 3, 0, 1), d(14, 3, 0, 2), blr});
  CHECK(ppc::ram_translator_stats().resumed == 1);
  differential("inline data", {mflr0, b(12, true), 0x11223344, 0, 0x7C6802A6, mtlr0, blr});
  differential("local calls", {mflr0, b(16, true), d(14, 3, 3, 1), mtlr0, blr, d(14, 3, 0, 4), blr});
  differential("computed local return", {mflr0, b(0xFFC, true), d(14, 3, 0, 99), mtlr0, blr}, context(),
    {0x7CA802A6, d(14, 5, 5, 4), 0x7CA803A6, blr});
  differential("compiled call", {mflr0, d(14, 3, 0, 4), b(int32_t(host_target - base - 8), true), mtlr0, blr});
  CHECK(ppc::ram_translator_stats().translated == 1 && ppc::ram_translator_stats().refused == 0);
  differential("compiled tail", {d(14, 3, 0, 4), b(int32_t(host_target - base - 4))});
  ppc::set_hook(host_target, host_patch);
  differential("callee patches active caller", {mflr0, b(int32_t(host_target - base - 4), true), d(14, 3, 0, 1), mtlr0, blr});
  CHECK(ppc::ram_translator_stats().resumed == 1);
  ppc::set_hook(host_target, host_nested);
  differential("nested native frame and caller invalidation", {mflr0, b(int32_t(host_target - base - 4), true), d(14, 3, 3, 1), mtlr0, blr}, context(), {d(14, 3, 0, 11), blr});
  CHECK(ppc::ram_translator_stats().translated == 2 && ppc::ram_translator_stats().resumed == 1);
  ppc::set_hook(host_target, host_call);
  differential("RAM tail", {b(0x1000)}, context(), {d(14, 3, 0, 42), blr});
  CHECK(ppc::ram_translator_stats().translated == 2 && ppc::ram_translator_stats().resumed == 0);
  differential("explicit icbi", {d(14, 3, 0, 3), (31u << 26) | (4u << 16) | (982u << 1), d(14, 3, 3, 2), blr});
  CHECK(ppc::ram_translator_stats().resumed == 1);
  differential("refused overflow form", {(31u << 26) | (3u << 21) | (3u << 16) | (4u << 11) | (778u << 1), blr});
  CHECK(ppc::ram_translator_stats().refused == 1);
  c = context();
  install(memory, base, {(31u << 26) | (3u << 21) | (3u << 16) | (4u << 11) | (778u << 1), blr});
  CHECK(!ppc::try_translate_ram(c, memory.data(), base));
  CHECK(ppc::ram_translator_stats().refused == 1); // Negative cache.
  install(memory, base, {d(14, 3, 0, 12), blr});
  CHECK(ppc::try_translate_ram(c, memory.data(), base)); CHECK(c.r[3] == 12);

  std::mt19937 random(0x09C0DE);
  for (unsigned i = 0; i < 100; ++i) {
    auto start = context(); start.r[3] = random(); start.r[5] = random(); start.ctr = 1 + random() % 50;
    // addi; xor; conditional CTR back edge; return. Exact back-edge count is compared.
    differential("integer loop", {d(14, 3, 3, random()), d(26, 5, 5, random()), 0x4200FFF8, blr}, start);
  }
  for (uint32_t bo = 0; bo < 32; ++bo) for (uint32_t bi = 0; bi < 8; ++bi) for (uint32_t sample = 0; sample < 4; ++sample) {
    auto start = context(); start.ctr = sample; start.cr[bi >> 2] = uint8_t(sample & 1 ? 15 : 0);
    differential("conditional branch", {d(14, 3, 0, 0), (16u << 26) | (bo << 21) | (bi << 16) | 8,
      d(14, 3, 0, 9), blr}, start);
  }
  auto indirect = context(); indirect.ctr = base + 12;
  differential("computed CTR target", {0x4E800420, d(14, 3, 0, 1), blr, d(14, 3, 0, 8), blr}, indirect);
  CHECK(ppc::ram_translator_stats().translated == 2); // An unplanned destination continues in RAM.
  // Restoring an earlier RAM snapshot bumps the existing write generation and replans.
  ppc::reset_ram_translator();
  install(memory, base, {d(14, 3, 0, 1), blr});
  const auto snapshot = memory;
  c = context(); CHECK(ppc::try_translate_ram(c, memory.data(), base));
  install(memory, base, {d(14, 3, 0, 2), blr});
  c = context(); CHECK(ppc::try_translate_ram(c, memory.data(), base)); CHECK(c.r[3] == 2);
  std::memcpy(memory.data(), snapshot.data(), snapshot.size());
  ppc::mark_ram_write(ppc::RAM_BASE, ppc::RAM_SIZE);
  c = context(); CHECK(ppc::try_translate_ram(c, memory.data(), base)); CHECK(c.r[3] == 1);
  // Both generations are checked when code straddles a 64 KB boundary.
  ppc::reset_ram_translator();
  constexpr uint32_t cross = ppc::RAM_BASE + 0x170FFFC;
  install(memory, cross, {d(14, 3, 0, 1), blr});
  c = context(); CHECK(ppc::try_translate_ram(c, memory.data(), cross));
  install(memory, cross + 4, {d(14, 3, 0, 2), blr});
  c = context(); CHECK(ppc::try_translate_ram(c, memory.data(), cross)); CHECK(c.r[3] == 2);
  ppc::set_hook(host_target, throws);
  install(memory, base, {mflr0, b(int32_t(host_target - base - 4), true), mtlr0, blr});
  c = context();
  try { ppc::call(c, memory.data(), base); CHECK(false); } catch (const std::runtime_error&) {}
  CHECK(c.call_depth == 0 && mu_ram_version0 == nullptr); // Native frame unwound and guard cells restored.
  ppc::configure_ram_translator(false);
  std::printf("RAM translator: %d interpreter comparisons, %d failures\n", cases, failures);
  return failures ? 1 : 0;
}
