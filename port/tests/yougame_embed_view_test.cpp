// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_embed_view.h"
#include <climits>
#include <initializer_list>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

static bool equal(yg_embed_box a, yg_embed_box b) {
  return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

static void legacy(int minimum) {
  auto s = yg_embed_view_init(1280, 960, minimum);
  CHECK(!yg_embed_view_visible(&s));
  for (const auto box : {yg_embed_box{10, 20, 640, 480}, {30, 40, 800, 600},
                         {-20, -10, 640, 480}, {0, 0, minimum, minimum}}) {
    const auto old = s.window;
    const auto r = yg_embed_view_update(&s, box, 0);
    CHECK(equal(r.window, box));
    CHECK(equal(r.clip, (yg_embed_box{0, 0, box.w, box.h})));
    CHECK(r.resize == (old.w != box.w || old.h != box.h));
    CHECK(r.valid && r.visible && !s.has_view && !r.warn_clamp);
  }
  const auto old = s.window;
  auto r = yg_embed_view_update(&s, {0, 0, minimum - 1, 480}, 0);
  CHECK(!r.valid && !r.visible && !r.resize && equal(r.window, old));
  yg_embed_view_show(&s, 0);
  r = yg_embed_view_update(&s, {0, 0, 640, 480}, 0);
  CHECK(r.valid && !r.visible);
  yg_embed_view_show(&s, 1);
  CHECK(yg_embed_view_visible(&s));
}

int main() {
  legacy(1); legacy(32);
  auto s = yg_embed_view_init(640, 480, 32);
  const yg_embed_box view{100, 80, 1000, 800};
  auto r = yg_embed_view_update(&s, view, 1);
  CHECK(s.has_view && !s.has_rect && r.resize && equal(r.window, view));
  CHECK(!r.visible && !r.valid && r.clip.w == 0 && r.clip.h == 0);
  r = yg_embed_view_update(&s, view, 0);
  CHECK(!r.resize && !r.move && r.visible);
  CHECK(equal(r.clip, (yg_embed_box{0, 0, 1000, 800})));
  // Six overlay transitions change only the clip, never layout or surface size.
  for (int trim = 0; trim < 6; ++trim) {
    r = yg_embed_view_update(&s, {100 + trim, 80 + trim, 1000 - 2 * trim, 800 - trim}, 0);
    CHECK(!r.resize && !r.move && r.visible && equal(r.window, view));
    CHECK(equal(r.clip, (yg_embed_box{trim, trim, 1000 - 2 * trim, 800 - trim})));
  }
  yg_embed_view_show(&s, 0);
  r = yg_embed_view_update(&s, view, 0);
  CHECK(!r.visible && !r.resize);
  r = yg_embed_view_update(&s, {100, 80, 1200, 900}, 1);
  CHECK(r.resize && !r.visible); // View changes resize, preserving hide.
  yg_embed_view_show(&s, 1);
  CHECK(yg_embed_view_visible(&s));
  r = yg_embed_view_update(&s, {150, 90, 1200, 900}, 1);
  CHECK(!r.resize && r.move); // Moving the whole view needs no new surface.

  s = yg_embed_view_init(640, 480, 32);
  yg_embed_view_update(&s, view, 1);
  r = yg_embed_view_update(&s, {0, 0, 1200, 1000}, 0);
  CHECK(r.warn_clamp && !r.resize && r.visible);
  CHECK(equal(r.clip, (yg_embed_box{0, 0, 1000, 800})));
  r = yg_embed_view_update(&s, {0, 0, 1200, 1000}, 0);
  CHECK(!r.warn_clamp && !r.resize); // Log once per attachment.
  r = yg_embed_view_update(&s, {1090, 870, 100, 100}, 0);
  CHECK(equal(r.clip, (yg_embed_box{990, 790, 10, 10})) && !r.visible && !r.resize);
  r = yg_embed_view_update(&s, {1200, 1000, 100, 100}, 0);
  CHECK(equal(r.clip, (yg_embed_box{1000, 800, 0, 0})) && !r.visible && !r.resize);
  yg_embed_view_show(&s, 1);
  CHECK(!yg_embed_view_visible(&s));
  r = yg_embed_view_update(&s, {0, 0, 10, 10}, 0);
  CHECK(equal(r.clip, (yg_embed_box{0, 0, 0, 0})) && !r.resize);
  r = yg_embed_view_update(&s, {100, 80, -1, -1}, 0);
  CHECK(r.clip.w == 0 && r.clip.h == 0 && !r.resize && !r.visible);
  r = yg_embed_view_update(&s, view, 0);
  CHECK(r.visible && !r.resize);
  r = yg_embed_view_update(&s, {100, 80, 0, 0}, 1);
  CHECK(!r.resize && !r.visible && equal(r.window, view));

  // A rect preceding the first view must switch from legacy mapping to clipping.
  s = yg_embed_view_init(1280, 960, 1);
  yg_embed_view_update(&s, {120, 100, 800, 600}, 0);
  r = yg_embed_view_update(&s, view, 1);
  CHECK(r.resize && r.visible && equal(r.window, view));
  CHECK(equal(r.clip, (yg_embed_box{20, 20, 800, 600})));
  r = yg_embed_view_update(&s, {INT_MAX - 100, INT_MAX - 100, 1000, 800}, 1);
  CHECK(!r.resize && !r.visible);
  r = yg_embed_view_update(&s, {INT_MAX - 90, INT_MAX - 90, 50, 50}, 0);
  CHECK(equal(r.clip, (yg_embed_box{10, 10, 50, 50})) && r.visible && !r.resize);
  std::puts("YouGame embed view geometry passed");
}
