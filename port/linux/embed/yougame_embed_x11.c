#include "yougame_embed_x11.h"
#include "../../runtime/hle/yougame_embed_view.h"
#include <stdio.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct yg_embed_x11 {
    Display *dpy;
    Window game, parent, root;
    XWindowAttributes old;
    long parent_mask;
    int old_x, old_y, w, h, x, y;
    int valid, shown, mapped, focus, viewable, dead;
    Atom motif, motif_type;
    int motif_format;
    unsigned long motif_n;
    unsigned char *motif_data;
    XRectangle *shape;
    int shape_n, shape_order;
    XRectangle *bounding;
    int bounding_n, bounding_order;
    yg_embed_view_state layout;
};

/* Scoped, synchronous trap: unrelated requests retain the host's handler.
 * The process must not issue concurrent Xlib requests or replace the handler. */
static struct {
    Display *dpy;
    unsigned long first;
    int failed, gone;
    XErrorHandler previous;
} trap;

static int on_error(Display *dpy, XErrorEvent *ev)
{
    if (dpy != trap.dpy || ev->serial < trap.first)
        return trap.previous ? trap.previous(dpy, ev) : 0;
    trap.failed = 1;
    if (ev->error_code == BadWindow || ev->error_code == BadDrawable)
        trap.gone = 1;
    return 0;
}

static void begin(Display *dpy)
{
    trap.dpy = dpy;
    trap.first = NextRequest(dpy);
    trap.failed = trap.gone = 0;
    trap.previous = XSetErrorHandler(on_error);
}

static int end(Display *dpy)
{
    XSync(dpy, False);
    XSetErrorHandler(trap.previous);
    return trap.failed;
}

static int refresh(yg_embed_x11 *e)
{
    XWindowAttributes a;
    if (!XGetWindowAttributes(e->dpy, e->parent, &a)) {
        e->dead = 1;
        return 0;
    }
    e->viewable = a.map_state == IsViewable;
    return 1;
}

static void focus(yg_embed_x11 *e, int force)
{
    Window current;
    int revert;
    if (!e->focus || !e->mapped || !e->viewable) return;
    XGetInputFocus(e->dpy, &current, &revert);
    if (force || current == e->parent || current == e->game)
        XSetInputFocus(e->dpy, e->game, RevertToParent, CurrentTime);
}

static void visibility(yg_embed_x11 *e)
{
    int map = e->shown && e->valid;
    if (map && !e->mapped) XMapRaised(e->dpy, e->game);
    if (!map && e->mapped) XUnmapWindow(e->dpy, e->game);
    e->mapped = map;
}

yg_embed_x11 *yg_embed_x11_attach(Display *dpy, Window game)
{
    const char *enabled = getenv("YOUGAME_EMBED");
    const char *id = getenv("YOUGAME_EMBED_PARENT");
    char *tail;
    unsigned long parent, left;
    Window child;
    XWindowAttributes app, own;
    int major, minor, event, error, ok;
    yg_embed_x11 *e;
    if (!dpy || !game || !enabled || strcmp(enabled, "1") || !id || !*id)
        return NULL;
    for (tail = (char *)id; *tail; ++tail)
        if (!isdigit((unsigned char)*tail)) return NULL;
    errno = 0;
    parent = strtoul(id, &tail, 10);
    if (errno || *tail || !parent || parent > 0xffffffffUL || parent == game)
        return NULL;
    e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->dpy = dpy; e->game = game; e->parent = parent;
    begin(dpy);
    ok = XGetWindowAttributes(dpy, parent, &app) &&
         XGetWindowAttributes(dpy, game, &own) && app.root == own.root &&
         app.class == InputOutput && own.class == InputOutput && parent != app.root &&
         XShapeQueryExtension(dpy, &event, &error) &&
         XShapeQueryVersion(dpy, &major, &minor) && (major > 1 || (major == 1 && minor >= 1));
    if (end(dpy) || !ok) { free(e); return NULL; }
    e->old = own; e->root = own.root; e->parent_mask = app.your_event_mask;
    e->w = own.width; e->h = own.height; e->shown = 1;
    e->layout = yg_embed_view_init(own.width, own.height, 32);
    e->viewable = app.map_state == IsViewable;
    begin(dpy);
    e->motif = XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
    ok = XTranslateCoordinates(dpy, game, e->root, 0, 0, &e->old_x, &e->old_y, &child);
    ok = ok && XGetWindowProperty(dpy, game, e->motif, 0, 1024, False, AnyPropertyType,
         &e->motif_type, &e->motif_format, &e->motif_n, &left, &e->motif_data) == Success;
    e->shape = XShapeGetRectangles(dpy, game, ShapeInput, &e->shape_n, &e->shape_order);
    e->bounding = XShapeGetRectangles(dpy, game, ShapeBounding, &e->bounding_n, &e->bounding_order);
    if (end(dpy) || !ok || left || (!e->shape && e->shape_n != 0) ||
        (!e->bounding && e->bounding_n != 0)) {
        XFree(e->bounding); XFree(e->shape); XFree(e->motif_data); free(e); return NULL;
    }
    begin(dpy);
    XWithdrawWindow(dpy, game, XScreenNumberOfScreen(own.screen));
    XSelectInput(dpy, parent, app.your_event_mask | StructureNotifyMask | FocusChangeMask);
    XSelectInput(dpy, game, own.your_event_mask | StructureNotifyMask | FocusChangeMask);
    {
        unsigned long hints[5] = { 2, 0, 0, 0, 0 }; /* MWM_HINTS_DECORATIONS: none */
        XChangeProperty(dpy, game, e->motif, e->motif, 32, PropModeReplace,
                        (unsigned char *)hints, 5);
    }
    /* A non-root child is outside WM management; no override-redirect needed. */
    {
        XSetWindowAttributes a;
        a.override_redirect = False;
        XChangeWindowAttributes(dpy, game, CWOverrideRedirect, &a);
    }
    XSetWindowBorderWidth(dpy, game, 0);
    XSetWindowBackground(dpy, game, own.depth == 32 ? 0xff000000UL : BlackPixelOfScreen(own.screen));
    XClearWindow(dpy, game);
    XShapeCombineRectangles(dpy, game, ShapeInput, 0, 0, NULL, 0, ShapeSet, Unsorted);
    XReparentWindow(dpy, game, parent, 0, 0);
    if (end(dpy)) { yg_embed_x11_detach(e); return NULL; }
    return e;
}

static int number(const char *s, int *out)
{
    char *tail;
    long n;
    errno = 0;
    n = strtol(s, &tail, 10);
    if (errno || tail == s || *tail || n < -32768 || n > 32767) return 0;
    *out = (int)n;
    return 1;
}

int yg_embed_x11_command(yg_embed_x11 *e, const char *line)
{
    char buf[160], *args[6], *p;
    size_t len = 0;
    int n = 0, x = 0, y = 0, w = 0, h = 0, force = 0;
    if (!e || !line) return YG_EMBED_IGNORED;
    if (e->dead) return YG_EMBED_QUIT;
    while (len < sizeof(buf) && line[len]) ++len;
    if (len == sizeof(buf)) return YG_EMBED_IGNORED;
    memcpy(buf, line, len + 1);
    p = buf;
    while (*p) {
        while (isspace((unsigned char)*p)) ++p;
        if (!*p) break;
        if (n == 6) return YG_EMBED_IGNORED;
        args[n++] = p;
        while (*p && !isspace((unsigned char)*p)) ++p;
        if (*p) *p++ = '\0';
    }
    if (n && !strcmp(args[0], "embed")) {
        --n;
        memmove(args, args + 1, (size_t)n * sizeof(*args));
    }
    if (!n) return YG_EMBED_IGNORED;
    if (n == 1 && !strcmp(args[0], "quit")) return YG_EMBED_QUIT;
    if (n == 2 && !strcmp(args[0], "volume")) {
        int dots = 0, digits = 0, whole = 0, fraction = 0;
        for (p = args[1]; *p; ++p) {
            if (*p == '.' && !dots) ++dots;
            else if (*p >= '0' && *p <= '9') {
                ++digits;
                if (dots) fraction |= *p != '0';
                else if (whole < 2) whole = whole * 10 + (*p - '0');
            }
            else return YG_EMBED_IGNORED;
        }
        return digits && whole <= 1 && !(whole == 1 && fraction)
               ? YG_EMBED_OK : YG_EMBED_IGNORED;
    }
    if (!strcmp(args[0], "rect") || !strcmp(args[0], "view")) {
        if (n != 5 || !number(args[1], &x) || !number(args[2], &y) ||
            !number(args[3], &w) || !number(args[4], &h) || w > 16384 || h > 16384 ||
            (!strcmp(args[0], "view") && (x < 0 || y < 0 || w < 0 || h < 0)))
            return YG_EMBED_IGNORED;
    } else if (n != 1 || (strcmp(args[0], "show") && strcmp(args[0], "hide") &&
               strcmp(args[0], "focus") && strcmp(args[0], "blur")))
        return YG_EMBED_IGNORED;
    begin(e->dpy);
    if (refresh(e)) {
        if (!strcmp(args[0], "rect") || !strcmp(args[0], "view")) {
            const int is_view = !strcmp(args[0], "view");
            const yg_embed_box box = {x, y, w, h};
            yg_embed_view_result result;
            /* Preserve the legacy actual-size comparison without a view. Once
             * a view owns layout, late ConfigureNotify events must not turn a
             * rect-only clip into another resize request. */
            if (!e->layout.has_view) {
                e->layout.window.w = e->w; e->layout.window.h = e->h;
            }
            result = yg_embed_view_update(&e->layout, box, is_view);
            e->valid = result.valid;
            if (is_view) fprintf(stderr, "embed: view %d %d %d %d\n", x, y, w, h);
            if (result.warn_clamp) fprintf(stderr, "embed: rect outside view; clamped to view\n");
            if (e->layout.has_view) {
                XRectangle clip;
                clip.x = (short)result.clip.x; clip.y = (short)result.clip.y;
                clip.width = (unsigned short)result.clip.w;
                clip.height = (unsigned short)result.clip.h;
                XShapeCombineRectangles(e->dpy, e->game, ShapeBounding, 0, 0,
                                        &clip, 1, ShapeSet, Unsorted);
                if (!result.resize)
                    fprintf(stderr, "embed: clip %d %d %d %d (no resize)\n",
                            result.clip.x, result.clip.y, result.clip.w, result.clip.h);
            }
            if (result.geometry) {
                const yg_embed_box window = result.window;
                if (result.resize) XResizeWindow(e->dpy, e->game, window.w, window.h);
                if (window.x != e->x || window.y != e->y)
                    XMoveWindow(e->dpy, e->game, window.x, window.y);
                e->x = window.x; e->y = window.y; e->w = window.w; e->h = window.h;
                XRaiseWindow(e->dpy, e->game);
            }
        } else if (!strcmp(args[0], "show")) {
            e->shown = 1; yg_embed_view_show(&e->layout, 1);
        } else if (!strcmp(args[0], "hide")) {
            e->shown = 0; yg_embed_view_show(&e->layout, 0);
        }
        else if (!strcmp(args[0], "focus")) { e->focus = 1; force = 1; }
        else if (!strcmp(args[0], "blur")) {
            e->focus = 0;
            if (e->viewable) XSetInputFocus(e->dpy, e->parent, RevertToParent, CurrentTime);
        }
        visibility(e);
        focus(e, force);
    }
    end(e->dpy);
    if (trap.gone) e->dead = 1;
    return e->dead ? YG_EMBED_QUIT : YG_EMBED_OK;
}

int yg_embed_x11_event(yg_embed_x11 *e, const XEvent *ev)
{
    if (!e || !ev || ev->xany.display != e->dpy) return YG_EMBED_IGNORED;
    if (e->dead) return YG_EMBED_QUIT;
    /* Synthetic events cannot establish destruction, mapping or geometry. */
    if (ev->xany.send_event) return YG_EMBED_IGNORED;
    if (ev->type == DestroyNotify &&
        (ev->xdestroywindow.window == e->parent || ev->xdestroywindow.window == e->game)) {
        e->dead = 1;
        return YG_EMBED_QUIT;
    }
    if (ev->type == ConfigureNotify && ev->xconfigure.window == e->game) {
        if (ev->xconfigure.width != e->w || ev->xconfigure.height != e->h) {
            e->w = ev->xconfigure.width; e->h = ev->xconfigure.height;
        }
        return YG_EMBED_OK;
    }
    if (ev->xany.window != e->parent || (ev->type != MapNotify &&
        ev->type != UnmapNotify && ev->type != FocusIn && ev->type != FocusOut))
        return YG_EMBED_IGNORED;
    begin(e->dpy);
    if (refresh(e)) {
        if (e->mapped) XRaiseWindow(e->dpy, e->game);
        /* Never pull keyboard focus back from another application. */
        if (ev->type != FocusOut) focus(e, 0);
    }
    end(e->dpy);
    if (trap.gone) e->dead = 1;
    return e->dead ? YG_EMBED_QUIT : YG_EMBED_OK;
}

void yg_embed_x11_detach(yg_embed_x11 *e)
{
    XWindowAttributes a;
    if (!e) return;
    begin(e->dpy);
    /* Query even after death: safe if a failed attach left the game alive. */
    if (XGetWindowAttributes(e->dpy, e->game, &a)) {
        XSetWindowAttributes restore;
        XUnmapWindow(e->dpy, e->game);
        XReparentWindow(e->dpy, e->game, e->root,
                        e->old_x - e->old.border_width, e->old_y - e->old.border_width);
        restore.override_redirect = e->old.override_redirect;
        XChangeWindowAttributes(e->dpy, e->game, CWOverrideRedirect, &restore);
        XSetWindowBorderWidth(e->dpy, e->game, e->old.border_width);
        XResizeWindow(e->dpy, e->game, e->old.width, e->old.height);
        XSelectInput(e->dpy, e->game, e->old.your_event_mask);
        if (e->shape_n == 1 && e->shape[0].x == 0 && e->shape[0].y == 0 &&
            e->shape[0].width == e->old.width && e->shape[0].height == e->old.height)
            XShapeCombineMask(e->dpy, e->game, ShapeInput, 0, 0, None, ShapeSet);
        else XShapeCombineRectangles(e->dpy, e->game, ShapeInput, 0, 0, e->shape,
                                     e->shape_n, ShapeSet, e->shape_order);
        if (e->layout.has_view) {
            if (e->bounding_n == 1 && e->bounding[0].x == 0 && e->bounding[0].y == 0 &&
                e->bounding[0].width == e->old.width && e->bounding[0].height == e->old.height)
                XShapeCombineMask(e->dpy, e->game, ShapeBounding, 0, 0, None, ShapeSet);
            else XShapeCombineRectangles(e->dpy, e->game, ShapeBounding, 0, 0, e->bounding,
                                         e->bounding_n, ShapeSet, e->bounding_order);
        }
        if (e->motif_type != None)
            XChangeProperty(e->dpy, e->game, e->motif, e->motif_type, e->motif_format,
                            PropModeReplace, e->motif_data, (int)e->motif_n);
        else XDeleteProperty(e->dpy, e->game, e->motif);
        if (e->old.map_state != IsUnmapped) XMapWindow(e->dpy, e->game);
    }
    if (XGetWindowAttributes(e->dpy, e->parent, &a))
        XSelectInput(e->dpy, e->parent, e->parent_mask);
    end(e->dpy);
    XFree(e->bounding); XFree(e->shape); XFree(e->motif_data); free(e);
}

int yg_embed_x11_size(const yg_embed_x11 *e, int *w, int *h)
{
    if (!e || e->dead) return 0;
    if (w) *w = e->w;
    if (h) *h = e->h;
    return 1;
}

int yg_embed_x11_visible(const yg_embed_x11 *e)
{
    return e && !e->dead && e->shown && e->valid;
}
