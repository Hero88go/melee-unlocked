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
#include "gx_d3d12.h"
#include "pc_settings.h"
#include "window.h"
#include "host.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace app {

using Microsoft::WRL::ComPtr;

int run_settings_d3d11(gx::D3D12Options& options, void* hwnd);
int run_settings_d3d12(gx::D3D12Options& options, void* hwnd);

int run_settings_window(gx::D3D12Options& options) {
  gx::settings_fill_window(true);   // the panel IS this window, not a box floating inside it
  void* hwnd = host::window_create(560, 660, L"Melee Unlocked settings", true);
  if (!hwnd) { host::log("settings: cannot create a window"); return 1; }
  // The backend the game is set to, so the panel runs on the renderer this machine will use. If it
  // cannot be created the other one still opens the settings rather than leaving no way in.
  const bool prefer_d3d12 = options.api == gx::RenderApi::D3D12;
  int rc = prefer_d3d12 ? run_settings_d3d12(options, hwnd) : run_settings_d3d11(options, hwnd);
  if (rc == 2) {
    host::log("settings: falling back to the other graphics API");
    rc = prefer_d3d12 ? run_settings_d3d11(options, hwnd) : run_settings_d3d12(options, hwnd);
  }
  return rc;
}

// Returns 2 when this API could not start, so the caller can try the other.
int run_settings_d3d11(gx::D3D12Options& options, void* hwnd) {
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

  // The panel opens straight away here: a window whose only content is the panel must not start
  // empty just because the player last closed it in game.
  const bool was_open = options.settings_open;
  options.settings_open = true;
  options.pc_settings = true;
  gx::PcSettingsUID3D11 ui(hwnd, device.Get(), context.Get(), options);

  while (!host::window_closed()) {
    host::window_pump();
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
    swapchain->Present(1, 0);   // vsync: this window has nothing to race
    // A settings box must not cost what a game costs. Unfocused it redraws a few times a second;
    // focused, vsync above already holds it at the monitor rate for one ImGui window.
    if (GetForegroundWindow() != (HWND)hwnd) Sleep(120);
    // Closing the panel from inside it (Return to game, or the title bar X) ends the process, since
    // there is no game to return to.
    if (!options.settings_open) break;
  }
  options.settings_open = was_open;   // do not let opening this window change the saved startup choice
  return 0;
}


// The same panel on Direct3D 12. One command allocator and list, one fence, two back buffers: a
// settings box has no pipelining to do, so each frame is recorded, submitted and waited on.
int run_settings_d3d12(gx::D3D12Options& options, void* hwnd) {
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

  const bool was_open = options.settings_open;
  options.settings_open = true;
  options.pc_settings = true;
  {
    gx::PcSettingsUI ui(hwnd, device.Get(), queue.Get(), options);
    while (!host::window_closed()) {
      host::window_pump();
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
      if (GetForegroundWindow() != (HWND)hwnd) Sleep(120);
      if (!options.settings_open) break;
    }
    wait_gpu();
  }
  CloseHandle(fence_event);
  options.settings_open = was_open;
  return 0;
}

}  // namespace app
