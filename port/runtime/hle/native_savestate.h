// Rollback snapshots of the native game (Slippi's capture and load savestate commands).
//
// A snapshot covers the game's main heap and the game image's writable sections, minus the ranges
// the game marks as outside the simulation: audio driver state, host-coupled shim state and the
// online bookkeeping itself. Those are exactly what a console savestate leaves out, so voices keep
// playing and the record of predicted frames survives a load.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "../abi/mu_host.h"

namespace native_savestate {

struct Span {
  uint8_t* address;
  uint32_t size;
};

// Regions minus exclusions, sorted by address; touching spans are merged. Parts of exclusions
// outside every region are ignored (the host validates exclusions separately).
std::vector<Span> subtract(const std::vector<MuStateRegion>& regions,
                           const std::vector<MuStateRegion>& exclusions);

// 64-bit hash of the bytes the spans cover, for determinism checks (not a wire checksum).
uint64_t hash_spans(const std::vector<Span>& spans);

struct Stats {
  uint64_t captures = 0, loads = 0, missing_loads = 0;
  double capture_ms_total = 0, capture_ms_max = 0;
  double load_ms_total = 0, load_ms_max = 0;
};

// Slot pool with the semantics of Slippi's savestate commands: capture keeps the current state under
// a frame number, taking a free slot or else the one holding the oldest frame (a frame captured
// twice reuses its own slot); load restores one frame and then frees every slot.
//
// Storage is incremental and exactly equivalent to a full copy per slot. The spans are cut into
// page chunks (4 KB, clipped to the span). A shadow holds every chunk as of the last capture or
// load. A capture finds the chunks written since then (GetWriteWatch where the memory was
// allocated with MEM_WRITE_WATCH, detected per span at begin(); memcmp against the shadow
// elsewhere), moves their old shadow pages into the new checkpoint's undo list and copies the live
// bytes into fresh shadow pages. Checkpoints form a timeline; undo lists of dropped checkpoints are
// merged into their successor, so memory stays bounded by live slots times dirty chunks. A load
// walks undo lists back to the target and copies each touched chunk once.
class Engine {
 public:
  void begin(std::vector<Span> spans, int slots);
  void end();
  bool active() const { return !spans_.empty(); }
  void capture(int32_t frame);
  // False when no state is kept under `frame` (nothing changes).
  bool load(int32_t frame);
  // The same restore for a replay viewer's jump back: the states kept under `frame` and before it
  // stay kept (it can be returned to again), only the newer ones are dropped.
  bool load_keep(int32_t frame);
  bool has(int32_t frame) const { return active_.count(frame) != 0; }
  // The newest kept frame at or below `frame`; false when there is none.
  bool newest_at_or_before(int32_t frame, int32_t* found) const {
    auto it = active_.upper_bound(frame);
    if (it == active_.begin()) return false;
    *found = std::prev(it)->first;
    return true;
  }
  size_t kept() const { return active_.size(); }
  // Memory held for the shadow and the kept states (the page pool, used or spare).
  size_t pool_bytes() const { return pool_blocks_.size() * (size_t)256 * kPage; }

  // Determinism self-test: keep the current state under `frame` as a reference, then later compare
  // the live state (re-simulated to the same frame with the same inputs) against it. Returns the
  // differing byte ranges as [address, length), adjacent differences within 16 bytes merged.
  void keep_reference(int32_t frame);
  bool has_reference(int32_t frame) const { return ref_frame_ == frame && ref_ != nullptr; }
  std::vector<std::pair<uintptr_t, uint32_t>> compare_reference() const;
  const uint8_t* reference_bytes_at(uintptr_t address) const;
  size_t bytes() const { return bytes_; }
  const std::vector<Span>& spans() const { return spans_; }
  const Stats& stats() const { return stats_; }
  // Bytes of span memory covered by write watching (the rest is diffed against the shadow).
  size_t watched_bytes() const { return watched_bytes_; }
  // Chunks copied by the last capture or load (diagnostics).
  size_t last_chunks() const { return last_chunks_; }

  Engine() = default;
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;
  ~Engine() { end(); }

 private:
  static constexpr uint32_t kPage = 4096;
  struct Chunk {
    uint8_t* address;
    uint32_t size;
    bool watched;
  };
  struct WatchRange {       // page-aligned, write-watched
    uintptr_t base;
    size_t pages;
    uint32_t first_chunk;   // lowest chunk index whose page is >= base
  };
  struct Undo {
    uint32_t chunk;
    uint8_t* page;          // chunk contents at the previous checkpoint
  };
  struct Checkpoint {
    int32_t frame;
    bool alive;
    std::vector<Undo> undo;
  };

  uint8_t* alloc_page();
  void free_page(uint8_t* p) { pool_.push_back(p); }
  void collect_dirty();     // fills dirty_ (unique chunk indices) and resets watches
  void reset_watches();
  void drop_dead();

  using Slot = std::unique_ptr<uint8_t[]>;
  std::vector<Span> spans_;
  size_t bytes_ = 0, watched_bytes_ = 0, last_chunks_ = 0;
  int slots_ = 0;
  std::vector<Chunk> chunks_;
  std::vector<WatchRange> watches_;
  std::vector<uint32_t> unwatched_;          // chunk indices diffed against the shadow
  std::vector<uint8_t> unwatched_diff_;
  std::vector<uint8_t*> shadow_;             // per chunk, a kPage pool page
  std::vector<uint32_t> stamp_;              // per chunk dedupe stamps
  uint32_t stamp_gen_ = 0;
  std::vector<uint32_t> dirty_;
  std::vector<void*> ww_buf_;
  std::vector<uint8_t*> pool_;
  std::vector<std::unique_ptr<uint8_t[]>> pool_blocks_;
  std::vector<Checkpoint> timeline_;         // oldest first
  std::map<int32_t, size_t> active_;         // alive frame -> timeline index (rebuilt on change)
  Stats stats_;
  Slot ref_;
  int32_t ref_frame_ = -1;

  uint32_t next_stamp();
  void reindex();

  // Copies of large dirty lists are split across a few persistent workers (one core cannot
  // saturate memory bandwidth). run(n, fn) calls fn(begin, end) over [0, n) and returns when done.
  void run(size_t n, const std::function<void(size_t, size_t)>& fn);
  void start_workers();
  void stop_workers();
  std::vector<std::thread> workers_;
  std::mutex mu_;
  std::condition_variable cv_;
  const std::function<void(size_t, size_t)>* job_ = nullptr;
  size_t job_n_ = 0;
  uint64_t job_gen_ = 0;
  std::atomic<int> job_left_{0};
  bool quit_ = false;
};

}  // namespace native_savestate
