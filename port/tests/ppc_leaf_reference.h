// Shared by the translator's native tests: an independent reference that executes guest words one
// at a time with the expressions port/recomp/emit.py writes, canonical encoders, random contexts
// and the runtime stubs a standalone test needs. Include from exactly one source file per test.
// No game, ISO, window, device or interpreter dependency.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "leaf_translator.h"
#include <cstdio>
#include <cstring>
#include <vector>

namespace ppc {
uint64_t g_enter_count = 0;
bool g_trace_funcs = false;
void hang_check(Context&) {}
void trace_enter(Context&, uint32_t) {}
// The poll changes guest-visible state and logs the whole context it saw, so a poll that is
// missing, extra, early or late shows in the final context and in the log.
uint64_t g_poll_count = 0;
uint64_t g_poll_digest = 1469598103934665603ull;
void loop_poll(Context& c) {
  const auto* raw = reinterpret_cast<const uint8_t*>(&c);
  for (size_t i = 0; i < sizeof c; ++i) g_poll_digest = (g_poll_digest ^ raw[i]) * 1099511628211ull;
  ++g_poll_count;
  c.tb += 977;
  c.dec ^= uint32_t(g_poll_digest);
}
}

namespace reference {
// splitmix64: small, fast and fully specified, so every run sees the same contexts.
struct Random {
  uint64_t state;
  explicit Random(uint64_t seed) : state(seed) {}
  uint64_t next() {
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
  uint32_t u32() { return uint32_t(next() >> 32); }
  uint32_t below(uint32_t n) { return uint32_t((uint64_t(u32()) * n) >> 32); }
};
constexpr uint32_t kBoundary[] = {
  0u, 1u, 2u, 31u, 32u, 33u, 63u, 64u, 0x7Fu, 0x80u, 0xFFu, 0x7FFFu, 0x8000u, 0xFFFFu, 0x10000u,
  0x7FFFFFFFu, 0x80000000u, 0x80000001u, 0xFFFF8000u, 0xFFFFFFFEu, 0xFFFFFFFFu,
};
constexpr size_t kBoundaryCount = sizeof kBoundary / sizeof kBoundary[0];
// Every byte of the context is random. entry stays 0 (run rejects a mid-function entry) and the
// three XER flags keep their real range of 0 or 1. A quarter of the GPRs take a boundary value.
inline void randomize(ppc::Context& c, Random& random) {
  auto* raw = reinterpret_cast<uint8_t*>(&c); // memcpy avoids writing the aliasing unions directly.
  for (size_t i = 0; i < sizeof c; i += 8) {
    const uint64_t value = random.next();
    std::memcpy(raw + i, &value, sizeof c - i < 8 ? sizeof c - i : 8);
  }
  for (auto& r : c.r) if (random.below(4) == 0) r = kBoundary[random.below(uint32_t(kBoundaryCount))];
  c.entry = 0; c.so &= 1; c.ca &= 1; c.ov &= 1;
}
inline bool same(const ppc::Context& a, const ppc::Context& b) { return std::memcmp(&a, &b, sizeof a) == 0; }
inline void describe(const ppc::Context& actual, const ppc::Context& expected) {
  for (int i = 0; i < 32; ++i)
    if (actual.r[i] != expected.r[i]) std::printf("  r%d: native %08X reference %08X\n", i, actual.r[i], expected.r[i]);
  for (int i = 0; i < 8; ++i)
    if (actual.cr[i] != expected.cr[i]) std::printf("  cr%d: native %02X reference %02X\n", i, actual.cr[i], expected.cr[i]);
#define MU_FIELD(name) if (actual.name != expected.name) \
    std::printf("  " #name ": native %08X reference %08X\n", unsigned(actual.name), unsigned(expected.name));
  MU_FIELD(lr) MU_FIELD(ctr) MU_FIELD(ca) MU_FIELD(so) MU_FIELD(ov) MU_FIELD(backedges)
  MU_FIELD(last_pc) MU_FIELD(trace_pos) MU_FIELD(dec)
#undef MU_FIELD
}

// ---- canonical encoders ----
inline uint32_t dform(uint32_t opcode, uint32_t first, uint32_t second, uint32_t immediate) {
  return (opcode << 26) | (first << 21) | (second << 16) | (immediate & 0xFFFFu);
}
inline uint32_t xform(uint32_t first, uint32_t second, uint32_t third, uint32_t xo, uint32_t rc = 0) {
  return (31u << 26) | (first << 21) | (second << 16) | (third << 11) | (xo << 1) | rc;
}
inline uint32_t mform(uint32_t opcode, uint32_t rs, uint32_t ra, uint32_t sh, uint32_t mb, uint32_t me, uint32_t rc = 0) {
  return (opcode << 26) | (rs << 21) | (ra << 16) | (sh << 11) | (mb << 6) | (me << 1) | rc;
}
inline uint32_t xlform(uint32_t first, uint32_t second, uint32_t third, uint32_t xo) {
  return (19u << 26) | (first << 21) | (second << 16) | (third << 11) | (xo << 1);
}
inline uint32_t spr_field(uint32_t spr) { return ((spr & 31) << 5) | (spr >> 5); }
inline uint32_t mfspr(uint32_t rd, uint32_t spr) { return (31u << 26) | (rd << 21) | (spr_field(spr) << 11) | (339u << 1); }
inline uint32_t mtspr(uint32_t spr, uint32_t rs) { return (31u << 26) | (rs << 21) | (spr_field(spr) << 11) | (467u << 1); }
inline uint32_t mtcrf(uint32_t crm, uint32_t rs) { return (31u << 26) | (rs << 21) | (crm << 12) | (144u << 1); }
// offset is in bytes from the branch itself.
inline uint32_t branch(int32_t offset) { return (18u << 26) | (uint32_t(offset) & 0x03FFFFFCu); }
inline uint32_t bc(uint32_t bo, uint32_t bi, int32_t offset) {
  return (16u << 26) | (bo << 21) | (bi << 16) | (uint32_t(offset) & 0xFFFCu);
}
inline uint32_t bclr(uint32_t bo, uint32_t bi) { return xlform(bo, bi, 0, 16); }
constexpr uint32_t kBlr = 0x4E800020u;
inline std::vector<uint8_t> bytes(const std::vector<uint32_t>& words) {
  std::vector<uint8_t> out;
  for (auto word : words) for (int shift : {24, 16, 8, 0}) out.push_back(uint8_t(word >> shift));
  return out;
}

// ---- reference execution, one guest word at a time ----
inline uint32_t bits(uint32_t w, int start, int count) { // gekko.py _bits: bit 0 is the MSB.
  return (w >> (32 - start - count)) & ((1u << count) - 1);
}
inline uint32_t emit_mask(uint32_t mb, uint32_t me) { // Emitter._mask
  const uint32_t begin = 0xFFFFFFFFu >> mb, end = 0x7FFFFFFFu >> me, m = begin ^ end;
  return me < mb ? ~m : m;
}
inline bool condition(ppc::Context& c, uint32_t bo, uint32_t bi) { // emit.py cond_expr
  if (!(bo & 4)) {
    if (bo & 2) { if (!(--c.ctr == 0)) return false; }
    else if (!(--c.ctr != 0)) return false;
  }
  if (!(bo & 16)) {
    const bool bit = (c.cr[bi >> 2] & (8 >> (bi & 3))) != 0;
    if ((bo & 8) ? !bit : bit) return false;
  }
  return true;
}
enum class Flow { Next, Jump, Return, Unknown };
// Each case is the statement emit.py writes for that instruction; the register operands are
// references so an instruction that names one register twice behaves as the emitted code does.
inline Flow step(ppc::Context& c, uint32_t w, uint32_t addr, uint32_t& target) {
  const uint32_t op = w >> 26;
  uint32_t& rd = c.r[bits(w, 6, 5)];
  uint32_t& rs = rd;
  uint32_t& ra = c.r[bits(w, 11, 5)];
  uint32_t& rb = c.r[bits(w, 16, 5)];
  const bool ra0 = bits(w, 11, 5) == 0;
  const uint32_t uimm = w & 0xFFFFu;
  const int32_t simm = (w & 0x8000u) ? int32_t(uimm) - 0x10000 : int32_t(uimm);
  const uint32_t hsimm = uint32_t(simm); // hexs(simm)
  const int crfd = int(bits(w, 6, 3));
  const bool rc = w & 1;
  switch (op) {
    case 14: rd = (ra0 ? 0u : ra) + hsimm; return Flow::Next;
    case 15: rd = (ra0 ? 0u : ra) + (hsimm << 16); return Flow::Next;
    case 12: { uint32_t a = ra; rd = a + hsimm; c.ca = ppc::carry(a, hsimm); } return Flow::Next;
    case 13: { uint32_t a = ra; rd = a + hsimm; c.ca = ppc::carry(a, hsimm); ppc::cr0(c, rd); } return Flow::Next;
    case 8: { uint32_t a = ra; rd = hsimm - a; c.ca = (a == 0) || ppc::carry(0u - a, hsimm); } return Flow::Next;
    case 7: rd = (uint32_t)((int32_t)ra * simm); return Flow::Next;
    case 11: ppc::cr_set_s(c, crfd, (int32_t)ra, simm); return Flow::Next;
    case 10: ppc::cr_set_u(c, crfd, ra, uimm); return Flow::Next;
    case 24: ra = rs | uimm; return Flow::Next;
    case 25: ra = rs | (uimm << 16); return Flow::Next;
    case 26: ra = rs ^ uimm; return Flow::Next;
    case 27: ra = rs ^ (uimm << 16); return Flow::Next;
    case 28: ra = rs & uimm; ppc::cr0(c, ra); return Flow::Next;
    case 29: ra = rs & (uimm << 16); ppc::cr0(c, ra); return Flow::Next;
    case 21: ra = _rotl(rs, int(bits(w, 16, 5))) & emit_mask(bits(w, 21, 5), bits(w, 26, 5));
             if (rc) ppc::cr0(c, ra); return Flow::Next;
    case 23: ra = _rotl(rs, rb & 31) & emit_mask(bits(w, 21, 5), bits(w, 26, 5));
             if (rc) ppc::cr0(c, ra); return Flow::Next;
    case 20: { const uint32_t m = emit_mask(bits(w, 21, 5), bits(w, 26, 5));
               ra = (ra & ~m) | (_rotl(rs, int(bits(w, 16, 5))) & m); }
             if (rc) ppc::cr0(c, ra); return Flow::Next;
    case 18:
      if (w & 3) return Flow::Unknown;
      target = addr + ((w & 0x02000000u) ? (w & 0x03FFFFFCu) | 0xFC000000u : (w & 0x03FFFFFCu));
      if (target <= addr) ppc::backedge(c);
      return Flow::Jump;
    case 16:
      if (w & 3) return Flow::Unknown;
      target = addr + ((w & 0x8000u) ? (w & 0xFFFCu) | 0xFFFF0000u : (w & 0xFFFCu));
      if (!condition(c, bits(w, 6, 5), bits(w, 11, 5))) return Flow::Next;
      if (target <= addr) ppc::backedge(c);
      return Flow::Jump;
    case 19: {
      const uint32_t xo = bits(w, 21, 10);
      if (xo == 16) {
        if (w & 1) return Flow::Unknown;
        return condition(c, bits(w, 6, 5), bits(w, 11, 5)) ? Flow::Return : Flow::Next;
      }
      if (xo == 0) { c.cr[crfd] = c.cr[bits(w, 11, 3)]; return Flow::Next; }
      const int crbd = int(bits(w, 6, 5)), crba = int(bits(w, 11, 5)), crbb = int(bits(w, 16, 5));
      uint32_t a = ppc::crbit(c, crba), b = ppc::crbit(c, crbb);
      switch (xo) {
        case 257: ppc::crbit_set(c, crbd, a & b); break;
        case 449: ppc::crbit_set(c, crbd, a | b); break;
        case 193: ppc::crbit_set(c, crbd, a ^ b); break;
        case 225: ppc::crbit_set(c, crbd, !(a & b)); break;
        case 33: ppc::crbit_set(c, crbd, !(a | b)); break;
        case 289: ppc::crbit_set(c, crbd, !(a ^ b)); break;
        case 129: ppc::crbit_set(c, crbd, a & !b); break;
        case 417: ppc::crbit_set(c, crbd, a | !b); break;
        default: return Flow::Unknown;
      }
      return Flow::Next;
    }
    case 31: break;
    default: return Flow::Unknown;
  }
  const uint32_t xo = bits(w, 21, 10);
  bool arithmetic = true; // Destination rd; logical forms write ra.
  switch (xo) {
    case 266: rd = ra + rb; break;
    case 40: rd = rb - ra; break;
    case 235: rd = (uint32_t)((int32_t)ra * (int32_t)rb); break;
    case 75: rd = (uint32_t)(((int64_t)(int32_t)ra * (int64_t)(int32_t)rb) >> 32); break;
    case 11: rd = (uint32_t)(((uint64_t)ra * (uint64_t)rb) >> 32); break;
    case 491: rd = ppc::divw((int32_t)ra, (int32_t)rb); break;
    case 459: rd = ppc::divwu(ra, rb); break;
    case 104: rd = 0u - ra; break;
    case 10: { uint32_t a = ra, b = rb; rd = a + b; c.ca = ppc::carry(a, b); } break;
    case 138: { uint32_t a = ra, b = rb, k = c.ca; rd = a + b + k;
                c.ca = ppc::carry(a, b) || (k && ppc::carry(a + b, k)); } break;
    case 202: { uint32_t a = ra, k = c.ca; rd = a + k; c.ca = ppc::carry(a, k); } break;
    case 234: { uint32_t a = ra, k = c.ca; rd = a + k - 1u; c.ca = ppc::carry(a, k - 1u); } break;
    case 8: { uint32_t a = ra, b = rb; rd = b - a; c.ca = (a == 0) || ppc::carry(b, 0u - a); } break;
    case 136: { uint32_t a = ~ra, b = rb, k = c.ca; rd = a + b + k;
                c.ca = ppc::carry(a, b) || ppc::carry(a + b, k); } break;
    case 200: { uint32_t a = ~ra, k = c.ca; rd = a + k; c.ca = ppc::carry(a, k); } break;
    case 232: { uint32_t a = ~ra, k = c.ca; rd = a + k - 1u; c.ca = ppc::carry(a, k - 1u); } break;
    default: arithmetic = false; break;
  }
  if (arithmetic) { if (rc) ppc::cr0(c, rd); return Flow::Next; }
  bool logical = true;
  switch (xo) {
    case 28: ra = rs & rb; break;
    case 444: ra = rs | rb; break;
    case 316: ra = rs ^ rb; break;
    case 476: ra = ~(rs & rb); break;
    case 124: ra = ~(rs | rb); break;
    case 284: ra = ~(rs ^ rb); break;
    case 60: ra = rs & ~rb; break;
    case 412: ra = rs | ~rb; break;
    case 954: ra = (uint32_t)(int32_t)(int8_t)rs; break;
    case 922: ra = (uint32_t)(int32_t)(int16_t)rs; break;
    case 26: ra = ppc::cntlzw(rs); break;
    case 24: ra = (rb & 0x20) ? 0u : (rs << (rb & 31)); break;
    case 536: ra = (rb & 0x20) ? 0u : (rs >> (rb & 31)); break;
    case 792: ra = ppc::sraw(c, rs, rb); break;
    case 824: ra = ppc::srawi(c, rs, int(bits(w, 16, 5))); break;
    default: logical = false; break;
  }
  if (logical) { if (rc) ppc::cr0(c, ra); return Flow::Next; }
  const uint32_t spr = (bits(w, 16, 5) << 5) | bits(w, 11, 5);
  switch (xo) {
    case 0: ppc::cr_set_s(c, crfd, (int32_t)ra, (int32_t)rb); return Flow::Next;
    case 32: ppc::cr_set_u(c, crfd, ra, rb); return Flow::Next;
    case 19: rd = ppc::mfcr(c); return Flow::Next;
    case 144: ppc::mtcrf(c, bits(w, 12, 8), rs); return Flow::Next;
    case 339:
      if (spr == 1) rd = ((c.so << 31) | (c.ov << 30) | (c.ca << 29));
      else if (spr == 8) rd = c.lr;
      else if (spr == 9) rd = c.ctr;
      else return Flow::Unknown;
      return Flow::Next;
    case 467:
      if (spr == 1) { c.so = (rs >> 31) & 1; c.ov = (rs >> 30) & 1; c.ca = (rs >> 29) & 1; }
      else if (spr == 8) c.lr = rs;
      else if (spr == 9) c.ctr = rs;
      else return Flow::Unknown;
      return Flow::Next;
    default: return Flow::Unknown;
  }
}
// Runs a whole function as the recompiled code would: enter, then instruction by instruction.
// False when a word is outside this reference, control leaves the range, or the step limit ends
// a loop that the generator should have bounded.
inline bool run(ppc::Context& c, const std::vector<uint32_t>& words, uint32_t base, uint64_t limit = 1u << 22) {
  ppc::enter(c, base);
  uint32_t pc = base;
  for (uint64_t steps = 0; steps < limit; ++steps) {
    const uint32_t index = (pc - base) / 4;
    if (pc < base || index >= words.size()) return false;
    uint32_t target = 0;
    switch (step(c, words[index], pc, target)) {
      case Flow::Next: pc += 4; break;
      case Flow::Jump: pc = target; break;
      case Flow::Return: return true;
      case Flow::Unknown: return false;
    }
  }
  return false;
}
} // namespace reference
