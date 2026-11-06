// Windows renderer factory. Generic per-instance dispatch lives in gx_backend_dispatch.cpp.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gx_backend.h"
#include "gx_d3d11.h"
#include "gx_d3d12.h"
#include "gx_vulkan.h"
#include "gx_fsr.h"
#include "gx_adapter.h"
#include "host.h"
#include "test_env.h"
#include <d3d11.h>
#include <cstdlib>
#include <string>

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

namespace {
[[noreturn]] void no_directx11_gpu(void* hwnd) {
  std::string gpu;
  IDXGIFactory1* factory = nullptr;
  if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory))) {
    IDXGIAdapter1* adapter = nullptr;
    if (enum_adapter_fastest_first(factory, 0, &adapter) == S_OK) {
      DXGI_ADAPTER_DESC1 desc{};
      if (SUCCEEDED(adapter->GetDesc1(&desc))) {
        char name[128]{};
        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof name, nullptr, nullptr);
        gpu = name;
      }
      adapter->Release();
    }
    factory->Release();
  }
  if (gpu.empty()) gpu = "this graphics card";
  host::log("graphics: neither Direct3D 12 nor Direct3D 11 starts on %s; the game needs a DirectX 11 graphics card", gpu.c_str());
  host::log_flush();
  if (!host::options.no_gc_adapter) {   // set for hidden and headless runs: a test never waits on a dialog
    const std::string text = "Melee Unlocked cannot draw on " + gpu + ".\n\n"
        "The game needs a graphics card with DirectX 11 (most made since 2010) and its current driver. "
        "If this PC has another graphics card, update its driver and start the game again.";
    MessageBoxA((HWND)hwnd, text.c_str(), "Melee Unlocked", MB_OK | MB_ICONERROR);
  }
  host::end_process(1);
}
}  // namespace

Backend* create_render_backend(void* hwnd, int client_w, int client_h, const RenderOptions& options) {
  if (options.api == RenderApi::Vulkan) {
    VulkanWindow window;
    window.window = hwnd;
    window.drawable_size = [hwnd](int* w, int* h) {
      RECT r{}; GetClientRect((HWND)hwnd, &r);
      *w = r.right - r.left; *h = r.bottom - r.top;
    };
    RenderOptions vulkan_options = options;
    // DLSS, XeSS and NVIDIA frame generation are Direct3D 12 features; AMD FSR upscaling and its
    // frame generation run on Vulkan too.
    if (!fsr::is_fsr_mode(vulkan_options.dlss_mode)) { vulkan_options.dlss_mode = 0; vulkan_options.frame_generation_mode = 0; }
    if (Backend* backend = create_vulkan_backend(window, client_w, client_h, vulkan_options)) return backend;
    host::log("vulkan: falling back to Direct3D 12");
  } else if (options.api == RenderApi::D3D11) {
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
    // MELEE_TEST_DXGI_FAULT=3: Direct3D 11 does not start either (with 2, Direct3D 12 cannot).
    const char* fault = host::test_env::getenv("MELEE_TEST_DXGI_FAULT");
    const bool d3d11_fault = fault && std::atoi(fault) >= 3;
    if (!d3d11_fault)
      if (Backend* backend = create_d3d11_backend(hwnd, client_w, client_h, d3d11_options)) return backend;
    // No hardware Direct3D 11 device either: a graphics card older than DirectX 11 (a 0.9.95 report
    // ran an ATI Radeon 3000, a DirectX 10 part) or no driver for it. Direct3D 12 would only stop on
    // "no adapter" with a crash report; say what is missing instead.
    if (d3d11_fault || !d3d11_available()) no_directx11_gpu(hwnd);
    host::log("d3d11: did not start either; trying Direct3D 12 anyway");
  }
  return create_d3d12_backend(hwnd, client_w, client_h, options);
}

}  // namespace gx
