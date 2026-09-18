// Runtime services for recompiled Gekko code: dispatch, MMIO routing, SPRs, PSQ, fres/frsqrte.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "ppc.h"
#include "functions.h"
#include "host.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <thread>
#include <vector>

namespace ppc {

static std::vector<Fn> g_dispatch;   // indexed by (addr - RAM_BASE) / 4
static uint8_t g_locked_cache[LC_SIZE];
uint64_t g_resumed_returns = 0;      // see ppc.h

// Covers all of RAM: Gecko caves live below .text (bootloader at 0x800028B8) and in the heap
// (the main code table the game loads), and their subroutines are called through pointers.
void init_dispatch() {
  g_dispatch.assign(RAM_SIZE / 4, nullptr);
  for (size_t i = 0; i < guest::fn_table_count; ++i) {
    const auto& e = guest::fn_table[i];
    uint32_t off = e.addr - RAM_BASE;
    if (off < RAM_SIZE) g_dispatch[off / 4] = e.fn;
  }
}

Fn lookup(uint32_t addr) {
  uint32_t off = addr - RAM_BASE;
  if (off >= RAM_SIZE || (addr & 3)) return nullptr;
  return g_dispatch[off / 4];
}

// Replaces the function called at `addr` and hands back what was there, so a host implementation can
// stand in front of a translated one and still call it. Every `bl` the recompiler emits goes through
// ppc::call, which reads this table (emit.py), so a swap here is seen by the whole game.
//
// This is how a feature that has to change what the game decides gets built without regenerating
// port/generated: the alternative is a two-way instruction baked in at translation time, which costs
// a full rebuild of the guest library for every such feature and cannot be switched off afterwards.
// A hook is not free of consequences, though: it changes what the simulation computes, so anything
// built on it has to be held back online exactly as automatic L-cancel is.
Fn set_hook(uint32_t addr, Fn fn) {
  uint32_t off = addr - RAM_BASE;
  // The table does not exist until init_dispatch has run. A setting restored at startup can reach
  // this before the guest is ready, and indexing an empty vector here would be silent corruption.
  if (g_dispatch.empty() || off >= RAM_SIZE || (addr & 3)) return nullptr;
  Fn previous = g_dispatch[off / 4];
  g_dispatch[off / 4] = fn;
  return previous;
}

void call(Context& c, uint8_t* m, uint32_t addr) {
  Fn fn = lookup(addr);
  if (++c.call_depth > 20000) fatal(c, "guest call depth exceeded", addr);
  // Guest longjmp and OSLoadContext unwind through here as C++ exceptions. The depth must drop on
  // that path too, or every unwind leaks the skipped frames until an ordinary call hits the limit
  // (seen after thousands of rollbacks in a long online session).
  CallDepthScope scope{c};
  if (fn) fn(c, m);
  else interpret(c, m, addr);   // code that only exists in RAM (dat-loaded routines)
}

uint64_t g_enter_count = 0;
bool g_trace_funcs = false;
static std::vector<std::pair<uint32_t, uint32_t>> g_traced;   // (addr, remaining prints)
void add_trace_func(uint32_t addr, uint32_t limit) { g_traced.push_back({addr, limit}); g_trace_funcs = true; }
void trace_enter(Context& c, uint32_t pc) {
  for (auto& t : g_traced) {
    if (t.first != pc || !t.second) continue;
    --t.second;
    host::log("[trace] frame %u %s(%08X) r3=%08X r4=%08X r5=%08X lr=%08X (from %s)", host::retrace_count(), host::symbol_name(pc), pc,
              c.r[3], c.r[4], c.r[5], c.lr, host::symbol_name(c.lr));
  }
}

// Hang diagnostics run on the simulation thread through hang_check.
void hang_check(Context& c) {
  // No retrace for `hang_watch` seconds while the guest keeps calling functions: report where.
  static uint32_t last_retraces = 0;
  static double stuck_since = 0.0;
  if (!host::options.hang_watch) return;
  uint32_t retraces = host::retrace_count();
  double now = host::now_seconds();
  if (retraces != last_retraces || stuck_since == 0.0) { last_retraces = retraces; stuck_since = now; return; }
  if (now - stuck_since > host::options.hang_watch) fatal(c, "no retrace for too long (guest spin loop?)", retraces);
}

void longjmp_restore(Context& c, uint8_t* m, uint32_t buf, uint32_t val) {
  // MSL jmp_buf: +0 LR, +4 CR, +8 r1, +12 r2, +20 r13..r31, +96 f14..f31, +240 FPSCR (as a double).
  c.lr = ld32(c, m, buf);
  mtcrf(c, 0xFFu, ld32(c, m, buf + 4));
  c.r[1] = ld32(c, m, buf + 8);
  c.r[2] = ld32(c, m, buf + 12);
  for (int i = 13, ea = (int)buf + 20; i < 32; ++i, ea += 4) c.r[i] = ld32(c, m, (uint32_t)ea);
  for (int i = 14; i < 32; ++i) c.f[i].u0 = ld64(c, m, buf + 96 + 8 * (uint32_t)(i - 14));
  c.f[0].u0 = ld64(c, m, buf + 240);
  c.fpscr = (uint32_t)c.f[0].u0; update_mxcsr(c);
  c.r[3] = val ? val : 1u;
}

void fatal(Context& c, const char* what, uint32_t a) {
  host::log("recent function entries (oldest first):");
  for (uint32_t i = 0; i < 64; ++i) {
    uint32_t pc = c.trace[(c.trace_pos + i) & 63];
    if (pc) host::log("  %08X %s", pc, host::symbol_name(pc));
  }
  host::log("r3=%08X r4=%08X r5=%08X r6=%08X r12=%08X r31=%08X", c.r[3], c.r[4], c.r[5], c.r[6], c.r[12], c.r[31]);
  host::die("guest fault: %s (%08X) in %s (%08X); lr=%08X r1=%08X", what, a,
            host::symbol_name(c.last_pc), c.last_pc, c.lr, c.r[1]);
}

uint8_t* locked_cache() { return g_locked_cache; }

uint32_t mmio_read(Context& c, uint32_t ea, int bytes) { return host::mmio_read(ea, bytes); }
void mmio_write(Context& c, uint32_t ea, uint32_t value, int bytes) { host::mmio_write(ea, value, bytes); }
uint64_t mmio_read64(Context& c, uint32_t ea) {
  return ((uint64_t)host::mmio_read(ea, 4) << 32) | host::mmio_read(ea + 4, 4);
}
void mmio_write64(Context& c, uint32_t ea, uint64_t value) {
  host::mmio_write(ea, (uint32_t)(value >> 32), 4);
  host::mmio_write(ea + 4, (uint32_t)value, 4);
}

uint32_t spr_read(Context& c, uint32_t n) {
  switch (n) {
    case 1017: return c.spr[n] & ~1u;  // L2CR: invalidate-in-progress bit always clear
    case 921: return 0;                // WPAR: write-gather pipe never busy
    case 272: case 273: case 274: case 275: return c.spr[n];  // SPRG0-3
    default: return c.spr[n & 1023];
  }
}

// The locked cache's DMA engine. The THP movie decoder (the opening, the special movie, the Classic
// and Adventure endings) builds each frame in the 16 KB locked cache and moves it out to the texture
// buffers with LCStoreData, which programs DMA_U (922) then DMA_L (923) with its trigger bit set.
// Writing these used to only store the value, so the copy never happened and every movie showed
// whatever was in those buffers before. Done here, synchronously and at once, so the transfer queue
// HID2 reports stays empty and LCQueueWait returns straight away, as Dolphin behaves.
//   DMA_U: memory address (32-byte aligned) | length bits 6..2 in lines
//   DMA_L: locked cache address | LD (0x10, memory to cache) | length bits 1..0 << 2 | T (0x2)
static void locked_cache_dma(Context& c, uint32_t dmal) {
  const uint32_t dmau = c.spr[922];
  uint32_t lines = ((dmau & 0x1Fu) << 2) | ((dmal >> 2) & 3u);
  if (!lines) lines = 128;
  const uint32_t bytes = lines * 32u;
  const uint32_t lc = dmal & 0xFFFFFFE0u, mem = (dmau & 0xFFFFFFE0u) & 0x3FFFFFFFu;
  const uint32_t lc_offset = lc & (LC_SIZE - 1);
  if ((lc & 0xFFFFC000u) != LC_BASE || lc_offset + bytes > LC_SIZE) {
    host::log("locked cache DMA outside the cache: %08X+%X", lc, bytes);
    return;
  }
  uint8_t* ram = host::ptr(0x80000000u | mem, bytes);   // checks the whole span
  static uint64_t transfers = 0;
  if (++transfers == 1 || transfers % 100000 == 0)
    host::log("locked cache DMA: %llu transfers (%s %08X+%X)", (unsigned long long)transfers, (dmal & 0x10u) ? "load" : "store", 0x80000000u | mem, bytes);
  if (dmal & 0x10u) std::memcpy(g_locked_cache + lc_offset, ram, bytes);   // LCLoadData
  else std::memcpy(ram, g_locked_cache + lc_offset, bytes);               // LCStoreData
}

void spr_write(Context& c, uint32_t n, uint32_t v) {
  n &= 1023;
  if (n == 923 && (v & 2u)) {
    locked_cache_dma(c, v);
    v &= ~2u;   // the trigger bit reads back clear once the transfer is done
  }
  c.spr[n] = v;
}

void syscall(Context& c, uint8_t* m) {
  // Melee only uses sc for cache maintenance from OS code; nothing to do.
}

void update_mxcsr(Context& c) {
  unsigned csr = _mm_getcsr() & ~(0x6000u | 0x8000u | 0x0040u);
  unsigned rn = c.fpscr & 3;                      // PPC RN: 0 nearest,1 zero,2 +inf,3 -inf
  static const unsigned x86_rc[4] = {0x0000, 0x6000, 0x4000, 0x2000};
  csr |= x86_rc[rn];
  if (c.fpscr & 4) csr |= 0x8000u | 0x0040u;      // NI -> FTZ | DAZ, as Jit64 does
  _mm_setcsr(csr);
}

void dcbz(Context& c, uint8_t* m, uint32_t ea) {
  ea &= ~31u;
  if (uint8_t* p = fast(m, ea)) { std::memset(p, 0, 32); return; }
  if (uint8_t* p = slowptr(ea)) { std::memset(p, 0, 32); return; }
  fatal(c, "dcbz outside RAM", ea);
}

void lswi(Context& c, uint8_t* m, uint32_t ea, uint32_t rd, uint32_t nb) {
  uint32_t r = rd;
  while (nb > 0) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
      v <<= 8;
      if (nb > 0) { v |= ld8(c, m, ea++); --nb; }
    }
    c.r[r] = v;
    r = (r + 1) & 31;
  }
}

void stswi(Context& c, uint8_t* m, uint32_t ea, uint32_t rs, uint32_t nb) {
  uint32_t r = rs;
  int shift = 24;
  while (nb > 0) {
    st8(c, m, ea++, (c.r[r] >> shift) & 0xFF);
    --nb;
    shift -= 8;
    if (shift < 0) { shift = 24; r = (r + 1) & 31; }
  }
}

// ---- paired-single quantized loads/stores (Interpreter_LoadStorePaired semantics) ----
static const float dequantize_table[] = {
  1.0f / (1u << 0), 1.0f / (1u << 1), 1.0f / (1u << 2), 1.0f / (1u << 3), 1.0f / (1u << 4), 1.0f / (1u << 5),
  1.0f / (1u << 6), 1.0f / (1u << 7), 1.0f / (1u << 8), 1.0f / (1u << 9), 1.0f / (1u << 10), 1.0f / (1u << 11),
  1.0f / (1u << 12), 1.0f / (1u << 13), 1.0f / (1u << 14), 1.0f / (1u << 15), 1.0f / (1u << 16), 1.0f / (1u << 17),
  1.0f / (1u << 18), 1.0f / (1u << 19), 1.0f / (1u << 20), 1.0f / (1u << 21), 1.0f / (1u << 22), 1.0f / (1u << 23),
  1.0f / (1u << 24), 1.0f / (1u << 25), 1.0f / (1u << 26), 1.0f / (1u << 27), 1.0f / (1u << 28), 1.0f / (1u << 29),
  1.0f / (1u << 30), 1.0f / (1u << 31),
  (float)(1ull << 32), (float)(1u << 31), (float)(1u << 30), (float)(1u << 29), (float)(1u << 28), (float)(1u << 27),
  (float)(1u << 26), (float)(1u << 25), (float)(1u << 24), (float)(1u << 23), (float)(1u << 22), (float)(1u << 21),
  (float)(1u << 20), (float)(1u << 19), (float)(1u << 18), (float)(1u << 17), (float)(1u << 16), (float)(1u << 15),
  (float)(1u << 14), (float)(1u << 13), (float)(1u << 12), (float)(1u << 11), (float)(1u << 10), (float)(1u << 9),
  (float)(1u << 8), (float)(1u << 7), (float)(1u << 6), (float)(1u << 5), (float)(1u << 4), (float)(1u << 3),
  (float)(1u << 2), (float)(1u << 1),
};
static const float quantize_table[] = {
  (float)(1u << 0), (float)(1u << 1), (float)(1u << 2), (float)(1u << 3), (float)(1u << 4), (float)(1u << 5),
  (float)(1u << 6), (float)(1u << 7), (float)(1u << 8), (float)(1u << 9), (float)(1u << 10), (float)(1u << 11),
  (float)(1u << 12), (float)(1u << 13), (float)(1u << 14), (float)(1u << 15), (float)(1u << 16), (float)(1u << 17),
  (float)(1u << 18), (float)(1u << 19), (float)(1u << 20), (float)(1u << 21), (float)(1u << 22), (float)(1u << 23),
  (float)(1u << 24), (float)(1u << 25), (float)(1u << 26), (float)(1u << 27), (float)(1u << 28), (float)(1u << 29),
  (float)(1u << 30), (float)(1u << 31),
  1.0f / (float)(1ull << 32), 1.0f / (1u << 31), 1.0f / (1u << 30), 1.0f / (1u << 29), 1.0f / (1u << 28), 1.0f / (1u << 27),
  1.0f / (1u << 26), 1.0f / (1u << 25), 1.0f / (1u << 24), 1.0f / (1u << 23), 1.0f / (1u << 22), 1.0f / (1u << 21),
  1.0f / (1u << 20), 1.0f / (1u << 19), 1.0f / (1u << 18), 1.0f / (1u << 17), 1.0f / (1u << 16), 1.0f / (1u << 15),
  1.0f / (1u << 14), 1.0f / (1u << 13), 1.0f / (1u << 12), 1.0f / (1u << 11), 1.0f / (1u << 10), 1.0f / (1u << 9),
  1.0f / (1u << 8), 1.0f / (1u << 7), 1.0f / (1u << 6), 1.0f / (1u << 5), 1.0f / (1u << 4), 1.0f / (1u << 3),
  1.0f / (1u << 2), 1.0f / (1u << 1),
};

template <typename T>
static T scale_clamp(double ps, uint32_t st_scale) {
  float conv = (float)ps * quantize_table[st_scale];
  float lo = (float)std::numeric_limits<T>::min(), hi = (float)std::numeric_limits<T>::max();
  if (conv < lo) conv = lo;
  if (conv > hi) conv = hi;
  return (T)conv;
}

// GQR layout: st_type bits 0-2, st_scale bits 8-13, ld_type bits 16-18, ld_scale bits 24-29.
void psq_load(Context& c, uint8_t* m, uint32_t ea, uint32_t rd, uint32_t w, uint32_t i) {
  uint32_t gqr = c.gqr[i];
  uint32_t type = (gqr >> 16) & 7, scale = (gqr >> 24) & 63;
  float ps0, ps1;
  switch (type) {
    case 0:  // float
      if (w) { uint32_t v = ld32(c, m, ea); std::memcpy(&ps0, &v, 4); ps1 = 1.0f; }
      else { uint32_t a = ld32(c, m, ea), b = ld32(c, m, ea + 4); std::memcpy(&ps0, &a, 4); std::memcpy(&ps1, &b, 4); }
      break;
    case 4:  // u8
      if (w) { ps0 = (float)(uint8_t)ld8(c, m, ea) * dequantize_table[scale]; ps1 = 1.0f; }
      else { uint32_t v = ld16(c, m, ea); ps0 = (float)(uint8_t)(v >> 8) * dequantize_table[scale]; ps1 = (float)(uint8_t)v * dequantize_table[scale]; }
      break;
    case 5:  // u16
      if (w) { ps0 = (float)(uint16_t)ld16(c, m, ea) * dequantize_table[scale]; ps1 = 1.0f; }
      else { uint32_t v = ld32(c, m, ea); ps0 = (float)(uint16_t)(v >> 16) * dequantize_table[scale]; ps1 = (float)(uint16_t)v * dequantize_table[scale]; }
      break;
    case 6:  // s8
      if (w) { ps0 = (float)(int8_t)ld8(c, m, ea) * dequantize_table[scale]; ps1 = 1.0f; }
      else { uint32_t v = ld16(c, m, ea); ps0 = (float)(int8_t)(v >> 8) * dequantize_table[scale]; ps1 = (float)(int8_t)v * dequantize_table[scale]; }
      break;
    case 7:  // s16
      if (w) { ps0 = (float)(int16_t)ld16(c, m, ea) * dequantize_table[scale]; ps1 = 1.0f; }
      else { uint32_t v = ld32(c, m, ea); ps0 = (float)(int16_t)(v >> 16) * dequantize_table[scale]; ps1 = (float)(int16_t)v * dequantize_table[scale]; }
      break;
    default:
      fatal(c, "psq_l invalid GQR type", gqr);
  }
  c.f[rd].ps0 = ps0;
  c.f[rd].ps1 = ps1;
}

void psq_store(Context& c, uint8_t* m, uint32_t ea, uint32_t rs, uint32_t w, uint32_t i) {
  uint32_t gqr = c.gqr[i];
  uint32_t type = gqr & 7, scale = (gqr >> 8) & 63;
  double ps0 = c.f[rs].ps0, ps1 = c.f[rs].ps1;
  switch (type) {
    case 0: {
      uint32_t a = double_to_float_bits(ps0);
      if (w) st32(c, m, ea, a);
      else { st32(c, m, ea, a); st32(c, m, ea + 4, double_to_float_bits(ps1)); }
      break;
    }
    case 4: {
      uint8_t a = (uint8_t)scale_clamp<uint8_t>(ps0, scale);
      if (w) st8(c, m, ea, a);
      else st16(c, m, ea, ((uint32_t)a << 8) | (uint8_t)scale_clamp<uint8_t>(ps1, scale));
      break;
    }
    case 5: {
      uint16_t a = (uint16_t)scale_clamp<uint16_t>(ps0, scale);
      if (w) st16(c, m, ea, a);
      else st32(c, m, ea, ((uint32_t)a << 16) | (uint16_t)scale_clamp<uint16_t>(ps1, scale));
      break;
    }
    case 6: {
      uint8_t a = (uint8_t)scale_clamp<int8_t>(ps0, scale);
      if (w) st8(c, m, ea, a);
      else st16(c, m, ea, ((uint32_t)a << 8) | (uint8_t)scale_clamp<int8_t>(ps1, scale));
      break;
    }
    case 7: {
      uint16_t a = (uint16_t)scale_clamp<int16_t>(ps0, scale);
      if (w) st16(c, m, ea, a);
      else st32(c, m, ea, ((uint32_t)a << 16) | (uint16_t)scale_clamp<int16_t>(ps1, scale));
      break;
    }
    default:
      fatal(c, "psq_st invalid GQR type", gqr);
  }
}

// ---- fres / frsqrte (Dolphin Common/MathUtil.cpp) ----
static const int frsqrte_expected_base[] = {
  0x3ffa000, 0x3c29000, 0x38aa000, 0x3572000, 0x3279000, 0x2fb7000, 0x2d26000, 0x2ac0000,
  0x2881000, 0x2665000, 0x2468000, 0x2287000, 0x20c1000, 0x1f12000, 0x1d79000, 0x1bf4000,
  0x1a7e800, 0x17cb800, 0x1552800, 0x130c000, 0x10f2000, 0x0eff000, 0x0d2e000, 0x0b7c000,
  0x09e5000, 0x0867000, 0x06ff000, 0x05ab800, 0x046a000, 0x0339800, 0x0218800, 0x0105800,
};
static const int frsqrte_expected_dec[] = {
  0x7a4, 0x700, 0x670, 0x5f2, 0x584, 0x524, 0x4cc, 0x47e, 0x43a, 0x3fa, 0x3c2, 0x38e,
  0x35e, 0x332, 0x30a, 0x2e6, 0x568, 0x4f3, 0x48d, 0x435, 0x3e7, 0x3a2, 0x365, 0x32e,
  0x2fc, 0x2d0, 0x2a8, 0x283, 0x261, 0x243, 0x226, 0x20b,
};

double frsqrte(double val) {
  int64_t vali; std::memcpy(&vali, &val, 8);
  int64_t mantissa = vali & ((1LL << 52) - 1);
  int64_t sign = vali & (1LL << 63);
  int64_t exponent = vali & (0x7FFLL << 52);
  if (mantissa == 0 && exponent == 0)
    return sign ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
  if (exponent == (0x7FFLL << 52)) {
    if (mantissa == 0) return sign ? std::numeric_limits<double>::quiet_NaN() : 0.0;
    return 0.0 + val;
  }
  if (sign) return std::numeric_limits<double>::quiet_NaN();
  if (!exponent) {
    do { exponent -= 1LL << 52; mantissa <<= 1; } while (!(mantissa & (1LL << 52)));
    mantissa &= (1LL << 52) - 1;
    exponent += 1LL << 52;
  }
  bool odd_exponent = !(exponent & (1LL << 52));
  exponent = ((0x3FFLL << 52) - ((exponent - (0x3FELL << 52)) / 2)) & (0x7FFLL << 52);
  int i = (int)(mantissa >> 37);
  vali = sign | exponent;
  int index = i / 2048 + (odd_exponent ? 16 : 0);
  vali |= (int64_t)(frsqrte_expected_base[index] - frsqrte_expected_dec[index] * (i % 2048)) << 26;
  double out; std::memcpy(&out, &vali, 8);
  return out;
}

static const int fres_expected_base[] = {
  0x7ff800, 0x783800, 0x70ea00, 0x6a0800, 0x638800, 0x5d6200, 0x579000, 0x520800,
  0x4cc800, 0x47ca00, 0x430800, 0x3e8000, 0x3a2c00, 0x360800, 0x321400, 0x2e4a00,
  0x2aa800, 0x272c00, 0x23d600, 0x209e00, 0x1d8800, 0x1a9000, 0x17ae00, 0x14f800,
  0x124400, 0x0fbe00, 0x0d3800, 0x0ade00, 0x088400, 0x065000, 0x041c00, 0x020c00,
};
static const int fres_expected_dec[] = {
  0x3e1, 0x3a7, 0x371, 0x340, 0x313, 0x2ea, 0x2c4, 0x2a0, 0x27f, 0x261, 0x245, 0x22a,
  0x212, 0x1fb, 0x1e5, 0x1d1, 0x1be, 0x1ac, 0x19b, 0x18b, 0x17c, 0x16e, 0x15b, 0x15b,
  0x143, 0x143, 0x12d, 0x12d, 0x11a, 0x11a, 0x108, 0x106,
};

double fres(double val) {
  int64_t vali; std::memcpy(&vali, &val, 8);
  int64_t mantissa = vali & ((1LL << 52) - 1);
  int64_t sign = vali & (1LL << 63);
  int64_t exponent = vali & (0x7FFLL << 52);
  if (mantissa == 0 && exponent == 0) return std::copysign(std::numeric_limits<double>::infinity(), val);
  if (exponent == (0x7FFLL << 52)) {
    if (mantissa == 0) return std::copysign(0.0, val);
    return 0.0 + val;
  }
  if (exponent < (895LL << 52)) return std::copysign((double)std::numeric_limits<float>::max(), val);
  if (exponent >= (1149LL << 52)) return std::copysign(0.0, val);
  exponent = (0x7FDLL << 52) - exponent;
  int i = (int)(mantissa >> 37);
  vali = sign | exponent;
  vali |= (int64_t)(fres_expected_base[i / 1024] - (fres_expected_dec[i / 1024] * (i % 1024) + 1) / 2) << 29;
  double out; std::memcpy(&out, &vali, 8);
  return out;
}

}  // namespace ppc
