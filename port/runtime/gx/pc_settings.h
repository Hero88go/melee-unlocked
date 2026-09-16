// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gx_d3d12.h"
#include <memory>
struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12GraphicsCommandList;
struct ID3D11Device;
struct ID3D11DeviceContext;
namespace gx {
void load_pc_settings(D3D12Options& options, int& volume);
class PcSettingsUI {
  struct Impl;
  std::unique_ptr<Impl> impl_;
public:
  PcSettingsUI(void* window, ID3D12Device* device, ID3D12CommandQueue* queue, const D3D12Options& options);
  ~PcSettingsUI();
  bool begin(D3D12Options& options); // returns true when render configuration changed
  void draw(ID3D12GraphicsCommandList* list);
};
// The same panel on the D3D11 backend (its own small ImGui renderer; the panel body is shared).
class PcSettingsUID3D11 {
  struct Impl;
  std::unique_ptr<Impl> impl_;
public:
  PcSettingsUID3D11(void* window, ID3D11Device* device, ID3D11DeviceContext* context, const D3D12Options& options);
  ~PcSettingsUID3D11();
  bool begin(D3D12Options& options);
  void draw();   // into whatever render target the caller has bound
};
}
