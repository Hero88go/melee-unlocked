// Guest memory for the translator's native tests: one full-size RAM, one locked cache and one
// record of host events per executor ("world"), so native code, emit.py's C++ and the hand-written
// reference each run the same function from the same start and everything they did is compared:
// the whole context, every byte of RAM they wrote (found with the system's write watch, so a
// stray write is seen too), the locked cache, the write generations, every host call and poll.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ppc_leaf_reference.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>

namespace worlds {
using reference::Random;
// An access at the last byte of RAM runs a few bytes past it in ppc.h, as in the game.
constexpr size_t kRamBytes = ppc::RAM_SIZE + 0x10000;
constexpr size_t kPage = 4096;
constexpr unsigned kMxcsrControl = 0xFFC0u; // Rounding, flush-to-zero, denormals-are-zero, masks.
struct Result {
  bool ok = false;
  ppc::Context context;
  host::Events events;
  uint64_t poll_count = 0, poll_digest = 0;
  unsigned mxcsr = 0;
  uint32_t versions[ppc::RAM_WATCH_COUNT];
  uint64_t resume_checks = 0, resumed = 0, fatals = 0; // g_computed_return_checks, g_resumed_returns, ppc::fatal
};
struct World {
  uint8_t* ram = nullptr;
  uint8_t lc[host::kLockedCacheBytes];
  Result result;
};
inline World g_world[3];
inline int g_count = 0;
inline uint64_t g_pages_compared = 0, g_host_events = 0, g_generation_bumps = 0;
// The host float state every world starts a run with: all exceptions masked, and the rounding
// mode and denormal handling a test chooses (the game changes them through mtfsf).
inline unsigned g_start_mxcsr = 0x1F80;

inline bool init(int count, Random& random) {
  g_count = count;
  for (int i = 0; i < count; ++i) {
    g_world[i].ram = static_cast<uint8_t*>(VirtualAlloc(nullptr, kRamBytes, MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE));
    if (!g_world[i].ram) { std::printf("FAIL: cannot allocate guest RAM\n"); return false; }
  }
  for (size_t at = 0; at < kRamBytes; at += 8) { const uint64_t value = random.next(); std::memcpy(g_world[0].ram + at, &value, 8); }
  for (size_t at = 0; at < sizeof g_world[0].lc; ++at) g_world[0].lc[at] = uint8_t(random.u32());
  for (int i = 1; i < count; ++i) {
    std::memcpy(g_world[i].ram, g_world[0].ram, kRamBytes);
    std::memcpy(g_world[i].lc, g_world[0].lc, sizeof g_world[0].lc);
  }
  for (int i = 0; i < count; ++i) ResetWriteWatch(g_world[i].ram, kRamBytes);
  return true;
}
// Which 64 KB blocks count their writes. Changed between groups of samples.
inline void watch(Random& random, uint32_t one_in) {
  for (auto& watched : ppc::g_ram_watched) watched.store(one_in && random.below(one_in) == 0 ? 1 : 0, std::memory_order_relaxed);
}
// The function's bytes go back into every RAM before each run: a store may have overwritten them.
inline void install(uint32_t address, const std::vector<uint8_t>& code) {
  for (int i = 0; i < g_count; ++i) std::memcpy(g_world[i].ram + (address - ppc::RAM_BASE), code.data(), code.size());
}
// Runs body(context, ram) in one world from `start` and records everything it did.
template <class Body> void run(int index, const ppc::Context& start, Body&& body) {
  World& world = g_world[index];
  host::g_locked_cache = world.lc;
  host::reset_events();
  ppc::g_poll_count = 0; ppc::g_poll_digest = host::kSeed;
  ppc::g_computed_return_checks = 0; ppc::g_resumed_returns = 0; host::g_fatal_count = 0;
  for (auto& version : ppc::g_ram_versions) version.store(0, std::memory_order_relaxed);
  std::memcpy(&world.result.context, &start, sizeof start);
  const unsigned saved = _mm_getcsr();
  _mm_setcsr(g_start_mxcsr);
  world.result.ok = body(world.result.context, world.ram);
  world.result.mxcsr = _mm_getcsr() & kMxcsrControl;
  _mm_setcsr(saved);
  world.result.events = host::g_events;
  world.result.poll_count = ppc::g_poll_count; world.result.poll_digest = ppc::g_poll_digest;
  world.result.resume_checks = ppc::g_computed_return_checks; world.result.resumed = ppc::g_resumed_returns;
  world.result.fatals = host::g_fatal_count;
  for (uint32_t i = 0; i < ppc::RAM_WATCH_COUNT; ++i) {
    world.result.versions[i] = ppc::g_ram_versions[i].load(std::memory_order_relaxed);
    if (index == 0) g_generation_bumps += world.result.versions[i];
  }
  if (index == 0) g_host_events += world.result.events.count;
}
// Pages written in any world since the last call, compared across all worlds. The worlds start
// equal and every difference fails the test, so pages nobody wrote are equal by induction.
inline bool ram_same() {
  static std::vector<size_t> pages;
  pages.clear();
  for (int i = 0; i < g_count; ++i) {
    for (;;) {
      void* dirty[512];
      ULONG_PTR found = 512;
      DWORD granularity = 0;
      if (GetWriteWatch(WRITE_WATCH_FLAG_RESET, g_world[i].ram, kRamBytes, dirty, &found, &granularity) != 0) return false;
      for (ULONG_PTR j = 0; j < found; ++j) pages.push_back(size_t(static_cast<uint8_t*>(dirty[j]) - g_world[i].ram) / kPage);
      if (found < 512) break;
    }
  }
  std::sort(pages.begin(), pages.end());
  pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
  for (size_t page : pages) {
    ++g_pages_compared;
    for (int i = 1; i < g_count; ++i)
      if (std::memcmp(g_world[0].ram + page * kPage, g_world[i].ram + page * kPage, kPage)) return false;
  }
  return true;
}
// Makes every world equal to world 0 again after a sample that is not compared: the pages any
// world wrote, and the locked cache.
inline void resync() {
  for (int i = 0; i < g_count; ++i) {
    for (;;) {
      void* dirty[512];
      ULONG_PTR found = 512;
      DWORD granularity = 0;
      if (GetWriteWatch(WRITE_WATCH_FLAG_RESET, g_world[i].ram, kRamBytes, dirty, &found, &granularity) != 0) return;
      for (ULONG_PTR j = 0; j < found; ++j) {
        const size_t at = size_t(static_cast<uint8_t*>(dirty[j]) - g_world[i].ram);
        for (int k = 1; k < g_count; ++k) std::memcpy(g_world[k].ram + at, g_world[0].ram + at, kPage);
      }
      if (found < 512) break;
    }
  }
  for (int i = 0; i < g_count; ++i) ResetWriteWatch(g_world[i].ram, kRamBytes);
  for (int k = 1; k < g_count; ++k) std::memcpy(g_world[k].lc, g_world[0].lc, sizeof g_world[0].lc);
}
// Set by a test for one comparison in which the reference met an operation whose NaN result C++
// leaves to the compiler (reference::g_nan_order_open): a float register half that is a NaN in
// both worlds then counts as equal. Everything else is still compared bit for bit.
inline bool g_accept_either_nan = false;
inline uint64_t g_either_nan_accepted = 0;
inline bool nan_bits(uint64_t bits) { return (bits & 0x7FF0000000000000ull) == 0x7FF0000000000000ull && (bits & 0x000FFFFFFFFFFFFFull); }
inline bool context_same(const ppc::Context& a, const ppc::Context& b) {
  if (reference::same(a, b)) return true;
  if (!g_accept_either_nan) return false;
  ppc::Context merged;
  std::memcpy(&merged, &a, sizeof a);
  for (int i = 0; i < 32; ++i) {
    if (nan_bits(a.f[i].u0) && nan_bits(b.f[i].u0)) merged.f[i].u0 = b.f[i].u0;
    if (nan_bits(a.f[i].u1) && nan_bits(b.f[i].u1)) merged.f[i].u1 = b.f[i].u1;
  }
  if (!reference::same(merged, b)) return false;
  ++g_either_nan_accepted;
  return true;
}
// Null when every world agrees with world 0, else what differs first.
inline const char* difference() {
  const char* what = nullptr;
  const Result& first = g_world[0].result;
  for (int i = 1; i < g_count && !what; ++i) {
    const Result& other = g_world[i].result;
    if (!first.ok || !other.ok) what = "a world did not run to its return";
    else if (!context_same(first.context, other.context)) what = "context";
    else if (first.events.count != other.events.count || first.events.digest != other.events.digest) what = "host calls";
    else if (first.poll_count != other.poll_count || first.poll_digest != other.poll_digest) what = "back-edge polls";
    else if (first.mxcsr != other.mxcsr) what = "MXCSR control bits";
    else if (first.resume_checks != other.resume_checks || first.resumed != other.resumed) what = "computed return counters";
    else if (std::memcmp(first.versions, other.versions, sizeof first.versions)) what = "RAM write generations";
    else if (std::memcmp(g_world[0].lc, g_world[i].lc, sizeof g_world[0].lc)) what = "locked cache";
  }
  if (!ram_same() && !what) what = "guest RAM";
  return what;
}
} // namespace worlds
