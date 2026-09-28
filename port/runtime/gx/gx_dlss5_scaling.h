// Native DLSS 5 resize/resolve. No NGX dependency, so the GPU math can be tested on WARP.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include <vector>

namespace gx::dlss5 {
class Scaling {
 public:
  // Inputs must be NON_PIXEL_SHADER_RESOURCE, destination UNORDERED_ACCESS.
  // mode: 0 downsample; 1 processed-image resolve; 2 RGB-residual resolve.
  // Input signal encoding is preserved; arithmetic uses stored RGB values.
  bool dispatch(ID3D12Device* device, ID3D12GraphicsCommandList* list,
                ID3D12Fence* fence, uint64_t signal_value,
                ID3D12Resource* source, ID3D12Resource* baseline,
                ID3D12Resource* original, ID3D12Resource* destination,
                uint32_t mode, uint32_t filter);
  const std::string& error() const { return error_; }
  void shutdown(); // GPU idle
 private:
  struct Descriptors { Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap; uint64_t until = 0; };
  Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
  std::vector<Descriptors> descriptors_;
  std::string error_;
  bool initialise(ID3D12Device* device);
};
}
