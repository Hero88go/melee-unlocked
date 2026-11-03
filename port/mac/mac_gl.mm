// macOS GL surface upkeep for the presenter (port/linux/renderer_gl.cpp present).
// A window that grows (the green button's full screen, a drag, a display change) keeps its old GL
// surface until the context is told to update; the last picture then sits small in the middle of
// the bigger window. SDL's context marks itself for an update here and applies it at the next swap.
#import <AppKit/AppKit.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
extern "C" void mac_gl_update_context() {
  @autoreleasepool {
    [[NSOpenGLContext currentContext] update];
  }
}
// The current context's GL surface in pixels, as AppKit sizes it: the view's backing size when the view asked for a
// Retina surface, its size in points when not. false without a context or view.
extern "C" bool mac_gl_surface_size(int* w, int* h) {
  @autoreleasepool {
    NSView* view = [NSOpenGLContext currentContext].view;
    if (!view) return false;
    const NSRect bounds = view.bounds;
    const NSRect surface = view.wantsBestResolutionOpenGLSurface ? [view convertRectToBacking:bounds] : bounds;
    *w = int(surface.size.width); *h = int(surface.size.height);
    return true;
  }
}
#pragma clang diagnostic pop

// The green button's own toggle (AppKit), with SDL told nothing first: Escape uses it to leave a full screen the
// player entered with the green button; MELEE_TEST_TOGGLE in window_sdl.inl uses it to test that path.
extern "C" bool mac_window_is_fullscreen(void* nswindow) {
  return ([(__bridge NSWindow*)nswindow styleMask] & NSWindowStyleMaskFullScreen) != 0;
}
extern "C" void mac_toggle_fullscreen(void* nswindow) {
  @autoreleasepool {
    [(__bridge NSWindow*)nswindow toggleFullScreen:nil];
  }
}
