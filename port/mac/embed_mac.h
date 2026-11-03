// Mac full-host window adapter. No AppKit types leak into the shared host.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
struct SDL_Window;
namespace mac_embed {
// Before SDL video initialization. Enabled only for embedded mode and a valid CGWindowID.
bool configure(bool embedded);
SDL_Window* create(int w, int h, const char* title, bool allowed_visible);
bool active(); // false after parent-gone fallback; the SDL/native surface remains the same
bool pump();   // main thread only; returns true for "embed quit"
void frame_presented(SDL_Window* window); // flag only; all AppKit work stays in pump()
void destroy(); // after SDL_DestroyWindow, on the main thread
} // namespace mac_embed
