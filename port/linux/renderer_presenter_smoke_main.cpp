// Standalone integration driver for the independent authored presenter.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "renderer_presenter.h"
#include <SDL.h>
#include <cstdio>
#include <exception>
int main(int argc, char** argv) {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_Window* window = SDL_CreateWindow("Linux authored presenter check", 0, 0, 640, 528, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
  int result = 0;
  try {
    if (!window) throw std::runtime_error(SDL_GetError());
    linux_renderer::presenter_self_test(window, argc > 1 ? argv[1] : "shadercache/linux");
  } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); result = 1; }
  if (window) SDL_DestroyWindow(window); SDL_Quit(); return result;
}
