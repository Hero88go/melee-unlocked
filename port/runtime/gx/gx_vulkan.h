// Vulkan renderer for the shared GX command decoder, one backend for Windows and Linux.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gx_core.h"
#include "render_options.h"
#include <functional>
#include <string>
#include <vector>

namespace gx {

// The window the renderer draws into. Windows: an HWND (window) and its HINSTANCE (display).
// Linux: an SDL_Window* (window); the surface comes from SDL_Vulkan_CreateSurface.
struct VulkanWindow {
  void* window = nullptr;
  void* display = nullptr;
  // Current drawable size; called whenever the swapchain is (re)built.
  std::function<void(int*, int*)> drawable_size;
  // Linux: SDL's surface creation and instance extensions (the backend links no SDL itself).
  std::function<bool(void* instance, void* surface_out)> create_surface;
  std::function<std::vector<const char*>()> instance_extensions;
};

// Whether a Vulkan 1.3 device that can present is on this machine (the loader is opened and an
// instance created once). The settings panel offers Vulkan only when this is true.
bool vulkan_available();

// Vulkan starts in a row that died before the renderer was up (a crash inside the driver leaves the
// start marker behind; the cleared shader cache is tried once). Picking Vulkan again forgets them.
int vulkan_failed_starts();
void vulkan_forget_failed_starts();

// nullptr (with the reason logged) when Vulkan cannot start; the caller falls back.
Backend* create_vulkan_backend(const VulkanWindow& window, int client_w, int client_h, const RenderOptions& options);

}  // namespace gx
