#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
namespace NativeMelee {
struct PackedTrack {
  int16_t start_frame = 0;
  uint8_t channel = 0, value_format = 0, slope_format = 0;
  std::vector<uint8_t> bytes;
  // Or bytes kept by the track's owner, when `view` is set and `bytes` is empty: the Source Port's pose
  // chains keep every track of a chain in one buffer that lives as long as the chain (one allocation
  // per chain instead of one per track). Read the bytes through data() and size().
  const uint8_t* view = nullptr;
  uint32_t view_size = 0;
  const uint8_t* data() const { return view ? view : bytes.data(); }
  size_t size() const { return view ? view_size : bytes.size(); }
};
inline bool SameBytes(const PackedTrack& a, const PackedTrack& b) {
  const size_t n = a.size();
  return n == b.size() && (n == 0 || a.data() == b.data() || std::memcmp(a.data(), b.data(), n) == 0);
}
// Short-lived state for a second sample of the same immutable track. It owns
// no data and must not outlive or span changes to that track.
struct PackedSampleCursor {
  const PackedTrack* track = nullptr;
  float elapsed_wait = 0;
  alignas(8) uint8_t state[96];
};
// Re-evaluates immutable authored data at an absolute animation time.
// Returns false for an inactive track. Does not execute guest callbacks.
bool SamplePacked(const PackedTrack& track, float frame, float& value, PackedSampleCursor* cursor = nullptr);
// Reuse a decoded curve segment when possible; otherwise take the exact normal
// interpreter path. This also accepts earlier times, loop wraps and boundaries.
bool SamplePackedFromCursor(const PackedTrack& track, float frame, float& value, const PackedSampleCursor& cursor);
}
