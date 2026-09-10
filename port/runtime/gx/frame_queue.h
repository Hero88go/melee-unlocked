// Bounded ordered transfer. Preserve every EFB command until resource dependencies
// are self-contained; never silently drop a simulation frame to report higher FPS.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gx_core.h"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
namespace gx {
class FrameQueue {
  std::mutex mutex;
  std::condition_variable changed;
  std::deque<Frame> frames;
  bool finished = false;
public:
  bool push(Frame frame) {
    std::unique_lock<std::mutex> lock(mutex);
    changed.wait(lock, [&] { return finished || frames.size() < 2; });
    if (finished) return false;
    frames.push_back(std::move(frame));
    changed.notify_all();
    return true;
  }
  bool pop(Frame& frame) {
    std::unique_lock<std::mutex> lock(mutex);
    changed.wait_for(lock, std::chrono::milliseconds(2), [&] { return finished || !frames.empty(); });
    if (frames.empty()) return false;
    frame = std::move(frames.front()); frames.pop_front();
    changed.notify_all();
    return true;
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
