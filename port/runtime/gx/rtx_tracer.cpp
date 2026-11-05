// The path tracer's ray-traced passes (rtx_tracer.h).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "rtx_tracer.h"

#include "rtx_scene.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace gx {
namespace {

using Microsoft::WRL::ComPtr;

enum TraceRoot : UINT {
  kRootFrame = 0,      // b0 FrameConstants (root CBV)
  kRootScene = 1,      // t0 the top-level structure (root SRV)
  kRootVertices = 2,   // t1 RtVertex (root SRV)
  kRootRecords = 3,    // t2 GeometryRecord (root SRV)
  kRootOutput = 4,     // u0 table
  kRootTextures = 5,   // t0 space1, unbounded table from the heap's start
  kRootGbuffer = 6,    // t3..t5 the G-buffer
  kRootCount
};

// cbuffer FrameConstants in rtx_trace.hlsl.
struct FrameConstants {
  float view_to_world[12];
  float clip_to_view[16];
  float viewport[4];
  uint32_t output_size[2];
  uint32_t mode;
  uint32_t orthographic;
};
static_assert(sizeof(FrameConstants) == 144, "FrameConstants layout");

constexpr UINT64 kRecordStride = 64;   // a 32-byte identifier, rounded to the table alignment
constexpr UINT kHitRecords = kRtxClassCount * kRtxRayTypes;
constexpr DXGI_FORMAT kOutputFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr D3D12_RESOURCE_STATES kShaderRead =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

bool read_binary(const std::wstring& path, std::vector<uint8_t>& out) {
  std::ifstream file(std::filesystem::path(path), std::ios::binary | std::ios::ate);
  if (!file) return false;
  const std::streamsize length = file.tellg();
  if (length <= 0 || length > (std::streamsize)(64 * 1024 * 1024)) return false;
  out.resize((size_t)length);
  file.seekg(0);
  return (bool)file.read((char*)out.data(), length);
}

bool make_buffer(ID3D12Device* device, ComPtr<ID3D12Resource>& out, uint64_t bytes, uint8_t** mapped) {
  D3D12_HEAP_PROPERTIES hp{};
  hp.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC d{};
  d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  d.Width = (bytes + 255) & ~uint64_t(255);
  d.Height = 1;
  d.DepthOrArraySize = 1;
  d.MipLevels = 1;
  d.SampleDesc.Count = 1;
  d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                             IID_PPV_ARGS(&out))))
    return false;
  D3D12_RANGE none{0, 0};
  if (FAILED(out->Map(0, &none, (void**)mapped))) {
    out.Reset();
    return false;
  }
  return true;
}

}  // namespace

bool rtx_invert4x4(const float m[16], float out[16]) {
  float inv[16];
  inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
  inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
  inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
  inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
  inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
  inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
  inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
  inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
  inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
  inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
  inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
  inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
  inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
  inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
  inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
  inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
  const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
  if (!std::isfinite(det) || std::fabs(det) < 1e-30f) return false;
  const float k = 1.0f / det;
  for (int i = 0; i < 16; ++i) {
    out[i] = inv[i] * k;
    if (!std::isfinite(out[i])) return false;
  }
  return true;
}

bool RtxTracer::initialize(ID3D12Device5* device, const std::wstring& exe_dir, std::string* error) {
  if (error) error->clear();
  if (!device) {
    if (error) *error = "no ray-tracing device";
    return false;
  }
  D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
  if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof options)) ||
      options.RaytracingTier < D3D12_RAYTRACING_TIER_1_1) {
    if (error) *error = "the adapter does not support DXR 1.1";
    return false;
  }
  device_ = device;
  std::vector<uint8_t> dxil;
  if (!read_binary(exe_dir + L"\\rtx_trace.dxil", dxil)) {
    if (error) *error = "rtx_trace.dxil is missing beside the executable";
    return false;
  }

  D3D12_DESCRIPTOR_RANGE output_range{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0};
  D3D12_DESCRIPTOR_RANGE texture_range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, ~0u, 0, 1, 0};   // unbounded
  D3D12_DESCRIPTOR_RANGE gbuffer_range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 3, 0, 0};
  D3D12_ROOT_PARAMETER params[kRootCount]{};
  params[kRootFrame].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[kRootFrame].Descriptor = {0, 0};
  params[kRootScene].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[kRootScene].Descriptor = {0, 0};
  params[kRootVertices].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[kRootVertices].Descriptor = {1, 0};
  params[kRootRecords].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[kRootRecords].Descriptor = {2, 0};
  params[kRootOutput].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[kRootOutput].DescriptorTable = {1, &output_range};
  params[kRootTextures].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[kRootTextures].DescriptorTable = {1, &texture_range};
  params[kRootGbuffer].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[kRootGbuffer].DescriptorTable = {1, &gbuffer_range};
  for (auto& p : params) p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_STATIC_SAMPLER_DESC sampler{};
  sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  sampler.MaxLOD = D3D12_FLOAT32_MAX;
  sampler.ShaderRegister = 0;
  sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC root_desc{};
  root_desc.NumParameters = kRootCount;
  root_desc.pParameters = params;
  root_desc.NumStaticSamplers = 1;
  root_desc.pStaticSamplers = &sampler;
  ComPtr<ID3DBlob> blob, diagnostics;
  HRESULT hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &diagnostics);
  if (FAILED(hr)) {
    if (error) *error = diagnostics ? (const char*)diagnostics->GetBufferPointer() : "trace root signature failed";
    return false;
  }
  hr = device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root_));
  if (FAILED(hr)) {
    if (error) *error = "trace root signature creation failed (" + std::to_string((uint32_t)hr) + ")";
    return false;
  }

  D3D12_EXPORT_DESC exports[3]{};
  exports[0].Name = L"RayGen";
  exports[1].Name = L"Miss";
  exports[2].Name = L"ClosestHit";
  D3D12_DXIL_LIBRARY_DESC library{};
  library.DXILLibrary = {dxil.data(), dxil.size()};
  library.NumExports = (UINT)std::size(exports);
  library.pExports = exports;
  D3D12_HIT_GROUP_DESC hit_group{};
  hit_group.HitGroupExport = L"HitGroup";
  hit_group.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
  hit_group.ClosestHitShaderImport = L"ClosestHit";
  D3D12_RAYTRACING_SHADER_CONFIG shader_config{};
  shader_config.MaxPayloadSizeInBytes = 20;
  shader_config.MaxAttributeSizeInBytes = 8;
  D3D12_GLOBAL_ROOT_SIGNATURE global_root{root_.Get()};
  D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config{1};
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
    root_.Reset();
    if (error) *error = "trace state object creation failed (" + std::to_string((uint32_t)hr) + ")";
    return false;
  }
  ComPtr<ID3D12StateObjectProperties> properties;
  const void *raygen = nullptr, *miss = nullptr, *hit = nullptr;
  if (FAILED(state_.As(&properties)) || !(raygen = properties->GetShaderIdentifier(L"RayGen")) ||
      !(miss = properties->GetShaderIdentifier(L"Miss")) || !(hit = properties->GetShaderIdentifier(L"HitGroup"))) {
    state_.Reset();
    root_.Reset();
    if (error) *error = "trace shader library is missing an export";
    return false;
  }
  raygen_offset_ = 0;
  miss_offset_ = kRecordStride;
  hit_offset_ = 2 * kRecordStride;
  record_stride_ = kRecordStride;
  hit_records_ = kHitRecords;
  uint8_t* table = nullptr;
  if (!make_buffer(device_.Get(), shader_table_, hit_offset_ + kHitRecords * kRecordStride, &table)) {
    state_.Reset();
    root_.Reset();
    if (error) *error = "trace shader table creation failed";
    return false;
  }
  std::memset(table, 0, (size_t)(hit_offset_ + kHitRecords * kRecordStride));
  std::memcpy(table + raygen_offset_, raygen, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
  std::memcpy(table + miss_offset_, miss, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
  for (UINT i = 0; i < kHitRecords; ++i)
    std::memcpy(table + hit_offset_ + i * kRecordStride, hit, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
  return true;
}

bool RtxTracer::debug_view(ID3D12GraphicsCommandList4* list, unsigned slot, const DebugInputs& in, std::string* error) {
  if (error) error->clear();
  if (!ready() || !list || slot >= kFrameSlots || !in.tlas || !in.vertices || !in.records || !in.width || !in.height) {
    if (error) *error = "debug view inputs are incomplete";
    return false;
  }
  Slot& s = slots_[slot];
  if (!s.constants && !make_buffer(device_.Get(), s.constants, 256, &s.constants_cpu)) {
    if (error) *error = "debug view constants could not be made";
    return false;
  }
  if (!s.output || s.output->GetDesc().Width != in.width || s.output->GetDesc().Height != in.height) {
    s.output.Reset();
    s.shader_read = false;
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = in.width;
    d.Height = in.height;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.Format = kOutputFormat;
    d.SampleDesc.Count = 1;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                nullptr, IID_PPV_ARGS(&s.output)))) {
      if (error) *error = "debug view output could not be made";
      return false;
    }
  }
  if (s.shader_read) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {s.output.Get(), 0, kShaderRead, D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    list->ResourceBarrier(1, &b);
    s.shader_read = false;
  }
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
  uav.Format = kOutputFormat;
  uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  device_->CreateUnorderedAccessView(s.output.Get(), nullptr, &uav, in.output_uav_cpu);

  FrameConstants k{};
  std::memcpy(k.view_to_world, in.view_to_world, sizeof k.view_to_world);
  std::memcpy(k.clip_to_view, in.clip_to_view, sizeof k.clip_to_view);
  std::memcpy(k.viewport, in.viewport, sizeof k.viewport);
  k.output_size[0] = in.width;
  k.output_size[1] = in.height;
  k.mode = in.mode;
  k.orthographic = in.orthographic ? 1u : 0u;
  std::memcpy(s.constants_cpu, &k, sizeof k);

  list->SetComputeRootSignature(root_.Get());
  list->SetPipelineState1(state_.Get());
  list->SetComputeRootConstantBufferView(kRootFrame, s.constants->GetGPUVirtualAddress());
  list->SetComputeRootShaderResourceView(kRootScene, in.tlas);
  list->SetComputeRootShaderResourceView(kRootVertices, in.vertices->GetGPUVirtualAddress());
  list->SetComputeRootShaderResourceView(kRootRecords, in.records->GetGPUVirtualAddress());
  list->SetComputeRootDescriptorTable(kRootOutput, in.output_uav_gpu);
  list->SetComputeRootDescriptorTable(kRootTextures, in.heap_start);
  list->SetComputeRootDescriptorTable(kRootGbuffer, in.gbuffer_srvs);
  const D3D12_GPU_VIRTUAL_ADDRESS table = shader_table_->GetGPUVirtualAddress();
  D3D12_DISPATCH_RAYS_DESC rays{};
  rays.RayGenerationShaderRecord = {table + raygen_offset_, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES};
  rays.MissShaderTable = {table + miss_offset_, record_stride_, record_stride_};
  rays.HitGroupTable = {table + hit_offset_, record_stride_ * hit_records_, record_stride_};
  rays.Width = in.width;
  rays.Height = in.height;
  rays.Depth = 1;
  list->DispatchRays(&rays);
  D3D12_RESOURCE_BARRIER b{};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition = {s.output.Get(), 0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, kShaderRead};
  list->ResourceBarrier(1, &b);
  s.shader_read = true;
  return true;
}

}  // namespace gx
