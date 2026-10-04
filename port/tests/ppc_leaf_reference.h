// Shared by the translator's native tests: an independent reference that executes guest words one
// at a time with the expressions port/recomp/emit.py writes, canonical encoders and random
// contexts. The runtime stubs are in ppc_leaf_host.h. Include from exactly one source file per test.
// No game, ISO, window, device or interpreter dependency.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "leaf_translator.h"
#include "ppc_leaf_host.h"
#include <cstdio>
#include <cstring>
#include <vector>

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
  c.call_depth &= 0xFFu; // Far from the depth limit of ppc::call.
}
// A guest address that reaches every path of the ppc.h accessors: the four RAM mirrors, the first
// and last bytes of RAM, the 64 KB write-generation block boundaries, the locked cache and its
// edges, memory-mapped I/O and unmapped space.
inline uint32_t random_address(Random& random) {
  static const uint32_t mirrors[] = {0x80000000u, 0x80000000u, 0x80000000u, 0xC0000000u, 0x00000000u, 0x40000000u};
  const uint32_t mirror = mirrors[random.below(6)];
  const int32_t nudge = int32_t(random.below(48)) - 24;
  switch (random.below(10)) {
    case 0: return mirror + random.below(0x4000);                                      // start of RAM
    case 1: return mirror + ppc::RAM_SIZE + uint32_t(nudge);                            // around the last byte
    case 2: return mirror + (random.below(ppc::RAM_WATCH_COUNT) << 16) + uint32_t(nudge); // a block boundary
    case 3: case 4: return mirror + (random.u32() % ppc::RAM_SIZE);                    // anywhere in RAM
    case 5: return ppc::LC_BASE + random.below(ppc::LC_SIZE);                           // locked cache
    case 6: return (random.below(2) ? ppc::LC_BASE : ppc::LC_BASE + ppc::LC_SIZE) + uint32_t(nudge);
    case 7: return 0xCC000000u + random.below(0x8000);                                  // I/O
    case 8: return 0xCD000000u + (random.below(0x800) << 2);
    default: return random.u32();
  }
}
// The doubles that separate one float rule from another: zeros, infinities, quiet and signalling
// NaNs with payloads, denormals, the limits of float and of int32, and values whose low 28 bits
// matter to ppc::f25.
constexpr uint64_t kSpecialDoubles[] = {
    0x0000000000000000ull, 0x8000000000000000ull, 0x7FF0000000000000ull, 0xFFF0000000000000ull,
    0x7FF8000000000000ull, 0xFFF8000000000001ull, 0x7FF0000000000001ull, 0x7FF4000000000123ull,
    0x7FFC0000DEADBEEFull, 0xFFF7FFFFFFFFFFFFull, 0x0000000000000001ull, 0x000FFFFFFFFFFFFFull,
    0x0010000000000000ull, 0x7FEFFFFFFFFFFFFFull, 0x3FF0000000000000ull, 0xBFF0000000000000ull,
    0x41DFFFFFFFC00000ull, 0x41E0000000000000ull, 0xC1E0000000000000ull, 0xC1E0000000200000ull,
    0x41DFFFFFFFE00000ull, 0x3FE0000000000000ull, 0xBFE0000000000000ull, 0x3FF8000000000000ull,
    0x4004000000000000ull, 0xC004000000000000ull, 0x47EFFFFFE0000000ull, 0x47EFFFFFF0000000ull,
    0x47F0000000000000ull, 0x36A0000000000000ull, 0x3690000000000000ull, 0x380FFFFFC0000000ull,
    0x3810000000000000ull, 0x3FF0000008000000ull, 0x3FF000000FFFFFFFull, 0x3FF0000007FFFFFFull,
    0x3FF0000010000000ull, 0x3FEFFFFFFFFFFFFFull, 0x4330000000000000ull, 0x4340000000000001ull,
};
constexpr size_t kSpecialDoubleCount = sizeof kSpecialDoubles / sizeof kSpecialDoubles[0];
inline uint64_t random_double_bits(Random& random) {
  switch (random.below(5)) {
    case 0: return kSpecialDoubles[random.below(uint32_t(kSpecialDoubleCount))];
    case 1: { // A float of moderate size, as a guest that only loads singles would hold.
      const uint32_t bits = (random.u32() & 0x807FFFFFu) | ((100u + random.below(56)) << 23);
      return ppc::double_to_bits(ppc::float_bits_to_double(bits));
    }
    case 2: return ppc::double_to_bits((int32_t(random.below(4001)) - 2000) * 0.5); // small, exact halves
    case 3: { // Any double of moderate exponent.
      return (random.next() & 0x800FFFFFFFFFFFFFull) | (uint64_t(1023 - 40 + random.below(80)) << 52);
    }
    default: return random.next();
  }
}
// randomize, then values that reach the memory and float paths: half of the GPRs hold an
// interesting address and every FPR half an interesting double.
inline void randomize_rich(ppc::Context& c, Random& random) {
  randomize(c, random);
  for (auto& r : c.r) if (random.below(2) == 0) r = random_address(random);
  for (auto& f : c.f) { f.u0 = random_double_bits(random); f.u1 = random_double_bits(random); }
}
inline bool same(const ppc::Context& a, const ppc::Context& b) { return std::memcmp(&a, &b, sizeof a) == 0; }
inline void describe(const ppc::Context& actual, const ppc::Context& expected) {
  for (int i = 0; i < 32; ++i)
    if (actual.r[i] != expected.r[i]) std::printf("  r%d: native %08X reference %08X\n", i, actual.r[i], expected.r[i]);
  for (int i = 0; i < 32; ++i) {
    if (actual.f[i].u0 != expected.f[i].u0)
      std::printf("  f%d.ps0: native %016llX reference %016llX\n", i, (unsigned long long)actual.f[i].u0, (unsigned long long)expected.f[i].u0);
    if (actual.f[i].u1 != expected.f[i].u1)
      std::printf("  f%d.ps1: native %016llX reference %016llX\n", i, (unsigned long long)actual.f[i].u1, (unsigned long long)expected.f[i].u1);
  }
  for (int i = 0; i < 8; ++i)
    if (actual.cr[i] != expected.cr[i]) std::printf("  cr%d: native %02X reference %02X\n", i, actual.cr[i], expected.cr[i]);
#define MU_FIELD(name) if (actual.name != expected.name) \
    std::printf("  " #name ": native %08X reference %08X\n", unsigned(actual.name), unsigned(expected.name));
  MU_FIELD(lr) MU_FIELD(ctr) MU_FIELD(ca) MU_FIELD(so) MU_FIELD(ov) MU_FIELD(backedges)
  MU_FIELD(last_pc) MU_FIELD(trace_pos) MU_FIELD(dec) MU_FIELD(fpscr) MU_FIELD(call_depth)
#undef MU_FIELD
  if (actual.tb != expected.tb)
    std::printf("  tb: native %016llX reference %016llX\n", (unsigned long long)actual.tb, (unsigned long long)expected.tb);
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
// Float and paired-single register forms: four 5-bit fields and a 5 or 10 bit extended opcode.
inline uint32_t aform(uint32_t opcode, uint32_t d, uint32_t a, uint32_t b, uint32_t c, uint32_t xo5, uint32_t rc = 0) {
  return (opcode << 26) | (d << 21) | (a << 16) | (b << 11) | (c << 6) | (xo5 << 1) | rc;
}
inline uint32_t xoform(uint32_t opcode, uint32_t d, uint32_t a, uint32_t b, uint32_t xo10, uint32_t rc = 0) {
  return (opcode << 26) | (d << 21) | (a << 16) | (b << 11) | (xo10 << 1) | rc;
}
inline uint32_t psq_d(uint32_t opcode, uint32_t fd, uint32_t ra, uint32_t w, uint32_t i, uint32_t d12) {
  return (opcode << 26) | (fd << 21) | (ra << 16) | (w << 15) | (i << 12) | (d12 & 0xFFFu);
}
inline uint32_t psq_x(uint32_t fd, uint32_t ra, uint32_t rb, uint32_t w, uint32_t i, uint32_t xo6) {
  return (4u << 26) | (fd << 21) | (ra << 16) | (rb << 11) | (w << 10) | (i << 7) | (xo6 << 1);
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
inline uint32_t bcctr(uint32_t bo, uint32_t bi, uint32_t lk = 0) { return xlform(bo, bi, 0, 528) | lk; }
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
// The function the word belongs to: emit.py decides between a jump, a call and a tail call by
// whether the target is one of the function's own addresses.
struct Range { uint32_t base, bytes; bool inside(uint32_t address) const { return address >= base && address - base < bytes; } };
// One invocation of a function, as emit.py writes it for cave code: `uint32_t lrs[32]; uint32_t
// lrn = 0;` when the function makes local calls (info.local_returns) and `const uint32_t entry_lr
// = c.lr;` for blrl. A `bl` to one of the function's own addresses is a local call.
struct Frame {
  uint32_t lrs[32];
  uint32_t lrn = 0;
  uint32_t entry_lr = 0;
  bool local = false;                    // The function has local calls.
  std::vector<uint32_t> local_returns;   // Their return addresses.
  // What is known about callees: (address, computed-return delta) pairs.
  const std::vector<std::pair<uint32_t, uint32_t>>* resumes = nullptr;
  // `if (ppc::local_return(lrs, lrn, t)) { if (t == r) goto L_r; ... }`
  bool returns_locally(uint32_t t) {
    if (!local || !ppc::local_return(lrs, lrn, t)) return false;
    for (uint32_t r : local_returns) if (r == t) return true;
    return false;
  }
};
// The one place where emit.py's C++ does not fix the result: `a + b`, `a * c` and the fused
// multiply-adds with two or more NaN operands. The processor returns the payload of whichever
// NaN the compiler put first, and C++ lets the compiler choose, so the recompiled game, a stencil
// and this reference may each return a different one of the operands' NaNs. The reference notes
// when it executes such an operation; the tests then accept either NaN (single instructions) or
// set the sample aside (longer functions, where the difference would spread).
inline bool g_nan_order_open = false;
inline bool is_nan(double value) { return value != value; }
inline void note_nan_order(double x, double y) { if (is_nan(x) && is_nan(y)) g_nan_order_open = true; }
inline void note_nan_order(double x, double y, double z) {
  if (int(is_nan(x)) + int(is_nan(y)) + int(is_nan(z)) >= 2) g_nan_order_open = true;
}
// Float and paired-single register forms, as Emitter._emit_float writes them.
inline Flow step_float(ppc::Context& c, uint32_t w) {
  const uint32_t op = w >> 26, xo5 = bits(w, 26, 5), xo10 = bits(w, 21, 10);
  ppc::FPR& d = c.f[bits(w, 6, 5)];
  ppc::FPR& a = c.f[bits(w, 11, 5)];
  ppc::FPR& b = c.f[bits(w, 16, 5)];
  ppc::FPR& k = c.f[bits(w, 21, 5)]; // fC
  const int crfd = int(bits(w, 6, 3));
  switch (xo5) { // Additions, multiplications and fused multiply-adds of every width.
    case 21: note_nan_order(a.ps0, b.ps0); if (op == 4) note_nan_order(a.ps1, b.ps1); break;
    case 25: note_nan_order(a.ps0, k.ps0); if (op == 4) note_nan_order(a.ps1, k.ps1); break;
    case 28: case 29: case 30: case 31:
      note_nan_order(a.ps0, k.ps0, b.ps0); if (op == 4) note_nan_order(a.ps1, k.ps1, b.ps1); break;
    case 12: if (op == 4) { note_nan_order(a.ps0, k.ps0); note_nan_order(a.ps1, k.ps0); } break;
    case 13: if (op == 4) { note_nan_order(a.ps0, k.ps1); note_nan_order(a.ps1, k.ps1); } break;
    case 14: if (op == 4) { note_nan_order(a.ps0, k.ps0, b.ps0); note_nan_order(a.ps1, k.ps0, b.ps1); } break;
    case 15: if (op == 4) { note_nan_order(a.ps0, k.ps1, b.ps0); note_nan_order(a.ps1, k.ps1, b.ps1); } break;
    case 10: case 11: if (op == 4) note_nan_order(a.ps0, b.ps1); break;
    default: break;
  }
  if (op == 59) {
    switch (xo5) {
      case 21: d.ps0 = d.ps1 = ppc::fs(a.ps0 + b.ps0); return Flow::Next;
      case 20: d.ps0 = d.ps1 = ppc::fs(a.ps0 - b.ps0); return Flow::Next;
      case 25: d.ps0 = d.ps1 = ppc::fs(a.ps0 * ppc::f25(k.ps0)); return Flow::Next;
      case 18: d.ps0 = d.ps1 = ppc::fs(a.ps0 / b.ps0); return Flow::Next;
      case 29: d.ps0 = d.ps1 = ppc::fs(ppc::fmadd(a.ps0, ppc::f25(k.ps0), b.ps0)); return Flow::Next;
      case 28: d.ps0 = d.ps1 = ppc::fs(ppc::fmsub(a.ps0, ppc::f25(k.ps0), b.ps0)); return Flow::Next;
      case 31: d.ps0 = d.ps1 = ppc::fs(ppc::fnmadd(a.ps0, ppc::f25(k.ps0), b.ps0)); return Flow::Next;
      case 30: d.ps0 = d.ps1 = ppc::fs(ppc::fnmsub(a.ps0, ppc::f25(k.ps0), b.ps0)); return Flow::Next;
      case 24: d.ps0 = d.ps1 = ppc::fres(b.ps0); return Flow::Next;
      default: return Flow::Unknown;
    }
  }
  if (op == 63) {
    switch (xo5) {
      case 21: d.ps0 = a.ps0 + b.ps0; return Flow::Next;
      case 20: d.ps0 = a.ps0 - b.ps0; return Flow::Next;
      case 25: d.ps0 = a.ps0 * k.ps0; return Flow::Next;
      case 18: d.ps0 = a.ps0 / b.ps0; return Flow::Next;
      case 29: d.ps0 = ppc::fmadd(a.ps0, k.ps0, b.ps0); return Flow::Next;
      case 28: d.ps0 = ppc::fmsub(a.ps0, k.ps0, b.ps0); return Flow::Next;
      case 31: d.ps0 = ppc::fnmadd(a.ps0, k.ps0, b.ps0); return Flow::Next;
      case 30: d.ps0 = ppc::fnmsub(a.ps0, k.ps0, b.ps0); return Flow::Next;
      case 26: d.ps0 = ppc::frsqrte(b.ps0); return Flow::Next;
      case 23: d.ps0 = (a.ps0 >= -0.0) ? k.ps0 : b.ps0; return Flow::Next;
      default: break;
    }
    switch (xo10) {
      case 12: d.ps0 = d.ps1 = ppc::fs(b.ps0); return Flow::Next;
      case 72: d.u0 = b.u0; return Flow::Next;
      case 40: d.u0 = b.u0 ^ 0x8000000000000000ull; return Flow::Next;
      case 264: d.u0 = b.u0 & 0x7FFFFFFFFFFFFFFFull; return Flow::Next;
      case 136: d.u0 = b.u0 | 0x8000000000000000ull; return Flow::Next;
      case 0: case 32: ppc::fcmp(c, crfd, a.ps0, b.ps0); return Flow::Next;
      case 14: d.u0 = ppc::fctiw(b.ps0, false); return Flow::Next;
      case 15: d.u0 = ppc::fctiw(b.ps0, true); return Flow::Next;
      case 583: d.u0 = 0xFFF8000000000000ull | c.fpscr; return Flow::Next;
      case 711: {
        uint32_t m = 0;
        for (int i = 0; i < 8; ++i) if (bits(w, 7, 8) & (0x80u >> i)) m |= 0xFu << (28 - 4 * i);
        c.fpscr = (c.fpscr & ~m) | ((uint32_t)b.u0 & m); ppc::update_mxcsr(c);
        return Flow::Next;
      }
      case 70: c.fpscr &= ~(0x80000000u >> bits(w, 6, 5)); ppc::update_mxcsr(c); return Flow::Next;
      case 38: c.fpscr |= 0x80000000u >> bits(w, 6, 5); ppc::update_mxcsr(c); return Flow::Next;
      case 134: { const int sh = 28 - 4 * crfd;
                  c.fpscr = (c.fpscr & ~(0xFu << sh)) | (bits(w, 16, 4) << sh); ppc::update_mxcsr(c); }
                return Flow::Next;
      case 64: c.cr[crfd] = (uint8_t)((c.fpscr >> (28 - 4 * bits(w, 11, 3))) & 15); return Flow::Next;
      default: return Flow::Unknown;
    }
  }
  // op 4
  switch (xo5) {
    case 21: { double x = a.ps0 + b.ps0, y = a.ps1 + b.ps1; d.ps0 = ppc::fs(x); d.ps1 = ppc::fs(y); } return Flow::Next;
    case 20: { double x = a.ps0 - b.ps0, y = a.ps1 - b.ps1; d.ps0 = ppc::fs(x); d.ps1 = ppc::fs(y); } return Flow::Next;
    case 25: { double x = a.ps0 * ppc::f25(k.ps0), y = a.ps1 * ppc::f25(k.ps1); d.ps0 = ppc::fs(x); d.ps1 = ppc::fs(y); } return Flow::Next;
    case 18: { double x = a.ps0 / b.ps0, y = a.ps1 / b.ps1; d.ps0 = ppc::fs(x); d.ps1 = ppc::fs(y); } return Flow::Next;
    case 12: { double q = ppc::f25(k.ps0); double x = a.ps0 * q, y = a.ps1 * q; d.ps0 = ppc::fs(x); d.ps1 = ppc::fs(y); } return Flow::Next;
    case 13: { double q = ppc::f25(k.ps1); double x = a.ps0 * q, y = a.ps1 * q; d.ps0 = ppc::fs(x); d.ps1 = ppc::fs(y); } return Flow::Next;
    case 29: { double x = ppc::fmadd(a.ps0, ppc::f25(k.ps0), b.ps0), y = ppc::fmadd(a.ps1, ppc::f25(k.ps1), b.ps1); d.ps0 = ppc::fs(x); d.ps1 = ppc::fs(y); } return Flow::Next;
    case 28: { double x = ppc::fmsub(a.ps0, ppc::f25(k.ps0), b.ps0), y = ppc::fmsub(a.ps1, ppc::f25(k.ps1), b.ps1); d.ps0 = ppc::fs(x); d.ps1 = ppc::fs(y); } return Flow::Next;
    case 31: { double x = ppc::fnmadd(a.ps0, ppc::f25(k.ps0), b.ps0), y = ppc::fnmadd(a.ps1, ppc::f25(k.ps1), b.ps1); d.ps0 = ppc::fs(x); d.ps1 = ppc::fs(y); } return Flow::Next;
    case 30: { double x = ppc::fnmsub(a.ps0, ppc::f25(k.ps0), b.ps0), y = ppc::fnmsub(a.ps1, ppc::f25(k.ps1), b.ps1); d.ps0 = ppc::fs(x); d.ps1 = ppc::fs(y); } return Flow::Next;
    case 14: { double q = ppc::f25(k.ps0); double x = ppc::fmadd(a.ps0, q, b.ps0), y = ppc::fmadd(a.ps1, q, b.ps1); d.ps0 = ppc::fs(x); d.ps1 = ppc::fs(y); } return Flow::Next;
    case 15: { double q = ppc::f25(k.ps1); double x = ppc::fmadd(a.ps0, q, b.ps0), y = ppc::fmadd(a.ps1, q, b.ps1); d.ps0 = ppc::fs(x); d.ps1 = ppc::fs(y); } return Flow::Next;
    case 10: { double x = a.ps0 + b.ps1, y = k.ps1; d.ps0 = ppc::fs(x); d.ps1 = ppc::fs(y); } return Flow::Next;
    case 11: { double x = k.ps0, y = a.ps0 + b.ps1; d.ps0 = ppc::fs(x); d.ps1 = ppc::fs(y); } return Flow::Next;
    case 24: { double x = ppc::fres(b.ps0), y = ppc::fres(b.ps1); d.ps0 = ppc::fs(x); d.ps1 = ppc::fs(y); } return Flow::Next;
    case 26: { double x = ppc::frsqrte(b.ps0), y = ppc::frsqrte(b.ps1); d.ps0 = ppc::fs(x); d.ps1 = ppc::fs(y); } return Flow::Next;
    case 23: { double x = (a.ps0 >= -0.0) ? k.ps0 : b.ps0, y = (a.ps1 >= -0.0) ? k.ps1 : b.ps1; d.ps0 = x; d.ps1 = y; } return Flow::Next;
    default: break;
  }
  switch (xo10) {
    case 72: { uint64_t x = b.u0, y = b.u1; d.u0 = x; d.u1 = y; } return Flow::Next;
    case 40: { uint64_t x = b.u0 ^ 0x8000000000000000ull, y = b.u1 ^ 0x8000000000000000ull; d.u0 = x; d.u1 = y; } return Flow::Next;
    case 264: { uint64_t x = b.u0 & 0x7FFFFFFFFFFFFFFFull, y = b.u1 & 0x7FFFFFFFFFFFFFFFull; d.u0 = x; d.u1 = y; } return Flow::Next;
    case 136: { uint64_t x = b.u0 | 0x8000000000000000ull, y = b.u1 | 0x8000000000000000ull; d.u0 = x; d.u1 = y; } return Flow::Next;
    case 528: { uint64_t x = a.u0, y = b.u0; d.u0 = x; d.u1 = y; } return Flow::Next;
    case 560: { uint64_t x = a.u0, y = b.u1; d.u0 = x; d.u1 = y; } return Flow::Next;
    case 592: { uint64_t x = a.u1, y = b.u0; d.u0 = x; d.u1 = y; } return Flow::Next;
    case 624: { uint64_t x = a.u1, y = b.u1; d.u0 = x; d.u1 = y; } return Flow::Next;
    case 0: case 32: ppc::fcmp(c, crfd, a.ps0, b.ps0); return Flow::Next;
    case 64: case 96: ppc::fcmp(c, crfd, a.ps1, b.ps1); return Flow::Next;
    default: return Flow::Unknown;
  }
}
// Each case is the statement emit.py writes for that instruction; the register operands are
// references so an instruction that names one register twice behaves as the emitted code does.
inline Flow step(ppc::Context& c, uint8_t* m, uint32_t w, uint32_t addr, const Range& range, uint32_t& target, Frame& frame) {
  const uint32_t op = w >> 26;
  uint32_t& rd = c.r[bits(w, 6, 5)];
  uint32_t& rs = rd;
  uint32_t& ra = c.r[bits(w, 11, 5)];
  uint32_t& rb = c.r[bits(w, 16, 5)];
  ppc::FPR& fd = c.f[bits(w, 6, 5)];
  const bool ra0 = bits(w, 11, 5) == 0;
  const uint32_t uimm = w & 0xFFFFu;
  const int32_t simm = (w & 0x8000u) ? int32_t(uimm) - 0x10000 : int32_t(uimm);
  const uint32_t hsimm = uint32_t(simm); // hexs(simm)
  const int crfd = int(bits(w, 6, 3));
  const bool rc = w & 1;
  const uint32_t ea_d = (ra0 ? 0u : ra) + hsimm;   // emit.py ea_d
  const uint32_t ea_x = (ra0 ? 0u : ra) + rb;      // emit.py ea_x
  // The body of b and bc: a jump inside the function, a call or a tail call.
  // After a tail call, or at a return: back to a local caller of this invocation, or out.
  const auto leave = [&](uint32_t t) {
    if (frame.returns_locally(t)) { target = t; return Flow::Jump; }
    return Flow::Return;
  };
  const auto transfer = [&](uint32_t destination) {
    target = destination;
    if (w & 1) {
      if (range.inside(target)) { // _local_call
        c.lr = addr + 4; frame.lrs[frame.lrn++ & 31u] = addr + 4;
        return Flow::Jump;
      }
      // _call, with the resume test when the callee has computed returns that land in this function.
      bool checked = false;
      if (frame.resumes)
        for (const auto& item : *frame.resumes)
          if (item.first == target && range.inside(addr + 4 + item.second)) checked = true;
      if (checked) ++ppc::g_computed_return_checks;
      c.lr = addr + 4; ppc::call(c, m, target);
      if (checked) {
        // emit.py tests the deltas in ascending order; at most one can match.
        for (const auto& item : *frame.resumes)
          if (item.first == destination && range.inside(addr + 4 + item.second) && c.lr == addr + 4 + item.second) {
            ++ppc::g_resumed_returns; target = addr + 4 + item.second; return Flow::Jump;
          }
      }
      return Flow::Next;
    }
    if (range.inside(target)) { if (target <= addr) ppc::backedge(c); return Flow::Jump; }
    const uint32_t tl = c.lr; // _tail_expr
    ppc::call(c, m, target);
    return leave(tl);
  };
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
    case 20: { const uint32_t mask = emit_mask(bits(w, 21, 5), bits(w, 26, 5));
               ra = (ra & ~mask) | (_rotl(rs, int(bits(w, 16, 5))) & mask); }
             if (rc) ppc::cr0(c, ra); return Flow::Next;
    // ---- D-form loads and stores ----
    case 32: rd = ppc::ld32(c, m, ea_d); return Flow::Next;
    case 34: rd = ppc::ld8(c, m, ea_d); return Flow::Next;
    case 40: rd = ppc::ld16(c, m, ea_d); return Flow::Next;
    case 42: rd = (uint32_t)(int32_t)(int16_t)ppc::ld16(c, m, ea_d); return Flow::Next;
    case 33: { uint32_t ea = ra + hsimm; rd = ppc::ld32(c, m, ea); ra = ea; } return Flow::Next;
    case 35: { uint32_t ea = ra + hsimm; rd = ppc::ld8(c, m, ea); ra = ea; } return Flow::Next;
    case 41: { uint32_t ea = ra + hsimm; rd = ppc::ld16(c, m, ea); ra = ea; } return Flow::Next;
    case 43: { uint32_t ea = ra + hsimm; rd = (uint32_t)(int32_t)(int16_t)ppc::ld16(c, m, ea); ra = ea; } return Flow::Next;
    case 36: ppc::st32(c, m, ea_d, rs); return Flow::Next;
    case 38: ppc::st8(c, m, ea_d, rs); return Flow::Next;
    case 44: ppc::st16(c, m, ea_d, rs); return Flow::Next;
    case 37: { uint32_t ea = ra + hsimm; ppc::st32(c, m, ea, rs); ra = ea; } return Flow::Next;
    case 39: { uint32_t ea = ra + hsimm; ppc::st8(c, m, ea, rs); ra = ea; } return Flow::Next;
    case 45: { uint32_t ea = ra + hsimm; ppc::st16(c, m, ea, rs); ra = ea; } return Flow::Next;
    case 46: { uint32_t ea = ea_d; for (int i = int(bits(w, 6, 5)); i < 32; ++i, ea += 4) c.r[i] = ppc::ld32(c, m, ea); } return Flow::Next;
    case 47: { uint32_t ea = ea_d; for (int i = int(bits(w, 6, 5)); i < 32; ++i, ea += 4) ppc::st32(c, m, ea, c.r[i]); } return Flow::Next;
    case 48: fd.ps0 = fd.ps1 = ppc::float_bits_to_double(ppc::ld32(c, m, ea_d)); return Flow::Next;
    case 49: { uint32_t ea = ra + hsimm; fd.ps0 = fd.ps1 = ppc::float_bits_to_double(ppc::ld32(c, m, ea)); ra = ea; } return Flow::Next;
    case 50: fd.u0 = ppc::ld64(c, m, ea_d); return Flow::Next;
    case 51: { uint32_t ea = ra + hsimm; fd.u0 = ppc::ld64(c, m, ea); ra = ea; } return Flow::Next;
    case 52: ppc::st32(c, m, ea_d, ppc::double_to_float_bits(fd.ps0)); return Flow::Next;
    case 53: { uint32_t ea = ra + hsimm; ppc::st32(c, m, ea, ppc::double_to_float_bits(fd.ps0)); ra = ea; } return Flow::Next;
    case 54: ppc::st64(c, m, ea_d, fd.u0); return Flow::Next;
    case 55: { uint32_t ea = ra + hsimm; ppc::st64(c, m, ea, fd.u0); ra = ea; } return Flow::Next;
    case 56: case 57: case 60: case 61: { // psq_l, psq_lu, psq_st, psq_stu
      const uint32_t d12 = w & 0xFFFu, s12 = (d12 & 0x800u) ? d12 | 0xFFFFF000u : d12;
      const uint32_t fr = bits(w, 6, 5), qw = bits(w, 16, 1), qi = bits(w, 17, 3);
      const bool load = op < 60;
      if (op & 1) {
        uint32_t ea = ra + s12;
        if (load) ppc::psq_load(c, m, ea, fr, qw, qi); else ppc::psq_store(c, m, ea, fr, qw, qi);
        ra = ea;
      } else {
        const uint32_t ea = (ra0 ? 0u : ra) + s12;
        if (load) ppc::psq_load(c, m, ea, fr, qw, qi); else ppc::psq_store(c, m, ea, fr, qw, qi);
      }
      return Flow::Next;
    }
    case 59: case 63: return step_float(c, w);
    case 4: {
      const uint32_t xo6 = bits(w, 25, 6);
      if (xo6 == 6 || xo6 == 7 || xo6 == 38 || xo6 == 39) {
        const uint32_t fr = bits(w, 6, 5), qw = bits(w, 21, 1), qi = bits(w, 22, 3);
        const bool load = (xo6 & 1) == 0;
        if (xo6 >= 38) {
          uint32_t ea = ra + rb;
          if (load) ppc::psq_load(c, m, ea, fr, qw, qi); else ppc::psq_store(c, m, ea, fr, qw, qi);
          ra = ea;
        } else {
          if (load) ppc::psq_load(c, m, ea_x, fr, qw, qi); else ppc::psq_store(c, m, ea_x, fr, qw, qi);
        }
        return Flow::Next;
      }
      return step_float(c, w);
    }
    case 18: {
      const uint32_t li = (w & 0x02000000u) ? (w & 0x03FFFFFCu) | 0xFC000000u : (w & 0x03FFFFFCu);
      return transfer((w & 2) ? li : addr + li);
    }
    case 16: {
      const uint32_t bd = (w & 0x8000u) ? (w & 0xFFFCu) | 0xFFFF0000u : (w & 0xFFFCu);
      const uint32_t destination = (w & 2) ? bd : addr + bd;
      if (!condition(c, bits(w, 6, 5), bits(w, 11, 5))) return Flow::Next;
      return transfer(destination);
    }
    case 19: {
      const uint32_t xo = bits(w, 21, 10);
      if (xo == 16) {
        if (!condition(c, bits(w, 6, 5), bits(w, 11, 5))) return Flow::Next;
        if (w & 1) { // blrl
          const uint32_t t = c.lr;
          c.lr = addr + 4;
          if (frame.returns_locally(t)) { target = t; return Flow::Jump; }
          if (t == frame.entry_lr) return Flow::Return;
          ppc::call(c, m, t);
          return Flow::Next;
        }
        return leave(c.lr);
      }
      if (xo == 528) { // bcctr: `ppc::call(c, m, c.ctr); return;` or, with LK, a call through CTR.
        if (!(bits(w, 6, 5) & 4)) return Flow::Unknown;
        if (!condition(c, bits(w, 6, 5), bits(w, 11, 5))) return Flow::Next;
        if (w & 1) { uint32_t t = c.ctr; c.lr = addr + 4; ppc::call(c, m, t); return Flow::Next; }
        const uint32_t tl = c.lr;
        ppc::call(c, m, c.ctr);
        return leave(tl);
      }
      if (xo == 150) return Flow::Next; // isync
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
    case 512: c.cr[crfd] = (uint8_t)((c.so << 3) | (c.ov << 2) | (c.ca << 1)); c.so = c.ov = c.ca = 0; return Flow::Next;
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
    // sync, eieio and the cache hints: emit.py writes nothing.
    case 598: case 854: case 54: case 86: case 246: case 278: case 470: case 982: return Flow::Next;
    // ---- indexed loads and stores ----
    case 23: rd = ppc::ld32(c, m, ea_x); return Flow::Next;
    case 87: rd = ppc::ld8(c, m, ea_x); return Flow::Next;
    case 279: rd = ppc::ld16(c, m, ea_x); return Flow::Next;
    case 343: rd = (uint32_t)(int32_t)(int16_t)ppc::ld16(c, m, ea_x); return Flow::Next;
    case 55: { uint32_t ea = ra + rb; rd = ppc::ld32(c, m, ea); ra = ea; } return Flow::Next;
    case 119: { uint32_t ea = ra + rb; rd = ppc::ld8(c, m, ea); ra = ea; } return Flow::Next;
    case 311: { uint32_t ea = ra + rb; rd = ppc::ld16(c, m, ea); ra = ea; } return Flow::Next;
    case 375: { uint32_t ea = ra + rb; rd = (uint32_t)(int32_t)(int16_t)ppc::ld16(c, m, ea); ra = ea; } return Flow::Next;
    case 151: ppc::st32(c, m, ea_x, rs); return Flow::Next;
    case 215: ppc::st8(c, m, ea_x, rs); return Flow::Next;
    case 407: ppc::st16(c, m, ea_x, rs); return Flow::Next;
    case 183: { uint32_t ea = ra + rb; ppc::st32(c, m, ea, rs); ra = ea; } return Flow::Next;
    case 247: { uint32_t ea = ra + rb; ppc::st8(c, m, ea, rs); ra = ea; } return Flow::Next;
    case 439: { uint32_t ea = ra + rb; ppc::st16(c, m, ea, rs); ra = ea; } return Flow::Next;
    case 534: rd = ppc::ld32r(c, m, ea_x); return Flow::Next;
    case 790: rd = ppc::ld16r(c, m, ea_x); return Flow::Next;
    case 662: ppc::st32r(c, m, ea_x, rs); return Flow::Next;
    case 918: ppc::st16r(c, m, ea_x, rs); return Flow::Next;
    case 535: fd.ps0 = fd.ps1 = ppc::float_bits_to_double(ppc::ld32(c, m, ea_x)); return Flow::Next;
    case 567: { uint32_t ea = ra + rb; fd.ps0 = fd.ps1 = ppc::float_bits_to_double(ppc::ld32(c, m, ea)); ra = ea; } return Flow::Next;
    case 599: fd.u0 = ppc::ld64(c, m, ea_x); return Flow::Next;
    case 631: { uint32_t ea = ra + rb; fd.u0 = ppc::ld64(c, m, ea); ra = ea; } return Flow::Next;
    case 663: ppc::st32(c, m, ea_x, ppc::double_to_float_bits(fd.ps0)); return Flow::Next;
    case 695: { uint32_t ea = ra + rb; ppc::st32(c, m, ea, ppc::double_to_float_bits(fd.ps0)); ra = ea; } return Flow::Next;
    case 727: ppc::st64(c, m, ea_x, fd.u0); return Flow::Next;
    case 759: { uint32_t ea = ra + rb; ppc::st64(c, m, ea, fd.u0); ra = ea; } return Flow::Next;
    case 983: ppc::st32(c, m, ea_x, (uint32_t)fd.u0); return Flow::Next;
    default: return Flow::Unknown;
  }
}
// Runs a whole function as the recompiled code would: enter, then instruction by instruction.
// False when a word is outside this reference, control leaves the range, or the step limit ends
// a loop that the generator should have bounded.
inline bool run(ppc::Context& c, uint8_t* m, const std::vector<uint32_t>& words, uint32_t base, uint64_t limit = 1u << 22,
                const std::vector<std::pair<uint32_t, uint32_t>>* resumes = nullptr) {
  const Range range{base, uint32_t(words.size() * 4)};
  Frame frame;
  frame.entry_lr = c.lr;
  frame.resumes = resumes;
  // analyze.py: every `bl` (b or bc with LK) whose target is one of the function's own addresses.
  for (size_t i = 0; i < words.size(); ++i) {
    const uint32_t w = words[i], op = w >> 26, at = base + uint32_t(i) * 4;
    if ((op != 18 && op != 16) || !(w & 1)) continue;
    uint32_t d = op == 18 ? (w & 0x03FFFFFCu) : (w & 0xFFFCu);
    if (op == 18 && (d & 0x02000000u)) d |= 0xFC000000u;
    if (op == 16 && (d & 0x8000u)) d |= 0xFFFF0000u;
    if (!range.inside((w & 2) ? d : at + d)) continue;
    frame.local = true;
    frame.local_returns.push_back(at + 4);
  }
  ppc::enter(c, base);
  uint32_t pc = base;
  for (uint64_t steps = 0; steps < limit; ++steps) {
    const uint32_t index = (pc - base) / 4;
    if (pc < base || index >= words.size()) return false;
    uint32_t target = 0;
    switch (step(c, m, words[index], pc, range, target, frame)) {
      case Flow::Next: pc += 4; break;
      case Flow::Jump: pc = target; break;
      case Flow::Return: return true;
      case Flow::Unknown: return false;
    }
  }
  return false;
}
// For functions that touch no memory and call nothing (the integer and branch tests).
inline bool run(ppc::Context& c, const std::vector<uint32_t>& words, uint32_t base, uint64_t limit = 1u << 22) {
  return run(c, nullptr, words, base, limit);
}
} // namespace reference
