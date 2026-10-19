// Linux: the PC settings panel on its own, with no game behind it (the launcher's Settings button),
// on an SDL window with an OpenGL context. The panel body is the same settings_frame the game runs.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "render_options.h"
#include "pc_settings.h"
#include "window.h"
#include "host.h"
#include <SDL.h>
#include <GL/glew.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace app {

namespace {
constexpr int kBaseWidth = 620, kBaseHeight = 700;

// MELEE_TEST_SETTINGS_SHOT=<file.bmp> (tests): a few frames hidden, the last one written, then out.
void save_test_shot(const std::string& path, int w, int h) {
  std::vector<uint8_t> rgba((size_t)w * h * 4);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return;
  const uint32_t row = (uint32_t)w * 4, size = row * (uint32_t)h;
  uint8_t header[54] = {'B', 'M'};
  auto put32 = [&](int at, uint32_t v) { for (int i = 0; i < 4; ++i) header[at + i] = (uint8_t)(v >> (8 * i)); };
  put32(2, 54 + size); put32(10, 54); put32(14, 40); put32(18, (uint32_t)w); put32(22, (uint32_t)h);
  header[26] = 1; header[28] = 32; put32(34, size);
  std::fwrite(header, 1, sizeof header, f);
  for (int y = 0; y < h; ++y)   // BMP rows run bottom to top, as glReadPixels returns them
    for (int x = 0; x < w; ++x) {
      const uint8_t* p = &rgba[((size_t)y * w + x) * 4];
      const uint8_t bgra[4] = {p[2], p[1], p[0], 255};
      std::fwrite(bgra, 1, 4, f);
    }
  std::fclose(f);
  host::log("settings: test shot %dx%d written to %s", w, h, path.c_str());
}
}  // namespace

int run_settings_window(gx::RenderOptions& options, unsigned long standby_parent, std::function<void()> reload) {
  (void)standby_parent;   // the Linux launcher starts the window when Settings is pressed
  (void)reload;
  const char* shot_env = std::getenv("MELEE_TEST_SETTINGS_SHOT");
  const std::string shot = shot_env ? shot_env : "";
  gx::settings_fill_window(true);
  void* window = host::window_create(kBaseWidth, kBaseHeight, L"Melee Unlocked settings", shot.empty());
  if (!window) { host::log("settings: cannot create a window"); return 1; }
  SDL_Window* sdl = static_cast<SDL_Window*>(window);
  SDL_GLContext context = SDL_GL_CreateContext(sdl);
  if (!context) { host::log("settings: no OpenGL context (%s)", SDL_GetError()); return 1; }
  glewExperimental = GL_TRUE;
  glewInit();
  glGetError();
  SDL_GL_SetSwapInterval(1);
  // A scaled desktop: the drawable is larger than the window in points; the panel follows it.
  {
    int dw = kBaseWidth, dh = kBaseHeight;
    SDL_GL_GetDrawableSize(sdl, &dw, &dh);
    if (dw > kBaseWidth + 8) gx::settings_set_window_scale((float)dw / kBaseWidth);
  }
  options.pc_settings = true;
  int frames = 0;
  {
    gx::PcSettingsUIGL ui(window, options);
    while (!host::window_closed()) {
      host::window_pump();
      host::PadState pads[4];
      host::input_poll(pads);   // the controller display and the port list follow live input here too
      int w = 0, h = 0;
      SDL_GL_GetDrawableSize(sdl, &w, &h);
      ui.begin(options);
      glViewport(0, 0, std::max(w, 1), std::max(h, 1));
      glClearColor(0.06f, 0.08f, 0.12f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      ui.draw();
      if (!shot.empty() && ++frames == 8) { save_test_shot(shot, w, h); break; }
      SDL_GL_SwapWindow(sdl);
      if (!(SDL_GetWindowFlags(sdl) & SDL_WINDOW_INPUT_FOCUS)) std::this_thread::sleep_for(std::chrono::milliseconds(120));
      if (gx::settings_close_requested()) break;
    }
  }
  SDL_GL_DeleteContext(context);
  return 0;
}

}  // namespace app
