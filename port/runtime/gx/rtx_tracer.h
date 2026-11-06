// The path tracer's ray-traced passes (shaders/rtx_trace.hlsl). First: the scene debug view that
// proves the GPU scene (rtx_scene.h) matches the raster picture.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

namespace gx {

class RtxTracer {
 public:
  static constexpr unsigned kFrameSlots = 3;

  // Loads rtx_trace.dxil (DXR 1.1) from beside the executable.
  bool initialize(ID3D12Device5* device, const std::wstring& exe_dir, std::string* error);
  bool ready() const { return state_ != nullptr; }

  struct DebugInputs {
    D3D12_GPU_VIRTUAL_ADDRESS tlas = 0;
    ID3D12Resource* vertices = nullptr;          // RtVertex soup (NON_PIXEL_SHADER_RESOURCE)
    ID3D12Resource* records = nullptr;           // RtxGeometryRecord (upload heap)
    D3D12_GPU_DESCRIPTOR_HANDLE heap_start{};    // the shader-visible heap the texture indices count from
    D3D12_CPU_DESCRIPTOR_HANDLE output_uav_cpu{};  // a free slot in that heap for the output's UAV
    D3D12_GPU_DESCRIPTOR_HANDLE output_uav_gpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gbuffer_srvs{};  // albedo, normal + depth, draw id (3 SRVs in a row)
    uint32_t width = 0, height = 0;              // output size (the EFB)
    float view_to_world[12]{};
    float clip_to_view[16]{};
    float viewport[4]{};                         // x, y, w, h in output pixels
    bool orthographic = false;
    uint32_t mode = 1;                           // 1 draw, 2 normal, 3 textured vertex colour; 4-6 G-buffer
  };
  // Records the debug view into `list`. The heap holding `heap_start` must be the one set on the
  // list. Afterwards output(slot) is readable by pixel and non-pixel shaders.
  bool debug_view(ID3D12GraphicsCommandList4* list, unsigned slot, const DebugInputs& in, std::string* error);
  ID3D12Resource* output(unsigned slot) const { return slot < kFrameSlots ? slots_[slot].output.Get() : nullptr; }

 private:
  struct Slot {
    Microsoft::WRL::ComPtr<ID3D12Resource> output;
    Microsoft::WRL::ComPtr<ID3D12Resource> constants;
    uint8_t* constants_cpu = nullptr;
    bool shader_read = false;
  };
  Microsoft::WRL::ComPtr<ID3D12Device5> device_;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
  Microsoft::WRL::ComPtr<ID3D12StateObject> state_;
  Microsoft::WRL::ComPtr<ID3D12Resource> shader_table_;
  UINT64 raygen_offset_ = 0, miss_offset_ = 0, hit_offset_ = 0, record_stride_ = 0;
  UINT hit_records_ = 0;
  Slot slots_[kFrameSlots];
};

// Inverse of a row-major 4x4 matrix. False when singular.
bool rtx_invert4x4(const float m[16], float out[16]);

}  // namespace gx
