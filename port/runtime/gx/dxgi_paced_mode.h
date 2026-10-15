#pragma once
// Exclusive fullscreen: a refresh rate the frame cap divides. At a 60 fps cap on a 144 or 175 Hz
// screen each frame stays up for two or three refreshes in turn, which reads as judder; at 120 or
// 60 Hz every frame stays up equally long. Used by both D3D backends before SetFullscreenState.
#include <windows.h>
#include <dxgi.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include "host.h"

namespace gx {

// Fills `mode` (Width, Height and Format already set) with the same size at the highest refresh
// that is a whole multiple of `cap`, when the output's current refresh is not one. Returns false
// (mode untouched, the desktop refresh stays) when the cap follows the monitor, when the current
// refresh already fits, or when the output lists no such mode.
inline bool pick_paced_mode(IDXGIOutput* output, double cap, DXGI_MODE_DESC& mode) {
  if (!output || !(cap >= 24.0)) return false;
  UINT count = 0;
  if (FAILED(output->GetDisplayModeList(mode.Format, 0, &count, nullptr)) || count == 0) return false;
  std::vector<DXGI_MODE_DESC> modes(count);
  if (FAILED(output->GetDisplayModeList(mode.Format, 0, &count, modes.data()))) return false;
  modes.resize(count);
  auto hz = [](const DXGI_MODE_DESC& m) { return m.RefreshRate.Denominator ? (double)m.RefreshRate.Numerator / m.RefreshRate.Denominator : 0.0; };
  auto fits = [&](double r) {
    if (r < cap * 0.99) return false;
    const double n = std::round(r / cap);
    return n >= 1.0 && std::fabs(r - n * cap) <= cap * 0.01;   // 59.94 counts as 60
  };
  DXGI_OUTPUT_DESC desc{};
  DEVMODEW dm{}; dm.dmSize = sizeof dm;
  if (FAILED(output->GetDesc(&desc)) || !EnumDisplaySettingsW(desc.DeviceName, ENUM_CURRENT_SETTINGS, &dm)) return false;
  const double current = dm.dmDisplayFrequency;   // the desktop's refresh, whole hertz
  if (current <= 0 || fits(current)) return false;
  const DXGI_MODE_DESC* best = nullptr;
  for (const auto& m : modes) {
    if (m.Width != mode.Width || m.Height != mode.Height || !fits(hz(m))) continue;
    if (!best || hz(m) > hz(*best)) best = &m;
  }
  if (!best) return false;
  host::log("display: exclusive fullscreen at %.2f Hz instead of %.2f Hz, a whole multiple of the %.0f fps cap", hz(*best), current, cap);
  mode = *best;
  return true;
}

}  // namespace gx
