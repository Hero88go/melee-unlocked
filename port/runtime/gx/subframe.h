// Sub-frame pose solver: builds per-draw transform overrides for a display frame that falls between
// two 60 Hz simulation frames. Presentation only; guest state is never touched.
//
// For each draw of the current frame that also existed in the previous frame (same identity, same
// vertex count and primitive), every position matrix slot the draw uses gets the delta transform
//   D = M_cur * inverse(M_prev)
// decomposed into rotation / uniform-ish scale / translation. The fraction t of that delta is applied
// on top of M_cur (extrapolation, zero added latency) or M_prev (interpolation, one frame of latency).
// Non-rigid deltas fall back to a linear matrix blend; camera cuts and teleports (large deltas) keep
// the exact simulation pose so nothing ever overshoots into a wrong place.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "gx_core.h"

namespace gx {

struct SubFrameStats { uint32_t draws = 0, paired = 0, rigid = 0, blended = 0, cuts = 0; };

class SubFrameSolver {
 public:
  // Thresholds for treating a per-frame delta as a discontinuity (world units / radians).
  float max_translation = 40.0f;
  float max_rotation = 1.2f;

  // Pairs draws of `cur` with `prev` and caches the per-slot deltas. Both frames must stay alive
  // until the next call.
  void set_frames(const Frame* prev, const Frame* cur);
  // Fills `out` (resized to cur->draws.size()) for phase t in [0, 1]. Interpolate: pose between
  // prev (t = 0) and cur (t = 1). Extrapolate: pose t frames beyond cur.
  void build(double t, bool interpolate, std::vector<DrawMatrices>& out) const;
  const SubFrameStats& stats() const { return stats_; }

  // Exposed for tests: apply fraction t of the rigid/blended delta between prev and cur 3x4 matrices.
  static void fractional(const float prev[12], const float cur[12], double t, bool interpolate,
                         float max_translation, float max_rotation, float out_pos[12], float out_nrm[9],
                         const float cur_nrm[9], const float prev_nrm[9], SubFrameStats* stats);

 private:
  struct Pair { int prev_draw; uint64_t used_slots; };   // bit i set: pos matrix row 3*i used (i < 22)
  const Frame* prev_ = nullptr;
  const Frame* cur_ = nullptr;
  std::vector<Pair> pairs_;
  std::unordered_map<uint64_t, int> prev_index_;
  SubFrameStats stats_;
};

}  // namespace gx
