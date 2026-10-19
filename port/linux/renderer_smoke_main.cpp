// Standalone shader/raster integration check; does not need game data.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "renderer_gl.h"
#include <SDL.h>
#include <cstdio>
#include <exception>
int main(int argc, char** argv) {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_Window* window = SDL_CreateWindow("GX Linux integration check", 0, 0, 640, 528, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
  if (!window) { std::fprintf(stderr, "SDL window: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
  int result = 0;
  try {
    auto backend = linux_renderer::create(window, argc > 2 ? argv[2] : ".gx-smoke-cache", 1, false);
    if (!linux_renderer::smoke_test(*backend, argc > 1 ? argv[1] : "gx-smoke.ppm")) result = 1;
  } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); result = 1; }
  SDL_DestroyWindow(window); SDL_Quit(); return result;
}
