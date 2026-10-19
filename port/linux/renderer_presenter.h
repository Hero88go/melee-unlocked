// Independent Linux presentation using the same authored animation solver as Windows.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "renderer_gl.h"
#include <cstdint>
namespace linux_renderer {
struct PresenterStats {
  uint64_t simulation = 0, presentation = 0, subframes = 0, posed = 0, drained = 0;
};
// The SDL window belongs to the caller and must outlive the backend. OpenGL
// creation, rendering and destruction all happen on the presentation thread.
// fps <= 60 disables re-posing; simulation always remains at the console rate.
std::unique_ptr<gx::Backend> create_threaded(SDL_Window* window, const std::filesystem::path& cache,
                                           int scale = 1, double fps = 120, bool vsync = false);
PresenterStats presenter_stats(const gx::Backend& backend);
// Returns false for synchronous backends. Captures queued frames first, then
// performs readback on the OpenGL owner thread and waits for the file to finish.
bool capture_threaded(gx::Backend& backend, const std::filesystem::path& path);
// Disc-free test of the real queue, authored-track sampling and independent clock.
bool presenter_self_test(SDL_Window* window, const std::filesystem::path& cache);
} // namespace linux_renderer
