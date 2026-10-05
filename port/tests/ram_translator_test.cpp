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
// Makes a word of the calling routine a compiled dispatch target while that routine runs.
void host_hooks(ppc::Context&, uint8_t*) { ppc::set_hook(base + 24, host_call); }

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
  // The mechanisms below are tested with translation on the first call; the threshold has its own cases.
  ppc::set_ram_translator_hot_calls(1);
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

  // Native branches and guard placement. A loop with a load, a store into the code's own 64 KB
  // block, a compare, a forward conditional branch and a CTR back edge never leaves native code.
  ppc::set_hook(host_target, host_call);
  for (unsigned i = 0; i < 50; ++i) {
    auto start = context(); start.r[3] = random(); start.r[5] = random(); start.ctr = 1 + random() % 40;
    differential("loop with loads and stores", {
      d(32, 6, 4, 0x6000),                                                  // lwz r6,0x6000(r4)
      (31u << 26) | (3u << 21) | (3u << 16) | (6u << 11) | (266u << 1),     // add r3,r3,r6
      d(36, 3, 4, 0x6000),                                                  // stw r3,0x6000(r4)
      d(11, 0, 3, random()),                                                // cmpwi r3,imm
      (16u << 26) | (12u << 21) | 8,                                        // blt +8
      d(26, 5, 5, random()),                                                // xori r5,r5,imm
      0x4200FFE8,                                                           // bdnz to the lwz
      blr}, start);
    CHECK(ppc::ram_translator_stats().translated == 1 && ppc::ram_translator_stats().resumed == 0);
  }
  // A loop that rewrites an instruction of its own body: the guard after the store hands the
  // rest of the invocation to the interpreter.
  { auto start = context(); start.ctr = 4;
    differential("loop patches its own body", {d(14, 3, 0, 0), d(15, 5, 0, 0x3863), d(24, 5, 5, 5),
      d(14, 3, 3, 1), d(36, 5, 4, 12), 0x4200FFF8, blr}, start);
    CHECK(ppc::ram_translator_stats().resumed == 1); }
  // A write into inline data that no path executes leaves the translation valid.
  differential("store into inline data", {mflr0, b(12, true), 0x11223344, 0, 0x7CC802A6, d(14, 7, 0, 77),
    d(36, 7, 6, 0), d(14, 3, 7, 1), mtlr0, blr});
  CHECK(ppc::ram_translator_stats().translated == 1 && ppc::ram_translator_stats().resumed == 0);
  // A return into the caller continues inside the caller's translation, not in a new one.
  const std::vector<uint32_t> callee{d(14, 3, 3, 5), blr};
  const std::vector<uint32_t> caller{mflr0, b(-0x1004, true), d(14, 3, 3, 1), mtlr0, blr};
  { ppc::reset_ram_translator();
    std::vector<uint8_t> reference(ppc::RAM_SIZE + 64), native(ppc::RAM_SIZE + 64);
    for (auto* ram : {&reference, &native}) { install(*ram, base, callee); install(*ram, base + 0x1000, caller); }
    auto a = context(), n = context();
    ppc::interpret(a, reference.data(), base + 0x1000);
    CHECK(ppc::try_translate_ram(n, native.data(), base + 0x1000));
    CHECK(a.r[3] == 6 && !std::memcmp(&a, &n, sizeof n) && reference == native); ++cases;
    CHECK(ppc::ram_translator_stats().translated == 2 && ppc::ram_translator_stats().joined == 1 &&
          ppc::ram_translator_stats().hits == 1 && ppc::ram_translator_stats().resumed == 0);
    n = context(); CHECK(ppc::try_translate_ram(n, native.data(), base + 0x1000));
    CHECK(!std::memcmp(&a, &n, sizeof n)); ++cases;
    CHECK(ppc::ram_translator_stats().translated == 2 && ppc::ram_translator_stats().hits == 4); }
  // A native branch target that becomes a compiled dispatch target between two calls.
  const uint32_t bgt8 = (16u << 26) | (12u << 21) | (1u << 16) | 8; // bgt +8
  { ppc::reset_ram_translator();
    // li r3,1; cmpwi r3,0; bgt over the li; li r3,2; addi r3,r3,10; blr. The addi is the target.
    const std::vector<uint32_t> words{d(14, 3, 0, 1), d(11, 0, 3, 0), bgt8, d(14, 3, 0, 2), d(14, 3, 3, 10), blr};
    std::vector<uint8_t> reference(ppc::RAM_SIZE + 64), native(ppc::RAM_SIZE + 64);
    install(reference, base, words); install(native, base, words);
    auto n = context(); CHECK(ppc::try_translate_ram(n, native.data(), base)); CHECK(n.r[3] == 11);
    CHECK(ppc::ram_translator_stats().translated == 1);
    const ppc::Fn before = ppc::set_hook(base + 16, host_call);
    auto a = context(); n = context();
    ppc::interpret(a, reference.data(), base);
    CHECK(ppc::try_translate_ram(n, native.data(), base));
    CHECK(a.r[3] == 6 && !std::memcmp(&a, &n, sizeof n) && reference == native); ++cases;
    CHECK(ppc::ram_translator_stats().translated == 2 && ppc::ram_translator_stats().invalidated == 1);
    ppc::set_hook(base + 16, before);
    n = context(); CHECK(ppc::try_translate_ram(n, native.data(), base)); CHECK(n.r[3] == 11);
    CHECK(ppc::ram_translator_stats().translated == 3); }
  // The same change made by a callee while the routine is running: the guard after the call
  // hands over before the branch, and the interpreter calls the new target (host_hooks: base + 24).
  { ppc::reset_ram_translator();
    ppc::set_hook(host_target, host_hooks);
    // mflr r0; bl host; mtlr r0; cmpwi r3,-1; bgt over the li; li r3,2; addi r3,r3,10; blr.
    const std::vector<uint32_t> words{mflr0, b(int32_t(host_target - base - 4), true), mtlr0,
      d(11, 0, 3, 0xFFFF), bgt8, d(14, 3, 0, 2), d(14, 3, 3, 10), blr};
    std::vector<uint8_t> reference(ppc::RAM_SIZE + 64), native(ppc::RAM_SIZE + 64);
    install(reference, base, words); install(native, base, words);
    auto n = context(); CHECK(ppc::try_translate_ram(n, native.data(), base));
    CHECK(ppc::ram_translator_stats().translated == 1 && ppc::ram_translator_stats().resumed == 1);
    ppc::set_hook(base + 24, nullptr);
    auto a = context(); ppc::interpret(a, reference.data(), base);
    CHECK(a.r[3] == 5 && !std::memcmp(&a, &n, sizeof n) && reference == native); ++cases;
    ppc::set_hook(base + 24, nullptr);
    ppc::set_hook(host_target, host_call); }

  // The hotness threshold. Cold calls are left to the interpreter and compute the same; code that
  // was translated and then changed has to be asked for more often before it is translated again.
  ppc::reset_ram_translator();
  ppc::set_ram_translator_hot_calls(3);
  install(memory, base, {d(14, 3, 0, 21), blr});
  for (int call = 0; call < 2; ++call) {
    c = context(); CHECK(!ppc::try_translate_ram(c, memory.data(), base));
    ppc::interpret(c, memory.data(), base); CHECK(c.r[3] == 21);
  }
  CHECK(ppc::ram_translator_stats().translated == 0 && ppc::ram_translator_stats().cold == 2);
  c = context(); CHECK(ppc::try_translate_ram(c, memory.data(), base)); CHECK(c.r[3] == 21);
  c = context(); CHECK(ppc::try_translate_ram(c, memory.data(), base)); CHECK(c.r[3] == 21);
  CHECK(ppc::ram_translator_stats().translated == 1 && ppc::ram_translator_stats().hits == 1);
  install(memory, base, {d(14, 3, 0, 22), blr});
  unsigned cold_calls = 0;
  for (; cold_calls < 100; ++cold_calls) {
    c = context();
    if (ppc::try_translate_ram(c, memory.data(), base)) break;
    ppc::interpret(c, memory.data(), base); CHECK(c.r[3] == 22);
  }
  CHECK(c.r[3] == 22 && cold_calls == 11 && ppc::ram_translator_stats().translated == 2);
  // A cold callee reached from a translated caller: the interpreter finishes the invocation.
  { ppc::reset_ram_translator();
    std::vector<uint8_t> reference(ppc::RAM_SIZE + 64), native(ppc::RAM_SIZE + 64);
    for (auto* ram : {&reference, &native}) { install(*ram, base, callee); install(*ram, base + 0x1000, caller); }
    auto a = context();
    ppc::interpret(a, reference.data(), base + 0x1000);
    for (int call = 0; call < 5; ++call) {
      auto n = context();
      if (!ppc::try_translate_ram(n, native.data(), base + 0x1000)) ppc::interpret(n, native.data(), base + 0x1000);
      CHECK(!std::memcmp(&a, &n, sizeof n) && reference == native); ++cases;
    }
    const auto stats = ppc::ram_translator_stats();
    CHECK(stats.translated == 2 && stats.cold == 4 && stats.resumed == 2 && stats.joined == 1); }
  ppc::set_ram_translator_hot_calls(1);
  ppc::configure_ram_translator(false);
  std::printf("RAM translator: %d interpreter comparisons, %d failures\n", cases, failures);
  return failures ? 1 : 0;
}
