// The PC settings panel on its own, with no game behind it.
//
// The launcher's Settings button used to start melee_port with --pc-settings-open, which opened the
// real panel but booted the whole game to do it: a disc read, the pipeline prewarm and a match
// engine, to change a frame rate. The panel itself needs none of that. It is ImGui drawn onto a
// swapchain, so this gives it a window and a device of its own and runs it directly.
//
// Both backends are here, chosen by the one the game is configured for, so the panel is exercised on
// whichever renderer the player actually uses and a machine that can only create one of the two
// still gets its settings. The panel body is the same settings_frame the game runs either way, so
// none of this can drift from what the game shows.
// SPDX-License-Identifier: GPL-2.0-or-later
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include "render_options.h"
#include "pc_settings.h"
#include "window.h"
#include "host.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace app {

using Microsoft::WRL::ComPtr;

int run_settings_d3d11(gx::RenderOptions& options, void* hwnd);
int run_settings_d3d12(gx::RenderOptions& options, void* hwnd);

// The controller display draws what the game read on its last PADRead, which is the right source in
// game: it shows what the simulation acted on. Here there is no game and nothing ever calls
// input_poll, so every port stayed at its zero-initialised state and the controller on screen never
// moved, in the one window whose whole purpose is checking that a controller works. Polling once per
// drawn frame publishes the same routed per-port state the game would, so the display, the port
// assignment section and the live "what is this device pressing" lines all work here too.
static void poll_pads_for_panel() {
  host::PadState pads[4];
  host::input_poll(pads);
}

// Standby: the launcher starts this window hidden ahead of time, with its device, fonts and panel all
// created, and its Settings button only signals it to show. Starting a process and a graphics device
// on the click took about a second, with a black window first.
static unsigned long g_standby_parent = 0;
static std::function<void()> g_standby_reload;

// Waits hidden until the launcher asks for the window. False: the launcher asked it to quit, or is gone.
static bool standby_wait(void* hwnd) {
  if (!g_standby_parent) return true;
  wchar_t name[96];
  swprintf_s(name, L"Local\\MeleeUnlockedSettingsShow-%lu", g_standby_parent);
  HANDLE show = CreateEventW(nullptr, FALSE, FALSE, name);
  swprintf_s(name, L"Local\\MeleeUnlockedSettingsQuit-%lu", g_standby_parent);
  HANDLE quit = CreateEventW(nullptr, FALSE, FALSE, name);
  HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, g_standby_parent);
  bool shown = false;
  if (show && quit && parent) {
    HANDLE waits[3] = {show, quit, parent};
    for (;;) {
      const DWORD r = MsgWaitForMultipleObjects(3, waits, FALSE, INFINITE, QS_ALLINPUT);
      if (r == WAIT_OBJECT_0) { shown = true; break; }
      if (r != WAIT_OBJECT_0 + 3) break;
      host::window_pump();
      if (host::window_closed()) break;
    }
  }
  if (show) CloseHandle(show);
  if (quit) CloseHandle(quit);
  if (parent) CloseHandle(parent);
  if (!shown) return false;
  if (g_standby_reload) g_standby_reload();   // what the game or an earlier panel saved meanwhile
  ShowWindow((HWND)hwnd, SW_SHOW);
  SetForegroundWindow((HWND)hwnd);
  return true;
}

// The window is 620 by 700 at 96 DPI and the panel is drawn in those units. On a scaled display both
// grow by dpi/96: the window was 620 by 700 pixels whatever the scaling, so at 250 percent on a 4K
// monitor it came out at 40 percent of the size of the launcher that opened it, with text to match.
constexpr int kBaseWidth = 620, kBaseHeight = 700;

// MELEE_TEST_UI_DPI=<dpi> (tests): the window behaves as if its monitor reported that DPI, so the
// scaled layout can be captured on a display that is not scaled. 192 is 200 percent.
static int g_test_dpi = 0;
// MELEE_TEST_SETTINGS_SHOT=<file.bmp> (tests): the window stays hidden, draws a few frames on
// Direct3D 11, writes the last one to that file and exits.
static std::string g_test_shot;
static int g_test_shot_frames = 0;
constexpr int kTestShotFrame = 8;   // the layout and the fonts have settled by then

static int window_dpi(HWND hwnd) {
  if (g_test_dpi) return g_test_dpi;
  const UINT dpi = GetDpiForWindow(hwnd);
  return dpi ? (int)dpi : 96;
}

// The client area for `dpi`, no larger than the monitor's work area, with the window moved back
// inside that area if growing pushed it over an edge.
static void size_window_for_dpi(HWND hwnd, int dpi) {
  MONITORINFO monitor{sizeof(monitor)};
  RECT window{}, client{};
  if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor) ||
      !GetWindowRect(hwnd, &window) || !GetClientRect(hwnd, &client)) return;
  // The frame is measured from the window itself: AdjustWindowRect answers for the system DPI,
  // which is not this monitor's when the two differ.
  const int frame_w = (int)(window.right - window.left) - (int)client.right;
  const int frame_h = (int)(window.bottom - window.top) - (int)client.bottom;
  const RECT& work = monitor.rcWork;
  const int w = std::min(MulDiv(kBaseWidth, dpi, 96) + frame_w, (int)(work.right - work.left));
  const int h = std::min(MulDiv(kBaseHeight, dpi, 96) + frame_h, (int)(work.bottom - work.top));
  const int x = std::clamp((int)window.left, (int)work.left, (int)work.right - w);
  const int y = std::clamp((int)window.top, (int)work.top, (int)work.bottom - h);
  SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

// The back buffer as a 32-bit BMP, for MELEE_TEST_SETTINGS_SHOT.
static void save_test_shot(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain* swapchain) {
  ComPtr<ID3D11Texture2D> back, copy;
  if (FAILED(swapchain->GetBuffer(0, IID_PPV_ARGS(&back)))) return;
  D3D11_TEXTURE2D_DESC desc{};
  back->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
  if (FAILED(device->CreateTexture2D(&desc, nullptr, &copy))) return;
  context->CopyResource(copy.Get(), back.Get());
  D3D11_MAPPED_SUBRESOURCE map{};
  if (FAILED(context->Map(copy.Get(), 0, D3D11_MAP_READ, 0, &map))) return;
  std::vector<uint8_t> pixels((size_t)desc.Width * desc.Height * 4);
  for (UINT y = 0; y < desc.Height; ++y) {
    const uint8_t* from = (const uint8_t*)map.pData + (size_t)y * map.RowPitch;
    uint8_t* to = pixels.data() + (size_t)y * desc.Width * 4;
    for (UINT x = 0; x < desc.Width; ++x) {   // the back buffer is RGBA, a BMP is BGRA
      to[x * 4 + 0] = from[x * 4 + 2]; to[x * 4 + 1] = from[x * 4 + 1];
      to[x * 4 + 2] = from[x * 4 + 0]; to[x * 4 + 3] = 255;
    }
  }
  context->Unmap(copy.Get(), 0);
  BITMAPFILEHEADER file{};
  BITMAPINFOHEADER info{};
  file.bfType = 0x4D42; file.bfOffBits = sizeof file + sizeof info;
  file.bfSize = file.bfOffBits + (DWORD)pixels.size();
  info.biSize = sizeof info; info.biWidth = (LONG)desc.Width; info.biHeight = -(LONG)desc.Height;   // top row first
  info.biPlanes = 1; info.biBitCount = 32; info.biCompression = BI_RGB;
  FILE* out = std::fopen(g_test_shot.c_str(), "wb");
  if (!out) { host::log("settings: cannot write %s", g_test_shot.c_str()); return; }
  std::fwrite(&file, sizeof file, 1, out);
  std::fwrite(&info, sizeof info, 1, out);
  std::fwrite(pixels.data(), 1, pixels.size(), out);
  std::fclose(out);
  host::log("settings: test shot %ux%u written to %s", desc.Width, desc.Height, g_test_shot.c_str());
}

int run_settings_window(gx::RenderOptions& options, unsigned long standby_parent, std::function<void()> reload) {
  g_standby_parent = standby_parent;
  g_standby_reload = std::move(reload);
  if (const char* dpi = std::getenv("MELEE_TEST_UI_DPI")) g_test_dpi = std::clamp(std::atoi(dpi), 96, 384);
  if (const char* shot = std::getenv("MELEE_TEST_SETTINGS_SHOT")) g_test_shot = shot;
  // This window scales itself, so the process says so before the window exists: nothing else
  // declares this executable DPI aware, and an unaware process is told 96 DPI and has its picture
  // stretched by Windows instead. The call fails harmlessly when the awareness is already set. The
  // game does not come this way and is not changed.
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  gx::settings_fill_window(true);   // the panel IS this window, not a box floating inside it
  // Created hidden, sized for its monitor, then shown, so it never appears at the wrong size.
  const bool visible = standby_parent == 0 && g_test_shot.empty();
  void* hwnd = host::window_create(kBaseWidth, kBaseHeight, L"Melee Unlocked settings", false);
  if (!hwnd) { host::log("settings: cannot create a window"); return 1; }
  // At 96 DPI the window and the panel are left exactly as they were created.
  if (const int dpi = window_dpi((HWND)hwnd); dpi != 96) {
    size_window_for_dpi((HWND)hwnd, dpi);
    gx::settings_set_window_scale(dpi / 96.0f);
    host::log("settings: window scaled for %d DPI", dpi);
  }
  // A move to a monitor with another DPI: the panel follows, and the window takes the size Windows
  // suggests for it there.
  host::window_set_dpi_callback([](int dpi) { if (!g_test_dpi) gx::settings_set_window_scale(dpi / 96.0f); });
  if (visible) ShowWindow((HWND)hwnd, SW_SHOW);
  if (!g_test_shot.empty()) {
    const int rc = run_settings_d3d11(options, hwnd);
    host::window_set_dpi_callback({});
    return rc == 2 ? 1 : rc;
  }
  // The backend the game is set to, so the panel runs on the renderer this machine will use. If it
  // cannot be created the other one still opens the settings rather than leaving no way in.
  const bool prefer_d3d12 = options.api == gx::RenderApi::D3D12;
  int rc = prefer_d3d12 ? run_settings_d3d12(options, hwnd) : run_settings_d3d11(options, hwnd);
  if (rc == 2) {
    host::log("settings: falling back to the other graphics API");
    rc = prefer_d3d12 ? run_settings_d3d11(options, hwnd) : run_settings_d3d12(options, hwnd);
  }
  host::window_set_dpi_callback({});
  return rc;
}

// Returns 2 when this API could not start, so the caller can try the other.
int run_settings_d3d11(gx::RenderOptions& options, void* hwnd) {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  DXGI_SWAP_CHAIN_DESC scd{};
  scd.BufferCount = 2;
  scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  scd.OutputWindow = (HWND)hwnd;
  scd.SampleDesc.Count = 1;
  scd.Windowed = TRUE;
  scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  ComPtr<IDXGISwapChain> swapchain;
  const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, want, 2,
                                           D3D11_SDK_VERSION, &scd, &swapchain, &device, nullptr, &context))) {
    host::log("settings: no Direct3D 11 device");
    return 2;
  }

  ComPtr<ID3D11RenderTargetView> rtv;
  auto make_rtv = [&] {
    rtv.Reset();
    ComPtr<ID3D11Texture2D> back;
    if (SUCCEEDED(swapchain->GetBuffer(0, IID_PPV_ARGS(&back))))
      device->CreateRenderTargetView(back.Get(), nullptr, &rtv);
  };
  make_rtv();

  // The panel opens straight away here (settings_fills_window). The saved "open the overlay at
  // startup" choice is not touched: forcing it on here made every save from this window write it
  // as on, so the overlay came back at each launch.
  options.pc_settings = true;
  gx::PcSettingsUID3D11 ui(hwnd, device.Get(), context.Get(), options);

  // One frame. Called from the loop, and also from the window's WM_SIZE while its edge is being
  // dragged: Windows runs its own message loop for the whole drag, so without this the last frame was
  // stretched to the new size until the mouse was let go. `drawing` keeps a resize that arrives from
  // inside a frame from starting another one.
  bool drawing = false;
  auto draw_frame = [&] {
    if (drawing) return;
    drawing = true;
    // Resizing releases the back buffer, so the view is rebuilt from whatever size it is now.
    RECT rc{}; GetClientRect((HWND)hwnd, &rc);
    const UINT w = (UINT)(rc.right - rc.left), h = (UINT)(rc.bottom - rc.top);
    DXGI_SWAP_CHAIN_DESC have{}; swapchain->GetDesc(&have);
    if (w && h && (have.BufferDesc.Width != w || have.BufferDesc.Height != h)) {
      rtv.Reset();
      context->OMSetRenderTargets(0, nullptr, nullptr);
      swapchain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
      make_rtv();
    }
    ui.begin(options);   // the return value says the game would need to re-create resources; nothing here has any
    if (rtv) {
      const float background[4] = {0.06f, 0.08f, 0.12f, 1.0f};
      ID3D11RenderTargetView* targets[] = {rtv.Get()};
      context->OMSetRenderTargets(1, targets, nullptr);
      context->ClearRenderTargetView(rtv.Get(), background);
      D3D11_VIEWPORT vp{0, 0, (float)w, (float)h, 0, 1};
      context->RSSetViewports(1, &vp);
    }
    ui.draw();
    if (!g_test_shot.empty() && ++g_test_shot_frames == kTestShotFrame)
      save_test_shot(device.Get(), context.Get(), swapchain.Get());
    swapchain->Present(1, 0);   // vsync: this window has nothing to race
    drawing = false;
  };
  host::window_set_resize_callback([&](int, int) { draw_frame(); });
  if (!standby_wait(hwnd)) return 0;
  while (!host::window_closed()) {
    host::window_pump();
    poll_pads_for_panel();
    draw_frame();
    if (g_test_shot_frames >= kTestShotFrame) break;   // MELEE_TEST_SETTINGS_SHOT: one picture, then out
    // A settings box must not cost what a game costs. Unfocused it redraws a few times a second;
    // focused, vsync above already holds it at the monitor rate for one ImGui window.
    if (GetForegroundWindow() != (HWND)hwnd) Sleep(120);
    // Closing the panel from inside it (Return to game, or the title bar X) ends the process, since
    // there is no game to return to.
    if (gx::settings_close_requested()) { ShowWindow((HWND)hwnd, SW_HIDE); break; }
  }
  host::window_set_resize_callback({});
  return 0;
}


// The same panel on Direct3D 12. One command allocator and list, one fence, two back buffers: a
// settings box has no pipelining to do, so each frame is recorded, submitted and waited on.
int run_settings_d3d12(gx::RenderOptions& options, void* hwnd) {
  ComPtr<ID3D12Device> device;
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
    host::log("settings: no Direct3D 12 device");
    return 2;
  }
  ComPtr<ID3D12CommandQueue> queue;
  D3D12_COMMAND_QUEUE_DESC qd{};
  if (FAILED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))) return 2;
  ComPtr<IDXGIFactory4> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return 2;
  DXGI_SWAP_CHAIN_DESC1 scd{};
  scd.BufferCount = 2; scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; scd.SampleDesc.Count = 1;
  scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  ComPtr<IDXGISwapChain1> swap1;
  if (FAILED(factory->CreateSwapChainForHwnd(queue.Get(), (HWND)hwnd, &scd, nullptr, nullptr, &swap1))) return 2;
  ComPtr<IDXGISwapChain3> swapchain;
  if (FAILED(swap1.As(&swapchain))) return 2;

  ComPtr<ID3D12DescriptorHeap> rtv_heap;
  D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2};
  if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtv_heap)))) return 2;
  const UINT rtv_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  ComPtr<ID3D12Resource> targets[2];
  auto make_targets = [&] {
    for (UINT i = 0; i < 2; ++i) {
      targets[i].Reset();
      if (FAILED(swapchain->GetBuffer(i, IID_PPV_ARGS(&targets[i])))) continue;
      D3D12_CPU_DESCRIPTOR_HANDLE h = rtv_heap->GetCPUDescriptorHandleForHeapStart();
      h.ptr += i * rtv_size;
      device->CreateRenderTargetView(targets[i].Get(), nullptr, h);
    }
  };
  make_targets();

  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)))) return 2;
  if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)))) return 2;
  list->Close();
  ComPtr<ID3D12Fence> fence;
  if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return 2;
  UINT64 fence_value = 0;
  HANDLE fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  auto wait_gpu = [&] {
    queue->Signal(fence.Get(), ++fence_value);
    if (fence->GetCompletedValue() < fence_value) {
      fence->SetEventOnCompletion(fence_value, fence_event);
      WaitForSingleObject(fence_event, INFINITE);
    }
  };

  options.pc_settings = true;
  {
    gx::PcSettingsUI ui(hwnd, device.Get(), queue.Get(), options);
    // One frame, from the loop and from WM_SIZE during a drag (see the Direct3D 11 version).
    bool drawing = false;
    auto draw_frame = [&] {
      if (drawing) return;
      drawing = true;
      RECT rc{}; GetClientRect((HWND)hwnd, &rc);
      const UINT w = (UINT)(rc.right - rc.left), h = (UINT)(rc.bottom - rc.top);
      DXGI_SWAP_CHAIN_DESC1 have{}; swapchain->GetDesc1(&have);
      if (w && h && (have.Width != w || have.Height != h)) {
        wait_gpu();
        for (auto& t : targets) t.Reset();
        swapchain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
        make_targets();
      }
      ui.begin(options);
      const UINT index = swapchain->GetCurrentBackBufferIndex();
      allocator->Reset();
      list->Reset(allocator.Get(), nullptr);
      D3D12_RESOURCE_BARRIER b{};
      b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      b.Transition = {targets[index].Get(), 0, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET};
      list->ResourceBarrier(1, &b);
      D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
      rtv.ptr += index * rtv_size;
      const float background[4] = {0.06f, 0.08f, 0.12f, 1.0f};
      list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
      list->ClearRenderTargetView(rtv, background, 0, nullptr);
      D3D12_VIEWPORT vp{0, 0, (float)w, (float)h, 0, 1};
      D3D12_RECT sr{0, 0, (LONG)w, (LONG)h};
      list->RSSetViewports(1, &vp);
      list->RSSetScissorRects(1, &sr);
      ui.draw(list.Get());
      b.Transition = {targets[index].Get(), 0, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT};
      list->ResourceBarrier(1, &b);
      list->Close();
      ID3D12CommandList* lists[] = {list.Get()};
      queue->ExecuteCommandLists(1, lists);
      swapchain->Present(1, 0);
      wait_gpu();
      drawing = false;
    };
    host::window_set_resize_callback([&](int, int) { draw_frame(); });
    if (!standby_wait(hwnd)) return 0;
    while (!host::window_closed()) {
      host::window_pump();
      poll_pads_for_panel();
      draw_frame();
      if (GetForegroundWindow() != (HWND)hwnd) Sleep(120);
      if (gx::settings_close_requested()) { ShowWindow((HWND)hwnd, SW_HIDE); break; }
    }
    host::window_set_resize_callback({});
    wait_gpu();
  }
  CloseHandle(fence_event);
  return 0;
}

}  // namespace app
