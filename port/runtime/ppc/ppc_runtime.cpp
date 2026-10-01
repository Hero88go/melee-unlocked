// Runtime services for recompiled Gekko code: dispatch, MMIO routing, SPRs, PSQ, fres/frsqrte.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "ppc.h"
#include "guest_registry.h"
#include "host.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

namespace ppc {
// Diagnostic hook the regenerated vanilla guest calls around the spline evaluator
// (0x80378A30..0x80378A90). Lives in the runtime, not main.cpp, so every binary that links
// guest.lib (tests, probes) resolves it. Off unless MELEE_TRACE_SPLINE_PPC=first:last is set.
void trace_spline_guest(const Context& context, uint32_t pc) {
  static bool initialized = false;
  static bool enabled = false;
  static bool active = false;
  static uint32_t first = 0, last = 0;
  static uint32_t trace_retrace = 0;
  if (!initialized) {
    initialized = true;
    const char* range = std::getenv("MELEE_TRACE_SPLINE_PPC");
    if (range && std::sscanf(range, "%u:%u", &first, &last) == 2 &&
        first <= last && last - first <= 120) enabled = true;
  }
  if (!enabled) return;
  auto bits = [](double value) {
    const float single = static_cast<float>(value);
    uint32_t raw;
    std::memcpy(&raw, &single, sizeof raw);
    return raw;
  };
  if (!active && pc == 0x80378A30u) {
    const uint32_t retrace = host::retrace_count();
    if (retrace < first || retrace > last || context.lr != 0x8036AFECu ||
        bits(context.f[1].ps0) != 0x3E2AAAABu ||
        bits(context.f[2].ps0) != 0x40000000u ||
        bits(context.f[3].ps0) != 0x00000000u ||
        bits(context.f[4].ps0) != 0x40A2F400u ||
        bits(context.f[5].ps0) != 0x3F26A400u ||
        bits(context.f[6].ps0) != 0x3FA39400u) return;
    active = true;
    trace_retrace = retrace;
  }
  if (!active) return;
  host::log("[spline-ppc] retrace=%u pc=%08X lr=%08X f0=%08X f1=%08X f2=%08X f3=%08X f4=%08X f5=%08X f6=%08X f7=%08X f8=%08X f9=%08X f10=%08X f11=%08X",
            trace_retrace, pc, context.lr,
            bits(context.f[0].ps0), bits(context.f[1].ps0),
            bits(context.f[2].ps0), bits(context.f[3].ps0),
            bits(context.f[4].ps0), bits(context.f[5].ps0),
            bits(context.f[6].ps0), bits(context.f[7].ps0),
            bits(context.f[8].ps0), bits(context.f[9].ps0),
            bits(context.f[10].ps0), bits(context.f[11].ps0));
  if (pc == 0x80378A90u) active = false;
}

std::atomic<uint8_t> g_ram_watched[RAM_WATCH_COUNT]{};
std::atomic<uint32_t> g_ram_versions[RAM_WATCH_COUNT]{};

static std::vector<Fn> g_dispatch;   // indexed by (addr - RAM_BASE) / 4
static uint8_t g_locked_cache[LC_SIZE];
uint64_t g_resumed_returns = 0;      // see ppc.h
uint64_t g_computed_return_checks = 0; // see ppc.h

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
  if (g_dispatch.empty() || off >= RAM_SIZE || (addr & 3)) return nullptr;
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

// ---- mods on the Static Recomp ----
// A mod's changed game code runs from RAM: the compiled function it replaces gets a 5-byte jump at its
// entry to a trampoline that interprets the function's current bytes. Unchanged functions keep their
// compiled code (no check on any call path), so a mod costs nothing where it changes nothing.
namespace {
uint8_t* g_tramp_pool = nullptr;
size_t g_tramp_used = 0, g_tramp_cap = 0;
std::vector<uint32_t> g_redirected;   // sorted guest addresses
void* alloc_near(const void* target, size_t size) {
  SYSTEM_INFO si; GetSystemInfo(&si);
  const uintptr_t gran = si.dwAllocationGranularity;
  const uintptr_t base = (uintptr_t)target & ~(gran - 1);
  for (uintptr_t step = gran; step < (1ull << 30); step += gran) {
    for (int dir = -1; dir <= 1; dir += 2) {
      const uintptr_t at = dir < 0 ? base - step : base + step;
      if (void* p = VirtualAlloc((void*)at, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE)) return p;
    }
  }
  return nullptr;
}
}  // namespace

// One byte per guest word: 1 = the interpreter follows this address in RAM (a redirected function's
// body, or one of Slippi's code tables in a mod session). O(1) on every interpreted branch.
static std::vector<uint8_t> g_inline_map;
static void mark_inline(uint32_t lo, uint32_t hi) {
  if (g_inline_map.empty()) g_inline_map.assign(RAM_SIZE / 4, 0);
  for (uint32_t a = lo & ~3u; a < hi; a += 4) { const uint32_t off = a - RAM_BASE; if (off < RAM_SIZE) g_inline_map[off / 4] = 1; }
}
void add_ram_code_range(uint32_t lo, uint32_t hi) { mark_inline(lo, hi); }

bool runs_from_ram(uint32_t addr) {
  if (g_inline_map.empty()) return false;
  const uint32_t off = addr - RAM_BASE;
  return off < RAM_SIZE && g_inline_map[off / 4] != 0;
}

// The guest function containing addr: [*lo, *hi) from the name table.
bool function_bounds(uint32_t addr, uint32_t* lo, uint32_t* hi) {
  size_t l = 0, h = guest::name_table_count;
  while (l < h) { const size_t mid = (l + h) / 2; if (guest::name_table[mid].addr <= addr) l = mid + 1; else h = mid; }
  if (!l) return false;
  *lo = guest::name_table[l - 1].addr;
  *hi = l < guest::name_table_count ? guest::name_table[l].addr : *lo + 4;
  return true;
}

void disable_dispatch_range(uint32_t lo, uint32_t hi) {
  for (uint32_t a = lo & ~3u; a < hi; a += 4) {
    const uint32_t off = a - RAM_BASE;
    if (off < RAM_SIZE && !g_dispatch.empty()) g_dispatch[off / 4] = nullptr;
  }
}

void interp_entry(Context& c, uint8_t* m, uint32_t addr) {
  const uint32_t start = c.entry ? c.entry : addr;   // a mid-function thunk asked for this entry
  c.entry = 0;
  interpret(c, m, start);
}

bool redirect_to_interpreter(uint32_t addr) {
  auto at = std::lower_bound(g_redirected.begin(), g_redirected.end(), addr);
  if (at != g_redirected.end() && *at == addr) return true;
  Fn fn = lookup(addr);
  if (!fn) return false;
  uint8_t* code = reinterpret_cast<uint8_t*>(fn);
  if (!g_tramp_pool) { g_tramp_cap = 4u << 20; g_tramp_pool = static_cast<uint8_t*>(alloc_near(code, g_tramp_cap)); }
  if (!g_tramp_pool || g_tramp_used + 32 > g_tramp_cap) return false;
  uint8_t* t = g_tramp_pool + g_tramp_used;
  const int64_t rel = (int64_t)(t - (code + 5));
  if (rel < INT32_MIN || rel > INT32_MAX) return false;
  g_tramp_used += 32;
  // mov r8d, addr ; mov rax, interp_entry ; jmp rax   (rcx = Context&, rdx = RAM, as the caller passed)
  t[0] = 0x41; t[1] = 0xB8; std::memcpy(t + 2, &addr, 4);
  const uint64_t target = reinterpret_cast<uint64_t>(&interp_entry);
  t[6] = 0x48; t[7] = 0xB8; std::memcpy(t + 8, &target, 8);
  t[16] = 0xFF; t[17] = 0xE0;
  DWORD old = 0;
  if (!VirtualProtect(code, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
  const int32_t r = (int32_t)rel;
  code[0] = 0xE9; std::memcpy(code + 1, &r, 4);
  VirtualProtect(code, 5, old, &old);
  FlushInstructionCache(GetCurrentProcess(), code, 5);
  g_redirected.insert(at, addr);
  uint32_t lo = 0, hi = 0;
  if (function_bounds(addr, &lo, &hi)) mark_inline(lo, hi);
  return true;
}

size_t redirect_changed_functions(const uint8_t* reference, const uint8_t* m, uint32_t base, uint32_t size) {
  size_t redirected = 0;
  uint32_t last_owner = 0;
  for (uint32_t off = 0; off + 4 <= size; off += 4) {
    const uint32_t addr = base + off;
    if (std::memcmp(reference + off, m + (addr - RAM_BASE), 4) == 0) continue;
    // The function containing this word: the last function start at or below it.
    size_t lo = 0, hi = guest::name_table_count;
    while (lo < hi) { const size_t mid = (lo + hi) / 2; if (guest::name_table[mid].addr <= addr) lo = mid + 1; else hi = mid; }
    if (!lo) continue;
    const uint32_t owner = guest::name_table[lo - 1].addr;
    if (owner == last_owner) continue;
    last_owner = owner;
    // Already running from RAM: a block written every frame (block 0 holds low memory as well as
    // code) is compared again every frame, and each of those functions was logged again each time.
    if (std::binary_search(g_redirected.begin(), g_redirected.end(), owner)) continue;
    if (redirect_to_interpreter(owner)) {
      ++redirected;
      host::log("mods: %08X %s runs the mod's code", owner, guest::name_table[lo - 1].name);
    } else {
      host::log("mods: %08X %s changed but cannot be redirected", owner, guest::name_table[lo - 1].name);
    }
  }
  return redirected;
}

void report_kept_compiled(const uint8_t* boot_reference, const uint8_t* reference, const uint8_t* m, uint32_t base, uint32_t size) {
  static std::vector<uint32_t> reported;   // sorted owners already logged
  if (!guest::name_table_count || std::memcmp(boot_reference, reference, size) == 0) return;   // nothing the served codes explain here
  // The owner of the first word, then the next function starts in order as the words pass them.
  size_t next = 0, hi = guest::name_table_count;
  while (next < hi) { const size_t mid = (next + hi) / 2; if (guest::name_table[mid].addr <= base) next = mid + 1; else hi = mid; }
  uint32_t owner = next ? guest::name_table[next - 1].addr : 0, words = 0;
  const char* name = next ? guest::name_table[next - 1].name : "";
  bool differs = false;
  auto finish = [&] {
    if (!owner || !words || differs) return;
    auto at = std::lower_bound(reported.begin(), reported.end(), owner);
    if (at != reported.end() && *at == owner) return;
    reported.insert(at, owner);
    if (std::binary_search(g_redirected.begin(), g_redirected.end(), owner)) return;   // already runs from RAM
    host::log("mods: kept compiled %08X %s: %u words, Slippi main list", owner, name, words);
  };
  for (uint32_t off = 0; off + 4 <= size; off += 4) {
    const uint32_t addr = base + off;
    while (next < guest::name_table_count && guest::name_table[next].addr <= addr) {
      finish();
      owner = guest::name_table[next].addr; name = guest::name_table[next].name; words = 0; differs = false;
      ++next;
    }
    const uint8_t* now = m + (addr - RAM_BASE);
    if (std::memcmp(reference + off, now, 4) != 0) differs = true;
    else if (std::memcmp(boot_reference + off, now, 4) != 0) ++words;
  }
  finish();
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
static std::vector<std::pair<uint32_t, EntryHook>> g_entry_hooks;
void add_entry_hook(uint32_t addr, EntryHook fn) { g_entry_hooks.push_back({addr, fn}); g_trace_funcs = true; }
static void (*g_any_entry_hook)(Context&, uint32_t) = nullptr;
void set_any_entry_hook(void (*fn)(Context&, uint32_t)) { g_any_entry_hook = fn; if (fn) g_trace_funcs = true; }
void trace_enter(Context& c, uint32_t pc) {
  if (g_any_entry_hook) g_any_entry_hook(c, pc);
  for (auto& h : g_entry_hooks) if (h.first == pc) h.second(c);
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
  else { std::memcpy(ram, g_locked_cache + lc_offset, bytes); mark_ram_write(0x80000000u | mem, bytes); } // LCStoreData
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
  if (uint8_t* p = fast(m, ea)) { std::memset(p, 0, 32); mark_ram_write(ea, 32); return; }
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

// ---- fres / frsqrte ----
#include "ppc_estimates.inc"

}  // namespace ppc
