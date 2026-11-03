// WindowServer-independent embed policy. Placement is in points; the app's player box arrives in backing pixels and is
// converted once (points_from_backing).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace mac_embed {
using Millis = int64_t; // monotonic clock, supplied by the adapter (or a test)
struct Rect { double x = 0, y = 0, w = 0, h = 0; };
inline bool equal(Rect a, Rect b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }
// The app sends the player box in its window's backing pixels (the page's placeEmbed: CSS pixels times devicePixelRatio,
// page zoom included, plus the title bar in backing pixels); AppKit places windows in points. Taking the pixels as points
// made the panel twice the box on a 2x display: d0017/d0018 drew the picture low and off to the side, d0019 twice too
// big (Luis 10-26). scale is the backing scale of the display the parent is on; anything not positive counts as 1.
inline Rect points_from_backing(Rect box, double scale) {
  if (!(scale > 0)) scale = 1;
  return {box.x / scale, box.y / scale, box.w / scale, box.h / scale};
}
// How much of a lies on b: the parent's display is the one it overlaps most, as Electron's getDisplayMatching picks it.
inline double overlap_area(Rect a, Rect b) {
  const double w = std::min(a.x + a.w, b.x + b.w) - std::max(a.x, b.x);
  const double h = std::min(a.y + a.h, b.y + b.h) - std::max(a.y, b.y);
  return w > 0 && h > 0 ? w * h : 0;
}
// parent: CGWindowList bounds (top-left origin, points); box: the player box in points. Returns a Cocoa frame.
inline Rect screen_rect(Rect parent, Rect box, double primary_height) {
  return {parent.x + box.x, primary_height - parent.y - box.y - box.h, box.w, box.h};
}
inline bool intersects(Rect a, Rect b) {
  return a.w > 0 && a.h > 0 && b.w > 0 && b.h > 0 &&
      a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
inline std::optional<uint32_t> parent_id(std::string_view text) {
  if (text.empty()) return {};
  uint32_t id = 0;
  auto result = std::from_chars(text.data(), text.data() + text.size(), id);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || id == 0) return {};
  return id;
}
enum class Kind { Rect, Show, Hide, Focus, Blur, Quit, Scale };
// scale: the app's display scale for the rects that follow ("embed scale n", desktop 0.13.1076+).
struct Command { Kind kind; Rect rect{}; double scale = 0; };
// The stdio bridge strips "embed ". Reject malformed/trailing/overflow input atomically.
inline std::optional<Command> parse_command(const std::string& line) {
  if (line == "show") return Command{Kind::Show};
  if (line == "hide") return Command{Kind::Hide};
  if (line == "focus") return Command{Kind::Focus};
  if (line == "blur") return Command{Kind::Blur};
  if (line == "quit") return Command{Kind::Quit};
  // "scale <n>": the YouGame window's display scale, written before the launch rect and before any rect whose scale
  // changed (desktop 126f55c97). Strictly one finite number in [0.5, 8]; anything else is ignored like before.
  if (line.size() <= 32 && line.rfind("scale ", 0) == 0) {
    std::istringstream in(line.substr(6));
    double scale = 0;
    std::string trailing;
    if (!(in >> scale) || (in >> trailing) || !std::isfinite(scale) || scale < 0.5 || scale > 8) return {};
    return Command{Kind::Scale, {}, scale};
  }
  if (line.size() > 128 || line.rfind("rect ", 0) != 0) return {};
  std::istringstream in(line.substr(5));
  int x, y, w, h;
  std::string trailing;
  if (!(in >> x >> y >> w >> h) || (in >> trailing)) return {};
  constexpr int limit = 100000;
  if (x < -limit || x > limit || y < -limit || y > limit || w < 0 || h < 0 || w > limit || h > limit) return {};
  return Command{Kind::Rect, {double(x), double(y), double(w), double(h)}};
}

class QueryTimer {
 public:
  explicit QueryTimer(Millis interval) : interval_(interval) {}
  void request() { requested_ = true; }
  void reset() { last_.reset(); requested_ = false; }
  bool due(Millis now, Millis interval = -1) {
    const Millis delay = requested_ ? 20 : (interval < 0 ? interval_ : interval);
    if (last_ && now - *last_ < delay) return false;
    last_ = now; requested_ = false;
    return true;
  }
 private:
  Millis interval_;
  std::optional<Millis> last_;
  bool requested_ = false;
};
class StartupBackoff {
 public:
  Millis next() {
    constexpr std::array<Millis, 4> delays{20, 50, 100, 200};
    const auto delay = delays[step_];
    step_ = std::min(step_ + 1, delays.size() - 1);
    return delay;
  }
 private:
  size_t step_ = 0;
};
// Ordering in waits for stable visible bounds; moving an already visible surface never waits.
class SettleGate {
 public:
  void observe(Millis now, bool visible, Rect bounds) {
    if (!visible) { since_.reset(); return; }
    if (!since_ || !equal(bounds_, bounds)) { since_ = now; bounds_ = bounds; }
  }
  bool ready(Millis now) const { return since_ && now - *since_ >= 500; }
 private:
  Rect bounds_{};
  std::optional<Millis> since_;
};
struct Parent { bool exists = false, on_screen = false; Rect bounds{}; int owner = 0; };

// Query at most 10 Hz (startup misses back off to 5 Hz), or 50 Hz on explicit requests.
// A minimized/other-Space parent still exists: only actual absence starts the grace.
class Tracker {
 public:
  void refresh() { parent_timer_.request(); above_timer_.request(); }
  void owner_activated() { parent_timer_.request(); above_timer_.reset(); }
  template<class Query> void tick(Millis now, Query query) {
    if (fallback_) return;
    if (parent_timer_.due(now, startup_ ? startup_delay_ : 100)) {
      auto next = query();
      // Hidden parents may only be represented by an ID. Retain the cached owner so an
      // activation still triggers an immediate ordering check while the parent is hidden.
      if (!next.owner) next.owner = parent_.owner;
      parent_ = next;
      if (parent_.exists) {
        startup_ = false;
        missing_since_.reset();
      } else {
        if (!missing_since_) missing_since_ = now;
        if (startup_) startup_delay_ = backoff_.next();
        // Decide only from a fresh sample. During startup the next bounded retry can be
        // up to 200 ms after the grace; a parent returning between queries must survive.
        if (now - *missing_since_ >= 2000) fallback_ = true;
      }
    }
  }
  const Parent& parent() const { return parent_; }
  bool fallback() const { return fallback_; }
  bool ordering_due(Millis now) { return above_timer_.due(now); }
 private:
  QueryTimer parent_timer_{100}, above_timer_{250};
  StartupBackoff backoff_;
  Parent parent_;
  bool startup_ = true, fallback_ = false;
  Millis startup_delay_ = 0;
  std::optional<Millis> missing_since_;
};

// Keep tiny/zero page boxes hidden even through subsequent "show" commands.
class Presentation {
 public:
  void apply(Command command) {
    switch (command.kind) {
      case Kind::Rect: box = command.rect; have_rect_ = true; break;
      case Kind::Show: shown_ = true; break;
      case Kind::Hide: shown_ = false; break;
      case Kind::Focus: focused = true; break;
      case Kind::Blur: focused = false; break;
      case Kind::Quit: quit = true; break;
      case Kind::Scale: app_scale = command.scale; break;
    }
  }
  void first_frame() { frame_ = true; }
  bool visible(const Parent& parent) const {
    return !quit && frame_ && have_rect_ && shown_ && box.w >= 32 && box.h >= 32 && parent.on_screen;
  }
  Rect box{};
  double app_scale = 0;   // from the app's "scale" line; 0 until one arrives (apps before 0.13.1076 send none)
  bool focused = true, quit = false;
 private:
  bool frame_ = false, have_rect_ = false, shown_ = true;
};
} // namespace mac_embed
