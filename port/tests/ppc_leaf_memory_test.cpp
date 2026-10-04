// Loads and stores: translated native code versus the reference in ppc_leaf_reference.h, in full
// size guest RAM. Every path of the ppc.h accessors is reached: the four RAM mirrors, the last
// bytes of RAM, the locked cache and its edges, memory-mapped I/O (a recording stub) and the
// write generations of watched and unwatched 64 KB blocks.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "ppc_leaf_forms.h"
#include "ppc_leaf_function.h"

namespace {
using namespace reference;
using worlds::Function;
using worlds::failures;
uint64_t known_total = 0;

void poke(uint32_t offset, std::initializer_list<uint8_t> values) {
  for (int i = 0; i < worlds::g_count; ++i) std::copy(values.begin(), values.end(), worlds::g_world[i].ram + offset);
}
bool ram_is(uint32_t offset, std::initializer_list<uint8_t> values) {
  return std::equal(values.begin(), values.end(), worlds::g_world[0].ram + offset);
}
// One instruction and blr from a zeroed context with the given registers.
struct Known {
  Function function;
  ppc::Context c{};
  bool run(uint32_t word) {
    ++known_total;
    return function.translate({word, kBlr}) && function.run_native(c);
  }
};

// Results worked out by hand from the PowerPC definitions and ppc.h, not from the reference.
void known_answers() {
  for (auto& watched : ppc::g_ram_watched) watched.store(0);
  poke(0x1008, {0x12, 0x34, 0x56, 0x78, 0x80, 0x01, 0xFF, 0x7F});
  { Known k; k.c.r[4] = 0x80001000u; CHECK(k.run(dform(32, 3, 4, 8)) && k.c.r[3] == 0x12345678u && k.c.r[4] == 0x80001000u); } // lwz
  { Known k; k.c.r[4] = 0x80001010u; CHECK(k.run(dform(32, 3, 4, 0xFFF8)) && k.c.r[3] == 0x12345678u); }  // negative displacement
  { Known k; k.c.r[4] = 0x80001000u; CHECK(k.run(dform(34, 3, 4, 8)) && k.c.r[3] == 0x12u); }             // lbz
  { Known k; k.c.r[4] = 0x80001000u; CHECK(k.run(dform(40, 3, 4, 8)) && k.c.r[3] == 0x1234u); }           // lhz
  { Known k; k.c.r[4] = 0x80001000u; CHECK(k.run(dform(42, 3, 4, 12)) && k.c.r[3] == 0xFFFF8001u); }      // lha sign-extends
  { Known k; k.c.r[4] = 0x80001000u; CHECK(k.run(dform(40, 3, 4, 12)) && k.c.r[3] == 0x8001u); }          // lhz does not
  { Known k; k.c.r[4] = 0x80001000u; CHECK(k.run(dform(33, 3, 4, 8)) && k.c.r[3] == 0x12345678u && k.c.r[4] == 0x80001008u); } // lwzu
  { Known k; k.c.r[4] = 0xC0001000u; CHECK(k.run(dform(32, 3, 4, 8)) && k.c.r[3] == 0x12345678u); }       // mirrors
  { Known k; k.c.r[4] = 0x00001000u; CHECK(k.run(dform(32, 3, 4, 8)) && k.c.r[3] == 0x12345678u); }
  { Known k; k.c.r[4] = 0x40001000u; CHECK(k.run(dform(32, 3, 4, 8)) && k.c.r[3] == 0x12345678u); }
  { Known k; k.c.r[0] = 0xDEAD0000u; CHECK(k.run(dform(32, 3, 0, 0x1008)) && k.c.r[3] == 0x12345678u); }  // RA=0 reads zero
  { Known k; k.c.r[4] = 0x80001000u; k.c.r[5] = 8; CHECK(k.run(xform(3, 4, 5, 23)) && k.c.r[3] == 0x12345678u); }  // lwzx
  { Known k; k.c.r[0] = 0xDEAD0000u; k.c.r[5] = 0x80001008u; CHECK(k.run(xform(3, 0, 5, 23)) && k.c.r[3] == 0x12345678u); }
  { Known k; k.c.r[4] = 0x80001000u; k.c.r[5] = 8;
    CHECK(k.run(xform(3, 4, 5, 55)) && k.c.r[3] == 0x12345678u && k.c.r[4] == 0x80001008u); }             // lwzux
  { Known k; k.c.r[4] = 0x80001000u; k.c.r[5] = 8; CHECK(k.run(xform(3, 4, 5, 534)) && k.c.r[3] == 0x78563412u); } // lwbrx
  { Known k; k.c.r[4] = 0x80001000u; k.c.r[5] = 8; CHECK(k.run(xform(3, 4, 5, 790)) && k.c.r[3] == 0x3412u); }     // lhbrx
  // Stores write big-endian bytes and nothing around them.
  poke(0x2000, {0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE});
  { Known k; k.c.r[3] = 0xAABBCCDDu; k.c.r[4] = 0x80002002u;
    CHECK(k.run(dform(36, 3, 4, 0)) && ram_is(0x2000, {0xEE, 0xEE, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xEE})); }  // stw
  { Known k; k.c.r[3] = 0xAABBCC11u; k.c.r[4] = 0x80002000u;
    CHECK(k.run(dform(38, 3, 4, 1)) && ram_is(0x2000, {0xEE, 0x11, 0xAA, 0xBB})); }                         // stb
  { Known k; k.c.r[3] = 0xAABB2233u; k.c.r[4] = 0x80002000u;
    CHECK(k.run(dform(44, 3, 4, 6)) && ram_is(0x2004, {0xCC, 0xDD, 0x22, 0x33})); }                         // sth
  { Known k; k.c.r[3] = 0x01020304u; k.c.r[4] = 0x80002010u;
    CHECK(k.run(dform(37, 3, 4, 0xFFF0)) && ram_is(0x2000, {1, 2, 3, 4}) && k.c.r[4] == 0x80002000u); }     // stwu
  { Known k; k.c.r[4] = 0x80002000u;                                                                        // stwu r4,4(r4)
    CHECK(k.run(dform(37, 4, 4, 4)) && ram_is(0x2004, {0x80, 0x00, 0x20, 0x00}) && k.c.r[4] == 0x80002004u); }
  { Known k; k.c.r[3] = 0x11223344u; k.c.r[4] = 0x80002000u; k.c.r[5] = 4;
    CHECK(k.run(xform(3, 4, 5, 662)) && ram_is(0x2004, {0x44, 0x33, 0x22, 0x11})); }                        // stwbrx
  // lmw and stmw run from the named register to r31.
  poke(0x2100, {0, 0, 0, 29, 0, 0, 0, 30, 0, 0, 0, 31, 0xEE, 0xEE, 0xEE, 0xEE});
  { Known k; k.c.r[4] = 0x80002100u; k.c.r[28] = 7;
    CHECK(k.run(dform(46, 29, 4, 0)) && k.c.r[28] == 7 && k.c.r[29] == 29 && k.c.r[30] == 30 && k.c.r[31] == 31); }
  { Known k; k.c.r[4] = 0x80002100u; k.c.r[30] = 0xA0B0C0D0u; k.c.r[31] = 0x01020304u;
    CHECK(k.run(dform(47, 30, 4, 4)) && ram_is(0x2100, {0, 0, 0, 29, 0xA0, 0xB0, 0xC0, 0xD0, 1, 2, 3, 4, 0xEE, 0xEE})); }
  // Floats: a single is widened into both halves; a double replaces ps0 only.
  poke(0x2200, {0x3F, 0x80, 0x00, 0x00, 0x40, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  { Known k; k.c.r[4] = 0x80002200u; k.c.f[1].u1 = 0x1111;
    CHECK(k.run(dform(48, 1, 4, 0)) && k.c.f[1].ps0 == 1.0 && k.c.f[1].ps1 == 1.0); }                       // lfs
  { Known k; k.c.r[4] = 0x80002200u; k.c.f[1].u1 = 0x1111;
    CHECK(k.run(dform(50, 1, 4, 4)) && k.c.f[1].ps0 == 2.5 && k.c.f[1].u1 == 0x1111); }                     // lfd
  { Known k; k.c.r[4] = 0x80002000u; k.c.f[2].ps0 = 2.5;
    CHECK(k.run(dform(52, 2, 4, 0)) && ram_is(0x2000, {0x40, 0x20, 0x00, 0x00})); }                         // stfs
  { Known k; k.c.r[4] = 0x80002000u; k.c.f[2].ps0 = -2.5;
    CHECK(k.run(dform(54, 2, 4, 0)) && ram_is(0x2000, {0xC0, 0x04, 0, 0, 0, 0, 0, 0})); }                   // stfd
  { Known k; k.c.r[4] = 0x80002000u; k.c.r[5] = 0; k.c.f[2].u0 = 0x1234567890ABCDEFull;
    CHECK(k.run(xform(2, 4, 5, 983)) && ram_is(0x2000, {0x90, 0xAB, 0xCD, 0xEF})); }                        // stfiwx
  // The locked cache.
  { Known k; k.c.r[3] = 0xCAFEF00Du; k.c.r[4] = 0xE0000010u;
    CHECK(k.run(dform(36, 3, 4, 0)) && worlds::g_world[0].lc[0x10] == 0xCA && worlds::g_world[0].lc[0x13] == 0x0D &&
          worlds::g_world[0].result.events.count == 0); }
  { Known k; k.c.r[4] = 0xE0000000u; CHECK(k.run(dform(32, 3, 4, 0x10)) && k.c.r[3] == 0xCAFEF00Du); }
  // Outside RAM and the locked cache every access is one host call; the stub moves the timebase.
  { Known k; k.c.r[4] = 0xCC002000u;
    CHECK(k.run(dform(32, 3, 4, 0)) && worlds::g_world[0].result.events.count == 1 && k.c.tb == 3); }
  { Known k; k.c.r[4] = 0xCC002000u;
    CHECK(k.run(dform(36, 3, 4, 0)) && worlds::g_world[0].result.events.count == 1 && k.c.tb == 5); }
  { Known k; k.c.r[4] = 0xCC002000u;
    CHECK(k.run(dform(50, 3, 4, 0)) && worlds::g_world[0].result.events.count == 1 && k.c.tb == 7); }       // lfd: one 64-bit read
  { Known k; k.c.r[4] = 0xCC002000u;
    CHECK(k.run(dform(47, 29, 4, 0)) && worlds::g_world[0].result.events.count == 3 && k.c.tb == 15); }     // stmw r29: three writes
  // The last byte of RAM is RAM; the next one is not, and neither is the byte before the mirror.
  { Known k; k.c.r[3] = 0x5A; k.c.r[4] = 0x80000000u + ppc::RAM_SIZE - 1;
    CHECK(k.run(dform(38, 3, 4, 0)) && worlds::g_world[0].ram[ppc::RAM_SIZE - 1] == 0x5A && worlds::g_world[0].result.events.count == 0); }
  { Known k; k.c.r[3] = 0x5B; k.c.r[4] = 0x80000000u + ppc::RAM_SIZE;
    CHECK(k.run(dform(38, 3, 4, 0)) && worlds::g_world[0].result.events.count == 1); }
  { Known k; k.c.r[3] = 0x5C; k.c.r[4] = 0x7FFFFFFFu;
    CHECK(k.run(dform(38, 3, 4, 0)) && worlds::g_world[0].result.events.count == 1); }
  // Write generations: only watched blocks count, and a store across two blocks counts in both.
  const auto versions = [](uint32_t block) { return worlds::g_world[0].result.versions[block]; };
  { Known k; k.c.r[4] = 0x80002000u; CHECK(k.run(dform(36, 3, 4, 0)) && versions(0) == 0); }
  ppc::g_ram_watched[0].store(1); ppc::g_ram_watched[1].store(1);
  { Known k; k.c.r[4] = 0x80002000u; CHECK(k.run(dform(36, 3, 4, 0)) && versions(0) == 1 && versions(1) == 0); }
  { Known k; k.c.r[4] = 0x8000FFFEu; CHECK(k.run(dform(36, 3, 4, 0)) && versions(0) == 1 && versions(1) == 1); }
  { Known k; k.c.r[4] = 0x8000FFFFu; CHECK(k.run(dform(38, 3, 4, 0)) && versions(0) == 1 && versions(1) == 0); }
  { Known k; k.c.r[4] = 0x8000FFFCu; CHECK(k.run(dform(47, 30, 4, 0)) && versions(0) == 1 && versions(1) == 1); } // stmw r30
  { Known k; k.c.r[4] = 0x80002000u; CHECK(k.run(dform(32, 3, 4, 0)) && versions(0) == 0); }                   // a load counts nothing
  { Known k; k.c.r[4] = 0xE0000000u; CHECK(k.run(dform(36, 3, 4, 0)) && versions(0) == 0); }                   // nor does the cache
  for (auto& watched : ppc::g_ram_watched) watched.store(0);
}

void random_contexts(const WideForm& form, Random& random) {
  for (int encoding = 0; encoding < 150 && failures < 20; ++encoding) {
    Function function;
    if (!function.translate({random_word(form, random), kBlr})) return;
    if ((encoding & 15) == 0) worlds::watch(random, encoding & 16 ? 2 : 6);
    for (int sample = 0; sample < 40; ++sample) {
      ppc::Context start;
      randomize_rich(start, random);
      if (!function.compare(start, form.name)) return;
    }
  }
}

// Every form at exact boundary addresses, with and without a displacement, in watched and
// unwatched RAM.
void boundaries(const WideForm& form, Random& random) {
  static const uint32_t addresses[] = {
    0x80000000u, 0x80000001u, 0x80000003u, 0x00000000u, 0x40000000u, 0xC0000000u,
    0x8000FFF8u, 0x8000FFFCu, 0x8000FFFDu, 0x8000FFFEu, 0x8000FFFFu, 0x80010000u,
    0x817FFFF0u, 0x817FFFF8u, 0x817FFFF9u, 0x817FFFFCu, 0x817FFFFDu, 0x817FFFFEu, 0x817FFFFFu, 0x81800000u, 0x81800001u,
    0x017FFFFCu, 0x017FFFFFu, 0x01800000u, 0xC17FFFFFu, 0xC1800000u, 0x3FFFFFFFu, 0x7FFFFFFFu, 0xBFFFFFFFu, 0xFFFFFFFFu,
    0xDFFFFFFCu, 0xDFFFFFFFu, 0xE0000000u, 0xE0000001u, 0xE0003FF8u, 0xE0003FFCu, 0xE0003FFDu, 0xE0003FFFu, 0xE0004000u,
    0xCC000000u, 0xCC006000u, 0xCD000004u,
  };
  static const uint32_t displacements[] = {0, 8, 0xFFF8};
  const bool indexed = form.kind == Wide::X || form.kind == Wide::XUpdate || form.kind == Wide::XUpdateLoad ||
                       form.kind == Wide::PsqX || form.kind == Wide::PsqXUpdate;
  for (uint32_t displacement : displacements) {
    if (indexed && displacement == 8) continue;
    const bool quantized = form.kind == Wide::PsqD || form.kind == Wide::PsqDUpdate;
    // lmw loads r6..r31 so its base r4 is outside the loaded range.
    const uint32_t first = form.kind == Wide::Lmw ? 6 : form.kind == Wide::Stmw ? 27 : 3;
    Function function;
    if (!function.translate({encode(form, first, 4, 5, 5, quantized ? displacement & 0xFFFu : displacement), kBlr})) return;
    for (int watched = 0; watched < 2; ++watched) {
      for (auto& block : ppc::g_ram_watched) block.store(uint8_t(watched));
      for (uint32_t address : addresses) {
        ppc::Context start;
        randomize_rich(start, random);
        const uint32_t offset = indexed ? (displacement ? 0x1234u : 0u)
                                        : ((displacement & 0x8000u) ? displacement | 0xFFFF0000u : displacement);
        start.r[4] = address - offset;
        start.r[5] = offset;
        if (!function.compare(start, form.name)) return;
      }
    }
  }
  for (auto& block : ppc::g_ram_watched) block.store(0);
}

// Random straight-line functions mixing loads, stores and the integer forms.
void sequences(Random& random) {
  for (int index = 0; index < 1500 && failures < 20; ++index) {
    std::vector<uint32_t> words;
    const uint32_t length = 1 + random.below(40);
    for (uint32_t i = 0; i < length; ++i) {
      if (random.below(2)) words.push_back(random_word(kMemoryForms[random.below(uint32_t(kMemoryFormCount))], random));
      else words.push_back(random_word(kForms[random.below(uint32_t(kFormCount))], random));
    }
    words.push_back(kBlr);
    Function function;
    if (!function.translate(words, ppc::RAM_BASE + 0x3000 + 4 * random.below(64))) return;
    if ((index & 31) == 0) worlds::watch(random, 3);
    for (int sample = 0; sample < 12; ++sample) {
      ppc::Context start;
      randomize_rich(start, random);
      if (!function.compare(start, "memory sequence")) return;
    }
  }
}

// The comparison can fail: a store of another value, a load from another address, one more host
// call and one more generation bump must each be seen.
void controls(Random& random) {
  Function stw, stw_other, lwz;
  if (!stw.translate({dform(36, 3, 4, 0), kBlr}) || !stw_other.translate({dform(36, 5, 4, 0), kBlr}) ||
      !lwz.translate({dform(32, 3, 4, 0), kBlr})) return;
  const auto differs = [&](Function& native, const std::vector<uint32_t>& other, const ppc::Context& start, const char* expect) {
    worlds::install(native.address, native.code);
    worlds::run(0, start, [&](ppc::Context& c, uint8_t* m) { return native.leaf.run(c, m); });
    worlds::run(1, start, [&](ppc::Context& c, uint8_t* m) { return run(c, m, other, native.address); });
    const char* what = worlds::difference();
    if (!what || std::strcmp(what, expect)) {
      std::printf("FAIL: control expected a difference in %s, saw %s\n", expect, what ? what : "none");
      ++failures;
    }
    // Put world 1 back in step with world 0 for the tests that follow.
    std::memcpy(worlds::g_world[1].ram, worlds::g_world[0].ram, 0x10000);
    std::memcpy(worlds::g_world[1].lc, worlds::g_world[0].lc, sizeof worlds::g_world[0].lc);
    worlds::ram_same();
  };
  ppc::Context start;
  randomize(start, random);
  start.r[3] = 1; start.r[5] = 2; start.r[4] = 0x80004000u;
  differs(stw, stw_other.words, start, "guest RAM");                       // another value stored
  differs(lwz, {dform(32, 3, 4, 4), kBlr}, start, "context");               // another address loaded
  start.r[4] = 0xE0000100u;
  differs(stw, stw_other.words, start, "locked cache");
  start.r[4] = 0xCC000000u;
  differs(stw, stw_other.words, start, "host calls");                       // the value reached the host call
  differs(stw, {0x60000000u, kBlr}, start, "context");                      // a missing host call (timebase)
  start.r[4] = 0x80004000u; start.r[3] = start.r[5];
  ppc::g_ram_watched[0].store(1);
  differs(stw, {dform(36, 3, 4, 0), dform(36, 3, 4, 0), kBlr}, start, "RAM write generations"); // one more bump
  ppc::g_ram_watched[0].store(0);
}
} // namespace

int main() {
  Random random(0x2026100300000001ull);
  if (!worlds::init(2, random)) return 1;
  known_answers();
  controls(random);
  const uint64_t fixed = worlds::comparisons;
  for (const auto& form : kMemoryForms) if (!failures) random_contexts(form, random);
  const uint64_t random_total = worlds::comparisons - fixed;
  for (const auto& form : kMemoryForms) if (!failures) boundaries(form, random);
  const uint64_t boundary_total = worlds::comparisons - fixed - random_total;
  if (!failures) sequences(random);
  const uint64_t sequence_total = worlds::comparisons - fixed - random_total - boundary_total;
  if (!failures)
    std::printf("leaf memory: %zu forms, %llu known answers, %llu random full-context comparisons (6000 per form), "
                "%llu boundary-address comparisons, %llu comparisons over 1500 random functions; %llu host calls, "
                "%llu write-generation bumps and %llu written RAM pages compared\n",
                kMemoryFormCount, (unsigned long long)known_total, (unsigned long long)random_total,
                (unsigned long long)boundary_total, (unsigned long long)sequence_total,
                (unsigned long long)worlds::g_host_events, (unsigned long long)worlds::g_generation_bumps,
                (unsigned long long)worlds::g_pages_compared);
  return failures ? 1 : 0;
}
