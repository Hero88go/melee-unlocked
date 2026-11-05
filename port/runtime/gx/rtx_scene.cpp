// GPU scene for the path tracer (rtx_scene.h).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "rtx_scene.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace gx {
namespace {

using Microsoft::WRL::ComPtr;

// Root parameters of the transform pass (shaders/rtx_transform.hlsl).
enum TransformRoot : UINT {
  kRootVS = 0,        // b0: the draw's VSConstants, root CBV
  kRootDraw = 1,      // b1: TransformConstants, root constants
  kRootFrame = 2,     // b2: view_to_world rows, root CBV
  kRootVertices = 3,  // t0: raw gx::Vertex array, root SRV
  kRootIndices = 4,   // t1: triangle-list indices, root SRV
  kRootOutput = 5,    // u0: RtVertex soup, root UAV
  kRootCount
};

// cbuffer DrawBlock in rtx_transform.hlsl.
struct TransformConstants {
  uint32_t index_count, output_base, components, num_texgens, dual_tex, chan_color0, chan_alpha0, draw_flags;
  uint32_t texgen[4], post[4];
};
static_assert(sizeof(TransformConstants) == 64, "DrawBlock layout");
constexpr UINT kTransformConstantWords = sizeof(TransformConstants) / 4;

constexpr uint8_t kClassMask[kRtxClassCount] = {kRtxMaskOpaque, kRtxMaskAlphaTested, kRtxMaskTranslucent};

uint64_t align_up(uint64_t value, uint64_t alignment) { return (value + alignment - 1) & ~(alignment - 1); }

void uav_barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource) {
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  barrier.UAV.pResource = resource;
  list->ResourceBarrier(1, &barrier);
}

void transition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES& state,
                D3D12_RESOURCE_STATES after) {
  if (state == after) return;
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, state, after};
  list->ResourceBarrier(1, &barrier);
  state = after;
}

bool read_binary(const std::wstring& path, std::vector<uint8_t>& out) {
  std::ifstream file(std::filesystem::path(path), std::ios::binary | std::ios::ate);
  if (!file) return false;
  const std::streamsize length = file.tellg();
  if (length <= 0 || length > (std::streamsize)(16 * 1024 * 1024)) return false;
  out.resize((size_t)length);
  file.seekg(0);
  return (bool)file.read((char*)out.data(), length);
}

}  // namespace

bool rtx_invert_affine(const float m[12], float out[12]) {
  const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
  const float c00 = e * i - f * h, c01 = -(d * i - f * g), c02 = d * h - e * g;
  const float det = a * c00 + b * c01 + c * c02;
  if (!std::isfinite(det) || std::fabs(det) < 1e-20f) return false;
  const float k = 1.0f / det;
  // Rows of the inverse of the 3x3 part: the transposed cofactors over the determinant.
  const float r[9] = {c00 * k, -(b * i - c * h) * k, (b * f - c * e) * k,
                      c01 * k, (a * i - c * g) * k, -(a * f - c * d) * k,
                      c02 * k, -(a * h - b * g) * k, (a * e - b * d) * k};
  for (int row = 0; row < 3; ++row) {
    out[row * 4 + 0] = r[row * 3 + 0];
    out[row * 4 + 1] = r[row * 3 + 1];
    out[row * 4 + 2] = r[row * 3 + 2];
    out[row * 4 + 3] = -(r[row * 3 + 0] * m[3] + r[row * 3 + 1] * m[7] + r[row * 3 + 2] * m[11]);
  }
  for (int n = 0; n < 12; ++n)
    if (!std::isfinite(out[n])) return false;
  return true;
}

bool RtxScene::initialize(ID3D12Device5* device, const std::wstring& exe_dir, std::string* error) {
  if (error) error->clear();
  if (!device) {
    if (error) *error = "no ray-tracing device";
    return false;
  }
  device_ = device;
  std::vector<uint8_t> dxil;
  if (!read_binary(exe_dir + L"\\rtx_transform.dxil", dxil)) {
    if (error) *error = "rtx_transform.dxil is missing beside the executable";
    return false;
  }
  D3D12_ROOT_PARAMETER params[kRootCount]{};
  params[kRootVS].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[kRootVS].Descriptor = {0, 0};
  params[kRootDraw].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[kRootDraw].Constants = {1, 0, kTransformConstantWords};
  params[kRootFrame].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[kRootFrame].Descriptor = {2, 0};
  params[kRootVertices].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[kRootVertices].Descriptor = {0, 0};
  params[kRootIndices].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[kRootIndices].Descriptor = {1, 0};
  params[kRootOutput].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  params[kRootOutput].Descriptor = {0, 0};
  for (auto& p : params) p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC root_desc{};
  root_desc.NumParameters = kRootCount;
  root_desc.pParameters = params;
  ComPtr<ID3DBlob> blob, diagnostics;
  HRESULT hr = D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &diagnostics);
  if (FAILED(hr)) {
    if (error) *error = diagnostics ? (const char*)diagnostics->GetBufferPointer() : "transform root signature failed";
    return false;
  }
  hr = device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&transform_root_));
  if (FAILED(hr)) {
    if (error) *error = "transform root signature creation failed (" + std::to_string((uint32_t)hr) + ")";
    return false;
  }
  D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
  pso.pRootSignature = transform_root_.Get();
  pso.CS = {dxil.data(), dxil.size()};
  hr = device_->CreateComputePipelineState(&pso, IID_PPV_ARGS(&transform_pso_));
  if (FAILED(hr)) {
    transform_root_.Reset();
    if (error) *error = "transform pipeline creation failed (" + std::to_string((uint32_t)hr) + ")";
    return false;
  }
  return true;
}

bool RtxScene::ensure_buffer(ComPtr<ID3D12Resource>& resource, uint64_t bytes, D3D12_HEAP_TYPE heap,
                             D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags, uint8_t** mapped,
                             std::string* error) {
  bytes = align_up(std::max<uint64_t>(bytes, 256), D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
  if (resource && resource->GetDesc().Width >= bytes) return true;
  // Growth keeps headroom so a busier scene a few frames later does not reallocate again.
  const uint64_t size = align_up(bytes + bytes / 2, 65536);
  resource.Reset();
  if (mapped) *mapped = nullptr;
  D3D12_HEAP_PROPERTIES hp{};
  hp.Type = heap;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = size;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  desc.Flags = flags;
  const HRESULT hr = device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                                      IID_PPV_ARGS(&resource));
  if (FAILED(hr)) {
    if (error) *error = "ray-tracing buffer of " + std::to_string(size) + " bytes failed (" + std::to_string((uint32_t)hr) + ")";
    return false;
  }
  if (mapped) {
    D3D12_RANGE none{0, 0};
    if (FAILED(resource->Map(0, &none, (void**)mapped))) {
      resource.Reset();
      if (error) *error = "ray-tracing upload buffer could not be mapped";
      return false;
    }
  }
  return true;
}

bool RtxScene::build(ID3D12GraphicsCommandList4* list, unsigned slot, const float view_to_world[12],
                     uint32_t heap_generation, std::string* error) {
  const auto started = std::chrono::steady_clock::now();
  if (error) error->clear();
  if (!ready() || !list || slot >= kFrameSlots) {
    if (error) *error = "the ray-tracing scene is not initialised";
    return false;
  }
  Slot& s = slots_[slot];
  s.built = false;

  // Draws in class order: each class is one bottom-level structure whose geometries are contiguous
  // records, so the hit shaders find theirs at InstanceID() + GeometryIndex().
  order_.clear();
  for (unsigned cls = 0; cls < kRtxClassCount; ++cls)
    for (uint32_t i = 0; i < (uint32_t)draws_.size(); ++i)
      if (draws_[i].cls == cls && draws_[i].index_count >= 3 && draws_[i].vertices && draws_[i].indices &&
          draws_[i].vs_constants)
        order_.push_back(i);
  if (order_.empty()) {
    if (error) *error = "no scene draws this present";
    return false;
  }
  uint64_t total_vertices = 0;
  for (uint32_t i : order_) total_vertices += draws_[i].index_count - draws_[i].index_count % 3;

  // A rebuilt vertex buffer starts in the UAV state it was created in.
  const bool vertices_existed = s.vertices != nullptr;
  ID3D12Resource* old_vertices = s.vertices.Get();
  if (!ensure_buffer(s.vertices, total_vertices * kRtxVertexStride, D3D12_HEAP_TYPE_DEFAULT,
                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, nullptr, error) ||
      !ensure_buffer(s.records, order_.size() * sizeof(RtxGeometryRecord), D3D12_HEAP_TYPE_UPLOAD,
                     D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE, &s.records_cpu, error) ||
      !ensure_buffer(s.frame_constants, 256, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                     D3D12_RESOURCE_FLAG_NONE, &s.constants_cpu, error) ||
      !ensure_buffer(s.instances, kRtxClassCount * sizeof(D3D12_RAYTRACING_INSTANCE_DESC), D3D12_HEAP_TYPE_UPLOAD,
                     D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_FLAG_NONE, &s.instances_cpu, error))
    return false;
  if (!vertices_existed || old_vertices != s.vertices.Get()) s.vertices_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

  // 1. The raster's transform, once per index: world-space triangles.
  std::memset(s.constants_cpu, 0, 256);
  std::memcpy(s.constants_cpu, view_to_world, 12 * sizeof(float));
  transition(list, s.vertices.Get(), s.vertices_state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  list->SetComputeRootSignature(transform_root_.Get());
  list->SetPipelineState(transform_pso_.Get());
  list->SetComputeRootConstantBufferView(kRootFrame, s.frame_constants->GetGPUVirtualAddress());
  list->SetComputeRootUnorderedAccessView(kRootOutput, s.vertices->GetGPUVirtualAddress());
  RtxGeometryRecord* records = (RtxGeometryRecord*)s.records_cpu;
  uint32_t base = 0;
  uint64_t triangles = 0;
  for (size_t n = 0; n < order_.size(); ++n) {
    const RtxDrawInput& d = draws_[order_[n]];
    const uint32_t count = d.index_count - d.index_count % 3;
    TransformConstants k{};
    k.index_count = count;
    k.output_base = base;
    k.components = d.components;
    k.num_texgens = d.num_texgens;
    k.dual_tex = d.dual_tex;
    k.chan_color0 = d.chan_color0;
    k.chan_alpha0 = d.chan_alpha0;
    std::memcpy(k.texgen, d.texgen, sizeof k.texgen);
    std::memcpy(k.post, d.post, sizeof k.post);
    list->SetComputeRootConstantBufferView(kRootVS, d.vs_constants);
    list->SetComputeRoot32BitConstants(kRootDraw, kTransformConstantWords, &k, 0);
    list->SetComputeRootShaderResourceView(kRootVertices, d.vertices);
    list->SetComputeRootShaderResourceView(kRootIndices, d.indices);
    list->Dispatch((count + 63) / 64, 1, 1);
    RtxGeometryRecord& r = records[n];
    r.first_vertex = base;
    r.triangle_count = count / 3;
    r.draw_index = d.draw_index;
    r.material = d.material;
    r.texture_set = d.texture_heap == heap_generation ? d.texture_set : kRtxNoTextures;
    r.flags = d.flags;
    r.reserved[0] = r.reserved[1] = 0;
    base += count;
    triangles += count / 3;
  }
  uav_barrier(list, s.vertices.Get());
  transition(list, s.vertices.Get(), s.vertices_state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

  // 2. One bottom-level structure per class, a geometry per draw (non-indexed triangles).
  const D3D12_GPU_VIRTUAL_ADDRESS vertex_base = s.vertices->GetGPUVirtualAddress();
  geometry_descs_.resize(order_.size());
  uint32_t first_of_class[kRtxClassCount]{}, count_of_class[kRtxClassCount]{};
  for (size_t n = 0; n < order_.size(); ++n) {
    const RtxDrawInput& d = draws_[order_[n]];
    if (count_of_class[d.cls]++ == 0) first_of_class[d.cls] = (uint32_t)n;
    D3D12_RAYTRACING_GEOMETRY_DESC& g = geometry_descs_[n];
    g = {};
    g.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    g.Flags = d.cls == kRtxOpaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE
                                  : D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION;
    g.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
    g.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    g.Triangles.VertexCount = records[n].triangle_count * 3;
    g.Triangles.VertexBuffer.StartAddress = vertex_base + (uint64_t)records[n].first_vertex * kRtxVertexStride;
    g.Triangles.VertexBuffer.StrideInBytes = kRtxVertexStride;
  }
  D3D12_RAYTRACING_INSTANCE_DESC instances[kRtxClassCount]{};
  UINT instance_count = 0;
  for (unsigned cls = 0; cls < kRtxClassCount; ++cls) {
    if (!count_of_class[cls]) continue;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.NumDescs = count_of_class[cls];
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.pGeometryDescs = geometry_descs_.data() + first_of_class[cls];
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
    device_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
    if (!info.ResultDataMaxSizeInBytes) {
      if (error) *error = "the driver gave no size for a bottom-level structure";
      return false;
    }
    if (!ensure_buffer(s.blas[cls], info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, nullptr, error) ||
        !ensure_buffer(s.blas_scratch[cls], info.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, nullptr, error))
      return false;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
    build.Inputs = inputs;
    build.DestAccelerationStructureData = s.blas[cls]->GetGPUVirtualAddress();
    build.ScratchAccelerationStructureData = s.blas_scratch[cls]->GetGPUVirtualAddress();
    list->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
    D3D12_RAYTRACING_INSTANCE_DESC& inst = instances[instance_count++];
    inst.Transform[0][0] = inst.Transform[1][1] = inst.Transform[2][2] = 1.0f;   // already world space
    inst.InstanceID = first_of_class[cls];
    inst.InstanceMask = kClassMask[cls];
    inst.InstanceContributionToHitGroupIndex = cls * kRtxRayTypes;
    // Melee's surfaces wind both ways and many are drawn without culling: rays see both sides.
    inst.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
    inst.AccelerationStructure = s.blas[cls]->GetGPUVirtualAddress();
  }
  for (unsigned cls = 0; cls < kRtxClassCount; ++cls)
    if (s.blas[cls] && count_of_class[cls]) uav_barrier(list, s.blas[cls].Get());

  // 3. The top level: one instance per non-empty class.
  std::memcpy(s.instances_cpu, instances, instance_count * sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS top{};
  top.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  top.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  top.NumDescs = instance_count;
  top.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  top.InstanceDescs = s.instances->GetGPUVirtualAddress();
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO top_info{};
  device_->GetRaytracingAccelerationStructurePrebuildInfo(&top, &top_info);
  if (!top_info.ResultDataMaxSizeInBytes) {
    if (error) *error = "the driver gave no size for the top-level structure";
    return false;
  }
  if (!ensure_buffer(s.tlas, top_info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                     D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                     D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, nullptr, error) ||
      !ensure_buffer(s.tlas_scratch, top_info.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, nullptr, error))
    return false;
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC top_build{};
  top_build.Inputs = top;
  top_build.DestAccelerationStructureData = s.tlas->GetGPUVirtualAddress();
  top_build.ScratchAccelerationStructureData = s.tlas_scratch->GetGPUVirtualAddress();
  list->BuildRaytracingAccelerationStructure(&top_build, 0, nullptr);
  uav_barrier(list, s.tlas.Get());

  s.vertex_count = base;
  s.geometry_count = (uint32_t)order_.size();
  s.built = true;
  ++stats_.builds;
  stats_.draws += order_.size();
  stats_.triangles += triangles;
  stats_.record_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
  return true;
}

D3D12_GPU_VIRTUAL_ADDRESS RtxScene::tlas(unsigned slot) const {
  return slot < kFrameSlots && slots_[slot].built && slots_[slot].tlas ? slots_[slot].tlas->GetGPUVirtualAddress() : 0;
}
ID3D12Resource* RtxScene::vertices(unsigned slot) const {
  return slot < kFrameSlots && slots_[slot].built ? slots_[slot].vertices.Get() : nullptr;
}
ID3D12Resource* RtxScene::geometry_records(unsigned slot) const {
  return slot < kFrameSlots && slots_[slot].built ? slots_[slot].records.Get() : nullptr;
}
uint32_t RtxScene::vertex_count(unsigned slot) const {
  return slot < kFrameSlots && slots_[slot].built ? slots_[slot].vertex_count : 0;
}
uint32_t RtxScene::geometry_count(unsigned slot) const {
  return slot < kFrameSlots && slots_[slot].built ? slots_[slot].geometry_count : 0;
}

}  // namespace gx
