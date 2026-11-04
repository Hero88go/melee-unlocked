#pragma once
#include <cstdint>
#include <vector>
namespace NativeMelee {
struct PackedTrack {
  int16_t start_frame = 0;
  uint8_t channel = 0, value_format = 0, slope_format = 0;
  std::vector<uint8_t> bytes;
};
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
