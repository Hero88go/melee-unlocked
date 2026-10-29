// Motion must refer to the preceding displayed frame, including after backlog draining.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace gx {
class TemporalMotionHistory {
public:
  struct Pose { std::array<float, 256> position{}; std::array<float, 16> projection{}; };
  const Pose* previous(uint64_t identity, uint64_t presentation, bool presenting) {
    if (!presenting) return nullptr;
    Entry& e = poses_[identity];
    if (e.frame != presentation) {
      e.previous = e.current;
      e.previous_frame = e.frame;
      e.frame = presentation;
    }
    return e.previous_frame && e.previous_frame + 1 == presentation ? &e.previous : nullptr;
  }
  void record(uint64_t identity, uint64_t presentation, bool presenting, const float* position, const float* projection) {
    if (!presenting) return;
    Entry& e = poses_[identity];
    // Call previous first: all passes of one object must retain the same preceding-frame pose.
    std::memcpy(e.current.position.data(), position, sizeof(float) * 256);
    std::memcpy(e.current.projection.data(), projection, sizeof(float) * 16);
    e.frame = presentation;
  }
  void clear() { poses_.clear(); }
  void prune(uint64_t presentation) {
    for (auto it = poses_.begin(); it != poses_.end();)
      if (it->second.frame + 4 < presentation) it = poses_.erase(it); else ++it;
  }
private:
  struct Entry { Pose current, previous; uint64_t frame = 0, previous_frame = 0; };
  std::unordered_map<uint64_t, Entry> poses_;
};
}  // namespace gx
