// DXR diffuse path-tracing pass and DLSS-RR guide buffers.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "gx_dxr_gpu_scene.h"

#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <string>

namespace gx {

class DxrPathTracer {
 public:
  static constexpr unsigned kFrameSlots = DxrGpuScene::kFrameSlots;
  bool initialize(ID3D12Device5* device, const std::wstring& executable_directory,
                  std::string* error);
  bool ready() const { return state_.Get() != nullptr; }
  bool dispatch(ID3D12GraphicsCommandList4* command_list, unsigned slot,
                const DxrGpuScene& scene, const DxrScene& cpu_scene,
                ID3D12Resource* raster_color, uint32_t width, uint32_t height,
                uint32_t frame_index, float bounce_intensity, uint32_t samples, uint32_t max_bounces,
                std::string* error);
  ID3D12Resource* color(unsigned slot) const;
  ID3D12Resource* albedo(unsigned slot) const;
  ID3D12Resource* normal_roughness(unsigned slot) const;
  ID3D12Resource* specular_albedo(unsigned slot) const;

 private:
  struct Targets {
    Microsoft::WRL::ComPtr<ID3D12Resource> color, albedo, normal_roughness, specular_albedo;
    bool shader_read = false;
  } targets_[kFrameSlots];
  Microsoft::WRL::ComPtr<ID3D12Device5> device_;
  Microsoft::WRL::ComPtr<ID3D12StateObject> state_;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptors_;
  Microsoft::WRL::ComPtr<ID3D12Resource> shader_table_;
  UINT descriptor_size_ = 0;
  bool create_targets(Targets& targets, uint32_t width, uint32_t height,
                      std::string* error);
  bool create_shader_pipeline(const void* dxil, size_t dxil_size, std::string* error);
};

}  // namespace gx
