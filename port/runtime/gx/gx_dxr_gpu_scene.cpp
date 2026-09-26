// GPU BLAS/TLAS construction for the extracted, current-frame GX scene.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gx_dxr_gpu_scene.h"

#include <algorithm>
#include <cstring>

namespace gx {
namespace {

uint64_t align_size(uint64_t value, uint64_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

bool create_buffer(ID3D12Device* device, Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
                   uint64_t bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state,
                   D3D12_RESOURCE_FLAGS flags, std::string* error) {
  D3D12_HEAP_PROPERTIES hp{};
  hp.Type = heap;
  hp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  hp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  hp.CreationNodeMask = hp.VisibleNodeMask = 1;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = align_size(bytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  desc.Flags = flags;
  const HRESULT hr = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc, state,
                                                       nullptr, IID_PPV_ARGS(&resource));
  if (FAILED(hr)) {
    if (error) *error = "CreateCommittedResource failed (" + std::to_string((uint32_t)hr) + ")";
    return false;
  }
  return true;
}

void uav_barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource) {
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  barrier.UAV.pResource = resource;
  list->ResourceBarrier(1, &barrier);
}

}  // namespace

bool DxrGpuScene::ensure_buffer(Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
                                uint64_t bytes, D3D12_HEAP_TYPE heap,
                                D3D12_RESOURCE_STATES state,
                                D3D12_RESOURCE_FLAGS flags, std::string* error) {
  const uint64_t needed = align_size(bytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
  if (resource && resource->GetDesc().Width >= needed) return true;
  resource.Reset();
  return create_buffer(device_.Get(), resource, needed, heap, state, flags, error);
}

bool DxrGpuScene::build(const DxrScene& scene, unsigned slot, ID3D12Device5* device,
                        ID3D12GraphicsCommandList4* command_list, std::string* error) {
  if (error) error->clear();
  if (!device || !command_list || slot >= kFrameSlots || scene.vertices.empty() ||
      scene.indices.empty() || (scene.indices.size() % 3) != 0 ||
      scene.vertices.size() > UINT_MAX || scene.indices.size() > UINT_MAX) {
    if (error) *error = "invalid DXR device, frame slot, or triangle scene";
    return false;
  }
  if (!device_) device_ = device;
  else if (device_.Get() != device) {
    if (error) *error = "DXR device changed after resource creation";
    return false;
  }

  Slot& s = slots_[slot];
  const uint64_t vertex_bytes = scene.vertices.size() * sizeof(DxrSceneVertex);
  const uint64_t index_bytes = scene.indices.size() * sizeof(uint32_t);
  if (!ensure_buffer(s.vertices, vertex_bytes, D3D12_HEAP_TYPE_UPLOAD,
                     D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE, error) ||
      !ensure_buffer(s.indices, index_bytes, D3D12_HEAP_TYPE_UPLOAD,
                     D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE, error) ||
      !ensure_buffer(s.instance, sizeof(D3D12_RAYTRACING_INSTANCE_DESC), D3D12_HEAP_TYPE_UPLOAD,
                     D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE, error))
    return false;

  void* mapped = nullptr;
  D3D12_RANGE no_read{0, 0};
  if (FAILED(s.vertices->Map(0, &no_read, &mapped))) {
    if (error) *error = "map DXR vertex upload failed";
    return false;
  }
  std::memcpy(mapped, scene.vertices.data(), (size_t)vertex_bytes);
  D3D12_RANGE written{0, (SIZE_T)vertex_bytes};
  s.vertices->Unmap(0, &written);
  if (FAILED(s.indices->Map(0, &no_read, &mapped))) {
    if (error) *error = "map DXR index upload failed";
    return false;
  }
  std::memcpy(mapped, scene.indices.data(), (size_t)index_bytes);
  written.End = (SIZE_T)index_bytes;
  s.indices->Unmap(0, &written);

  D3D12_RAYTRACING_GEOMETRY_DESC geometry{};
  geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
  geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
  geometry.Triangles.Transform3x4 = 0;
  geometry.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
  geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
  geometry.Triangles.IndexCount = (UINT)scene.indices.size();
  geometry.Triangles.VertexCount = (UINT)scene.vertices.size();
  geometry.Triangles.IndexBuffer = s.indices->GetGPUVirtualAddress();
  geometry.Triangles.VertexBuffer.StartAddress = s.vertices->GetGPUVirtualAddress();
  geometry.Triangles.VertexBuffer.StrideInBytes = sizeof(DxrSceneVertex);

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas_inputs{};
  blas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  blas_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  blas_inputs.NumDescs = 1;
  blas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  blas_inputs.pGeometryDescs = &geometry;
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blas_info{};
  device->GetRaytracingAccelerationStructurePrebuildInfo(&blas_inputs, &blas_info);
  if (!blas_info.ResultDataMaxSizeInBytes || !blas_info.ScratchDataSizeInBytes) {
    if (error) *error = "driver returned zero BLAS build size";
    return false;
  }
  if (!ensure_buffer(s.blas, blas_info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                     D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                     D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, error) ||
      !ensure_buffer(s.blas_scratch, blas_info.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                     D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, error))
    return false;

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blas_build{};
  blas_build.Inputs = blas_inputs;
  blas_build.DestAccelerationStructureData = s.blas->GetGPUVirtualAddress();
  blas_build.ScratchAccelerationStructureData = s.blas_scratch->GetGPUVirtualAddress();
  command_list->BuildRaytracingAccelerationStructure(&blas_build, 0, nullptr);
  uav_barrier(command_list, s.blas.Get());

  D3D12_RAYTRACING_INSTANCE_DESC instance{};
  instance.Transform[0][0] = instance.Transform[1][1] = instance.Transform[2][2] = 1.0f;
  instance.InstanceMask = 0xFF;
  instance.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
  instance.AccelerationStructure = s.blas->GetGPUVirtualAddress();
  if (FAILED(s.instance->Map(0, &no_read, &mapped))) {
    if (error) *error = "map DXR instance upload failed";
    return false;
  }
  std::memcpy(mapped, &instance, sizeof(instance));
  written.End = sizeof(instance);
  s.instance->Unmap(0, &written);

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_inputs{};
  tlas_inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  tlas_inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  tlas_inputs.NumDescs = 1;
  tlas_inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  tlas_inputs.InstanceDescs = s.instance->GetGPUVirtualAddress();
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlas_info{};
  device->GetRaytracingAccelerationStructurePrebuildInfo(&tlas_inputs, &tlas_info);
  if (!tlas_info.ResultDataMaxSizeInBytes || !tlas_info.ScratchDataSizeInBytes) {
    if (error) *error = "driver returned zero TLAS build size";
    return false;
  }
  if (!ensure_buffer(s.tlas, tlas_info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                     D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                     D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, error) ||
      !ensure_buffer(s.tlas_scratch, tlas_info.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                     D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, error))
    return false;
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlas_build{};
  tlas_build.Inputs = tlas_inputs;
  tlas_build.DestAccelerationStructureData = s.tlas->GetGPUVirtualAddress();
  tlas_build.ScratchAccelerationStructureData = s.tlas_scratch->GetGPUVirtualAddress();
  command_list->BuildRaytracingAccelerationStructure(&tlas_build, 0, nullptr);
  uav_barrier(command_list, s.tlas.Get());
  return true;
}

D3D12_GPU_VIRTUAL_ADDRESS DxrGpuScene::top_level(unsigned slot) const {
  return slot < kFrameSlots && slots_[slot].tlas ? slots_[slot].tlas->GetGPUVirtualAddress() : 0;
}

ID3D12Resource* DxrGpuScene::vertex_buffer(unsigned slot) const {
  return slot < kFrameSlots ? slots_[slot].vertices.Get() : nullptr;
}

ID3D12Resource* DxrGpuScene::index_buffer(unsigned slot) const {
  return slot < kFrameSlots ? slots_[slot].indices.Get() : nullptr;
}

}  // namespace gx
