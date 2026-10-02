// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_savestate.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace native_savestate {
namespace {

double elapsed_ms(std::chrono::steady_clock::time_point since) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - since).count();
}

// Equality of two chunks, 64 bytes per step (MSVC's memcmp is several times slower for this).
bool same_bytes(const uint8_t* a, const uint8_t* b, uint32_t n) {
  while (n >= 64) {
    uint64_t x[8], y[8];
    std::memcpy(x, a, 64);
    std::memcpy(y, b, 64);
    uint64_t d = 0;
    for (int i = 0; i < 8; ++i) d |= x[i] ^ y[i];
    if (d) return false;
    a += 64;
    b += 64;
    n -= 64;
  }
  while (n--)
    if (*a++ != *b++) return false;
  return true;
}

}  // namespace

std::vector<Span> subtract(const std::vector<MuStateRegion>& regions,
                           const std::vector<MuStateRegion>& exclusions) {
  std::vector<std::pair<uintptr_t, uintptr_t>> holes;
  for (const auto& e : exclusions)
    if (e.address && e.size) holes.emplace_back((uintptr_t)e.address, (uintptr_t)e.address + e.size);
  std::sort(holes.begin(), holes.end());
  std::vector<std::pair<uintptr_t, uintptr_t>> out;
  for (const auto& r : regions) {
    if (!r.address || !r.size) continue;
    uintptr_t cursor = (uintptr_t)r.address;
    const uintptr_t end = cursor + r.size;
    for (const auto& h : holes) {
      if (h.second <= cursor) continue;
      if (h.first >= end) break;
      if (h.first > cursor) out.emplace_back(cursor, h.first);
      cursor = std::max(cursor, h.second);
      if (cursor >= end) break;
    }
    if (cursor < end) out.emplace_back(cursor, end);
  }
  std::sort(out.begin(), out.end());
  std::vector<Span> spans;
  for (const auto& s : out) {
    if (!spans.empty()) {
      Span& last = spans.back();
      const uintptr_t last_end = (uintptr_t)last.address + last.size;
      if (s.first <= last_end) {   // touching or overlapping regions merge
        const uintptr_t new_end = std::max(last_end, s.second);
        last.size = (uint32_t)(new_end - (uintptr_t)last.address);
        continue;
      }
    }
    spans.push_back({(uint8_t*)s.first, (uint32_t)(s.second - s.first)});
  }
  return spans;
}

uint64_t hash_spans(const std::vector<Span>& spans) {
  uint64_t h = 0x9E3779B97F4A7C15ull;
  for (const auto& s : spans) {
    const uint8_t* p = s.address;
    uint32_t n = s.size;
    while (n >= 8) {
      uint64_t w;
      std::memcpy(&w, p, 8);
      h = (h ^ w) * 0x100000001B3ull;
      h ^= h >> 29;
      p += 8;
      n -= 8;
    }
    while (n--) h = (h ^ *p++) * 0x100000001B3ull;
    h ^= (uint64_t)s.size;   // span boundaries are part of the identity
  }
  return h;
}

void Engine::start_workers() {
  const unsigned hw = std::thread::hardware_concurrency();
  const int count = hw >= 8 ? 3 : hw >= 4 ? 1 : 0;
  quit_ = false;
  // New workers start from the current job generation. Starting from 0 made each worker of a
  // second match run the first match's last job at once, through a std::function that no longer
  // existed: every second online game crashed.
  const uint64_t start_gen = job_gen_;
  for (int i = 0; i < count; ++i) {
    workers_.emplace_back([this, i, start_gen] {
      uint64_t seen = start_gen;
      for (;;) {
        const std::function<void(size_t, size_t)>* job;
        size_t n, parts;
        {
          std::unique_lock<std::mutex> lock(mu_);
          cv_.wait(lock, [&] { return quit_ || job_gen_ != seen; });
          if (quit_) return;
          seen = job_gen_;
          job = job_;
          n = job_n_;
          parts = workers_.size() + 1;
        }
        const size_t part = (size_t)i + 1;
        (*job)(n * part / parts, n * (part + 1) / parts);
        job_left_.fetch_sub(1, std::memory_order_release);
      }
    });
  }
}

void Engine::stop_workers() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    quit_ = true;
  }
  cv_.notify_all();
  for (auto& t : workers_) t.join();
  workers_.clear();
  job_ = nullptr;
}

void Engine::run(size_t n, const std::function<void(size_t, size_t)>& fn) {
  if (workers_.empty() || n < 256) {   // below ~1 MB the wake-up costs more than it saves
    fn(0, n);
    return;
  }
  const size_t parts = workers_.size() + 1;
  job_left_.store((int)workers_.size(), std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(mu_);
    job_ = &fn;
    job_n_ = n;
    ++job_gen_;
  }
  cv_.notify_all();
  fn(0, n / parts);
  while (job_left_.load(std::memory_order_acquire) != 0) std::this_thread::yield();
}

uint8_t* Engine::alloc_page() {
  if (pool_.empty()) {
    constexpr size_t kBlockPages = 256;
    pool_blocks_.emplace_back(new uint8_t[kBlockPages * kPage]);
    uint8_t* base = pool_blocks_.back().get();
    for (size_t i = kBlockPages; i-- > 0;) pool_.push_back(base + i * kPage);
  }
  uint8_t* p = pool_.back();
  pool_.pop_back();
  return p;
}

uint32_t Engine::next_stamp() {
  if (++stamp_gen_ == 0) {
    std::fill(stamp_.begin(), stamp_.end(), 0u);
    stamp_gen_ = 1;
  }
  return stamp_gen_;
}

void Engine::reset_watches() {
#ifdef _WIN32
  for (const auto& w : watches_) ResetWriteWatch((void*)w.base, w.pages * kPage);
#endif
}

void Engine::collect_dirty() {
  const uint32_t gen = next_stamp();
  dirty_.clear();
  auto mark = [&](uint32_t c) {
    if (stamp_[c] != gen) {
      stamp_[c] = gen;
      dirty_.push_back(c);
    }
  };
#ifdef _WIN32
  for (const auto& w : watches_) {
    const uintptr_t w_end = w.base + w.pages * kPage;
    ULONG_PTR count = (ULONG_PTR)w.pages;
    DWORD gran = 0;
    if (GetWriteWatch(WRITE_WATCH_FLAG_RESET, (void*)w.base, w.pages * kPage, ww_buf_.data(), &count,
                      &gran) != 0) {
      // Cannot happen for a range that passed detection; stay exact anyway.
      for (uint32_t c = w.first_chunk; c < chunks_.size() && (uintptr_t)chunks_[c].address < w_end; ++c)
        mark(c);
      continue;
    }
    uint32_t c = w.first_chunk;
    for (ULONG_PTR i = 0; i < count; ++i) {   // addresses come back ascending
      const uintptr_t page = (uintptr_t)ww_buf_[i];
      while (c < chunks_.size() && (uintptr_t)chunks_[c].address < page) ++c;
      for (uint32_t k = c; k < chunks_.size() && (uintptr_t)chunks_[k].address < page + kPage; ++k) mark(k);
    }
  }
#endif
  run(unwatched_.size(), [this](size_t b, size_t e) {
    for (size_t i = b; i < e; ++i) {
      const uint32_t c = unwatched_[i];
      unwatched_diff_[i] = !same_bytes(chunks_[c].address, shadow_[c], chunks_[c].size);
    }
  });
  for (size_t i = 0; i < unwatched_.size(); ++i)
    if (unwatched_diff_[i]) mark(unwatched_[i]);
}

void Engine::begin(std::vector<Span> spans, int slots) {
  end();
  spans_ = std::move(spans);
  slots_ = slots;
  bytes_ = 0;
  for (const auto& s : spans_) {
    bytes_ += s.size;
    uintptr_t a = (uintptr_t)s.address;
    const uintptr_t e = a + s.size;
    while (a < e) {
      const uintptr_t next = std::min<uintptr_t>(e, (a & ~(uintptr_t)(kPage - 1)) + kPage);
      chunks_.push_back({(uint8_t*)a, (uint32_t)(next - a), false});
      a = next;
    }
  }
  // Page ranges of the spans (spans sharing a page merge); each is tested for write watching.
  std::vector<std::pair<uintptr_t, uintptr_t>> ranges;
  for (const auto& s : spans_) {
    const uintptr_t lo = (uintptr_t)s.address & ~(uintptr_t)(kPage - 1);
    const uintptr_t hi = ((uintptr_t)s.address + s.size + kPage - 1) & ~(uintptr_t)(kPage - 1);
    if (!ranges.empty() && lo < ranges.back().second) ranges.back().second = std::max(ranges.back().second, hi);
    else ranges.emplace_back(lo, hi);
  }
  watched_bytes_ = 0;
#ifdef _WIN32
  size_t max_pages = 0;
  for (const auto& r : ranges) max_pages = std::max(max_pages, (size_t)((r.second - r.first) / kPage));
  ww_buf_.resize(max_pages);
  uint32_t ci = 0;
  for (const auto& r : ranges) {
    while (ci < chunks_.size() && (uintptr_t)chunks_[ci].address < r.first) ++ci;
    const size_t pages = (r.second - r.first) / kPage;
    ULONG_PTR count = (ULONG_PTR)pages;
    DWORD gran = 0;
    if (GetWriteWatch(0, (void*)r.first, pages * kPage, ww_buf_.data(), &count, &gran) != 0 || gran != kPage)
      continue;
    watches_.push_back({r.first, pages, ci});
    for (uint32_t k = ci; k < chunks_.size() && (uintptr_t)chunks_[k].address < r.second; ++k) {
      chunks_[k].watched = true;
      watched_bytes_ += chunks_[k].size;
    }
  }
#endif
  for (uint32_t c = 0; c < chunks_.size(); ++c)
    if (!chunks_[c].watched) unwatched_.push_back(c);
  reset_watches();
  // Prefault a spare page pool (half the snapshot) so match-time captures do not take first-touch
  // page faults; the pool grows past it only for unusually large dirty sets.
  for (size_t spare = bytes_ / 2 / kPage; spare-- > 0;) std::memset(alloc_page(), 0, kPage);
  pool_.clear();
  for (auto& b : pool_blocks_)
    for (size_t i = 256; i-- > 0;) pool_.push_back(b.get() + i * kPage);
  unwatched_diff_.assign(unwatched_.size(), 0);
  start_workers();
  shadow_.resize(chunks_.size());
  stamp_.assign(chunks_.size(), 0u);
  stamp_gen_ = 0;
  for (size_t c = 0; c < chunks_.size(); ++c) {
    shadow_[c] = alloc_page();
    std::memcpy(shadow_[c], chunks_[c].address, chunks_[c].size);
  }
}

void Engine::end() {
  stop_workers();
  spans_.clear();
  bytes_ = watched_bytes_ = 0;
  slots_ = 0;
  chunks_.clear();
  watches_.clear();
  unwatched_.clear();
  unwatched_diff_.clear();
  shadow_.clear();
  stamp_.clear();
  dirty_.clear();
  ww_buf_.clear();
  timeline_.clear();
  active_.clear();
  pool_.clear();
  pool_blocks_.clear();   // owns every page
  ref_.reset();
  ref_frame_ = -1;
}

void Engine::reindex() {
  active_.clear();
  for (size_t i = 0; i < timeline_.size(); ++i)
    if (timeline_[i].alive) active_[timeline_[i].frame] = i;
}

// Removes dead checkpoints. A dropped checkpoint's undo list (contents at its predecessor) merges
// into its successor's, the older content winning; the oldest kept checkpoint needs no undo list.
void Engine::drop_dead() {
  std::vector<Checkpoint> out;
  out.reserve(timeline_.size());
  std::vector<Undo> pending;
  bool have_pending = false;
  for (size_t i = 0; i < timeline_.size(); ++i) {
    Checkpoint& cp = timeline_[i];
    if (have_pending) {
      const uint32_t gen = next_stamp();
      for (const auto& u : pending) stamp_[u.chunk] = gen;
      for (const auto& u : cp.undo) {
        if (stamp_[u.chunk] == gen) free_page(u.page);
        else pending.push_back(u);
      }
      cp.undo = std::move(pending);
      pending = {};
      have_pending = false;
    }
    const bool last = i + 1 == timeline_.size();
    if (!cp.alive && !last) {
      pending = std::move(cp.undo);
      cp.undo = {};
      have_pending = true;
      continue;
    }
    if (out.empty()) {
      for (const auto& u : cp.undo) free_page(u.page);
      cp.undo.clear();
    }
    out.push_back(std::move(cp));
  }
  timeline_ = std::move(out);
}

void Engine::keep_reference(int32_t frame) {
  if (!active()) return;
  if (!ref_) ref_ = Slot(new uint8_t[bytes_]);
  uint8_t* dst = ref_.get();
  for (const auto& s : spans_) {
    std::memcpy(dst, s.address, s.size);
    dst += s.size;
  }
  ref_frame_ = frame;
}

std::vector<std::pair<uintptr_t, uint32_t>> Engine::compare_reference() const {
  std::vector<std::pair<uintptr_t, uint32_t>> diffs;
  if (!ref_) return diffs;
  const uint8_t* ref = ref_.get();
  for (const auto& s : spans_) {
    for (uint32_t i = 0; i < s.size; ++i) {
      if (s.address[i] == ref[i]) continue;
      const uintptr_t a = (uintptr_t)s.address + i;
      if (!diffs.empty() && a <= diffs.back().first + diffs.back().second + 16)
        diffs.back().second = (uint32_t)(a + 1 - diffs.back().first);
      else
        diffs.emplace_back(a, 1u);
    }
    ref += s.size;
  }
  return diffs;
}

const uint8_t* Engine::reference_bytes_at(uintptr_t address) const {
  if (!ref_) return nullptr;
  const uint8_t* ref = ref_.get();
  for (const auto& s : spans_) {
    const uintptr_t a = (uintptr_t)s.address;
    if (address >= a && address < a + s.size) return ref + (address - a);
    ref += s.size;
  }
  return nullptr;
}

void Engine::capture(int32_t frame) {
  if (!active() || slots_ <= 0) return;
  const auto start = std::chrono::steady_clock::now();
  auto same = active_.find(frame);
  if (same != active_.end()) timeline_[same->second].alive = false;
  else if ((int)active_.size() >= slots_) timeline_[active_.begin()->second].alive = false;   // lowest frame
  collect_dirty();
  Checkpoint cp{frame, true, {}};
  const bool keep_undo = !timeline_.empty();
  if (keep_undo) cp.undo.reserve(dirty_.size());
  for (uint32_t c : dirty_) {
    uint8_t* old = shadow_[c];
    shadow_[c] = alloc_page();
    if (keep_undo) cp.undo.push_back({c, old});
    else free_page(old);
  }
  run(dirty_.size(), [this](size_t b, size_t e) {
    for (size_t i = b; i < e; ++i) {
      const uint32_t c = dirty_[i];
      std::memcpy(shadow_[c], chunks_[c].address, chunks_[c].size);
    }
  });
  last_chunks_ = dirty_.size();
  timeline_.push_back(std::move(cp));
  drop_dead();
  reindex();
  const double ms = elapsed_ms(start);
  ++stats_.captures;
  stats_.capture_ms_total += ms;
  stats_.capture_ms_max = std::max(stats_.capture_ms_max, ms);
}

bool Engine::load(int32_t frame) {
  auto it = active_.find(frame);
  if (it == active_.end()) {
    ++stats_.missing_loads;
    return false;
  }
  const auto start = std::chrono::steady_clock::now();
  const size_t target = it->second;
  collect_dirty();   // chunks written since the newest checkpoint; their stamps mark "touched"
  const uint32_t gen = stamp_gen_;
  for (size_t k = timeline_.size(); k-- > target + 1;) {
    for (const auto& u : timeline_[k].undo) {
      free_page(shadow_[u.chunk]);
      shadow_[u.chunk] = u.page;
      if (stamp_[u.chunk] != gen) {
        stamp_[u.chunk] = gen;
        dirty_.push_back(u.chunk);
      }
    }
    timeline_[k].undo.clear();
  }
  run(dirty_.size(), [this](size_t b, size_t e) {
    for (size_t i = b; i < e; ++i) {
      const uint32_t c = dirty_[i];
      std::memcpy(chunks_[c].address, shadow_[c], chunks_[c].size);
    }
  });
  last_chunks_ = dirty_.size();
  for (auto& cp : timeline_)
    for (const auto& u : cp.undo) free_page(u.page);
  timeline_.clear();
  active_.clear();
  reset_watches();   // the restore's own writes are not game writes
  const double ms = elapsed_ms(start);
  ++stats_.loads;
  stats_.load_ms_total += ms;
  stats_.load_ms_max = std::max(stats_.load_ms_max, ms);
  return true;
}

// As load(), then the timeline is cut after the target instead of emptied: the shadow is the
// target's state, every older checkpoint still has its undo list, and the newer ones (whose undo
// lists were just consumed) are gone.
bool Engine::load_keep(int32_t frame) {
  auto it = active_.find(frame);
  if (it == active_.end()) {
    ++stats_.missing_loads;
    return false;
  }
  const auto start = std::chrono::steady_clock::now();
  const size_t target = it->second;
  collect_dirty();
  const uint32_t gen = stamp_gen_;
  for (size_t k = timeline_.size(); k-- > target + 1;) {
    for (const auto& u : timeline_[k].undo) {
      free_page(shadow_[u.chunk]);
      shadow_[u.chunk] = u.page;
      if (stamp_[u.chunk] != gen) {
        stamp_[u.chunk] = gen;
        dirty_.push_back(u.chunk);
      }
    }
    timeline_[k].undo.clear();
  }
  run(dirty_.size(), [this](size_t b, size_t e) {
    for (size_t i = b; i < e; ++i) {
      const uint32_t c = dirty_[i];
      std::memcpy(chunks_[c].address, shadow_[c], chunks_[c].size);
    }
  });
  last_chunks_ = dirty_.size();
  timeline_.erase(timeline_.begin() + (std::ptrdiff_t)target + 1, timeline_.end());
  reindex();
  reset_watches();   // the restore's own writes are not game writes
  const double ms = elapsed_ms(start);
  ++stats_.loads;
  stats_.load_ms_total += ms;
  stats_.load_ms_max = std::max(stats_.load_ms_max, ms);
  return true;
}

}  // namespace native_savestate
