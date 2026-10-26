// Linux renderer factory: the OpenGL GX renderer (port/linux/renderer_gl.cpp) with the settings
// panel drawn over each presented frame, as the Windows renderers do.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gx_backend.h"
#include "pc_settings.h"
#include "pc_settings_shared.h"
#include "host.h"
#include "window.h"
#include "renderer_gl.h"
#include "exi_slippi.h"
#include "gx_shader.h"
#include <SDL.h>
#include <algorithm>
#include <memory>

namespace gx {

// The GL renderer reads its options through a pointer, so they live beside it for its lifetime.
namespace {
struct LinuxPresentation {
  RenderOptions options;
  std::unique_ptr<PcSettingsUIGL> settings;
};
std::unique_ptr<LinuxPresentation> g_presentation;

// Dolphin's "Auto (Window Size)": the integer EFB scale that covers the window, as the Windows
// renderers pick it; a fixed choice from the settings is used as is.
int linux_scale(const RenderOptions& options, int client_h) {
  // Supersampling renders at twice the chosen scale and the present pass averages it down, as the
  // Windows renderers do (up to the 6x the GL renderer supports).
  const int ssaa = std::clamp(options.ssaa, 1, 2);
  if (options.efb_scale > 0) return std::clamp(options.efb_scale * ssaa, 1, 6);
  return std::clamp((client_h + EFB_HEIGHT - 1) / EFB_HEIGHT * ssaa, 1, 6);
}
}  // namespace

bool d3d11_available() { return false; }

Backend* create_render_backend(void* hwnd, int client_w, int client_h, const RenderOptions& options) {
  auto* window = static_cast<SDL_Window*>(hwnd);
  g_presentation = std::make_unique<LinuxPresentation>();
  g_presentation->options = options;
  RenderOptions& opts = g_presentation->options;
  const int scale = linux_scale(opts, client_h);
  std::unique_ptr<Backend> renderer;
  try {
    renderer = linux_renderer::create(window, opts.shader_cache.empty() ? "shadercache" : opts.shader_cache, scale, opts.vsync);
  } catch (const std::exception& e) {
    host::die("OpenGL renderer: %s", e.what());
  }
  host::log("opengl: internal resolution EFB x%d (window %dx%d)", scale, client_w, client_h);
  if (opts.pc_settings) g_presentation->settings = std::make_unique<PcSettingsUIGL>(hwnd, opts);
  linux_renderer::Hooks hooks;
  hooks.options = &opts;
  hooks.frame_begin = [](const Frame& frame) {
    auto& p = *g_presentation;
    // As the Windows renderers: the Slippi widescreen code, Fountain of Dreams reflections and
    // True widescreen follow the settings every frame (they are game changes, not drawing).
    static bool widescreen_sent = false, fod_sent = false;
    if (p.options.widescreen != widescreen_sent) { widescreen_sent = p.options.widescreen; slippi::request_widescreen(widescreen_sent); }
    if (p.options.fod_reflections != fod_sent) { fod_sent = p.options.fod_reflections; slippi::request_fod_reflections(fod_sent); }
    {
      int cw = 0, ch = 0;
      host::window_client_size(&cw, &ch);
      set_true_widescreen(p.options.true_widescreen && !p.options.widescreen, true_widescreen_aspect(p.options, cw, ch));
    }
    if (!p.settings) return;
    int w = 0, h = 0;
    host::window_client_size(&w, &h);
    settings_set_game_aspect(presented_aspect(p.options, w, h, false));
    settings_set_hud_snapshot(frame);
    if (p.settings->begin(p.options)) {
      host::window_set_fullscreen(p.options.fullscreen);
      SDL_GL_SetSwapInterval(p.options.vsync ? 1 : 0);
    }
  };
  hooks.before_swap = [](int, int) {
    auto& p = *g_presentation;
    if (p.settings) p.settings->draw();
  };
  linux_renderer::set_hooks(*renderer, std::move(hooks));
  return renderer.release();
}

}  // namespace gx
