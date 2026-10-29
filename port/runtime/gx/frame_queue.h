// Ordered transfer from the simulation to the renderer. Every frame is executed (EFB copies
// made in one frame feed later ones), never dropped; a backlog is drained by the renderer
// without presenting (threaded_backend.cpp), so the simulation only waits in the pathological
// case of a renderer that has stopped consuming altogether (cap 32 frames, half a second, or
// 256 MB of queued frames, see kQueuedBytes).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gx_core.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <mutex>
namespace gx {
class FrameQueue {
  std::mutex mutex;
  std::atomic<uint64_t> pushes{0};   // every push, readable without the mutex
  std::condition_variable changed;
  std::deque<Frame> frames;
  std::deque<Frame> recycled;   // buffers returned by the renderer, capacity preserved
  bool finished = false;
  // The memory the queued frames hold (each keeps a recycled buffer's capacity, 10 to 20 MB). A
  // slow GPU on a 5.7 GB PC let the backlog reach 32 such frames, several hundred MB at once, and
  // the simulation's next allocation failed ("bad allocation"). Past this the producer waits as it
  // does at 32 frames; two frames always fit, so a large scene never waits on its own size.
  static constexpr size_t kQueuedBytes = 256u << 20;
  static size_t frame_bytes(const Frame& f) {
    return f.vertices.capacity() * sizeof(Vertex) + f.draws.capacity() * sizeof(DrawCall) +
           f.segments.capacity() * sizeof(f.segments[0]) + f.commands.capacity() * sizeof(f.commands[0]);
  }
  bool has_room() const {
    if (frames.size() >= 32) return false;
    if (frames.size() < 2) return true;
    size_t bytes = 0;
    for (const Frame& f : frames) bytes += frame_bytes(f);
    return bytes < kQueuedBytes;
  }
public:
  bool push(Frame frame) {
    std::unique_lock<std::mutex> lock(mutex);
    changed.wait(lock, [&] { return finished || has_room(); });   // the renderer drains backlogs; this only trips if it is stuck
    if (finished) return false;
    frames.push_back(std::move(frame));
    pushes.fetch_add(1, std::memory_order_release);
    changed.notify_all();
    return true;
  }
  // How many frames have been pushed so far: a renderer compares it with the count it saw when it
  // started a present to learn that a newer frame is waiting.
  uint64_t pushed() const { return pushes.load(std::memory_order_acquire); }
  // Hands the simulation's frame to the renderer without copying it, and hands back a cleared
  // frame that still owns its buffers (a few MB of vertices and draw state), so the simulation
  // thread refills them instead of reallocating every frame.
  bool push_and_recycle(Frame& frame) {
    std::unique_lock<std::mutex> lock(mutex);
    changed.wait(lock, [&] { return finished || has_room(); });
    if (finished) return false;
    // Keep one frame of headroom in every buffer handed back to simulation. An abrupt increase in
    // Stadium geometry should allocate into an empty recycled frame here instead of moving a live
    // multi-megabyte DrawCall vector halfway through the next simulation frame.
    const size_t want_vertices = std::max<size_t>(65536, frame.vertices.size() + frame.vertices.size() / 4 + 1);
    const size_t want_draws = std::max<size_t>(1024, frame.draws.size() + frame.draws.size() / 4 + 1);
    const size_t want_segments = std::max<size_t>(2048, frame.segments.size() + frame.segments.size() / 4 + 1);
    const size_t want_commands = std::max<size_t>(2048, frame.commands.size() + frame.commands.size() / 4 + 1);
    const size_t want_copies = std::max<size_t>(16, frame.copies.size() + 4);
    frames.push_back(std::move(frame));
    pushes.fetch_add(1, std::memory_order_release);
    if (!recycled.empty()) { frame = std::move(recycled.back()); recycled.pop_back(); }
    frame.clear();
    changed.notify_all();
    lock.unlock();
    // Reserving a cleared local frame does not need the queue mutex. Large capacity changes must
    // not hold the render thread out of pop/recycle while the allocator obtains memory.
    frame.reserve_gameplay_capacity(want_vertices, want_draws, want_segments, want_commands, want_copies);
    return true;
  }
  // The renderer returns buffers it is about to overwrite.
  void recycle(Frame&& frame) {
    std::lock_guard<std::mutex> lock(mutex);
    if (recycled.size() >= 4) return;
    frame.clear();
    recycled.push_back(std::move(frame));
  }
  bool pop(Frame& frame) {
    std::unique_lock<std::mutex> lock(mutex);
    changed.wait_for(lock, std::chrono::milliseconds(2), [&] { return finished || !frames.empty(); });
    if (frames.empty()) return false;
    frame = std::move(frames.front()); frames.pop_front();
    changed.notify_all();
    return true;
  }
  size_t size() { std::lock_guard<std::mutex> lock(mutex); return frames.size(); }
  // Non-blocking variant for a presenter that renders between frames.
  bool try_pop(Frame& frame) {
    std::lock_guard<std::mutex> lock(mutex);
    if (frames.empty()) return false;
    frame = std::move(frames.front()); frames.pop_front();
    changed.notify_all();
    return true;
  }
  // Blocks up to `wait` for a frame to become available (or finish).
  template <class Rep, class Period>
  bool wait_available(std::chrono::duration<Rep, Period> wait) {
    std::unique_lock<std::mutex> lock(mutex);
    return changed.wait_for(lock, wait, [&] { return finished || !frames.empty(); }) && !frames.empty();
  }
  void finish(bool discard = false) {
    std::lock_guard<std::mutex> lock(mutex);
    finished = true;
    if (discard) frames.clear();
    changed.notify_all();
  }
  bool drained() {
    std::lock_guard<std::mutex> lock(mutex);
    return finished && frames.empty();
  }
};
}
