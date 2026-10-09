// Which GPU the renderer asks for first.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <dxgi1_6.h>
#include <wrl/client.h>

namespace gx {

// The i-th adapter with the fastest GPU first (Windows' "high performance" order), where the
// plain enumeration starts with whichever GPU drives the display. On a laptop with an integrated
// and a dedicated GPU that was the integrated one: a 0.8.85 log shows the game on Intel UHD
// Graphics at 51 fps beside an NVIDIA GPU it never asked for. Older Windows without the newer
// factory keeps the plain order.
inline HRESULT enum_adapter_fastest_first(IDXGIFactory1* factory, UINT i, IDXGIAdapter1** out) {
  Microsoft::WRL::ComPtr<IDXGIFactory6> newer;
  if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&newer))))
    return newer->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(out));
  return factory->EnumAdapters1(i, out);
}

}  // namespace gx
