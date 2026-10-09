// Windows renderer factory. Generic per-instance dispatch lives in gx_backend_dispatch.cpp.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gx_backend.h"
#include "gx_d3d11.h"
#include "gx_d3d12.h"
#include "host.h"
#include <d3d11.h>

// Laptops with two GPUs: both vendors' drivers read these from the program file and start it on the
// dedicated GPU. The renderer also asks DXGI for the fastest adapter itself (gx_adapter.h).
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

namespace gx {

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

Backend* create_render_backend(void* hwnd, int client_w, int client_h, const RenderOptions& options) {
  if (options.api == RenderApi::D3D11) {
    RenderOptions d3d11_options = options;
    if (d3d11_options.dlss_mode) host::log("d3d11: DLSS is a Direct3D 12 feature; rendering natively");
    d3d11_options.dlss_mode = 0;
    if (Backend* backend = create_d3d11_backend(hwnd, client_w, client_h, d3d11_options)) {
      return backend;
    }
    host::log("d3d11: falling back to Direct3D 12");
  } else if (!d3d12_usable()) {
    // A 0.8.84 report crashed inside DXGI at startup with Direct3D 12 selected: start on Direct3D 11
    // when 12 cannot, rather than stopping at the first frame.
    host::log("d3d12: cannot start on this system; starting on Direct3D 11");
    RenderOptions d3d11_options = options;
    d3d11_options.dlss_mode = 0;
    if (Backend* backend = create_d3d11_backend(hwnd, client_w, client_h, d3d11_options)) return backend;
    host::log("d3d11: did not start either; trying Direct3D 12 anyway");
  }
  return create_d3d12_backend(hwnd, client_w, client_h, options);
}

}  // namespace gx
