// A plain "preparing graphics" frame for backends that compile pipelines before the game starts.
// SPDX-License-Identifier: GPL-2.0-or-later
//
// A first start compiles every known pipeline before boot (20 s and more on some machines). The
// window used to stay frozen on its last image meanwhile, which reads as a hang; inside the desktop
// app the window title (the only progress there was on Linux) is not even visible. This lays out a
// frame as solid rectangles only (a panel, a progress bar and text in a 5x7 block font), so any
// backend can draw it with the clears it already has: vkCmdClearAttachments on Vulkan, a scissored
// glClear on OpenGL. No shaders, textures or fonts, so it works before anything else is ready.
//
// Header-only, like pipeline_cache_store.h, so no build lists change.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace gx::progress_frame {

inline constexpr const char* kTitle = "Preparing graphics for this PC (first start only)";
// Every pipeline so far came from the cache: only loading, not the first start's compiling (GitHub #138: after
// a reboot the cache files are read from a cold disk, and "first start only" stayed up on every first launch).
inline constexpr const char* kLoadingTitle = "Loading graphics";
inline const char* title(bool compiling) { return compiling ? kTitle : kLoadingTitle; }

struct Rect {
  int x, y, w, h;      // pixels, top-left origin
  uint32_t rgba;       // 0xRRGGBBAA
};

inline constexpr uint32_t kBackground = 0x0B0D12FFu;
inline constexpr uint32_t kPanel = 0x1A1E27FFu;
inline constexpr uint32_t kTrack = 0x2C323FFFu;
inline constexpr uint32_t kFill = 0x4C8DFFFFu;
inline constexpr uint32_t kText = 0xE8ECF3FFu;
inline constexpr uint32_t kDim = 0x9AA3B5FFu;

// 5x7 glyphs, one byte per row, bit 4 = leftmost column. Lower case draws as upper case; anything
// without a glyph is a space.
inline const uint8_t* glyph(char c) {
  static const uint8_t blank[7] = {};
  static const uint8_t digits[10][7] = {
      {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
      {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},
      {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
      {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
      {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}};
  static const uint8_t letters[26][7] = {
      {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},  // A B
      {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C},  // C D
      {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},  // E F
      {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},  // G H
      {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C},  // I J
      {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},  // K L
      {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11},  // M N
      {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},  // O P
      {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},  // Q R
      {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},  // S T
      {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},  // U V
      {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},  // W X
      {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}}; // Y Z
  static const uint8_t open[7] = {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02};
  static const uint8_t close[7] = {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08};
  static const uint8_t slash[7] = {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10};
  static const uint8_t dot[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C};
  static const uint8_t comma[7] = {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08};
  static const uint8_t colon[7] = {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00};
  static const uint8_t dash[7] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
  if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
  if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
  if (c >= '0' && c <= '9') return digits[c - '0'];
  switch (c) {
    case '(': return open;
    case ')': return close;
    case '/': return slash;
    case '.': return dot;
    case ',': return comma;
    case ':': return colon;
    case '-': return dash;
    default: return blank;
  }
}

// Text as rectangles: each glyph row's runs of lit pixels become one rectangle, so a line is a few
// hundred clears at most. A glyph cell is 6 x 7 pixels (one column of spacing) times scale.
inline void text(std::vector<Rect>& out, const std::string& s, int x, int y, int scale, uint32_t rgba) {
  for (size_t i = 0; i < s.size(); ++i) {
    const uint8_t* rows = glyph(s[i]);
    const int gx = x + (int)i * 6 * scale;
    for (int row = 0; row < 7; ++row) {
      int col = 0;
      while (col < 5) {
        if (!(rows[row] & (0x10 >> col))) { ++col; continue; }
        int end = col;
        while (end < 5 && (rows[row] & (0x10 >> end))) ++end;
        out.push_back({gx + col * scale, y + row * scale, (end - col) * scale, scale, rgba});
        col = end;
      }
    }
  }
}
inline int text_width(const std::string& s, int scale) { return s.empty() ? 0 : ((int)s.size() * 6 - 1) * scale; }

inline std::string counts(size_t done, size_t total) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%zu / %zu", (std::min)(done, total), total);
  return buf;
}

// The whole frame for a width x height target: background, panel, title, bar, "done / total".
inline std::vector<Rect> layout(int width, int height, size_t done, size_t total, const std::string& title = kTitle) {
  std::vector<Rect> out;
  if (width <= 0 || height <= 0) return out;
  out.push_back({0, 0, width, height, kBackground});
  const std::string count = counts(done, total);
  const int chars = (int)(std::max<size_t>)(title.size(), 1);
  // Big enough to read on a TV, small enough that the title fits in 90% of the width.
  int scale = (std::max)(1, (std::min)(height / 240, (width * 9 / 10) / (chars * 6)));
  const int line = 7 * scale;
  const int pad = 6 * scale;
  const int panel_w = (std::min)(width, text_width(title, scale) + 2 * pad);
  const int bar_h = (std::max)(2, 3 * scale);
  const int panel_h = (std::min)(height, pad + line + pad + bar_h + pad + line + pad);
  const int px = (width - panel_w) / 2, py = (height - panel_h) / 2;
  out.push_back({px, py, panel_w, panel_h, kPanel});
  text(out, title, px + (panel_w - text_width(title, scale)) / 2, py + pad, scale, kText);
  const int bar_x = px + pad, bar_y = py + pad + line + pad, bar_w = (std::max)(1, panel_w - 2 * pad);
  out.push_back({bar_x, bar_y, bar_w, bar_h, kTrack});
  const int filled = total ? (int)((long long)bar_w * (long long)(std::min)(done, total) / (long long)total) : 0;
  if (filled > 0) out.push_back({bar_x, bar_y, filled, bar_h, kFill});
  text(out, count, px + (panel_w - text_width(count, scale)) / 2, bar_y + bar_h + pad, scale, kDim);
  // Clip to the target: a clear outside it is an error on Vulkan.
  std::vector<Rect> clipped;
  clipped.reserve(out.size());
  for (Rect r : out) {
    const int x0 = (std::max)(0, r.x), y0 = (std::max)(0, r.y);
    const int x1 = (std::min)(width, r.x + r.w), y1 = (std::min)(height, r.y + r.h);
    if (x1 > x0 && y1 > y0) clipped.push_back({x0, y0, x1 - x0, y1 - y0, r.rgba});
  }
  return clipped;
}

// When to draw: nothing for the first moment (a warm cache finishes before anyone would notice,
// so a normal launch never flashes it), then at most ten frames a second.
class Pacer {
 public:
  using Clock = std::chrono::steady_clock;
  explicit Pacer(std::chrono::milliseconds delay = std::chrono::milliseconds(400),
                 std::chrono::milliseconds every = std::chrono::milliseconds(100))
      : start_(Clock::now()), delay_(delay), every_(every) {}
  bool due(Clock::time_point now = Clock::now()) {
    if (now - start_ < delay_) return false;
    if (shown_ && now - last_ < every_) return false;
    shown_ = true;
    last_ = now;
    return true;
  }
  bool shown() const { return shown_; }

 private:
  Clock::time_point start_, last_{};
  std::chrono::milliseconds delay_, every_;
  bool shown_ = false;
};

}  // namespace gx::progress_frame
