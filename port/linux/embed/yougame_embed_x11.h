#ifndef YOUGAME_EMBED_X11_H
#define YOUGAME_EMBED_X11_H

#include <X11/Xlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct yg_embed_x11 yg_embed_x11;
enum { YG_EMBED_OK = 0, YG_EMBED_IGNORED = 1, YG_EMBED_QUIT = 2 };

/* All calls on the Display-owning thread; serialize process-wide Xlib activity
 * during calls (XSetErrorHandler is global). No event reading or SDL dependency.
 * NULL: disabled/invalid env, unreachable parent, unsupported ShapeInput, failure.
 * Attach a hidden, ordinary SDL X11 window before showing it or rendering. */
yg_embed_x11 *yg_embed_x11_attach(Display *dpy, Window game);
/* One bounded, NUL-terminated line, optionally prefixed "embed ". Volume is a
 * validated no-op: the host applies audio gain. Too-small rects hide the child. */
int yg_embed_x11_command(yg_embed_x11 *e, const char *line);
/* Feed every native X event, including events addressed to the parent. */
int yg_embed_x11_event(yg_embed_x11 *e, const XEvent *ev);
/* Restore a surviving window to a normal root child; NULL/destroy-safe. */
void yg_embed_x11_detach(yg_embed_x11 *e);
/* Returns 1 and the current pixel size, or 0 when absent/dead. Nullable outputs. */
int yg_embed_x11_size(const yg_embed_x11 *e, int *w, int *h);
/* Accepted rect/view intersection and explicit show/hide intent. */
int yg_embed_x11_visible(const yg_embed_x11 *e);

#ifdef __cplusplus
}
#endif
#endif
