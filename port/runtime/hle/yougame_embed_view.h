// Pure parent-relative embed geometry shared by the Win32 and X11 adapters.
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef YOUGAME_EMBED_VIEW_H
#define YOUGAME_EMBED_VIEW_H
#include <stdint.h>

typedef struct yg_embed_box { int x, y, w, h; } yg_embed_box;
typedef struct yg_embed_view_state {
    yg_embed_box view, rect, window;
    int has_view, has_rect, shown, valid, minimum, warned_clamp;
} yg_embed_view_state;
typedef struct yg_embed_view_result {
    yg_embed_box window, clip;
    int geometry, move, resize, valid, visible, warn_clamp;
} yg_embed_view_result;

static inline yg_embed_view_state yg_embed_view_init(int w, int h, int minimum)
{
    yg_embed_view_state s = {0};
    s.window.w = w; s.window.h = h;
    s.shown = 1; s.minimum = minimum;
    return s;
}

static inline int yg_embed_view_visible(const yg_embed_view_state *s)
{
    return s->shown && s->valid;
}

static inline void yg_embed_view_show(yg_embed_view_state *s, int shown)
{
    s->shown = shown;
}

/* The adapters retain their legacy parsing/range checks. minimum is 1 on Win32,
 * 32 on X11. Invalid/tiny geometry hides without changing the drawable size.
 * A view may precede the first rect: size the surface, but keep an empty clip.
 * Geometry updates never undo hide. No-view rect mapping stays unchanged. */
static inline yg_embed_view_result yg_embed_view_update(yg_embed_view_state *s,
                                                       yg_embed_box box, int is_view)
{
    yg_embed_view_result r = {0};
    yg_embed_box target;
    if (is_view) { s->view = box; s->has_view = 1; }
    else { s->rect = box; s->has_rect = 1; }
    target = s->has_view ? s->view : s->rect;
    r.geometry = target.w >= s->minimum && target.h >= s->minimum;
    r.window = s->window;
    if (r.geometry) {
        r.window = target;
        r.resize = target.w != s->window.w || target.h != s->window.h;
        r.move = target.x != s->window.x || target.y != s->window.y;
        s->window = target;
    }
    if (s->has_view && s->has_rect) {
        /* Wide sums also cover the largest accepted Win32 integers. Clamp each
         * edge into the view, so even disjoint rects produce a local empty clip. */
        int64_t vx = s->view.x, vy = s->view.y;
        int64_t right = vx + s->view.w, bottom = vy + s->view.h;
        int64_t x = s->rect.x, y = s->rect.y;
        int64_t x2 = x + s->rect.w, y2 = y + s->rect.h;
        int outside = x < vx || y < vy || x2 > right || y2 > bottom ||
                      s->rect.w < 0 || s->rect.h < 0;
        if (x < vx) x = vx;
        if (x > right) x = right;
        if (y < vy) y = vy;
        if (y > bottom) y = bottom;
        if (x2 < x) x2 = x;
        if (x2 > right) x2 = right;
        if (y2 < y) y2 = y;
        if (y2 > bottom) y2 = bottom;
        r.clip.x = (int)(x - vx); r.clip.y = (int)(y - vy);
        r.clip.w = (int)(x2 - x); r.clip.h = (int)(y2 - y);
        r.warn_clamp = outside && !s->warned_clamp;
        if (outside) s->warned_clamp = 1;
    } else if (!s->has_view && s->has_rect) {
        r.clip.w = s->rect.w; r.clip.h = s->rect.h;
    }
    r.valid = r.geometry && s->has_rect && r.clip.w >= s->minimum &&
              r.clip.h >= s->minimum;
    s->valid = r.valid;
    r.visible = yg_embed_view_visible(s);
    return r;
}
#endif
