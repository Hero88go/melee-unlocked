// The Melee integrator (rtx_integrator.h): this project's own lighting for the path tracer. First
// stage: direct light from the game's lights with ray-traced soft shadows and ambient occlusion
// (shaders/rtx_direct.hlsl). ReSTIR and the radiance cache build on it.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "rtx_integrator.h"
#include "test_env.h"

#include <wrl/client.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace gx {
namespace {

using Microsoft::WRL::ComPtr;

enum Root : UINT { kFrame = 0, kScene, kLights, kDrawTable, kRecords, kInputs, kOutputs, kVertices, kTextures, kDrawColour, kPass, kRootCount };
// The average on-screen albedo of each draw (rtx_direct.hlsl DrawColour): sums of r, g, b and a count.
constexpr UINT kMaxDrawColours = 16384;

struct FrameConstants {        // cbuffer Frame in rtx_direct.hlsl
  float view_to_world[12];
  float clip_to_view[16];
  float viewport[4];
  uint32_t size[2];
  uint32_t frame_index;
  uint32_t light_count;
  uint32_t draw_count;
  uint32_t orthographic;
  float ao_radius;
  float light_scale;
  uint32_t main_light_count;
  float look;
  uint32_t switches;          // 1 shadows, 2 ambient occlusion, 4 fighters' shadows on baked stages
  uint32_t still_frames;      // photo mode
  float reflection_strength, floor_strength, shadow_strength, bounce_strength;
};
static_assert(sizeof(FrameConstants) == 192, "Frame layout");

constexpr DXGI_FORMAT kOutputFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr D3D12_RESOURCE_STATES kShaderRead =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr unsigned kSlots = 3;
constexpr UINT kInputCount = 6;   // G-buffer albedo, normal + depth, id; raster; overlay; transmittance
constexpr UINT kOutputCount = 5;  // colour; Ray Reconstruction's diffuse albedo, specular albedo, normal + roughness; photo-mode accumulation
constexpr UINT kSlotTargets = 4;  // the per-slot ones (the accumulation is one, kept across presents)

bool read_binary(const std::wstring& path, std::vector<uint8_t>& out) {
  std::ifstream file(std::filesystem::path(path), std::ios::binary | std::ios::ate);
  if (!file) return false;
  const std::streamsize length = file.tellg();
  if (length <= 0 || length > (std::streamsize)(16 * 1024 * 1024)) return false;
  out.resize((size_t)length);
  file.seekg(0);
  return (bool)file.read((char*)out.data(), length);
}

class MeleeIntegrator final : public RtxIntegrator {
 public:
  const char* name() const override { return "Melee"; }

  bool initialize(ID3D12Device5* device, const std::wstring& exe_dir, std::string* error) override {
    if (error) error->clear();
    device_ = device;
    std::vector<uint8_t> dxil;
    if (!device_ || !read_binary(exe_dir + L"\\rtx_direct.dxil", dxil)) {
      if (error) *error = "rtx_direct.dxil is missing beside the executable";
      return false;
    }
    D3D12_DESCRIPTOR_RANGE inputs{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, kInputCount, 4, 0, 0};   // t4..t9
    D3D12_DESCRIPTOR_RANGE outputs{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, kOutputCount, 0, 0, 0};   // u0 colour, u1..u3 RR guides
    D3D12_ROOT_PARAMETER p[kRootCount]{};
    p[kFrame].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    p[kFrame].Descriptor = {0, 0};
    const UINT srv_registers[] = {0, 1, 2, 3};
    const Root srv_roots[] = {kScene, kLights, kDrawTable, kRecords};
    for (int i = 0; i < 4; ++i) {
      p[srv_roots[i]].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
      p[srv_roots[i]].Descriptor = {srv_registers[i], 0};
    }
    p[kInputs].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    p[kInputs].DescriptorTable = {1, &inputs};
    p[kOutputs].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    p[kOutputs].DescriptorTable = {1, &outputs};
    // Surfaces rays hit: the scene's vertices and every texture in the renderer's heap.
    p[kVertices].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    p[kVertices].Descriptor = {10, 0};
    D3D12_DESCRIPTOR_RANGE textures{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 1, 0};
    p[kTextures].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    p[kTextures].DescriptorTable = {1, &textures};
    p[kDrawColour].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    p[kDrawColour].Descriptor = {5, 0};   // u5
    p[kPass].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    p[kPass].Constants = {1, 0, 1};       // b1: which pass of the shader runs
    for (auto& x : p) x.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rd{};
    rd.NumParameters = kRootCount;
    rd.pParameters = p;
    rd.NumStaticSamplers = 1;
    rd.pStaticSamplers = &sampler;
    ComPtr<ID3DBlob> blob, diag;
    if (FAILED(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &diag)) ||
        FAILED(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root_)))) {
      if (error) *error = diag ? (const char*)diag->GetBufferPointer() : "direct-light root signature failed";
      return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = root_.Get();
    pd.CS = {dxil.data(), dxil.size()};
    if (FAILED(device_->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso_)))) {
      root_.Reset();
      if (error) *error = "direct-light pipeline creation failed";
      return false;
    }
    return true;
  }

  bool render(const RtxFrameInputs& in, RtxFrameOutputs* out, std::string* error) override {
    if (error) error->clear();
    if (!pso_ || !in.list || in.slot >= kSlots || !in.tlas || !in.lights || !in.draws || !in.records || !in.vertices ||
        !in.albedo || !in.normal_depth || !in.draw_id || !in.raster_color || !in.overlay || !in.transmittance || !in.heap ||
        in.free_slot_count < kInputCount + kOutputCount ||
        !in.width || !in.height) {
      if (error) *error = "direct-light inputs are incomplete";
      return false;
    }
    Slot& s = slots_[in.slot];
    if (!draw_colours_) {
      D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
      D3D12_RESOURCE_DESC d{};
      d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = (UINT64)kMaxDrawColours * 16; d.Height = 1; d.DepthOrArraySize = 1;
      d.MipLevels = 1; d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
      if (FAILED(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                  IID_PPV_ARGS(&draw_colours_)))) {
        if (error) *error = "draw colour buffer could not be made";
        return false;
      }
    }
    if (!s.constants) {
      D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_UPLOAD};
      D3D12_RESOURCE_DESC d{};
      d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = 256; d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1;
      d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      D3D12_RANGE none{0, 0};
      if (FAILED(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                  IID_PPV_ARGS(&s.constants))) ||
          FAILED(s.constants->Map(0, &none, (void**)&s.constants_cpu))) {
        s.constants.Reset();
        if (error) *error = "direct-light constants could not be made";
        return false;
      }
    }
    if (!accum_ || accum_->GetDesc().Width != in.width || accum_->GetDesc().Height != in.height) {
      accum_.Reset();
      D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
      D3D12_RESOURCE_DESC d{};
      d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = in.width; d.Height = in.height; d.DepthOrArraySize = 1;
      d.MipLevels = 1; d.Format = kOutputFormat; d.SampleDesc.Count = 1; d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
      if (FAILED(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                  IID_PPV_ARGS(&accum_)))) {
        if (error) *error = "photo-mode accumulation could not be made";
        return false;
      }
    }
    if (!s.targets[0] || s.targets[0]->GetDesc().Width != in.width || s.targets[0]->GetDesc().Height != in.height) {
      for (auto& t : s.targets) t.Reset();
      s.shader_read = false;
      D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
      D3D12_RESOURCE_DESC d{};
      d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = in.width; d.Height = in.height; d.DepthOrArraySize = 1;
      d.MipLevels = 1; d.Format = kOutputFormat; d.SampleDesc.Count = 1; d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
      for (auto& t : s.targets)
        if (FAILED(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                    IID_PPV_ARGS(&t)))) {
          for (auto& x : s.targets) x.Reset();
          if (error) *error = "direct-light outputs could not be made";
          return false;
        }
    }
    if (s.shader_read) {
      D3D12_RESOURCE_BARRIER b[kSlotTargets]{};
      for (UINT i = 0; i < kSlotTargets; ++i) {
        b[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b[i].Transition = {s.targets[i].Get(), 0, kShaderRead, D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
      }
      in.list->ResourceBarrier(kSlotTargets, b);
      s.shader_read = false;
    }

    // Views in the renderer's heap: t4..t9 then u0.
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = in.heap->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += (SIZE_T)in.free_slot * in.descriptor_size;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = in.heap->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += (UINT64)in.free_slot * in.descriptor_size;
    ID3D12Resource* srvs[kInputCount] = {in.albedo, in.normal_depth, in.draw_id, in.raster_color, in.overlay, in.transmittance};
    for (UINT i = 0; i < kInputCount; ++i) {
      D3D12_CPU_DESCRIPTOR_HANDLE h = cpu;
      h.ptr += (SIZE_T)i * in.descriptor_size;
      device_->CreateShaderResourceView(srvs[i], nullptr, h);
    }
    D3D12_CPU_DESCRIPTOR_HANDLE uav_cpu = cpu;
    uav_cpu.ptr += (SIZE_T)kInputCount * in.descriptor_size;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = kOutputFormat;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    for (UINT i = 0; i < kOutputCount; ++i) {
      D3D12_CPU_DESCRIPTOR_HANDLE h = uav_cpu;
      h.ptr += (SIZE_T)i * in.descriptor_size;
      device_->CreateUnorderedAccessView(i < kSlotTargets ? s.targets[i].Get() : accum_.Get(), nullptr, &uav, h);
    }
    D3D12_GPU_DESCRIPTOR_HANDLE uav_gpu = gpu;
    uav_gpu.ptr += (UINT64)kInputCount * in.descriptor_size;

    FrameConstants k{};
    std::memcpy(k.view_to_world, in.view_to_world, sizeof k.view_to_world);
    std::memcpy(k.clip_to_view, in.clip_to_view, sizeof k.clip_to_view);
    std::memcpy(k.viewport, in.viewport, sizeof k.viewport);
    k.size[0] = in.width;
    k.size[1] = in.height;
    k.frame_index = (uint32_t)in.frame_index;
    k.light_count = in.light_count;
    k.draw_count = in.draw_count;
    k.orthographic = in.orthographic ? 1u : 0u;
    // Contact-range occlusion: until bounce light comes in, a long reach turned walls facing a
    // neighbouring building black.
    k.ao_radius = 8.0f;
    k.light_scale = in.light_scale;
    k.main_light_count = in.main_light_count;
    k.look = in.look;
    k.still_frames = in.still_frames;
    k.reflection_strength = in.reflection_strength;
    k.floor_strength = in.floor_strength;
    k.shadow_strength = in.shadow_strength;
    k.bounce_strength = in.bounce_strength;
    k.switches = (in.shadows ? 1u : 0u) | (in.ambient_occlusion ? 2u : 0u) | (in.fighter_shadows ? 4u : 0u) |
                 (in.bounce ? 8u : 0u) | (in.reflections ? 16u : 0u) | (in.skin ? 32u : 0u) |
                 (in.stage_shade ? 64u : 0u) | (in.glossy_floors ? 128u : 0u);
    // Test runs only (MELEE_TEST_RTX_ALBEDO=1): the G-buffer albedo as the picture.
    static const bool show_albedo = host::test_env::getenv("MELEE_TEST_RTX_ALBEDO") != nullptr;
    if (show_albedo) k.switches |= 1u << 30;
    static const bool no_highlight = host::test_env::getenv("MELEE_TEST_RTX_NO_HIGHLIGHT") != nullptr;
    if (no_highlight) k.switches |= 1u << 29;
    static const bool show_draw_colour = host::test_env::getenv("MELEE_TEST_RTX_DRAW_COLOUR") != nullptr;
    if (show_draw_colour) k.switches |= 1u << 28;
    std::memcpy(s.constants_cpu, &k, sizeof k);

    in.list->SetComputeRootSignature(root_.Get());
    in.list->SetPipelineState(pso_.Get());
    in.list->SetComputeRootConstantBufferView(kFrame, s.constants->GetGPUVirtualAddress());
    in.list->SetComputeRootShaderResourceView(kScene, in.tlas);
    in.list->SetComputeRootShaderResourceView(kLights, in.lights->GetGPUVirtualAddress());
    in.list->SetComputeRootShaderResourceView(kDrawTable, in.draws->GetGPUVirtualAddress());
    in.list->SetComputeRootShaderResourceView(kRecords, in.records->GetGPUVirtualAddress());
    in.list->SetComputeRootDescriptorTable(kInputs, gpu);
    in.list->SetComputeRootDescriptorTable(kOutputs, uav_gpu);
    in.list->SetComputeRootShaderResourceView(kVertices, in.vertices->GetGPUVirtualAddress());
    in.list->SetComputeRootDescriptorTable(kTextures, in.heap->GetGPUDescriptorHandleForHeapStart());
    in.list->SetComputeRootUnorderedAccessView(kDrawColour, draw_colours_->GetGPUVirtualAddress());
    // Pass 0 empties the per-draw colour sums, pass 1 adds every pixel's albedo to its draw's,
    // pass 2 lights the picture (reflections of fighters read their parts' colours from them).
    D3D12_RESOURCE_BARRIER between{};
    between.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    between.UAV.pResource = draw_colours_.Get();
    in.list->SetComputeRoot32BitConstant(kPass, 0u, 0);
    in.list->Dispatch(kMaxDrawColours / 64, 1, 1);
    in.list->ResourceBarrier(1, &between);
    in.list->SetComputeRoot32BitConstant(kPass, 1u, 0);
    in.list->Dispatch((in.width + 7) / 8, (in.height + 7) / 8, 1);
    in.list->ResourceBarrier(1, &between);
    in.list->SetComputeRoot32BitConstant(kPass, 2u, 0);
    in.list->Dispatch((in.width + 7) / 8, (in.height + 7) / 8, 1);
    D3D12_RESOURCE_BARRIER b[kSlotTargets + 1]{};
    for (UINT i = 0; i < kSlotTargets; ++i) {
      b[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      b[i].Transition = {s.targets[i].Get(), 0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, kShaderRead};
    }
    b[kSlotTargets].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;   // next present reads what this one wrote
    b[kSlotTargets].UAV.pResource = accum_.Get();
    in.list->ResourceBarrier(kSlotTargets + 1, b);
    s.shader_read = true;
    if (out) {
      *out = {};
      out->color = s.targets[0].Get();
      out->diffuse_albedo = s.targets[1].Get();
      out->specular_albedo = s.targets[2].Get();
      out->normal_roughness = s.targets[3].Get();
    }
    return true;
  }

 private:
  struct Slot {
    ComPtr<ID3D12Resource> targets[kSlotTargets];   // colour, then the three guides
    ComPtr<ID3D12Resource> constants;
    uint8_t* constants_cpu = nullptr;
    bool shader_read = false;
  };
  ComPtr<ID3D12Device5> device_;
  ComPtr<ID3D12RootSignature> root_;
  ComPtr<ID3D12PipelineState> pso_;
  ComPtr<ID3D12Resource> accum_;   // photo mode
  ComPtr<ID3D12Resource> draw_colours_;   // per draw: summed on-screen albedo
  Slot slots_[kSlots];
};

}  // namespace

std::unique_ptr<RtxIntegrator> make_melee_integrator() { return std::make_unique<MeleeIntegrator>(); }

}  // namespace gx
