// --flicker-scan: finds one-frame glitches in the presented image, for both renderers.
//
// A one-frame glitch is invisible to a screenshot key and too rare to catch by dumping frames: at an
// unlocked rate a few thousand dumped frames cover a fraction of a second. So each presented frame
// is reduced to a 64x64 grid of luminance values and compared against its two neighbours, and only a
// frame that pops out and comes back is reported (to the log, with the three grids written to
// flicker/ so a hit can be recognised afterwards).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>

namespace gx {

class FlickerScanner {
 public:
  static constexpr int kGrid = 64;
  // One presented frame's grid (kGrid * kGrid luminance values, 0-255), in presentation order.
  // `frame` is the presented frame number it belongs to.
  void push(const float* grid, uint32_t frame);

 private:
  float sig_[3][kGrid * kGrid] = {};
  uint32_t frames_[3] = {};
  uint64_t history_ = 0, hits_ = 0, cell_hits_ = 0, dumps_ = 0;
};

}  // namespace gx
