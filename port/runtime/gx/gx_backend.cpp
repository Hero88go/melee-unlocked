// Render backend selection. Keeping this out of gx_d3d12.cpp leaves that file (and with it the
// shader cache identity, see port/CMakeLists.txt) untouched by the D3D11 work.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gx_backend.h"
#include "gx_d3d11.h"
#include "host.h"
#include <d3d11.h>
#include <mutex>
#include <unordered_set>

namespace gx {
namespace {

// Which live backends are D3D11 ones. One backend exists at a time in the game, but the GPU
// resource test creates several in sequence, so this is a set rather than a single pointer.
std::mutex g_mutex;
std::unordered_set<Backend*> g_d3d11_backends;

bool is_d3d11(Backend* backend) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_d3d11_backends.count(backend) != 0;
}

}  // namespace

// A device with no swapchain, created and dropped once. Everything that decides whether the D3D11
// backend can run at all (driver, feature level 11_0) is decided here, so the settings panel can
// offer the backend only when it would really start.
bool d3d11_available() {
  static const bool available = [] {
    const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    ID3D11Device* device = nullptr;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, want, 2,
                                   D3D11_SDK_VERSION, &device, nullptr, nullptr);
    if (device) device->Release();
    if (FAILED(hr)) host::log("d3d11: no hardware device on this machine (0x%08X)", (unsigned)hr);
    return SUCCEEDED(hr);
  }();
  return available;
}

Backend* create_render_backend(void* hwnd, int client_w, int client_h, const D3D12Options& options) {
  if (options.api == RenderApi::D3D11) {
    D3D12Options d3d11_options = options;
    if (d3d11_options.dlss_mode) host::log("d3d11: DLSS is a Direct3D 12 feature; rendering natively");
    d3d11_options.dlss_mode = 0;
    if (Backend* backend = create_d3d11_backend(hwnd, client_w, client_h, d3d11_options)) {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_d3d11_backends.insert(backend);
      return backend;
    }
    host::log("d3d11: falling back to Direct3D 12");
  }
  return create_d3d12_backend(hwnd, client_w, client_h, options);
}

const D3D12Options& render_options(Backend* backend) {
  return is_d3d11(backend) ? d3d11_options(backend) : d3d12_options(backend);
}
void render_resize(Backend* backend, int w, int h) {
  if (is_d3d11(backend)) d3d11_resize(backend, w, h); else d3d12_resize(backend, w, h);
}
void render_stats(Backend* backend, uint32_t* frames, uint32_t* pipelines, uint32_t* textures) {
  if (is_d3d11(backend)) d3d11_stats(backend, frames, pipelines, textures);
  else d3d12_stats(backend, frames, pipelines, textures);
}
void render_backend_unregister(Backend* backend) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_d3d11_backends.erase(backend);
}
std::string render_profile_line() {
  bool any_d3d11;
  { std::lock_guard<std::mutex> lock(g_mutex); any_d3d11 = !g_d3d11_backends.empty(); }
  return any_d3d11 ? d3d11_profile_line() : d3d12_profile_line();
}

}  // namespace gx
