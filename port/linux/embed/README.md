# YouGame native X11 embedding

`yougame_embed_x11.h` + `yougame_embed_x11.c` reparent a native game's existing
X11 window into the desktop app's player box. C99; Xlib and libXext only. The
first intended host is Melee Unlocked's SDL2 Linux build. This implements the
Orca child-window approach, with keyboard focus deliberately given to the game.

The child has a black background, no border or decorations, and an empty XShape
INPUT region: pointer events hit the app underneath. Keys still reach the game
when explicitly focused. A child is outside the window manager's top-level
management, so the helper withdraws the old top-level and reparents it; it sets
override-redirect to false rather than relying on it. The existing title is
retained as metadata and has no separate title bar while embedded.

## Wire contract

The app supplies `YOUGAME_EMBED=1`, `YOUGAME_EMBED_PARENT=<decimal X11 Window>`,
and access to that parent's X display through `DISPLAY`. X11 or XWayland is
required for both processes. A native Wayland SDL window cannot be attached.
Missing/invalid environment, an unreachable parent or unavailable Shape 1.1
returns NULL; the host shows its ordinary standalone window instead.

The host's existing stdin reader forwards complete lines, optionally including
the `embed ` prefix. The helper never reads stdin or consumes X events itself.

| Line | Effect |
| --- | --- |
| `embed rect x y w h` | Move/resize in parent-relative X11 pixels; valid rect initially maps the child. |
| `embed show` / `embed hide` | Show/hide intent; rect updates preserve an explicit hide. |
| `embed focus` | Give the visible child X keyboard focus; retain the request if hidden. |
| `embed blur` | Return keyboard focus to the parent when viewable. |
| `embed quit` | Return `YG_EMBED_QUIT`; host exits cleanly. |
| `embed volume 0..1` | Validate and return OK; host applies the actual audio gain. |

On Linux the origin is the Electron X11 window's inside top-left, **not** screen
coordinates or the web page's top-left. The page already converts CSS pixels
using devicePixelRatio, and `native.ts` calls `frameRectLine` to add the Linux
menu-bar offset to y. Apply neither a second scale nor another title/menu offset.
Sources: `desktop/src/native.ts:1083-1088,1418-1420`,
`desktop/src/orca-policy.ts:1280-1283`, `desktop/src/main.ts:675-692`,
`web/src/lib/desktop.ts:115`.

Both dimensions must be at least 32 to display. Smaller dimensions, including
zero/negative, hide without resizing; `show` cannot override that. A subsequent
valid rect restores the requested visibility. Coordinates are signed 16-bit;
dimensions are capped at 16384. Overflow, malformed numbers, extra tokens,
unknown commands and lines longer than 159 bytes return `YG_EMBED_IGNORED`
without changing state. Volume accepts decimal notation only. Zero-size layouts
never reach the renderer. Sizes reported while hidden retain the last real size.

## SDL2 host integration

1. Build this C file with the host; link `-lX11 -lXext`. In embedded launches,
   select SDL's `x11` video backend **before SDL_Init** (e.g. SDL_SetHint with
   SDL_HINT_VIDEODRIVER where supported, or the launch environment). Use the same
   DISPLAY as the app. Do not switch a running SDL video backend.
2. Create the window with `SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL` immediately
   before attaching. Avoid fullscreen, mouse capture, relative mouse mode and
   keyboard grabs in embedded mode. A 24-bit opaque visual is preferred; for an
   ARGB visual the helper sets an opaque black background, but the renderer must
   also write alpha 1 (or disable framebuffer alpha) to keep rendered frames opaque.
3. Immediately after `SDL_CreateWindow`, get `SDL_SysWMinfo` and attach, before
   showing the window/creating the GL context. On NULL, call SDL_ShowWindow and
   keep the normal standalone path. With an attached helper, let it own window
   mapping, placement and focus; do not call SDL_ShowWindow/HideWindow/SetWindowSize
   or SDL_SetWindowFullscreen on that path.

```c
#include <SDL.h>
#include <SDL_syswm.h>
#include "yougame_embed_x11.h"

/* window was just created hidden; all this runs on the video/Display thread. */
SDL_SysWMinfo wm;
SDL_VERSION(&wm.version);
yg_embed_x11 *embed = NULL;
if (SDL_GetWindowWMInfo(window, &wm) && wm.subsystem == SDL_SYSWM_X11)
    embed = yg_embed_x11_attach(wm.info.x11.display, wm.info.x11.window);
if (embed) SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
else SDL_ShowWindow(window);
/* Now create/make current the host's GL context as usual. */
```

4. Queue complete stdin lines from your reader onto the video thread; call
   `yg_embed_x11_command(embed, line)` there. Do not call Xlib from the reader
   thread. Bound your line accumulator and discard a whole overlong line, not
   separate truncated commands. Handle volume in the host's existing audio
   command path, using a strict finite 0..1 parser; the helper has no audio API.
   Treat stdin EOF as quit for an embedded run. Continue routing other stdio
   bridge commands through the host's existing handler.
5. In the SDL_PollEvent loop, forward **every** SDL_SYSWMEVENT for X11 immediately,
   including the foreign parent's events. Do not filter by the SDL game window
   id or retain the SDL_SysWMmsg pointer after processing the event. Leave SDL in
   charge of reading its Display; do not add a competing XNextEvent loop.

```c
int running = 1;
SDL_Event ev;
while (running && SDL_PollEvent(&ev)) {
    if (embed && ev.type == SDL_SYSWMEVENT && ev.syswm.msg &&
        ev.syswm.msg->subsystem == SDL_SYSWM_X11 &&
        yg_embed_x11_event(embed, &ev.syswm.msg->msg.x11.event) == YG_EMBED_QUIT)
        running = 0;
    if (ev.type == SDL_QUIT) running = 0;
    if (embed && ev.type == SDL_KEYDOWN && !ev.key.repeat &&
        ev.key.keysym.sym == SDLK_ESCAPE) {
        fputs("yg {\"key\":\"escape\"}\n", stdout);
        fflush(stdout);
    }
    /* Existing host input handling follows; stop rendering if running == 0. */
}
/* Also set running=0 when a queued stdin command returns YG_EMBED_QUIT. */
```

SDL2 forwards SysWM events before looking up its own window, so parent events
can use this route ([SDL2 X11 event source](https://github.com/libsdl-org/SDL/blob/SDL2/src/video/x11/SDL_x11events.c#L814-L862)).
Confirm this with the SDL version linked into the Linux host.

6. After draining stdin commands and SDL events, query the size. With the GL
   context current, change the viewport/render targets only when dimensions
   change. A move/restack ConfigureNotify must not recreate a swapchain.

```c
int w, h; /* viewport_w / viewport_h are host-owned cached dimensions. */
if (running && yg_embed_x11_size(embed, &w, &h) &&
    (w != viewport_w || h != viewport_h)) {
    glViewport(0, 0, w, h); /* Or resize the host's Vulkan surface/render targets. */
    viewport_w = w;
    viewport_h = h;
}
```

7. On QUIT (command, parent/child destruction, or detected vanished window), stop
   input/render submission immediately, save and shut down through the host's
   normal exit path. Call detach before destroying the SDL window/Display, and
   discard the helper pointer. Parent destruction has already destroyed the X11
   child; do not render another frame or try to show it as a standalone window.
   Verify SDL/GL teardown on that path on the target build. For a deliberate live
   detach, detach restores root parenting, original geometry, border, event
   masks, input shape, Motif hints and original mapped state; call SDL_ShowWindow
   afterward if the original window was hidden. The background remains black.

All APIs are confined to the Display-owning thread. Serialize **all process-wide
Xlib activity**, including SDL/GL calls on other threads, during helper calls:
XSetErrorHandler is process-global. Traps cover helper-issued requests and sync
before restoring the previous handler; they cannot guard arbitrary later SDL
requests or a disconnected X server. Do not call from an SDL event filter while
SDL is itself replacing the X error handler. One helper per game/parent per
Display; detach restores the event selections saved at attach.

## Demo and build

On Linux with X11 development headers/libraries already installed:

```sh
make
./demo
```

The Xlib-only demo creates its own fake app parent and child on DISPLAY. It prints
PASS/FAIL for attachment, shape, rect/show/focus/blur/hide, validation, too-small
handling, parent destruction/QUIT, safe detach and stale-parent fallback. It
exits nonzero on failure. Run on an agent test display; it changes keyboard focus
to its own windows briefly. No SDL or desktop app is needed for this smoke test.

For XQuartz headers under `/opt/X11/include`, compile-only check:

```sh
cc -std=c99 -Wall -Wextra -Werror -I/opt/X11/include -c yougame_embed_x11.c demo.c
```

On this Mac the equivalent headers/libraries are in `/opt/homebrew`:

```sh
make CPPFLAGS=-I/opt/homebrew/include LDFLAGS=-L/opt/homebrew/lib
```
