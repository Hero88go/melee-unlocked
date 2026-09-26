// GD's Melee UI motion port. Source: menu/pipeline/hub_motion.py,
// commit 477aff14eb2608ccf7780f936e65136cb1c1cbe2.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>

namespace gx::gd_melee_ui {

// Evaluate one cubic Hermite segment. The source motion data expresses each
// tangent in pixels per 60 Hz frame; convert it to the segment's unit interval.
inline float spline(float from, float to, float from_slope, float to_slope,
                    float duration, float frame) {
  const float u = std::clamp(frame / duration, 0.0f, 1.0f);
  const float u2 = u * u;
  const float u3 = u2 * u;
  const float h00 = 2.0f * u3 - 3.0f * u2 + 1.0f;
  const float h10 = u3 - 2.0f * u2 + u;
  const float h01 = -2.0f * u3 + 3.0f * u2;
  const float h11 = u3 - u2;
  return h00 * from + h10 * duration * from_slope + h01 * to + h11 * duration * to_slope;
}

// Exact keyframes from ev_slide_in / ev_slide_out. The app retargets an
// interrupted transition from its live position to avoid a toggle pop; the
// source's normal completed-open/closed tracks start at 0/720 as specified.
inline float slide_in(float from, float frame) {
  // When a close is interrupted and reopened, preserve the live position and
  // ease it back to the resting point. Reusing the source's -120 px/frame
  // entry tangent from an arbitrary offset made the panel race through the
  // viewport and appear clipped at high refresh rates.
  if (from != 720.0f) {
    const float t = std::clamp(frame / 17.0f, 0.0f, 1.0f);
    const float eased = 1.0f - (1.0f-t)*(1.0f-t)*(1.0f-t);
    return from * (1.0f-eased);
  }
  if (frame <= 12.0f) return spline(from, -6.0f, -120.0f, 0.0f, 12.0f, frame);
  if (frame <= 17.0f) return spline(-6.0f, 0.0f, 0.0f, 0.0f, 5.0f, frame - 12.0f);
  return 0.0f;
}

inline float slide_out(float from, float frame) {
  if (frame <= 12.0f) return spline(from, -720.0f, 0.0f, -120.0f, 12.0f, frame);
  return -720.0f;
}

}  // namespace gx::gd_melee_ui
