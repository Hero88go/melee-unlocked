// A foreign SDL/OpenGL surface following the YouGame player's window in screen points.
// SPDX-License-Identifier: GPL-2.0-or-later
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#include <SDL.h>
#include "embed_mac.h"
#include "embed_mac_state.h"
#include "../runtime/hle/yougame_link.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>

// host::log reaches melee.log (--log-file); declared here to keep the Win32 compatibility headers out of AppKit.
namespace host { void log(const char* fmt, ...); }

@interface YGMeleeEmbedPanel : NSPanel
@end
@implementation YGMeleeEmbedPanel
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return (self.styleMask & NSWindowStyleMaskTitled) != 0; }
// The panel sits exactly over the player box, which in full screen reaches the top of the screen. AppKit keeps ordinary
// windows out of the menu bar's band and moved the panel down; track() set it back the next tick, and full screen
// flickered (d0020's real-app check, 10-26). The fallback's titled window keeps AppKit's rule.
- (NSRect)constrainFrameRect:(NSRect)frameRect toScreen:(NSScreen*)screen {
  return (self.styleMask & NSWindowStyleMaskTitled) ? [super constrainFrameRect:frameRect toScreen:screen] : frameRect;
}
@end

namespace mac_embed {
namespace {
Millis now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
struct Inbox {
  std::mutex mutex;
  std::deque<Command> commands;
  bool quit = false;
};
struct Surface {
  YGMeleeEmbedPanel* panel = nil;
  SDL_Window* sdl = nullptr;
  id activation = nil;
  Tracker tracker;
  SettleGate settle;
  Presentation presentation;
  std::shared_ptr<Inbox> inbox = std::make_shared<Inbox>();
  bool allowed_visible = false, embedded = true, ordered = false;
  bool wants_key = false;
  int width = 0, height = 0;
};
std::unique_ptr<Surface> surface;
std::optional<uint32_t> configured_parent;
std::atomic<SDL_Window*> frame_window{nullptr};

// Only IDs from these lists: never request window names, images or Screen Recording access.
bool contains(CFArrayRef list, CGWindowID id) {
  if (!list) return false;
  for (CFIndex i = 0; i < CFArrayGetCount(list); ++i)
    if (static_cast<CGWindowID>(reinterpret_cast<uintptr_t>(CFArrayGetValueAtIndex(list, i))) == id)
      return true;
  return false;
}
int number(CFDictionaryRef info, CFStringRef key) {
  int value = 0;
  auto n = static_cast<CFNumberRef>(CFDictionaryGetValue(info, key));
  if (n && CFGetTypeID(n) == CFNumberGetTypeID()) CFNumberGetValue(n, kCFNumberIntType, &value);
  return value;
}
Parent query_parent(CGWindowID id) {
  Parent parent;
  // OnScreenOnly excludes minimized windows and windows on other Spaces. Access only the
  // matching window's number, bounds, layer and owner PID; never kCGWindowName/owner names.
  if (CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly, kCGNullWindowID)) {
    for (CFIndex i = 0; i < CFArrayGetCount(list); ++i) {
      auto info = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(list, i));
      if (static_cast<CGWindowID>(number(info, kCGWindowNumber)) != id) continue;
      parent.exists = true;
      parent.owner = number(info, kCGWindowOwnerPID);
      CGRect bounds{};
      auto rect = static_cast<CFDictionaryRef>(CFDictionaryGetValue(info, kCGWindowBounds));
      if (rect && CGRectMakeWithDictionaryRepresentation(rect, &bounds)) {
        parent.bounds = {bounds.origin.x, bounds.origin.y, bounds.size.width, bounds.size.height};
        parent.on_screen = number(info, kCGWindowLayer) == 0 && bounds.size.width > 0 && bounds.size.height > 0;
      }
      break;
    }
    CFRelease(list);
  }
  if (!parent.exists) {
    // On recent macOS a minimized parent can disappear from IncludingWindow too. An ID-only
    // All list distinguishes hidden from closed, without pulling metadata for other windows.
    if (CFArrayRef all = CGWindowListCreate(kCGWindowListOptionAll, kCGNullWindowID)) {
      parent.exists = contains(all, id);
      CFRelease(all);
    }
  }
  return parent;
}
void hide(Surface& s) {
  if (s.ordered || s.panel.visible) [s.panel orderOut:nil];
  s.ordered = false;
}
void fallback(Surface& s) {
  hide(s);
  s.embedded = false;
  s.panel.styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                      NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
  s.panel.collectionBehavior = NSWindowCollectionBehaviorDefault;
  s.panel.hidesOnDeactivate = NO;
  s.panel.hasShadow = YES;
  [s.panel setContentSize:NSMakeSize(s.width, s.height)];
  [s.panel center];
  [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
  if (s.allowed_visible) {
    [s.panel makeKeyAndOrderFront:nil];
    [s.panel makeMainWindow];
  }
  host::log("embed: parent gone for 2 seconds; using a regular window");
}
void track(Surface& s) {
  if (!s.embedded) return;
  s.tracker.tick(now_ms(), [&] { return query_parent(*configured_parent); });
  // WindowServer may block: time inside the query is not observed stable time.
  const auto now = now_ms();
  if (s.tracker.fallback()) { fallback(s); return; }
  const Parent& parent = s.tracker.parent();
  bool show = s.allowed_visible && s.presentation.visible(parent);
  s.settle.observe(now, show, parent.bounds);
  const auto screens = NSScreen.screens;
  const double primary_height = screens.count ? screens[0].frame.size.height : 0;
  // The box is in the parent's backing pixels: convert with the app's scale when it sent one (0.13.1076+), else with
  // the scale of the display holding most of the parent (older apps). Never both: dividing twice halves the panel.
  double scale = s.presentation.app_scale;
  const char* source = "from app";
  if (!(scale > 0)) {
    source = "from display overlap";
    const Rect parent_cocoa{parent.bounds.x, primary_height - parent.bounds.y - parent.bounds.h, parent.bounds.w, parent.bounds.h};
    double most = 0;
    for (NSScreen* screen in screens) {
      const NSRect f = screen.frame;
      const double area = overlap_area(parent_cocoa, {f.origin.x, f.origin.y, f.size.width, f.size.height});
      if (area > most) { most = area; scale = screen.backingScaleFactor; }
    }
    if (!(scale > 0)) scale = NSScreen.mainScreen ? NSScreen.mainScreen.backingScaleFactor : 1;
  }
  static double logged_scale = 0;
  static const char* logged_source = nullptr;
  if (scale != logged_scale || source != logged_source) {
    logged_scale = scale; logged_source = source;
    host::log("embed: scale %.2f %s", scale, source);
  }
  const Rect frame = screen_rect(parent.bounds, points_from_backing(s.presentation.box, scale), primary_height);
  bool intersects_screen = false;
  for (NSScreen* screen in screens) {
    const NSRect bounds = screen.frame;
    if (intersects(frame, {bounds.origin.x, bounds.origin.y, bounds.size.width, bounds.size.height}))
      intersects_screen = true;
  }
  show = show && intersects_screen;
  if (show && !s.ordered && !s.settle.ready(now)) show = false;
  if (!show) { hide(s); return; }
  const NSRect wanted = NSMakeRect(frame.x, frame.y, frame.w, frame.h);
  // A frame AppKit would not keep is not set again every tick (that flickered): up to three more tries, a second apart,
  // in case the refusal was passing (a Space switch, a full-screen animation), then it waits for the next new frame.
  static NSRect refused = NSZeroRect;
  static int refused_tries = 0;
  static Millis refused_at = 0;
  const bool retry = NSEqualRects(wanted, refused) && refused_tries < 4 && now - refused_at >= 1000;
  if (!NSEqualRects(s.panel.frame, wanted) && (!NSEqualRects(wanted, refused) || retry)) {
    [s.panel setFrame:wanted display:YES animate:NO];
    const NSRect got = s.panel.frame;
    if (NSEqualRects(got, wanted)) {
      refused = NSZeroRect; refused_tries = 0;
    } else {
      if (!NSEqualRects(wanted, refused)) { refused = wanted; refused_tries = 0; }
      ++refused_tries; refused_at = now;
    }
    // Once per new frame, with where AppKit put it if not there.
    static Rect logged{};
    if (!equal(logged, frame)) {
      logged = frame;
      const Rect& box = s.presentation.box;
      const Rect& pb = parent.bounds;
      char elsewhere[96] = "";
      if (!NSEqualRects(got, wanted))
        std::snprintf(elsewhere, sizeof elsewhere, " (AppKit placed it at %.1f,%.1f %.1fx%.1f)", got.origin.x, got.origin.y,
                      got.size.width, got.size.height);
      host::log("embed: panel %.1f,%.1f %.1fx%.1f pt from box %.0f,%.0f %.0fx%.0f px at %.2fx in window %.0f,%.0f %.0fx%.0f "
                "(top-left) of a %.0f-pt-high main screen%s", frame.x, frame.y, frame.w, frame.h, box.x, box.y, box.w, box.h,
                scale, pb.x, pb.y, pb.w, pb.h, primary_height, elsewhere);
    }
  }
  bool below = !s.ordered || !s.panel.visible;
  // Consume the ordering timer even on the initial order-in, keeping all queries bounded.
  if (s.tracker.ordering_due(now) && !below) {
    if (CFArrayRef above = CGWindowListCreate(kCGWindowListOptionOnScreenAboveWindow,
                                             static_cast<CGWindowID>(s.panel.windowNumber))) {
      below = contains(above, *configured_parent);
      CFRelease(above);
    }
  }
  if (below) {
    // Relative ordering preserves other apps and the parent's menus above the game.
    [s.panel orderWindow:NSWindowAbove relativeTo:*configured_parent];
    s.ordered = true;
  }
  if (s.wants_key && s.presentation.focused) {
    [s.panel makeKeyWindow]; // NSNonactivatingPanel: keyboard without activating this process
    s.wants_key = false;
  }
}
} // namespace

bool configure(bool embedded) {
  configured_parent.reset();
  if (!embedded) return false;
  const char* parent = std::getenv("YOUGAME_EMBED_PARENT");
  if (parent) configured_parent = parent_id(parent);
  unsetenv("YOUGAME_EMBED_PARENT");
  if (!configured_parent) {
    std::fprintf(stderr, "embed: missing/invalid YOUGAME_EMBED_PARENT; using a regular SDL window\n");
    return false;
  }
  // SDL's video init must not turn this background game into the frontmost app.
  SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
  return true;
}
SDL_Window* create(int w, int h, const char* title, bool allowed_visible) {
  @autoreleasepool {
    NSCAssert(NSThread.isMainThread, @"Mac embed must be created on the main thread");
    if (!configured_parent) return nullptr;
    surface = std::make_unique<Surface>();
    auto& s = *surface;
    s.width = w; s.height = h; s.allowed_visible = allowed_visible;
    s.panel = [[YGMeleeEmbedPanel alloc] initWithContentRect:NSMakeRect(0, 0, w, h)
        styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
        backing:NSBackingStoreBuffered defer:NO];
    if (!s.panel) { surface.reset(); return nullptr; }
    s.panel.releasedWhenClosed = NO;
    s.panel.hidesOnDeactivate = NO;
    s.panel.floatingPanel = NO;
    s.panel.becomesKeyOnlyIfNeeded = NO;
    s.panel.opaque = YES;
    s.panel.backgroundColor = NSColor.blackColor;
    s.panel.hasShadow = NO;
    s.panel.animationBehavior = NSWindowAnimationBehaviorNone;
    s.panel.level = NSNormalWindowLevel;
    s.panel.collectionBehavior = NSWindowCollectionBehaviorMoveToActiveSpace |
        NSWindowCollectionBehaviorIgnoresCycle | NSWindowCollectionBehaviorTransient;
    s.panel.title = [NSString stringWithUTF8String:title];
    SDL_SetHint(SDL_HINT_VIDEO_FOREIGN_WINDOW_OPENGL, "1");
    s.sdl = SDL_CreateWindowFrom(s.panel);
    if (!s.sdl) { destroy(); return nullptr; }
    // SDL owns the event observer/view wrapper, we retain ownership of the native panel.
    // The panel's GL surface stays in points, as SDL_CreateWindowFrom left it: SDL 2.30 never marks a foreign window
    // high-DPI, so SDL_GL_GetDrawableSize, present()'s letterbox and ImGui's viewport all count in points. Asking for a
    // Retina surface here made it twice that size, and on a 2x display the picture drew half-size in its lower left in
    // theater and black in full screen (d0017/d0018, Luis 10-26). present() logs the window and surface sizes.
    std::weak_ptr<Inbox> inbox = s.inbox;
    yougame::set_box_handler([inbox](const std::string& line) {
      const auto command = parse_command(line);
      if (!command) return;
      // One log line per command from the app (a rect only when it changes), so a page/host disagreement about the
      // player box can be told apart (native-experience-lead #15940).
      static std::string last_rect;
      if (command->kind == Kind::Rect) {
        if (line != last_rect) { last_rect = line; host::log("embed: got %s", line.c_str()); }
      } else {
        host::log("embed: got %s", line.c_str());
      }
      if (auto queue = inbox.lock()) {
        std::lock_guard<std::mutex> lock(queue->mutex);
        if (command->kind == Kind::Quit) queue->quit = true;
        else {
          // One pending command per channel (rect, show/hide, focus/blur, scale): a rect flood must not discard hide/blur.
          const auto channel = [](Kind kind) {
            return kind == Kind::Rect ? 0 : kind == Kind::Show || kind == Kind::Hide ? 1 : kind == Kind::Scale ? 3 : 2;
          };
          const int target = channel(command->kind);
          auto& commands = queue->commands;
          commands.erase(std::remove_if(commands.begin(), commands.end(),
              [&](const Command& old) { return channel(old.kind) == target; }), commands.end());
          queue->commands.push_back(*command);
        }
      }
    });
    s.activation = [NSWorkspace.sharedWorkspace.notificationCenter
        addObserverForName:NSWorkspaceDidActivateApplicationNotification object:nil
        queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification* notification) {
          if (!surface || !surface->embedded) return;
          NSRunningApplication* app = notification.userInfo[NSWorkspaceApplicationKey];
          if (app.processIdentifier == surface->tracker.parent().owner) {
            surface->tracker.owner_activated();
            track(*surface);
          }
        }];
    // No startup sleeps on the main thread. pump() implements the same 20/50/100/200 ms
    // retry schedule, keeping the SDL event loop and the bridge responsive during startup.
    // set_box_handler just replayed what the app sent before this window existed ('scale' first on 0.13.1076+), so the
    // first placement applies it through pump() instead of a bare track(): never the display-overlap guess when the
    // app already said its scale (d0022 logged one startup 'from display overlap').
    pump();
    return s.sdl;
  }
}
bool active() { return surface && surface->embedded; }
bool pump() {
  @autoreleasepool {
    if (!surface) return false;
    NSCAssert(NSThread.isMainThread, @"Mac embed commands must run on the main thread");
    auto& s = *surface;
    std::deque<Command> commands;
    bool quit = false;
    {
      std::lock_guard<std::mutex> lock(s.inbox->mutex);
      commands.swap(s.inbox->commands);
      quit = s.inbox->quit;
    }
    if (quit) { hide(s); return true; }
    for (const auto& command : commands) {
      s.presentation.apply(command);
      s.tracker.refresh();
      if (command.kind == Kind::Focus) s.wants_key = true;
      if (command.kind == Kind::Blur) {
        s.wants_key = false;
        if (s.panel.keyWindow) [s.panel resignKeyWindow];
      }
    }
    if (frame_window.exchange(nullptr) == s.sdl) s.presentation.first_frame();
    track(s);
    return false;
  }
}
void frame_presented(SDL_Window* window) { frame_window.store(window); }
void destroy() {
  @autoreleasepool {
    if (!surface) return;
    yougame::set_box_handler({});
    if (surface->activation)
      [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:surface->activation];
    [surface->panel orderOut:nil];
    [surface->panel close];
    [surface->panel release];
    surface.reset();
    frame_window.store(nullptr);
  }
}
} // namespace mac_embed
