// D3D12 DXR one-bounce diffuse path tracer.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gx_dxr_path_tracer.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace gx {
namespace {
using Microsoft::WRL::ComPtr;

constexpr UINT kDescriptorCount = 8;
constexpr UINT kRecordBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
constexpr UINT kTableStride = D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT * 2;
constexpr DXGI_FORMAT kHdrFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

bool make_buffer(ID3D12Device* device, Microsoft::WRL::ComPtr<ID3D12Resource>& out,
                 uint64_t size, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_FLAGS flags,
                 std::string* error) {
  D3D12_HEAP_PROPERTIES hp{};
  hp.Type = heap;
  hp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  hp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  hp.CreationNodeMask = hp.VisibleNodeMask = 1;
  D3D12_RESOURCE_DESC d{};
  d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  d.Width = (size + 255) & ~uint64_t(255);
  d.Height = 1;
  d.DepthOrArraySize = 1;
  d.MipLevels = 1;
  d.SampleDesc.Count = 1;
  d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  d.Flags = flags;
  const HRESULT hr = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d,
      heap == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ :
                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      nullptr, IID_PPV_ARGS(&out));
  if (FAILED(hr)) {
    if (error) *error = "DXR shader-table resource creation failed (" + std::to_string((uint32_t)hr) + ")";
    return false;
  }
  return true;
}

bool make_texture(ID3D12Device* device, Microsoft::WRL::ComPtr<ID3D12Resource>& out,
                  uint32_t width, uint32_t height, std::string* error) {
  D3D12_HEAP_PROPERTIES hp{};
  hp.Type = D3D12_HEAP_TYPE_DEFAULT;
  hp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  hp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  hp.CreationNodeMask = hp.VisibleNodeMask = 1;
  D3D12_RESOURCE_DESC d{};
  d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  d.Width = width;
  d.Height = height;
  d.DepthOrArraySize = 1;
  d.MipLevels = 1;
  d.Format = kHdrFormat;
  d.SampleDesc.Count = 1;
  d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  const HRESULT hr = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&out));
  if (FAILED(hr)) {
    if (error) *error = "DXR guide texture creation failed (" + std::to_string((uint32_t)hr) + ")";
    return false;
  }
  return true;
}

D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle(ID3D12DescriptorHeap* heap, UINT size, UINT index) {
  D3D12_CPU_DESCRIPTOR_HANDLE h = heap->GetCPUDescriptorHandleForHeapStart();
  h.ptr += (SIZE_T)size * index;
  return h;
}

}  // namespace

bool DxrPathTracer::initialize(ID3D12Device5* device, const std::wstring& exe_dir,
                               std::string* error) {
  if (error) error->clear();
  if (!device) {
    if (error) *error = "DXR device is unavailable";
    return false;
  }
  device_ = device;
  const std::wstring path = exe_dir + L"\\dxr_pathtrace.dxil";
  std::ifstream file(std::filesystem::path(path), std::ios::binary | std::ios::ate);
  if (!file) {
    if (error) *error = "compiled dxr_pathtrace.dxil is missing beside the executable";
    return false;
  }
  const std::streamsize length = file.tellg();
  if (length <= 0 || length > (std::streamsize)(64 * 1024 * 1024)) {
    if (error) *error = "compiled DXR shader has an invalid size";
    return false;
  }
  std::vector<uint8_t> dxil((size_t)length);
  file.seekg(0);
  if (!file.read((char*)dxil.data(), length)) {
    if (error) *error = "could not read compiled DXR shader";
    return false;
  }
  if (!create_shader_pipeline(dxil.data(), dxil.size(), error)) return false;

  D3D12_DESCRIPTOR_HEAP_DESC heap{};
  heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap.NumDescriptors = kDescriptorCount * kFrameSlots;
  heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  const HRESULT hr = device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&descriptors_));
  if (FAILED(hr)) {
    if (error) *error = "DXR descriptor heap creation failed (" + std::to_string((uint32_t)hr) + ")";
    state_.Reset(); root_.Reset();
    return false;
  }
  descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  return true;
}

bool DxrPathTracer::create_shader_pipeline(const void* dxil, size_t dxil_size,
                                          std::string* error) {
  D3D12_DESCRIPTOR_RANGE ranges[2]{};
  ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0, 0};
  ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4, 0, 0, 4};
  D3D12_ROOT_PARAMETER parameters[2]{};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 2;
  parameters[0].DescriptorTable.pDescriptorRanges = ranges;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].Constants.ShaderRegister = 0;
  parameters[1].Constants.Num32BitValues = 12;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC root_desc{};
  root_desc.NumParameters = 2;
  root_desc.pParameters = parameters;
  ComPtr<ID3DBlob> signature, diagnostics;
  HRESULT hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                           &signature, &diagnostics);
  if (FAILED(hr)) {
    if (error) *error = diagnostics ? (const char*)diagnostics->GetBufferPointer() : "DXR root signature serialization failed";
    return false;
  }
  hr = device_->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                   IID_PPV_ARGS(&root_));
  if (FAILED(hr)) {
    if (error) *error = "DXR root signature creation failed (" + std::to_string((uint32_t)hr) + ")";
    return false;
  }

  D3D12_EXPORT_DESC exports[3]{};
  exports[0].Name = L"RayGen";
  exports[1].Name = L"Miss";
  exports[2].Name = L"ClosestHit";
  D3D12_DXIL_LIBRARY_DESC library{};
  library.DXILLibrary = {dxil, dxil_size};
  library.NumExports = (UINT)std::size(exports);
  library.pExports = exports;
  D3D12_HIT_GROUP_DESC hit_group{};
  hit_group.HitGroupExport = L"HitGroup";
  hit_group.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
  hit_group.ClosestHitShaderImport = L"ClosestHit";
  D3D12_RAYTRACING_SHADER_CONFIG shader_config{};
  shader_config.MaxPayloadSizeInBytes = 48;
  shader_config.MaxAttributeSizeInBytes = 8;
  D3D12_GLOBAL_ROOT_SIGNATURE global_root{root_.Get()};
  D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config{2};
  D3D12_STATE_SUBOBJECT subobjects[5]{};
  subobjects[0] = {D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &library};
  subobjects[1] = {D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hit_group};
  subobjects[2] = {D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &shader_config};
  subobjects[3] = {D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &global_root};
  subobjects[4] = {D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pipeline_config};
  D3D12_STATE_OBJECT_DESC state_desc{};
  state_desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
  state_desc.NumSubobjects = (UINT)std::size(subobjects);
  state_desc.pSubobjects = subobjects;
  hr = device_->CreateStateObject(&state_desc, IID_PPV_ARGS(&state_));
  if (FAILED(hr)) {
    if (error) *error = "DXR state-object creation failed (" + std::to_string((uint32_t)hr) + ")";
    root_.Reset();
    return false;
  }
  Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> properties;
  hr = state_.As(&properties);
  if (FAILED(hr)) {
    if (error) *error = "DXR state-object properties are unavailable";
    state_.Reset(); root_.Reset();
    return false;
  }
  if (!properties->GetShaderIdentifier(L"RayGen") || !properties->GetShaderIdentifier(L"Miss") ||
      !properties->GetShaderIdentifier(L"HitGroup")) {
    if (error) *error = "DXR shader library is missing a required export";
    state_.Reset(); root_.Reset();
    return false;
  }
  if (!make_buffer(device_.Get(), shader_table_, kTableStride * 3,
                   D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, error)) {
    state_.Reset(); root_.Reset();
    return false;
  }
  uint8_t* mapped = nullptr;
  D3D12_RANGE no_read{0, 0};
  if (FAILED(shader_table_->Map(0, &no_read, (void**)&mapped))) {
    if (error) *error = "DXR shader table mapping failed";
    state_.Reset(); root_.Reset(); shader_table_.Reset();
    return false;
  }
  std::memset(mapped, 0, kTableStride * 3);
  std::memcpy(mapped + 0 * kTableStride, properties->GetShaderIdentifier(L"RayGen"), kRecordBytes);
  std::memcpy(mapped + 1 * kTableStride, properties->GetShaderIdentifier(L"Miss"), kRecordBytes);
  std::memcpy(mapped + 2 * kTableStride, properties->GetShaderIdentifier(L"HitGroup"), kRecordBytes);
  D3D12_RANGE written{0, kTableStride * 3};
  shader_table_->Unmap(0, &written);
  return true;
}

bool DxrPathTracer::create_targets(Targets& t, uint32_t width, uint32_t height,
                                  std::string* error) {
  for (auto* resource : {t.color.Get(), t.albedo.Get(), t.normal_roughness.Get(), t.specular_albedo.Get()}) {
    if (resource) {
      const auto desc = resource->GetDesc();
      if (desc.Width != width || desc.Height != height) break;
      if (resource == t.normal_roughness.Get()) return true;
    }
  }
  t = {};
  if (!make_texture(device_.Get(), t.color, width, height, error) ||
      !make_texture(device_.Get(), t.albedo, width, height, error) ||
      !make_texture(device_.Get(), t.normal_roughness, width, height, error) ||
      !make_texture(device_.Get(), t.specular_albedo, width, height, error))
    return false;
  return true;
}

bool DxrPathTracer::dispatch(ID3D12GraphicsCommandList4* list, unsigned slot,
                             const DxrGpuScene& scene, const DxrScene& cpu_scene,
                             ID3D12Resource* raster_color, uint32_t width, uint32_t height,
                             uint32_t frame_index, float bounce_intensity, uint32_t samples, uint32_t max_bounces,
                             std::string* error) {
  if (error) error->clear();
  if (!ready() || !list || slot >= kFrameSlots || !scene.top_level(slot) ||
      !scene.vertex_buffer(slot) || !scene.index_buffer(slot) || !raster_color || !width || !height) {
    if (error) *error = "DXR path-tracing inputs are incomplete";
    return false;
  }
  Targets& t = targets_[slot];
  if (!create_targets(t, width, height, error)) return false;
  if (t.shader_read) {
    D3D12_RESOURCE_BARRIER barriers[4]{};
    ID3D12Resource* resources[] = {t.color.Get(), t.albedo.Get(), t.normal_roughness.Get(), t.specular_albedo.Get()};
    for (int i = 0; i < 4; ++i) {
      barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barriers[i].Transition = {resources[i], 0,
          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
          D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    }
    list->ResourceBarrier(4, barriers);
    t.shader_read = false;
  }
  const UINT base = slot * kDescriptorCount;
  D3D12_SHADER_RESOURCE_VIEW_DESC as_srv{};
  as_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  as_srv.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
  as_srv.RaytracingAccelerationStructure.Location = scene.top_level(slot);
  device_->CreateShaderResourceView(nullptr, &as_srv, cpu_handle(descriptors_.Get(), descriptor_size_, base + 0));
  D3D12_SHADER_RESOURCE_VIEW_DESC structured{};
  structured.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  structured.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  structured.Format = DXGI_FORMAT_UNKNOWN;
  structured.Buffer.FirstElement = 0;
  structured.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
  structured.Buffer.StructureByteStride = sizeof(DxrSceneVertex);
  structured.Buffer.NumElements = (UINT)cpu_scene.vertices.size();
  device_->CreateShaderResourceView(scene.vertex_buffer(slot), &structured,
                                    cpu_handle(descriptors_.Get(), descriptor_size_, base + 1));
  structured.Buffer.StructureByteStride = sizeof(uint32_t);
  structured.Buffer.NumElements = (UINT)cpu_scene.indices.size();
  device_->CreateShaderResourceView(scene.index_buffer(slot), &structured,
                                    cpu_handle(descriptors_.Get(), descriptor_size_, base + 2));
  device_->CreateShaderResourceView(raster_color, nullptr,
                                    cpu_handle(descriptors_.Get(), descriptor_size_, base + 3));
  ID3D12Resource* outputs[] = {t.color.Get(), t.albedo.Get(), t.normal_roughness.Get(), t.specular_albedo.Get()};
  for (UINT i = 0; i < 4; ++i) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = kHdrFormat;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device_->CreateUnorderedAccessView(outputs[i], nullptr, &uav,
                                      cpu_handle(descriptors_.Get(), descriptor_size_, base + 4 + i));
  }

  ID3D12DescriptorHeap* heap = descriptors_.Get();
  list->SetDescriptorHeaps(1, &heap);
  list->SetPipelineState1(state_.Get());
  list->SetComputeRootSignature(root_.Get());
  D3D12_GPU_DESCRIPTOR_HANDLE table = descriptors_->GetGPUDescriptorHandleForHeapStart();
  table.ptr += (UINT64)base * descriptor_size_;
  list->SetComputeRootDescriptorTable(0, table);
  uint32_t constants[12]{};
  constants[0] = width;
  constants[1] = height;
  constants[2] = frame_index;
  constants[3] = std::clamp(samples, 1u, 4u);
  std::memcpy(constants + 4, cpu_scene.gx_projection, 6 * sizeof(float));
  constants[10] = *reinterpret_cast<const uint32_t*>(&bounce_intensity);
  constants[11] = std::clamp(max_bounces, 1u, 4u);
  list->SetComputeRoot32BitConstants(1, 12, constants, 0);

  const D3D12_GPU_VIRTUAL_ADDRESS start = shader_table_->GetGPUVirtualAddress();
  D3D12_DISPATCH_RAYS_DESC dispatch{};
  dispatch.RayGenerationShaderRecord = {start, kTableStride};
  dispatch.MissShaderTable = {start + kTableStride, kTableStride, kTableStride};
  dispatch.HitGroupTable = {start + kTableStride * 2, kTableStride, kTableStride};
  dispatch.Width = width;
  dispatch.Height = height;
  dispatch.Depth = 1;
  list->DispatchRays(&dispatch);
  D3D12_RESOURCE_BARRIER barriers[4]{};
  for (int i = 0; i < 4; ++i) {
    barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[i].Transition = {outputs[i], 0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
  }
  list->ResourceBarrier(4, barriers);
  t.shader_read = true;
  return true;
}

ID3D12Resource* DxrPathTracer::color(unsigned slot) const {
  return slot < kFrameSlots ? targets_[slot].color.Get() : nullptr;
}
ID3D12Resource* DxrPathTracer::albedo(unsigned slot) const {
  return slot < kFrameSlots ? targets_[slot].albedo.Get() : nullptr;
}
ID3D12Resource* DxrPathTracer::normal_roughness(unsigned slot) const {
  return slot < kFrameSlots ? targets_[slot].normal_roughness.Get() : nullptr;
}
ID3D12Resource* DxrPathTracer::specular_albedo(unsigned slot) const {
  return slot < kFrameSlots ? targets_[slot].specular_albedo.Get() : nullptr;
}

}  // namespace gx
