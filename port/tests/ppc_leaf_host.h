// The runtime a standalone translator test links in place of the game's: every host symbol the
// stencil table references (extract_ppc_stencils.py EXTERNALS) is defined here as a stub that
// records what it was asked, so a call that is missing, extra, reordered or made with other
// arguments shows up in the comparison. Include from exactly one source file per test.
// No game, ISO, window, device or interpreter dependency.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ppc.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace host {
constexpr uint64_t kSeed = 1469598103934665603ull, kPrime = 1099511628211ull;
// Everything a run did through the host besides the context and guest memory.
struct Events { uint64_t count = 0, digest = kSeed; };
inline Events g_events;
inline void note(uint64_t value) { g_events.digest = (g_events.digest ^ value) * kPrime; }
// The whole context as the host function saw it, eight bytes at a time.
inline void note_context(const ppc::Context& c) {
  const auto* raw = reinterpret_cast<const uint8_t*>(&c);
  uint64_t digest = g_events.digest;
  size_t i = 0;
  for (; i + 8 <= sizeof c; i += 8) { uint64_t word; std::memcpy(&word, raw + i, 8); digest = (digest ^ word) * kPrime; }
  for (; i < sizeof c; ++i) digest = (digest ^ raw[i]) * kPrime;
  g_events.digest = digest;
}
inline void event(char kind, const ppc::Context& c, uint64_t a, uint64_t b = 0, uint64_t d = 0) {
  ++g_events.count;
  note(uint64_t(kind)); note(a); note(b); note(d);
  note_context(c);
}
inline void reset_events() { g_events = Events{}; }
// The locked cache. A 4 or 8 byte access at its last bytes runs past LC_SIZE in ppc.h, as it does
// in the game; the padding keeps that inside this buffer and is compared like the rest.
constexpr size_t kLockedCacheBytes = ppc::LC_SIZE + 64;
inline uint8_t g_locked_cache_default[kLockedCacheBytes];
inline uint8_t* g_locked_cache = g_locked_cache_default;
// What a guest call reaches. A test installs its own; the default is a host function that changes
// the context as a function of the target, so a call to the wrong address is seen.
using Dispatch = void (*)(ppc::Context&, uint8_t*, uint32_t);
// Addresses from kResumeBase up stand for callees with a computed return (analyze.py
// _computed_return_delta): when r3 is odd they return to the caller's LR plus 8, or plus 4 when
// the address has bit 2 set, as a Gecko cave that unwinds its caller by hand does.
constexpr uint32_t kResumeBase = 0x80600000u, kResumeCount = 64;
inline uint32_t resume_delta(uint32_t addr) {
  return addr >= kResumeBase && addr < kResumeBase + 4 * kResumeCount ? ((addr & 4) ? 4u : 8u) : 0u;
}
inline void default_dispatch(ppc::Context& c, uint8_t*, uint32_t addr) {
  if (resume_delta(addr) && (c.r[3] & 1)) c.lr += resume_delta(addr);
  c.r[3] = (c.r[3] ^ addr) * 0x9E3779B1u + c.lr;
  c.r[4] += addr >> 2;
  c.cr[1] = uint8_t((c.cr[1] + addr) & 15);
  c.f[1].u0 ^= uint64_t(addr) << 20;
  c.ctr += 3;
}
inline Dispatch g_dispatch = default_dispatch;
// Called from inside the I/O stubs when set: a test uses it to walk the stack or to throw from
// under a load or store stencil.
inline void (*g_io_hook)(uint32_t ea) = nullptr;
// ppc::fatal: the game stops there. A test only needs to know that it was reached, and why.
inline uint64_t g_fatal_count = 0;
inline const char* g_fatal_what = "";
inline uint64_t mix(uint64_t x) {
  x ^= x >> 33; x *= 0xFF51AFD7ED558CCDull; x ^= x >> 33; x *= 0xC4CEB9FE1A85EC53ull; return x ^ (x >> 33);
}
} // namespace host

namespace ppc {
uint64_t g_enter_count = 0;
bool g_trace_funcs = false;
void hang_check(Context&) {}
void trace_enter(Context&, uint32_t) {}
std::atomic<uint8_t> g_ram_watched[RAM_WATCH_COUNT]{};
std::atomic<uint32_t> g_ram_versions[RAM_WATCH_COUNT]{};
// The poll changes guest-visible state and logs the whole context it saw, so a poll that is
// missing, extra, early or late shows in the final context and in the log.
uint64_t g_poll_count = 0;
uint64_t g_poll_digest = host::kSeed;
void loop_poll(Context& c) {
  const auto* raw = reinterpret_cast<const uint8_t*>(&c);
  for (size_t i = 0; i < sizeof c; ++i) g_poll_digest = (g_poll_digest ^ raw[i]) * host::kPrime;
  ++g_poll_count;
  c.tb += 977;
  c.dec ^= uint32_t(g_poll_digest);
}
uint8_t* locked_cache() { return host::g_locked_cache; }
uint64_t g_resumed_returns = 0;
uint64_t g_computed_return_checks = 0;
void fatal(Context&, const char* what, uint32_t a) {
  ++host::g_fatal_count;
  host::g_fatal_what = what;
  host::note('f'); host::note(a);
}
// Memory-mapped I/O: the value read depends on the address, the width and how many host events
// came before it, and is a full 32 or 64 bits, as the accessors of ppc.h pass it on unmasked.
// Each access also moves the timebase, so guest state cached across the call would be seen.
uint32_t mmio_read(Context& c, uint32_t ea, int bytes) {
  host::event('r', c, ea, uint64_t(bytes));
  c.tb += 3;
  if (host::g_io_hook) host::g_io_hook(ea);
  return uint32_t(host::mix((uint64_t(ea) << 8) ^ uint64_t(bytes) ^ (host::g_events.count << 40)));
}
void mmio_write(Context& c, uint32_t ea, uint32_t value, int bytes) {
  host::event('w', c, ea, value, uint64_t(bytes));
  c.tb += 5;
  if (host::g_io_hook) host::g_io_hook(ea);
}
uint64_t mmio_read64(Context& c, uint32_t ea) {
  host::event('R', c, ea);
  c.tb += 7;
  if (host::g_io_hook) host::g_io_hook(ea);
  return host::mix(uint64_t(ea) ^ (host::g_events.count << 40));
}
void mmio_write64(Context& c, uint32_t ea, uint64_t value) {
  host::event('W', c, ea, value);
  c.tb += 11;
  if (host::g_io_hook) host::g_io_hook(ea);
}
// The dispatch, with the depth accounting of the game's ppc::call.
void call(Context& c, uint8_t* m, uint32_t addr) {
  host::event('c', c, addr);
  if (++c.call_depth > 20000) { std::printf("FAIL: guest call depth exceeded\n"); std::exit(1); }
  CallDepthScope scope{c};
  host::g_dispatch(c, m, addr);
}
// The game's update_mxcsr, so the rounding mode a guest sets applies to what follows it.
void update_mxcsr(Context& c) {
  host::event('x', c, c.fpscr);
  unsigned csr = _mm_getcsr() & ~(0x6000u | 0x8000u | 0x0040u);
  static const unsigned x86_rc[4] = {0x0000, 0x6000, 0x4000, 0x2000};
  csr |= x86_rc[c.fpscr & 3];
  if (c.fpscr & 4) csr |= 0x8000u | 0x0040u;
  _mm_setcsr(csr);
}
// Stand-ins for the game's table-driven estimates: the tests check that the stencil calls the
// host function with the right operand and stores what it returns, not the estimate itself.
double fres(double v) { host::note('e'); host::note(double_to_bits(v)); ++host::g_events.count; return fs(1.0 / v); }
double frsqrte(double v) { host::note('q'); host::note(double_to_bits(v)); ++host::g_events.count; return 1.0 / std::sqrt(v); }
// Stand-ins for the quantized load and store: every argument is recorded, and the access goes
// through the ordinary accessors so its memory and I/O effects are compared like any other.
void psq_load(Context& c, uint8_t* m, uint32_t ea, uint32_t rd, uint32_t w, uint32_t i) {
  host::event('l', c, ea, (uint64_t(rd) << 8) | (w << 4) | i);
  c.f[rd & 31].ps0 = float_bits_to_double(ld32(c, m, ea));
  c.f[rd & 31].ps1 = w ? 1.0 : float_bits_to_double(ld32(c, m, ea + 4));
}
void psq_store(Context& c, uint8_t* m, uint32_t ea, uint32_t rs, uint32_t w, uint32_t i) {
  host::event('s', c, ea, (uint64_t(rs) << 8) | (w << 4) | i);
  st32(c, m, ea, double_to_float_bits(c.f[rs & 31].ps0));
  if (!w) st32(c, m, ea + 4, double_to_float_bits(c.f[rs & 31].ps1));
}
} // namespace ppc

// The isolated emit.py suites do not install runtime translations.
extern "C" {
const std::atomic<uint32_t>* mu_ram_version0 = nullptr;
const std::atomic<uint32_t>* mu_ram_version1 = nullptr;
uint32_t mu_ram_expected0 = 0, mu_ram_expected1 = 0;
bool mu_ram_invalidated = false;
void mu_ram_translation_guard(uint32_t) {}
void mu_ram_translation_invalidate(uint32_t, uint32_t) {}
}
