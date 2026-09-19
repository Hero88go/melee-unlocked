// SPDX-License-Identifier: GPL-2.0-or-later
#include "flicker_scan.h"
#include "host.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace gx {
namespace {

void write_grid(const float* grid, const char* path) {
  FILE* f = std::fopen(path, "wb");
  if (!f) return;
  std::fprintf(f, "P6\n%d %d\n255\n", FlickerScanner::kGrid, FlickerScanner::kGrid);
  for (int i = 0; i < FlickerScanner::kGrid * FlickerScanner::kGrid; ++i) {
    const float value = grid[i];
    const unsigned char v = (unsigned char)(value < 0 ? 0 : value > 255 ? 255 : value);
    const unsigned char rgb[3] = {v, v, v};
    std::fwrite(rgb, 1, 3, f);
  }
  std::fclose(f);
}

void write_trio(const float* a, const float* b, const float* c, const char* kind, uint64_t n, uint32_t frame) {
  CreateDirectoryA("flicker", nullptr);
  const float* trio[3] = {a, b, c};
  for (int k = 0; k < 3; ++k) {
    char path[160];
    std::snprintf(path, sizeof path, "flicker/%s%03llu_%u_%s.ppm", kind, (unsigned long long)n, frame,
                  k == 0 ? "1prev" : k == 1 ? "2BAD" : "3next");
    write_grid(trio[k], path);
  }
}

}  // namespace

// A frame in normal motion sits between its neighbours: the distance from each is about half the
// distance between the neighbours themselves. A frame that is wrong sits outside both, so
// min(to previous, to next) exceeds the distance between previous and next. That ratio is the test,
// and it does not care whether the frame is too bright, too dark or in the wrong place.
void FlickerScanner::push(const float* grid, uint32_t frame) {
  constexpr int N = kGrid * kGrid;
  std::memcpy(sig_[history_ % 3], grid, sizeof sig_[0]);
  frames_[history_ % 3] = frame;
  ++history_;
  if (history_ < 3) return;
  const float* a = sig_[(history_ - 3) % 3];
  const float* b = sig_[(history_ - 2) % 3];
  const float* c = sig_[(history_ - 1) % 3];
  const uint32_t bad_frame = frames_[(history_ - 2) % 3];
  float d_prev = 0, d_next = 0, d_skip = 0;
  for (int i = 0; i < N; ++i) {
    d_prev += std::abs(b[i] - a[i]); d_next += std::abs(b[i] - c[i]); d_skip += std::abs(c[i] - a[i]);
  }
  d_prev /= N; d_next /= N; d_skip /= N;
  const float out = std::min(d_prev, d_next);
  // Averaging over the whole picture only finds a defect that covers the whole picture. What a
  // flickering stage actually does is change one part of the screen and change it back, which an
  // average buries. So count cells that pop out and come back on their own, and report where.
  int cells = 0, min_x = kGrid, min_y = kGrid, max_x = -1, max_y = -1;
  float loudest = 0;
  for (int y = 0; y < kGrid; ++y)
    for (int x = 0; x < kGrid; ++x) {
      const int i = y * kGrid + x;
      const float pop = std::min(std::abs(b[i] - a[i]), std::abs(b[i] - c[i]));
      const float across = std::abs(c[i] - a[i]);
      if (pop > 8.0f && pop > across * 3.0f + 1.0f) {
        ++cells;
        min_x = std::min(min_x, x); max_x = std::max(max_x, x);
        min_y = std::min(min_y, y); max_y = std::max(max_y, y);
        loudest = std::max(loudest, pop);
      }
    }
  if (cells >= 4) {
    ++cell_hits_;
    host::log("flicker: presented frame %u has %d of %d cells popping out and back, x %d-%d y %d-%d of %d, loudest %.0f; %llu so far",
              bad_frame, cells, N, min_x, max_x, min_y, max_y, kGrid, loudest, (unsigned long long)cell_hits_);
    if (dumps_ < 60) write_trio(a, b, c, "cell", ++dumps_, bad_frame);
  }
  // The floor keeps a still image, where every difference is near zero, from reporting noise.
  if (out > 1.5f && out > d_skip * 1.25f) {
    ++hits_;
    host::log("flicker: presented frame %u pops out and back (prev %.2f, next %.2f, neighbours %.2f, ratio %.1f); %llu so far",
              bad_frame, d_prev, d_next, d_skip, out / (d_skip > 0.01f ? d_skip : 0.01f), (unsigned long long)hits_);
    if (hits_ <= 40) write_trio(a, b, c, "hit", hits_, bad_frame);
  }
}

}  // namespace gx
