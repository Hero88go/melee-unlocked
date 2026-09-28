// A stand-in for the recompiler runtime's ppc.h, for translated functions compiled INTO the native game.
//
// Some SDK routines exist only as paired-single assembly, and their results have to match the
// console bit for bit: every matrix the game builds goes through them. The recompiler already turns
// each one into C++ that reproduces the original instruction by instruction, with the processor's
// floating-point behaviour spelled out (fused multiply-adds computed in double and rounded once
// more to single, the 25-bit multiplicand of the single multiplies, the estimate tables). Rather
// than re-derive all that by hand, the native build compiles that same translation against this
// header, where a register holds a real pointer and memory is the host's.
//
// Only what leaf maths routines use is here. Anything else fails to compile, which is the point.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <immintrin.h>

namespace ppc {

union FPR { struct { double ps0, ps1; }; struct { uint64_t u0, u1; }; };

struct Context {
  uint64_t r[32];       // 64-bit: a register may carry a host pointer
  FPR f[32];
  uint8_t cr[8];
  uint64_t lr, ctr;
  uint32_t entry;
};

inline void enter(Context&, uint32_t) {}

// ---- memory: host order, host addresses ----
inline uint32_t ld32(Context&, uint8_t*, uint64_t ea) { uint32_t v; std::memcpy(&v, (const void*)(uintptr_t)ea, 4); return v; }
inline uint64_t ld64(Context&, uint8_t*, uint64_t ea) { uint64_t v; std::memcpy(&v, (const void*)(uintptr_t)ea, 8); return v; }
inline void st32(Context&, uint8_t*, uint64_t ea, uint64_t v) { uint32_t w = (uint32_t)v; std::memcpy((void*)(uintptr_t)ea, &w, 4); }
inline void st64(Context&, uint8_t*, uint64_t ea, uint64_t v) { std::memcpy((void*)(uintptr_t)ea, &v, 8); }

// ---- condition register ----
inline void cr_set_u(Context& c, int field, uint64_t a, uint64_t b) {
  c.cr[field] = (uint8_t)((uint32_t)a < (uint32_t)b ? 8 : (uint32_t)a > (uint32_t)b ? 4 : 2);
}
inline void fcmp(Context& c, int field, double a, double b) {
  c.cr[field] = (uint8_t)((a != a || b != b) ? 1 : a < b ? 8 : a > b ? 4 : 2);
}

// ---- floating point: identical arithmetic to port/runtime/ppc/ppc.h ----
inline double fs(double x) { return (double)(float)x; }
inline double f25(double d) {
  uint64_t i; std::memcpy(&i, &d, 8);
  i = (i & 0xFFFFFFFFF8000000ull) + (i & 0x8000000ull);
  std::memcpy(&d, &i, 8); return d;
}
inline double fmadd(double a, double c, double b) { return _mm_cvtsd_f64(_mm_fmadd_sd(_mm_set_sd(a), _mm_set_sd(c), _mm_set_sd(b))); }
inline double fmsub(double a, double c, double b) { return _mm_cvtsd_f64(_mm_fmsub_sd(_mm_set_sd(a), _mm_set_sd(c), _mm_set_sd(b))); }
inline double fnmadd(double a, double c, double b) { return _mm_cvtsd_f64(_mm_fnmsub_sd(_mm_set_sd(a), _mm_set_sd(c), _mm_set_sd(b))); }
inline double fnmsub(double a, double c, double b) { return _mm_cvtsd_f64(_mm_fnmadd_sd(_mm_set_sd(a), _mm_set_sd(c), _mm_set_sd(b))); }
inline double bits_to_double(uint64_t u) { double d; std::memcpy(&d, &u, 8); return d; }
inline uint64_t double_to_bits(double d) { uint64_t u; std::memcpy(&u, &d, 8); return u; }
inline double float_bits_to_double(uint32_t u) { float f; std::memcpy(&f, &u, 4); return (double)f; }
inline uint32_t double_to_float_bits(double d) { float f = (float)d; uint32_t u; std::memcpy(&u, &f, 4); return u; }
double fres(double v);
double frsqrte(double v);

// ---- quantized loads and stores: the maths routines only ever use the float type (GQR0 = 0) ----
inline void psq_load(Context& c, uint8_t* m, uint64_t ea, uint32_t rd, uint32_t w, uint32_t) {
  c.f[rd].ps0 = float_bits_to_double(ld32(c, m, ea));
  c.f[rd].ps1 = w ? 1.0 : float_bits_to_double(ld32(c, m, ea + 4));
}
inline void psq_store(Context& c, uint8_t* m, uint64_t ea, uint32_t rs, uint32_t w, uint32_t) {
  st32(c, m, ea, double_to_float_bits(c.f[rs].ps0));
  if (!w) st32(c, m, ea + 4, double_to_float_bits(c.f[rs].ps1));
}

}  // namespace ppc
