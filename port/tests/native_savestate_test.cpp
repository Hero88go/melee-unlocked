// Snapshot spans, Slippi savestate slot semantics and the determinism hash.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_savestate.h"
#include <cstdio>
#include <chrono>
#include <cstring>
#include <map>
#include <random>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

using namespace native_savestate;

namespace {
MuStateRegion reg(uint8_t* base, uint32_t off, uint32_t size) { return {base + off, size}; }

// Memory the engine can write-watch (as the host reserves MEM1), or plain heap memory elsewhere.
uint8_t* alloc_watched(size_t size) {
#ifdef _WIN32
  return (uint8_t*)VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE);
#else
  return new uint8_t[size];
#endif
}

// Randomized differential test: the incremental engine against a naive full-copy model, byte for
// byte over the whole buffers (exclusions included), across random writes, captures and loads.
int differential() {
  const size_t kW = 1u << 20, kU = 300000;
  uint8_t* w = alloc_watched(kW);
  std::vector<uint8_t> u_buf(kU);
  uint8_t* u = u_buf.data();
  std::vector<uint8_t> ew(kW), eu(kU);   // expected contents of both buffers
  std::mt19937 rng(12345);
  for (size_t i = 0; i < kW; ++i) w[i] = ew[i] = (uint8_t)rng();
  for (size_t i = 0; i < kU; ++i) u[i] = eu[i] = (uint8_t)rng();
  std::vector<MuStateRegion> excl;
  for (int i = 0; i < 12; ++i) {
    uint32_t off = rng() % (kW - 9000), len = 1 + rng() % 9000;
    excl.push_back(reg(w, off, len));
  }
  for (int i = 0; i < 5; ++i) excl.push_back(reg(u, rng() % (kU - 3000), 1 + rng() % 3000));
  excl.push_back(reg(w, 4096 * 7 + 100, 50));     // hole inside one page
  const auto spans = subtract({reg(w, 0, (uint32_t)kW), reg(u, 0, (uint32_t)kU)}, excl);
  auto to_expected = [&](uint8_t* p) -> uint8_t* {
    if (p >= w && p < w + kW) return ew.data() + (p - w);
    return eu.data() + (p - u);
  };
  auto model_copy = [&]() {
    std::vector<uint8_t> out;
    for (const auto& s : spans) out.insert(out.end(), to_expected(s.address), to_expected(s.address) + s.size);
    return out;
  };
  for (int round = 0; round < 6; ++round) {
    const int slots = 1 + round % 4 + (round >= 4 ? 4 : 0);
    Engine e;
    e.begin(spans, slots);
#ifdef _WIN32
    CHECK(e.watched_bytes() > 900000 && e.watched_bytes() < e.bytes());
#endif
    std::map<int32_t, std::vector<uint8_t>> model;
    int32_t frame = 0;
    for (int op = 0; op < 4000; ++op) {
      const int kind = rng() % 10;
      if (kind < 5) {   // writes: page bursts, single bytes, same-value writes
        const int n = 1 + rng() % 40;
        for (int k = 0; k < n; ++k) {
          const bool in_w = rng() % 4 != 0;
          const size_t size = in_w ? kW : kU;
          uint8_t* live = in_w ? w : u;
          uint8_t* exp = in_w ? ew.data() : eu.data();
          size_t off = rng() % size;
          size_t len = rng() % 3 == 0 ? std::min<size_t>(size - off, 1 + rng() % 9000) : 1;
          for (size_t j = 0; j < len; ++j) {
            const uint8_t v = rng() % 8 == 0 ? live[off + j] : (uint8_t)rng();
            live[off + j] = exp[off + j] = v;
          }
        }
      } else if (kind < 8) {   // capture: mostly advancing, sometimes a repeat or older frame
        const int r = rng() % 6;
        int32_t f = r == 0 ? frame - (int32_t)(rng() % 4) : r == 1 ? frame : ++frame;
        e.capture(f);
        if (!model.count(f) && (int)model.size() >= slots) model.erase(model.begin());
        model[f] = model_copy();
      } else {   // load an existing frame, or a missing one
        int32_t f;
        if (!model.empty() && rng() % 5 != 0) {
          auto it = model.begin();
          std::advance(it, rng() % model.size());
          f = it->first;
        } else {
          f = frame + 100;
        }
        const bool ok = e.load(f);
        CHECK(ok == (model.count(f) != 0));
        if (ok) {
          const auto& snap = model[f];
          size_t pos = 0;
          for (const auto& s : spans) {
            std::memcpy(to_expected(s.address), snap.data() + pos, s.size);
            pos += s.size;
          }
          model.clear();
          frame = f;
        }
        CHECK(std::memcmp(w, ew.data(), kW) == 0);
        CHECK(std::memcmp(u, eu.data(), kU) == 0);
      }
      for (int32_t f = frame - 8; f <= frame + 2; ++f) CHECK(e.has(f) == (model.count(f) != 0));
    }
    // Reference self-test still works on top.
    e.keep_reference(frame);
    CHECK(e.has_reference(frame) && e.compare_reference().empty());
    spans[0].address[0] ^= 1;
    CHECK(e.compare_reference().size() == 1 && *e.reference_bytes_at((uintptr_t)spans[0].address) ==
                                                   (uint8_t)(spans[0].address[0] ^ 1));
    spans[0].address[0] ^= 1;
    to_expected(spans[0].address)[0] = spans[0].address[0];
  }
  std::printf("native savestate: differential ok\n");
  return 0;
}

// One engine across several matches, as the game keeps it: begin, captures and loads big enough to
// use the worker threads, end, and again. Workers of a second match used to run the first match's
// last job (a dead std::function) at once: every second online game crashed.
int restart() {
  const size_t kW = 8u << 20;
  uint8_t* w = alloc_watched(kW);
  std::memset(w, 0, kW);
  Engine e;
  for (int match = 0; match < 3; ++match) {
    e.begin(subtract({reg(w, 0, (uint32_t)kW)}, {}), 7);
    for (int32_t f = 1; f <= 30; ++f) {
      for (size_t p = 0; p < kW; p += 4096 * 3) w[p] = (uint8_t)(f + match);
      e.capture(f);
      if (f % 10 == 0) {
        CHECK(e.load(f - 3));
        CHECK(w[0] == (uint8_t)(f - 3 + match));
      }
    }
    e.end();
  }
  std::printf("native savestate: restart ok\n");
  return 0;
}

// 43 MB snapshot (40 MB write-watched, 2.7 MB diffed) with 2-6 MB dirtied per frame; worst-case
// rollback = load 7 frames back, then 7 frames of writes + captures.
void benchmark() {
  using clock = std::chrono::steady_clock;
  const size_t kW = 40u << 20, kU = 2700u << 10, kPages = kW / 4096;
  uint8_t* w = alloc_watched(kW);
  std::vector<uint8_t> u(kU, 0);
  std::memset(w, 0, kW);
  std::vector<uint8_t> full(kW + kU);
  auto t0 = clock::now();
  for (int i = 0; i < 20; ++i) {
    std::memcpy(full.data(), w, kW);
    std::memcpy(full.data() + kW, u.data(), kU);
  }
  const double full_ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count() / 20;
  std::printf("bench: old full copy of %.1f MB: %.2f ms per capture or load\n", (kW + kU) / 1048576.0, full_ms);

  for (bool realistic : {true, false})
  for (size_t dirty_mb : {2, 4, 6}) {
    Engine e;
    e.begin(subtract({reg(w, 0, (uint32_t)kW), reg(u.data(), 0, (uint32_t)kU)}, {}), 7);
    std::mt19937 rng(7);
    // Hot set rewritten every frame (heap objects, stacks, player state), the rest scattered.
    const size_t hot = realistic ? dirty_mb * 256 * 8 / 10 : kPages / 40;
    auto dirty_frame = [&]() {
      for (size_t p = 0; p < hot; ++p) w[(p * 7 % kPages) * 4096 + (rng() & 4095)] = (uint8_t)rng();
      const size_t random_pages = dirty_mb * 256 - hot;
      for (size_t k = 0; k < random_pages; ++k) w[(rng() % kPages) * 4096 + (rng() & 4095)] = (uint8_t)rng();
      for (int k = 0; k < 25; ++k) u[rng() % kU] = (uint8_t)rng();
    };
    int32_t frame = 0;
    for (; frame < 20; ++frame) { dirty_frame(); e.capture(frame); }
    double worst = 0, total = 0, cap_sum = 0, load_sum = 0;
    int caps = 0, loads = 0;
    const int kRollbacks = 30;
    for (int r = 0; r < kRollbacks; ++r) {
      for (int k = 0; k < 3; ++k, ++frame) {   // steady frames between rollbacks
        dirty_frame();
        auto s = clock::now();
        e.capture(frame);
        cap_sum += std::chrono::duration<double, std::milli>(clock::now() - s).count();
        ++caps;
      }
      auto rb = clock::now();
      dirty_frame();
      auto s = clock::now();
      e.load(frame - 7);
      load_sum += std::chrono::duration<double, std::milli>(clock::now() - s).count();
      ++loads;
      double snap = std::chrono::duration<double, std::milli>(clock::now() - s).count();
      frame -= 7;
      for (int k = 0; k < 7; ++k, ++frame) {
        dirty_frame();
        auto c = clock::now();
        e.capture(frame);
        const double ms = std::chrono::duration<double, std::milli>(clock::now() - c).count();
        snap += ms;
        cap_sum += ms;
        ++caps;
      }
      (void)rb;
      total += snap;
      worst = std::max(worst, snap);
    }
    std::printf("bench: %s %zu MB/frame dirty: capture avg %.3f ms (max %.3f), load avg %.3f ms (max %.3f), "
                "7-frame rollback snapshot work avg %.2f ms, worst %.2f ms\n",
                realistic ? "hot-set (80%)" : "scattered (25% hot)", dirty_mb, cap_sum / caps, e.stats().capture_ms_max, load_sum / loads, e.stats().load_ms_max,
                total / kRollbacks, worst);
  }
}
}  // namespace

int main() {
  if (restart() != 0) return 1;
  static uint8_t mem[400];
  uint8_t* m = mem;

  // Holes inside, straddling a region end, and in the gap between regions.
  auto spans = subtract({reg(m, 0, 100), reg(m, 200, 100)},
                        {reg(m, 10, 10), reg(m, 90, 20), reg(m, 250, 10), reg(m, 295, 10), reg(m, 150, 10)});
  CHECK(spans.size() == 4);
  CHECK(spans[0].address == m + 0 && spans[0].size == 10);
  CHECK(spans[1].address == m + 20 && spans[1].size == 70);
  CHECK(spans[2].address == m + 200 && spans[2].size == 50);
  CHECK(spans[3].address == m + 260 && spans[3].size == 35);

  // Touching regions merge; an exclusion covering a whole region removes it.
  spans = subtract({reg(m, 0, 100), reg(m, 100, 50), reg(m, 300, 20)}, {reg(m, 300, 20)});
  CHECK(spans.size() == 1 && spans[0].address == m && spans[0].size == 150);
  spans = subtract({reg(m, 0, 100)}, {});
  CHECK(spans.size() == 1 && spans[0].size == 100);

  // Capture and load restore spans and leave holes alone.
  std::memset(mem, 0, sizeof mem);
  Engine e;
  e.begin(subtract({reg(m, 0, 100), reg(m, 200, 100)}, {reg(m, 40, 10)}), 2);
  CHECK(e.active() && e.bytes() == 190);
  for (int i = 0; i < 400; ++i) mem[i] = (uint8_t)i;
  const uint64_t h1 = hash_spans(e.spans());
  e.capture(1);
  mem[5] = 0xAA; mem[45] = 0xBB; mem[250] = 0xCC;
  CHECK(hash_spans(e.spans()) != h1);
  e.capture(2);
  mem[6] = 0xDD;
  CHECK(e.load(1));
  CHECK(mem[5] == 5 && mem[6] == 6 && mem[250] == 250);
  CHECK(mem[45] == 0xBB);                       // excluded byte keeps its live value
  mem[45] = 45;
  CHECK(hash_spans(e.spans()) == h1);
  mem[45] = 0xEE;                               // hole bytes do not affect the hash
  CHECK(hash_spans(e.spans()) == h1);
  CHECK(!e.has(1) && !e.has(2));                // a load frees every slot
  CHECK(e.stats().captures == 2 && e.stats().loads == 1);

  // Two slots: the lowest frame number is evicted, as Slippi does; a frame captured twice keeps
  // one slot.
  e.capture(10); e.capture(11); e.capture(12);
  CHECK(!e.has(10) && e.has(11) && e.has(12));
  mem[0] = 1; e.capture(11);
  CHECK(e.has(11) && e.has(12));
  mem[0] = 2; e.capture(13);
  CHECK(e.has(12) && e.has(13) && !e.has(11));
  CHECK(e.load(12) && mem[0] == 0);
  CHECK(!e.load(99) && e.stats().missing_loads == 1);

  // A replay viewer's jump back: the state returned to and the older ones stay kept, newer ones go,
  // and the same state can be returned to again after playing on.
  e.end();
  e.begin(subtract({reg(m, 0, 100), reg(m, 200, 100)}, {reg(m, 40, 10)}), 8);
  for (int i = 0; i < 400; ++i) mem[i] = (uint8_t)i;
  e.capture(100);
  mem[5] = 0x11; mem[250] = 0x12; e.capture(200);
  mem[5] = 0x21; mem[60] = 0x22; e.capture(300);
  mem[5] = 0x31; mem[250] = 0x32; mem[45] = 0x33; e.capture(400);
  mem[7] = 0x41;
  int32_t found = 0;
  CHECK(e.newest_at_or_before(399, &found) && found == 300 && e.newest_at_or_before(100, &found) && found == 100);
  CHECK(!e.newest_at_or_before(99, &found) && e.kept() == 4);
  CHECK(e.load_keep(200));
  CHECK(mem[5] == 0x11 && mem[250] == 0x12 && mem[60] == 60 && mem[7] == 7);
  CHECK(mem[45] == 0x33);                       // excluded byte keeps its live value
  CHECK(e.has(100) && e.has(200) && !e.has(300) && !e.has(400));
  mem[5] = 0x51; mem[8] = 0x52; e.capture(300); // playing on from the jump
  mem[9] = 0x61;
  CHECK(e.load_keep(200) && mem[5] == 0x11 && mem[8] == 8 && mem[9] == 9 && e.has(200) && !e.has(300));
  CHECK(e.load_keep(100) && mem[5] == 5 && mem[250] == 250 && e.has(100) && !e.has(200));
  CHECK(e.load_keep(100) && mem[5] == 5);       // and again, with nothing newer
  CHECK(!e.load_keep(200));
  mem[45] = 45;

  e.end();
  CHECK(!e.active());
  e.capture(1);                                 // inactive engine ignores commands
  CHECK(!e.has(1));
  if (int r = differential()) return r;
  benchmark();
  std::printf("native savestate: ok\n");
  return 0;
}
