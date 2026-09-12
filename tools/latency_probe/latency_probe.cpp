// Measures input-to-present latency of any running program, from the outside.
//
// A key is injected with SendInput at a known QPC timestamp, then the desktop compositor's own
// copy of the screen is watched until the picture changes. Windows reports, per captured frame,
// the time the application actually presented it (DXGI_OUTDUPL_FRAME_INFO::LastPresentTime), so
// the result is the time from the key reaching the system to the responding frame being handed to
// the display, with the capture pipeline's own delay excluded. Everything after that (scanout and
// panel response) is identical for any program on the same monitor, so two programs measured this
// way can be compared directly.
//
// The scene must be still enough that the response stands out: the probe first watches for a
// moment with no input and records the largest change it sees, then counts only changes several
// times larger than that. Menus with a cursor work well.
//
//   latency_probe --title "Melee" --key down --trials 30 [--settle 600] [--timeout 400]
//
// SPDX-License-Identifier: GPL-2.0-or-later
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "user32.lib")

namespace {

double qpc_frequency() {
  LARGE_INTEGER f;
  QueryPerformanceFrequency(&f);
  return (double)f.QuadPart;
}
long long qpc_now() {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return t.QuadPart;
}

struct FoundWindow { HWND window = nullptr; std::wstring title; };
FoundWindow g_found;
std::wstring g_wanted;

BOOL CALLBACK enum_window(HWND window, LPARAM) {
  if (!IsWindowVisible(window)) return TRUE;
  wchar_t title[512];
  if (!GetWindowTextW(window, title, 512)) return TRUE;
  std::wstring text(title);
  if (text.find(g_wanted) == std::wstring::npos) return TRUE;
  RECT r{};
  GetWindowRect(window, &r);
  if (r.right - r.left < 200 || r.bottom - r.top < 150) return TRUE;   // skip tool windows
  g_found.window = window;
  g_found.title = text;
  return FALSE;
}

// One captured frame reduced to a coarse luminance grid, so comparing two frames is cheap and
// insensitive to single-pixel noise.
constexpr int GRID_W = 48, GRID_H = 27;
using Grid = std::array<uint16_t, GRID_W * GRID_H>;

struct Duplicator {
  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* context = nullptr;
  IDXGIOutputDuplication* duplication = nullptr;
  ID3D11Texture2D* staging = nullptr;
  UINT width = 0, height = 0;

  bool start(HWND window) {
    D3D_FEATURE_LEVEL level;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, &level, &context))) {
      std::fprintf(stderr, "cannot create a D3D11 device\n");
      return false;
    }
    IDXGIDevice* dxgi = nullptr;
    IDXGIAdapter* adapter = nullptr;
    if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi)) || FAILED(dxgi->GetAdapter(&adapter))) return false;
    HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY);
    IDXGIOutput* output = nullptr;
    for (UINT i = 0; adapter->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND; ++i) {
      DXGI_OUTPUT_DESC desc{};
      output->GetDesc(&desc);
      if (desc.Monitor == monitor) break;
      output->Release();
      output = nullptr;
    }
    if (!output) { std::fprintf(stderr, "cannot find the monitor showing that window\n"); return false; }
    IDXGIOutput1* output1 = nullptr;
    if (FAILED(output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1))) return false;
    if (FAILED(output1->DuplicateOutput(device, &duplication))) {
      std::fprintf(stderr, "cannot duplicate the display output (is another capture running?)\n");
      return false;
    }
    DXGI_OUTDUPL_DESC desc{};
    duplication->GetDesc(&desc);
    width = desc.ModeDesc.Width;
    height = desc.ModeDesc.Height;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = width; td.Height = height; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_STAGING; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device->CreateTexture2D(&td, nullptr, &staging))) return false;
    adapter->Release(); dxgi->Release(); output1->Release(); output->Release();
    return true;
  }

  // Grabs the next presented frame. Returns false on timeout. `present` is the QPC time the
  // application presented it.
  bool next(Grid& grid, long long* present, UINT timeout_ms) {
    DXGI_OUTDUPL_FRAME_INFO info{};
    IDXGIResource* resource = nullptr;
    HRESULT hr = duplication->AcquireNextFrame(timeout_ms, &info, &resource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return false;
    if (FAILED(hr)) { std::fprintf(stderr, "capture lost (%08lX)\n", (unsigned long)hr); return false; }
    bool ok = false;
    ID3D11Texture2D* texture = nullptr;
    if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&texture))) {
      context->CopyResource(staging, texture);
      D3D11_MAPPED_SUBRESOURCE map{};
      if (SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &map))) {
        grid.fill(0);
        for (int gy = 0; gy < GRID_H; ++gy) {
          for (int gx = 0; gx < GRID_W; ++gx) {
            const UINT x = (UINT)((gx + 0.5) * width / GRID_W);
            const UINT y = (UINT)((gy + 0.5) * height / GRID_H);
            const uint8_t* p = (const uint8_t*)map.pData + (size_t)y * map.RowPitch + (size_t)x * 4;
            grid[gy * GRID_W + gx] = (uint16_t)((p[2] * 77 + p[1] * 151 + p[0] * 28) >> 8);
          }
        }
        context->Unmap(staging, 0);
        *present = info.LastPresentTime.QuadPart;
        ok = true;
      }
      texture->Release();
    }
    resource->Release();
    duplication->ReleaseFrame();
    return ok;
  }
};

// Menus animate constantly, so a plain whole-screen difference is swamped by the background. Only
// the cells that hold still while nothing is pressed are watched; the response (a cursor moving, a
// panel highlighting) lands in those.
using Mask = std::array<bool, GRID_W * GRID_H>;

int difference(const Grid& a, const Grid& b, const Mask& mask) {
  int total = 0;
  for (size_t i = 0; i < a.size(); ++i) if (mask[i]) total += std::abs((int)a[i] - (int)b[i]);
  return total;
}

void press(WORD key) {
  INPUT input[2]{};
  input[0].type = INPUT_KEYBOARD; input[0].ki.wVk = key;
  input[1].type = INPUT_KEYBOARD; input[1].ki.wVk = key; input[1].ki.dwFlags = KEYEVENTF_KEYUP;
  SendInput(1, &input[0], sizeof(INPUT));
  Sleep(24);   // a GameCube controller poll is 1 frame; hold long enough for one to land
  SendInput(1, &input[1], sizeof(INPUT));
}

WORD parse_key(const std::string& name) {
  if (name == "down") return VK_DOWN;
  if (name == "up") return VK_UP;
  if (name == "left") return VK_LEFT;
  if (name == "right") return VK_RIGHT;
  if (name == "start" || name == "enter") return VK_RETURN;
  if (name.size() == 1) return (WORD)std::toupper(name[0]);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::string title = "Melee", key_name = "down", key2_name;
  int trials = 30, settle_ms = 600, timeout_ms = 400;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--title") title = next();
    else if (a == "--key") key_name = next();
    else if (a == "--key2") key2_name = next();   // alternated with --key, so a menu cursor keeps moving
    else if (a == "--trials") trials = std::atoi(next());
    else if (a == "--settle") settle_ms = std::atoi(next());
    else if (a == "--timeout") timeout_ms = std::atoi(next());
    else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
  }
  const WORD key = parse_key(key_name);
  if (!key) { std::fprintf(stderr, "unknown key %s\n", key_name.c_str()); return 2; }
  const WORD key2 = key2_name.empty() ? key : parse_key(key2_name);
  if (!key2) { std::fprintf(stderr, "unknown key %s\n", key2_name.c_str()); return 2; }

  g_wanted.assign(title.begin(), title.end());
  EnumWindows(enum_window, 0);
  if (!g_found.window) { std::fprintf(stderr, "no visible window whose title contains \"%s\"\n", title.c_str()); return 1; }
  std::wprintf(L"window: %s\n", g_found.title.c_str());
  SetForegroundWindow(g_found.window);
  Sleep(400);

  Duplicator capture;
  if (!capture.start(g_found.window)) return 1;
  const double frequency = qpc_frequency();

  // Watch the idle scene: per cell, how much it moves on its own.
  Grid previous{}, current{};
  long long present = 0;
  int frames = 0;
  std::array<int, GRID_W * GRID_H> cell_noise{};
  const long long noise_until = qpc_now() + (long long)(frequency * settle_ms / 1000.0);
  while (qpc_now() < noise_until) {
    if (!capture.next(current, &present, 100)) continue;
    if (frames++)
      for (size_t i = 0; i < current.size(); ++i) cell_noise[i] = std::max(cell_noise[i], std::abs((int)previous[i] - (int)current[i]));
    previous = current;
  }
  Mask mask{};
  int still = 0;
  for (size_t i = 0; i < cell_noise.size(); ++i) { mask[i] = cell_noise[i] <= 2; still += mask[i] ? 1 : 0; }
  if (still < GRID_W * GRID_H / 8) { mask.fill(true); still = GRID_W * GRID_H; }   // nothing is still: watch everything
  // How much those still cells move on their own (compression and dithering keep it small but nonzero).
  int noise = 0;
  const long long recheck_until = qpc_now() + (long long)(frequency * settle_ms / 1000.0);
  while (qpc_now() < recheck_until) {
    if (!capture.next(current, &present, 100)) continue;
    noise = std::max(noise, difference(previous, current, mask));
    previous = current;
  }
  const int threshold = std::max(noise * 3, 150);
  std::printf("%d of %d cells hold still; idle change in them up to %d; response counts above %d\n",
              still, GRID_W * GRID_H, noise, threshold);

  std::vector<double> results;
  for (int trial = 0; trial < trials; ++trial) {
    // Start from a settled picture so the response is the only large change.
    while (capture.next(previous, &present, 50)) {}
    SetForegroundWindow(g_found.window);
    const long long pressed = qpc_now();
    press(trial % 2 ? key2 : key);
    double latency = -1;
    const long long give_up = pressed + (long long)(frequency * timeout_ms / 1000.0);
    while (qpc_now() < give_up) {
      if (!capture.next(current, &present, 50)) continue;
      if (difference(previous, current, mask) >= threshold) {
        if (present > pressed) latency = (double)(present - pressed) * 1000.0 / frequency;
        break;
      }
      previous = current;
    }
    if (latency >= 0) { results.push_back(latency); std::printf("  trial %2d: %6.1f ms\n", trial + 1, latency); }
    else std::printf("  trial %2d: no response\n", trial + 1);
    Sleep(180);
  }

  if (results.empty()) { std::fprintf(stderr, "no trial produced a measurable response\n"); return 1; }
  std::sort(results.begin(), results.end());
  const double median = results[results.size() / 2];
  const double p95 = results[std::min(results.size() - 1, (size_t)(results.size() * 0.95))];
  double sum = 0; for (double r : results) sum += r;
  std::printf("\n%zu of %d trials measured\nmedian %.1f ms | mean %.1f ms | p95 %.1f ms | min %.1f | max %.1f\n",
              results.size(), trials, median, sum / results.size(), p95, results.front(), results.back());
  return 0;
}
