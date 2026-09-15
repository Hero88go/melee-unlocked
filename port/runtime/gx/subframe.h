// Sub-frame pose solver: builds per-draw transform overrides for a display frame that falls between
// two 60 Hz simulation frames. Presentation only; guest state is never touched.
//
// For each draw of the current frame that also existed in the previous frame (same identity, same
// vertex count and primitive), every position matrix slot the draw uses gets the delta transform
//   D = M_cur * inverse(M_prev)
// decomposed into rotation / uniform-ish scale / translation. The fraction t of that delta is applied
// on top of M_cur (extrapolation, predicted motion) or M_prev (interpolation, one frame of latency).
// Non-rigid deltas and detected cuts retain the latest pose. Experimental matrix modes
// use heuristic pairing; authored mode samples validated tracks forward and holds all
// unsupported draws at the latest pose. Guest object generations guard address reuse.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "gx_core.h"
#include "authored_pose.h"

namespace gx {

struct SubFrameStats { uint32_t draws = 0, paired = 0, rigid = 0, blended = 0, cuts = 0; uint32_t missing = 0, hud = 0, state = 0, geometry = 0, projection = 0, state_register = 256, authored = 0, carried = 0, vertex_blended = 0;
  uint32_t skinned = 0;   // skinned (character model) draws in the current frame; zero on menus and stage select
};

class SubFrameSolver {
 public:
  // Thresholds for treating a per-frame delta as a discontinuity (world units / radians).
  // Screw-motion extrapolation of a 3x4 matrix `t` frames past `cur` given `prev` (t in [0,1]);
  // holds `cur` at discontinuities. Used for camera motion by the authored path.
  static void extrapolate_matrix(const float prev[12], const float cur[12], double t, float out[12]);
  static void interpolate_matrix(const float prev[12], const float cur[12], double t, float out[12]);   // between prev and cur
  float max_translation = 40.0f;
  float max_rotation = 1.2f;

  // Pairs draws of `cur` with `prev` and caches the per-slot deltas. Both frames must stay alive
  // until the next call.
  void set_frames(const Frame* prev, const Frame* cur);
  // Fills `out` (resized to cur->draws.size()) for phase t in [0, 1]. Interpolate: pose between
  // prev (t = 0) and cur (t = 1). Extrapolate: pose t frames beyond cur.
  void build(double t, bool interpolate, std::vector<DrawMatrices>& out, bool authored = false) const;
  const SubFrameStats& stats() const { return stats_; }

  // Exposed for tests: apply fraction t of the rigid/blended delta between prev and cur 3x4 matrices.
  static void fractional(const float prev[12], const float cur[12], double t, bool interpolate,
                         float max_translation, float max_rotation, float out_pos[12], float out_nrm[9],
                         const float cur_nrm[9], const float prev_nrm[9], SubFrameStats* stats);

 private:
  // bit i set: a 3x4 matrix starts at row i. Position and texture-coordinate matrices share the
  // array but are advanced differently, so they are tracked apart.
  struct Pair { int prev_draw; uint64_t used_slots; uint64_t pos_slots; uint64_t tex_slots; bool blend_vertices; size_t blend_offset; };
  const Frame* prev_ = nullptr;
  const Frame* cur_ = nullptr;
  std::vector<Pair> pairs_;
  mutable std::vector<Vertex> vertex_blend_;   // per presented frame: blended streams, one disjoint range per draw
  // Previous frame draws by identity, sorted; reused across simulation frames so pairing does not
  // allocate a hash node per draw every tick.
  std::vector<std::pair<uint64_t, int>> prev_index_;
  // This frame's camera (from the first paired draw that carries a view): used to move draws that
  // have no pair onto the same timeline as the rest of the frame.
  const AuthoredPose* camera_previous_ = nullptr;
  const AuthoredPose* camera_current_ = nullptr;
  mutable SubFrameStats stats_;
};

}  // namespace gx
