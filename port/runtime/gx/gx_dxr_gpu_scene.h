// GPU acceleration structures for frame-local GX geometry.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "gx_dxr_scene.h"

#include <d3d12.h>
#include <wrl/client.h>
#include <string>

namespace gx {

class DxrGpuScene {
 public:
  static constexpr unsigned kFrameSlots = 3;
  bool build(const DxrScene& scene, unsigned slot, ID3D12Device5* device,
             ID3D12GraphicsCommandList4* command_list, std::string* error);
  D3D12_GPU_VIRTUAL_ADDRESS top_level(unsigned slot) const;
  ID3D12Resource* vertex_buffer(unsigned slot) const;
  ID3D12Resource* index_buffer(unsigned slot) const;

 private:
  Microsoft::WRL::ComPtr<ID3D12Device5> device_;
  struct Slot {
    Microsoft::WRL::ComPtr<ID3D12Resource> vertices, indices, instance;
    Microsoft::WRL::ComPtr<ID3D12Resource> blas, blas_scratch, tlas, tlas_scratch;
  } slots_[kFrameSlots];
  bool ensure_buffer(Microsoft::WRL::ComPtr<ID3D12Resource>& resource, uint64_t bytes,
                     D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state,
                     D3D12_RESOURCE_FLAGS flags, std::string* error);
};

}  // namespace gx
