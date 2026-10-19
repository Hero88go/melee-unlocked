// Native Linux GX presentation. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include "gx_core.h"
#include "render_options.h"

struct SDL_Window;
namespace linux_renderer {
// The caller creates an SDL_WINDOW_OPENGL window after requesting a 4.5 core context.
// GX FIFO decoding and presentation run on the thread that calls create().
std::unique_ptr<gx::Backend> create(SDL_Window* window, const std::filesystem::path& shader_cache,
                                    int scale = 1, bool vsync = true);
// Exercises the actual generated GX shader translator and render path without a disc image.
// The result is a PPM from the OpenGL EFB, suitable for a headless Mesa/Xvfb build check.
bool smoke_test(gx::Backend& backend, const std::filesystem::path& capture);
// What the full Source Port host attaches: its options (aspect, filtering), the settings panel at the
// start of each frame and its overlay just before the buffers swap.
struct Hooks {
  gx::RenderOptions* options = nullptr;
  std::function<void(const gx::Frame&)> frame_begin;
  std::function<void(int, int)> before_swap;
};
void set_hooks(gx::Backend& backend, Hooks hooks);
void capture_efb(gx::Backend& backend, const std::filesystem::path& path);
// Development comparison of the same retained simulation frame, with both
// winding conventions. Output names append .cw.ppm and .ccw.ppm to prefix.
void capture_culling_variants(gx::Backend& backend, const gx::Frame& frame, const std::filesystem::path& prefix);
} // namespace linux_renderer
