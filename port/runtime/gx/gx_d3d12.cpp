// D3D12 backend: replays captured GX frames into an EFB render target and presents XFB copies.
// SPDX-License-Identifier: GPL-2.0-or-later
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <unordered_map>
#include <vector>
#include "gx_d3d12.h"
#include "gx_shader.h"
#include "gx_texture.h"
#include "host.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

namespace gx {

namespace {

void check(HRESULT hr, const char* what) { if (FAILED(hr)) host::die("D3D12: %s failed (%08X)", what, (unsigned)hr); }

struct PsoKey {
  uint64_t vs, ps;
  uint32_t blend, zmode, cull, topology, pixel_format;
  bool operator==(const PsoKey& o) const { return memcmp(this, &o, sizeof *this) == 0; }
};
struct PsoKeyHash { size_t operator()(const PsoKey& k) const { return (size_t)hash_bytes(&k, sizeof k); } };

struct TextureEntry {
  ComPtr<ID3D12Resource> resource;
  uint32_t width = 0, height = 0, levels = 1;
  uint64_t last_used = 0;
};

struct SamplerSetKey {
  uint32_t mode0[8], mode1[8];
  bool operator==(const SamplerSetKey& o) const { return memcmp(this, &o, sizeof *this) == 0; }
};
struct SamplerSetHash { size_t operator()(const SamplerSetKey& k) const { return (size_t)hash_bytes(&k, sizeof k); } };

class Ring {
 public:
  void init(ID3D12Device* dev, size_t size) {
    size_ = size;
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_UPLOAD};
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = size; rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    check(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buffer_)), "ring buffer");
    check(buffer_->Map(0, nullptr, (void**)&cpu_), "ring map");
    gpu_ = buffer_->GetGPUVirtualAddress();
  }
  void reset() { offset_ = 0; }
  // Returns CPU pointer and GPU address for `bytes` aligned to `align`.
  bool alloc(size_t bytes, size_t align, uint8_t** cpu, D3D12_GPU_VIRTUAL_ADDRESS* gpu) {
    size_t off = (offset_ + align - 1) & ~(align - 1);
    if (off + bytes > size_) return false;
    *cpu = cpu_ + off; *gpu = gpu_ + off; offset_ = off + bytes;
    return true;
  }
  ID3D12Resource* resource() { return buffer_.Get(); }
 private:
  ComPtr<ID3D12Resource> buffer_;
  uint8_t* cpu_ = nullptr;
  D3D12_GPU_VIRTUAL_ADDRESS gpu_ = 0;
  size_t size_ = 0, offset_ = 0;
};

class D3D12Backend : public Backend {
 public:
  D3D12Backend(HWND hwnd, int w, int h, const D3D12Options& o) : hwnd_(hwnd), opts_(o), client_w_(w), client_h_(h) { init(); }
  ~D3D12Backend() override { wait_gpu(); flush_captures(); if (fence_event_) CloseHandle(fence_event_); }
  void submit_frame(const Frame& frame) override { submit_frame(frame, nullptr); }
  void submit_frame(const Frame& frame, const DrawMatrices* overrides) override;
  void resize(int w, int h) {
    wait_gpu(); client_w_ = w; client_h_ = h; create_swapchain_targets(true);
    // Auto scale follows the window like Dolphin's "Auto (Window Size)" integral mode: the EFB is
    // re-created at the new multiplier and scaled EFB-copy textures are dropped (their size changed).
    if (opts_.efb_scale == 0 && pick_scale() != scale_) { efb_copies_.clear(); create_efb(); }
  }
  int scale() const { return scale_; }
  uint32_t frames_presented() const { return frames_presented_; }
  uint32_t pipeline_count() const { return (uint32_t)psos_.size(); }
  uint32_t texture_count() const { return (uint32_t)textures_.size(); }

 private:
  void init();
  void create_swapchain_targets(bool resize);
  void create_efb();
  int pick_scale() const;
  void wait_gpu();
  ID3D12PipelineState* get_pso(const DrawCall& dc, D3D12_PRIMITIVE_TOPOLOGY_TYPE topo);
  ID3D12Resource* get_texture(const TextureRef& t, uint32_t* w, uint32_t* h);
  D3D12_GPU_DESCRIPTOR_HANDLE bind_textures(const DrawCall& dc);
  D3D12_GPU_DESCRIPTOR_HANDLE bind_samplers(const DrawCall& dc);
  void execute_draw(const Frame& frame, const DrawCall& dc, const DrawMatrices* override_matrices);
  void execute_copy(const EfbCopy& copy);
  void present_efb(const EfbCopy& copy);
  void clear_efb(const EfbCopy& copy);
  void capture_backbuffer();
  void flush_captures();
  struct PendingCapture { std::string path; UINT w, h, pitch; uint64_t sequence; std::vector<uint8_t> data; };
  std::vector<PendingCapture> pending_captures_;
  uint64_t capture_sequence_ = 0;

  HWND hwnd_;
  D3D12Options opts_;
  int client_w_, client_h_;
  int scale_ = 1;                                // current internal-resolution multiplier
  int efb_w_ = EFB_WIDTH, efb_h_ = EFB_HEIGHT;
  ComPtr<ID3D12Device> device_;
  ComPtr<ID3D12CommandQueue> queue_;
  ComPtr<IDXGISwapChain3> swapchain_;
  ComPtr<ID3D12DescriptorHeap> rtv_heap_, dsv_heap_, srv_heap_, sampler_heap_;
  ComPtr<ID3D12Resource> backbuffers_[3];
  ComPtr<ID3D12Resource> efb_color_, efb_depth_;
  ComPtr<ID3D12CommandAllocator> allocator_;
  ComPtr<ID3D12GraphicsCommandList> list_;
  ComPtr<ID3D12Fence> fence_;
  HANDLE fence_event_ = nullptr;
  uint64_t fence_value_ = 0;
  ComPtr<ID3D12RootSignature> root_;
  ComPtr<ID3D12RootSignature> blit_root_;
  ComPtr<ID3D12PipelineState> blit_pso_;
  UINT rtv_size_ = 0, srv_size_ = 0, sampler_size_ = 0;
  Ring vertex_ring_, index_ring_, constant_ring_, upload_ring_;
  std::unordered_map<uint64_t, ComPtr<ID3DBlob>> vs_blobs_, ps_blobs_;
  std::unordered_map<PsoKey, ComPtr<ID3D12PipelineState>, PsoKeyHash> psos_;
  std::unordered_map<uint64_t, TextureEntry> textures_;       // key: hash of (addr, dims, format, data, tlut)
  std::unordered_map<uint32_t, TextureEntry> efb_copies_;     // key: guest dest address
  std::unordered_map<SamplerSetKey, uint32_t, SamplerSetHash> sampler_sets_;  // -> heap slot base
  uint32_t sampler_slots_used_ = 0;
  uint32_t srv_cursor_ = 0;
  uint64_t frame_counter_ = 0;
  uint32_t frames_presented_ = 0;
  bool efb_is_rt_ = true;
  bool have_clear_ = false;
  EfbCopy pending_clear_{};
  std::vector<ComPtr<ID3D12Resource>> frame_garbage_;
  std::vector<uint8_t> decode_scratch_;
};

void D3D12Backend::init() {
#ifdef _DEBUG
  { ComPtr<ID3D12Debug> dbg; if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) dbg->EnableDebugLayer(); }
#endif
  ComPtr<IDXGIFactory4> factory;
  check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "factory");
  ComPtr<IDXGIAdapter1> adapter;
  for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 desc; adapter->GetDesc1(&desc);
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
    if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)))) {
      char name[128]; WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof name, nullptr, nullptr);
      host::log("d3d12: using %s", name);
      break;
    }
  }
  if (!device_) host::die("D3D12: no adapter");
  D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  check(device_->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue_)), "queue");
  DXGI_SWAP_CHAIN_DESC1 sd{};
  sd.Width = client_w_; sd.Height = client_h_; sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.SampleDesc.Count = 1;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 3; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
  ComPtr<IDXGISwapChain1> sc1;
  check(factory->CreateSwapChainForHwnd(queue_.Get(), hwnd_, &sd, nullptr, nullptr, &sc1), "swapchain");
  check(sc1.As(&swapchain_), "swapchain3");
  factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);

  D3D12_DESCRIPTOR_HEAP_DESC hd{};
  hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; hd.NumDescriptors = 8;
  check(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtv_heap_)), "rtv heap");
  hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV; hd.NumDescriptors = 2;
  check(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&dsv_heap_)), "dsv heap");
  hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = 65536; hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  check(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&srv_heap_)), "srv heap");
  hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER; hd.NumDescriptors = 2048;
  check(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&sampler_heap_)), "sampler heap");
  rtv_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  srv_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  sampler_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

  check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator_)), "allocator");
  check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(), nullptr, IID_PPV_ARGS(&list_)), "list");
  list_->Close();
  check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "fence");
  fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);

  create_swapchain_targets(false);
  create_efb();
  vertex_ring_.init(device_.Get(), 48 << 20);
  index_ring_.init(device_.Get(), 24 << 20);
  constant_ring_.init(device_.Get(), 32 << 20);
  upload_ring_.init(device_.Get(), 64 << 20);

  // Root signature: b0 (VS constants), b1 (PS constants), t0-7, s0-7.
  D3D12_DESCRIPTOR_RANGE srv_range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0, 0, 0};
  D3D12_DESCRIPTOR_RANGE samp_range{D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 8, 0, 0, 0};
  D3D12_ROOT_PARAMETER params[4]{};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; params[0].Descriptor.ShaderRegister = 0; params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; params[1].Descriptor.ShaderRegister = 1; params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; params[2].DescriptorTable = {1, &srv_range}; params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; params[3].DescriptorTable = {1, &samp_range}; params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC rsd{4, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
  ComPtr<ID3DBlob> sig, err;
  if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err)))
    host::die("root signature: %s", err ? (const char*)err->GetBufferPointer() : "?");
  check(device_->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&root_)), "root sig");

  // Blit pipeline (EFB -> backbuffer).
  D3D12_STATIC_SAMPLER_DESC ss{};
  ss.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; ss.AddressU = ss.AddressV = ss.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  ss.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_PARAMETER bp[2]{};
  bp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; D3D12_DESCRIPTOR_RANGE br{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
  bp[0].DescriptorTable = {1, &br}; bp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  bp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; bp[1].Constants.Num32BitValues = 4; bp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  D3D12_ROOT_SIGNATURE_DESC brsd{2, bp, 1, &ss, D3D12_ROOT_SIGNATURE_FLAG_NONE};
  check(D3D12SerializeRootSignature(&brsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err), "blit root");
  check(device_->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&blit_root_)), "blit root sig");
  const char* blit = R"(
Texture2D src : register(t0); SamplerState samp : register(s0);
cbuffer C : register(b0) { float4 rect; };  // xy = uv scale, zw = uv offset (source rect)
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O VS(uint id : SV_VertexID) { O o; float2 p = float2((id << 1) & 2, id & 2); o.pos = float4(p * float2(2,-2) + float2(-1,1), 0, 1); o.uv = p * rect.xy + rect.zw; return o; }
float4 PS(O i) : SV_Target { return float4(src.Sample(samp, i.uv).rgb, 1); })";
  ComPtr<ID3DBlob> bvs, bps;
  check(D3DCompile(blit, strlen(blit), nullptr, nullptr, nullptr, "VS", "vs_5_0", 0, 0, &bvs, &err), "blit vs");
  check(D3DCompile(blit, strlen(blit), nullptr, nullptr, nullptr, "PS", "ps_5_0", 0, 0, &bps, &err), "blit ps");
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
  pd.pRootSignature = blit_root_.Get();
  pd.VS = {bvs->GetBufferPointer(), bvs->GetBufferSize()}; pd.PS = {bps->GetBufferPointer(), bps->GetBufferSize()};
  pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  pd.SampleMask = UINT_MAX;
  pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; pd.RasterizerState.DepthClipEnable = TRUE;
  pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pd.NumRenderTargets = 1; pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; pd.SampleDesc.Count = 1;
  check(device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&blit_pso_)), "blit pso");
}

void D3D12Backend::create_swapchain_targets(bool resize) {
  for (auto& b : backbuffers_) b.Reset();
  if (resize) check(swapchain_->ResizeBuffers(3, client_w_, client_h_, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING), "resize");
  for (UINT i = 0; i < 3; ++i) {
    check(swapchain_->GetBuffer(i, IID_PPV_ARGS(&backbuffers_[i])), "backbuffer");
    D3D12_CPU_DESCRIPTOR_HANDLE h = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    h.ptr += i * rtv_size_;
    device_->CreateRenderTargetView(backbuffers_[i].Get(), nullptr, h);
  }
}

// Mirrors Dolphin Renderer::CalculateTargetSize: an explicit multiplier, or (auto) the smallest
// integer multiplier whose scaled 640x480 visible area covers the 4:3 output rectangle in the window.
int D3D12Backend::pick_scale() const {
  constexpr int max_scale = 16384 / EFB_WIDTH;   // D3D12 texture limit
  if (opts_.efb_scale > 0) return std::clamp(opts_.efb_scale, 1, max_scale);
  float ww = (float)std::max(client_w_, 1), wh = (float)std::max(client_h_, 1);
  float vw = ww, vh = ww * 3.0f / 4.0f;
  if (vh > wh) { vh = wh; vw = wh * 4.0f / 3.0f; }
  int s = std::max((int)std::ceil(vw / 640.0f), (int)std::ceil(vh / 480.0f));
  return std::clamp(s, 1, max_scale);
}

void D3D12Backend::create_efb() {
  scale_ = pick_scale();
  efb_w_ = EFB_WIDTH * scale_; efb_h_ = EFB_HEIGHT * scale_;
  host::log("d3d12: internal resolution %dx%d (EFB x%d, window %dx%d)", efb_w_, efb_h_, scale_, client_w_, client_h_);
  D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
  D3D12_RESOURCE_DESC rd{};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = efb_w_; rd.Height = efb_h_; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
  rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; rd.SampleDesc.Count = 1; rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  D3D12_CLEAR_VALUE cv{DXGI_FORMAT_R8G8B8A8_UNORM, {0, 0, 0, 1}};
  check(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET, &cv, IID_PPV_ARGS(&efb_color_)), "efb color");
  D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart(); rtv.ptr += 3 * rtv_size_;
  device_->CreateRenderTargetView(efb_color_.Get(), nullptr, rtv);
  rd.Format = DXGI_FORMAT_D32_FLOAT; rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  D3D12_CLEAR_VALUE dv{DXGI_FORMAT_D32_FLOAT}; dv.DepthStencil.Depth = 0.0f;
  check(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_DEPTH_WRITE, &dv, IID_PPV_ARGS(&efb_depth_)), "efb depth");
  device_->CreateDepthStencilView(efb_depth_.Get(), nullptr, dsv_heap_->GetCPUDescriptorHandleForHeapStart());
  efb_is_rt_ = true;
}

void D3D12Backend::wait_gpu() {
  ++fence_value_;
  queue_->Signal(fence_.Get(), fence_value_);
  if (fence_->GetCompletedValue() < fence_value_) {
    fence_->SetEventOnCompletion(fence_value_, fence_event_);
    WaitForSingleObject(fence_event_, INFINITE);
  }
}

// ---------------- pipelines ----------------
ID3D12PipelineState* D3D12Backend::get_pso(const DrawCall& dc, D3D12_PRIMITIVE_TOPOLOGY_TYPE topo) {
  VSUid vsu = make_vs_uid(dc);
  PSUid psu = make_ps_uid(dc);
  uint64_t vh = vsu.hash(), ph = psu.hash();
  PsoKey key{vh, ph, dc.bp.blendmode() & 0xFFFF, dc.bp.zmode() & 0x1F, dc.bp.cullmode(), (uint32_t)topo, dc.bp.zcontrol() & 7};
  auto it = psos_.find(key);
  if (it != psos_.end()) return it->second.Get();

  ComPtr<ID3DBlob>& vs = vs_blobs_[vh];
  if (!vs) {
    std::string src = generate_vertex_shader(vsu);
    ComPtr<ID3DBlob> err;
    if (FAILED(D3DCompile(src.c_str(), src.size(), "vs", nullptr, nullptr, "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs, &err))) {
      host::log("vertex shader compile failed:\n%s\n%s", err ? (const char*)err->GetBufferPointer() : "?", src.c_str());
      host::die("vertex shader compile failed");
    }
  }
  ComPtr<ID3DBlob>& ps = ps_blobs_[ph];
  if (!ps) {
    std::string src = generate_pixel_shader(psu);
    ComPtr<ID3DBlob> err;
    if (FAILED(D3DCompile(src.c_str(), src.size(), "ps", nullptr, nullptr, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, &err))) {
      host::log("pixel shader compile failed:\n%s\n%s", err ? (const char*)err->GetBufferPointer() : "?", src.c_str());
      host::die("pixel shader compile failed");
    }
  }
  static const D3D12_INPUT_ELEMENT_DESC layout[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 2, DXGI_FORMAT_R32G32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 3, DXGI_FORMAT_R32G32_FLOAT, 0, 56, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 4, DXGI_FORMAT_R32G32_FLOAT, 0, 64, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 5, DXGI_FORMAT_R32G32_FLOAT, 0, 72, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 6, DXGI_FORMAT_R32G32_FLOAT, 0, 80, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 7, DXGI_FORMAT_R32G32_FLOAT, 0, 88, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 96, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"BLENDINDICES", 1, DXGI_FORMAT_R8G8B8A8_UINT, 0, 100, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
  pd.pRootSignature = root_.Get();
  pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
  pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
  pd.InputLayout = {layout, _countof(layout)};
  pd.SampleMask = UINT_MAX;
  // Blend
  uint32_t bm = dc.bp.blendmode();
  bool alpha_in_efb = (dc.bp.zcontrol() & 7) == 1;  // RGBA6_Z24
  static const D3D12_BLEND src_factors[] = {D3D12_BLEND_ZERO, D3D12_BLEND_ONE, D3D12_BLEND_DEST_COLOR, D3D12_BLEND_INV_DEST_COLOR,
                                            D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_DEST_ALPHA, D3D12_BLEND_INV_DEST_ALPHA};
  static const D3D12_BLEND dst_factors[] = {D3D12_BLEND_ZERO, D3D12_BLEND_ONE, D3D12_BLEND_SRC_COLOR, D3D12_BLEND_INV_SRC_COLOR,
                                            D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_DEST_ALPHA, D3D12_BLEND_INV_DEST_ALPHA};
  auto& rt = pd.BlendState.RenderTarget[0];
  bool blend_enable = bits(bm, 0, 1);
  uint32_t sf = bits(bm, 8, 3), df = bits(bm, 5, 3);
  D3D12_BLEND s = src_factors[sf], d = dst_factors[df];
  if (!alpha_in_efb) {
    if (s == D3D12_BLEND_DEST_ALPHA) s = D3D12_BLEND_ONE; if (s == D3D12_BLEND_INV_DEST_ALPHA) s = D3D12_BLEND_ZERO;
    if (d == D3D12_BLEND_DEST_ALPHA) d = D3D12_BLEND_ONE; if (d == D3D12_BLEND_INV_DEST_ALPHA) d = D3D12_BLEND_ZERO;
  }
  rt.BlendEnable = blend_enable;
  rt.SrcBlend = s; rt.DestBlend = d; rt.BlendOp = bits(bm, 11, 1) ? D3D12_BLEND_OP_REV_SUBTRACT : D3D12_BLEND_OP_ADD;
  rt.SrcBlendAlpha = D3D12_BLEND_ONE; rt.DestBlendAlpha = D3D12_BLEND_ZERO; rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
  rt.RenderTargetWriteMask = (bits(bm, 3, 1) ? (D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE) : 0) |
                             (bits(bm, 4, 1) ? D3D12_COLOR_WRITE_ENABLE_ALPHA : 0);
  // Logic op CLEAR/SET etc. are rare; ignore.
  // Rasterizer
  static const D3D12_CULL_MODE cull_modes[] = {D3D12_CULL_MODE_NONE, D3D12_CULL_MODE_BACK, D3D12_CULL_MODE_FRONT, D3D12_CULL_MODE_BACK};
  pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pd.RasterizerState.CullMode = cull_modes[dc.bp.cullmode()];
  pd.RasterizerState.FrontCounterClockwise = FALSE;
  pd.RasterizerState.DepthClipEnable = TRUE;
  // Depth (reversed range: GC near = 1.0)
  uint32_t zm = dc.bp.zmode();
  static const D3D12_COMPARISON_FUNC cmp[] = {D3D12_COMPARISON_FUNC_NEVER, D3D12_COMPARISON_FUNC_GREATER, D3D12_COMPARISON_FUNC_EQUAL,
                                              D3D12_COMPARISON_FUNC_GREATER_EQUAL, D3D12_COMPARISON_FUNC_LESS, D3D12_COMPARISON_FUNC_NOT_EQUAL,
                                              D3D12_COMPARISON_FUNC_LESS_EQUAL, D3D12_COMPARISON_FUNC_ALWAYS};
  pd.DepthStencilState.DepthEnable = bits(zm, 0, 1);
  pd.DepthStencilState.DepthWriteMask = (bits(zm, 0, 1) && bits(zm, 4, 1)) ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
  pd.DepthStencilState.DepthFunc = bits(zm, 0, 1) ? cmp[bits(zm, 1, 3)] : D3D12_COMPARISON_FUNC_ALWAYS;
  pd.PrimitiveTopologyType = topo;
  pd.NumRenderTargets = 1; pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; pd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
  pd.SampleDesc.Count = 1;
  ComPtr<ID3D12PipelineState> pso;
  check(device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso)), "pso");
  psos_[key] = pso;
  return pso.Get();
}

// ---------------- textures ----------------
ID3D12Resource* D3D12Backend::get_texture(const TextureRef& t, uint32_t* w, uint32_t* h) {
  auto ec = efb_copies_.find(t.addr);
  if (ec != efb_copies_.end() && ec->second.resource) {
    ec->second.last_used = frame_counter_;
    *w = ec->second.width; *h = ec->second.height;
    return ec->second.resource.Get();
  }
  if (!t.data) return nullptr;
  const uint8_t* src = t.data->image.data();
  uint32_t lw = t.width, lh = t.height;
  const uint32_t meta[] = {t.width, t.height, t.format, t.mip_levels, t.tlut_format};
  uint64_t key = t.data->hash ^ hash_bytes(meta, sizeof meta);
  auto it = textures_.find(key);
  if (it != textures_.end()) { it->second.last_used = frame_counter_; *w = it->second.width; *h = it->second.height; return it->second.resource.Get(); }

  TextureEntry e;
  e.width = t.width; e.height = t.height; e.levels = t.mip_levels; e.last_used = frame_counter_;
  D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
  D3D12_RESOURCE_DESC rd{};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = t.width; rd.Height = t.height; rd.DepthOrArraySize = 1;
  rd.MipLevels = (UINT16)t.mip_levels; rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; rd.SampleDesc.Count = 1;
  check(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&e.resource)), "texture");
  // Decode each level and copy through the upload ring.
  lw = t.width; lh = t.height;
  const uint8_t* level_src = src;
  for (uint32_t l = 0; l < t.mip_levels && lw && lh; ++l) {
    decode_texture(level_src, lw, lh, t.format, t.data->palette.data(), t.tlut_format, decode_scratch_);
    uint32_t pitch = (lw * 4 + 255) & ~255u;
    uint8_t* cpu; D3D12_GPU_VIRTUAL_ADDRESS gpu;
    if (!upload_ring_.alloc((size_t)pitch * lh, 512, &cpu, &gpu)) { host::log("d3d12: upload ring full"); break; }
    for (uint32_t y = 0; y < lh; ++y) memcpy(cpu + (size_t)y * pitch, &decode_scratch_[(size_t)y * lw * 4], lw * 4);
    D3D12_TEXTURE_COPY_LOCATION dst{e.resource.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX}; dst.SubresourceIndex = l;
    D3D12_TEXTURE_COPY_LOCATION srcloc{upload_ring_.resource(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    srcloc.PlacedFootprint.Offset = gpu - upload_ring_.resource()->GetGPUVirtualAddress();
    srcloc.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, lw, lh, 1, pitch};
    list_->CopyTextureRegion(&dst, 0, 0, 0, &srcloc, nullptr);
    level_src += texture_level_bytes(lw, lh, t.format);
    lw = std::max(1u, lw / 2); lh = std::max(1u, lh / 2);
  }
  D3D12_RESOURCE_BARRIER b{};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition = {e.resource.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
  list_->ResourceBarrier(1, &b);
  ID3D12Resource* res = e.resource.Get();
  textures_[key] = std::move(e);
  *w = t.width; *h = t.height;
  return res;
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12Backend::bind_textures(const DrawCall& dc) {
  if (srv_cursor_ + 8 > 65536) srv_cursor_ = 0;
  uint32_t base = srv_cursor_;
  srv_cursor_ += 8;
  D3D12_CPU_DESCRIPTOR_HANDLE cpu = srv_heap_->GetCPUDescriptorHandleForHeapStart();
  for (int i = 0; i < 8; ++i) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = cpu; h.ptr += (base + i) * srv_size_;
    ID3D12Resource* res = nullptr;
    uint32_t w = 1, hgt = 1;
    if (dc.textures[i].used) res = get_texture(dc.textures[i], &w, &hgt);
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sd.Texture2D.MipLevels = res ? -1 : 1;
    if (res) device_->CreateShaderResourceView(res, &sd, h);
    else device_->CreateShaderResourceView(nullptr, &sd, h);   // null descriptor
  }
  D3D12_GPU_DESCRIPTOR_HANDLE g = srv_heap_->GetGPUDescriptorHandleForHeapStart();
  g.ptr += base * srv_size_;
  return g;
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12Backend::bind_samplers(const DrawCall& dc) {
  SamplerSetKey key{};
  for (int i = 0; i < 8; ++i) { key.mode0[i] = dc.textures[i].used ? dc.textures[i].mode0 : 0; key.mode1[i] = dc.textures[i].used ? dc.textures[i].mode1 : 0; }
  auto it = sampler_sets_.find(key);
  uint32_t base;
  if (it != sampler_sets_.end()) base = it->second;
  else {
    if (sampler_slots_used_ + 8 > 2048) { sampler_sets_.clear(); sampler_slots_used_ = 0; }
    base = sampler_slots_used_; sampler_slots_used_ += 8;
    sampler_sets_[key] = base;
    for (int i = 0; i < 8; ++i) {
      uint32_t m0 = key.mode0[i], m1 = key.mode1[i];
      D3D12_SAMPLER_DESC sd{};
      static const D3D12_TEXTURE_ADDRESS_MODE wrap[] = {D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_MIRROR, D3D12_TEXTURE_ADDRESS_MODE_WRAP};
      sd.AddressU = wrap[bits(m0, 0, 2)]; sd.AddressV = wrap[bits(m0, 2, 2)]; sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
      bool mag_linear = bits(m0, 4, 1);
      uint32_t minf = bits(m0, 5, 3);
      bool min_linear = minf & 4;
      uint32_t mip = minf & 3;  // 0 none, 1 point, 2 linear
      D3D12_FILTER_TYPE mn = min_linear ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT;
      D3D12_FILTER_TYPE mg = mag_linear ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT;
      D3D12_FILTER_TYPE mp = mip == 2 ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT;
      sd.Filter = D3D12_ENCODE_BASIC_FILTER(mn, mg, mp, D3D12_FILTER_REDUCTION_TYPE_STANDARD);
      sd.MipLODBias = (float)(int32_t)((int32_t)sbits(m0, 9, 8)) / 32.0f;
      sd.MinLOD = bits(m1, 0, 8) / 16.0f;
      sd.MaxLOD = mip ? bits(m1, 8, 8) / 16.0f : 0.0f;
      sd.MaxAnisotropy = 1;
      sd.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
      D3D12_CPU_DESCRIPTOR_HANDLE h = sampler_heap_->GetCPUDescriptorHandleForHeapStart(); h.ptr += (base + i) * sampler_size_;
      device_->CreateSampler(&sd, h);
    }
  }
  D3D12_GPU_DESCRIPTOR_HANDLE g = sampler_heap_->GetGPUDescriptorHandleForHeapStart();
  g.ptr += base * sampler_size_;
  return g;
}

// ---------------- draws ----------------
void D3D12Backend::execute_draw(const Frame& frame, const DrawCall& dc, const DrawMatrices* override_matrices) {
  // Build index list (triangle list / line list) from the GX primitive.
  std::vector<uint32_t> idx;
  uint32_t n = dc.vertex_count;
  D3D12_PRIMITIVE_TOPOLOGY_TYPE topo = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  D3D12_PRIMITIVE_TOPOLOGY prim = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  switch (dc.primitive) {
    case 0x80: case 0x88:
      for (uint32_t i = 0; i + 3 < n; i += 4) { idx.insert(idx.end(), {i, i + 1, i + 2, i, i + 2, i + 3}); }
      break;
    case 0x90:
      for (uint32_t i = 0; i + 2 < n; i += 3) idx.insert(idx.end(), {i, i + 1, i + 2});
      break;
    case 0x98:
      for (uint32_t i = 2; i < n; ++i) { if (i & 1) idx.insert(idx.end(), {i - 1, i - 2, i}); else idx.insert(idx.end(), {i - 2, i - 1, i}); }
      break;
    case 0xA0:
      for (uint32_t i = 2; i < n; ++i) idx.insert(idx.end(), {0, i - 1, i});
      break;
    case 0xA8:
      topo = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE; prim = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
      for (uint32_t i = 0; i + 1 < n; i += 2) idx.insert(idx.end(), {i, i + 1});
      break;
    case 0xB0:
      topo = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE; prim = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
      for (uint32_t i = 1; i < n; ++i) idx.insert(idx.end(), {i - 1, i});
      break;
    default:
      return;  // points not supported yet
  }
  if (idx.empty()) return;
  // Vertices
  uint8_t* vcpu; D3D12_GPU_VIRTUAL_ADDRESS vgpu;
  size_t vbytes = (size_t)n * sizeof(Vertex);
  if (!vertex_ring_.alloc(vbytes, 16, &vcpu, &vgpu)) { host::log("d3d12: vertex ring full"); return; }
  memcpy(vcpu, &frame.vertices[dc.first_vertex], vbytes);
  uint8_t* icpu; D3D12_GPU_VIRTUAL_ADDRESS igpu;
  if (!index_ring_.alloc(idx.size() * 4, 4, &icpu, &igpu)) { host::log("d3d12: index ring full"); return; }
  memcpy(icpu, idx.data(), idx.size() * 4);
  // Constants
  uint8_t* ccpu; D3D12_GPU_VIRTUAL_ADDRESS vs_gpu, ps_gpu;
  if (!constant_ring_.alloc(sizeof(VSConstants), 256, &ccpu, &vs_gpu)) { host::log("d3d12: constant ring full"); return; }
  fill_vs_constants(dc, *(VSConstants*)ccpu, scale_, override_matrices);
  if (!constant_ring_.alloc(sizeof(PSConstants), 256, &ccpu, &ps_gpu)) return;
  fill_ps_constants(dc, *(PSConstants*)ccpu, scale_);
  // Pipeline
  ID3D12PipelineState* pso = get_pso(dc, topo);
  D3D12_GPU_DESCRIPTOR_HANDLE srvs = bind_textures(dc);
  D3D12_GPU_DESCRIPTOR_HANDLE samps = bind_samplers(dc);
  // Viewport / scissor
  const float* vp = (const float*)&dc.xf_regs[0x1A];
  float s = (float)scale_;
  float X = (vp[3] - vp[0] - 342.0f) * s, Y = (vp[4] + vp[1] - 342.0f) * s, W = 2.0f * vp[0] * s, H = -2.0f * vp[1] * s;
  if (W < 0) { X += W; W = -W; }
  if (H < 0) { Y += H; H = -H; }
  float min_depth = 1.0f - vp[5] / 16777216.0f, max_depth = 1.0f - (vp[5] - vp[2]) / 16777216.0f;
  min_depth = std::clamp(min_depth, 0.0f, 1.0f); max_depth = std::clamp(max_depth, 0.0f, 1.0f);
  if (max_depth < min_depth) std::swap(min_depth, max_depth);
  D3D12_VIEWPORT viewport{X, Y, std::max(W, 1.0f), std::max(H, 1.0f), min_depth, max_depth};
  uint32_t tl = dc.bp.reg[BP_SCISSORTL], br = dc.bp.reg[BP_SCISSORBR], so = dc.bp.reg[BP_SCISSOROFFSET];
  int xoff = (int)bits(so, 0, 10) * 2 - 342, yoff = (int)bits(so, 10, 10) * 2 - 342;
  // GX adds 342 to scissor coordinates so they stay positive (see Dolphin BPFunctions::SetScissor).
  int sl = (int)bits(tl, 12, 12) - xoff - 342, st = (int)bits(tl, 0, 12) - yoff - 342;
  int sr = (int)bits(br, 12, 12) - xoff - 341, sb = (int)bits(br, 0, 12) - yoff - 341;
  sl = std::clamp(sl, 0, EFB_WIDTH); sr = std::clamp(sr, 0, EFB_WIDTH); st = std::clamp(st, 0, EFB_HEIGHT); sb = std::clamp(sb, 0, EFB_HEIGHT);
  if (sr <= sl || sb <= st) return;
  D3D12_RECT scissor{(LONG)(sl * scale_), (LONG)(st * scale_), (LONG)(sr * scale_), (LONG)(sb * scale_)};

  list_->SetPipelineState(pso);
  list_->SetGraphicsRootSignature(root_.Get());
  list_->SetGraphicsRootConstantBufferView(0, vs_gpu);
  list_->SetGraphicsRootConstantBufferView(1, ps_gpu);
  list_->SetGraphicsRootDescriptorTable(2, srvs);
  list_->SetGraphicsRootDescriptorTable(3, samps);
  list_->RSSetViewports(1, &viewport);
  list_->RSSetScissorRects(1, &scissor);
  D3D12_VERTEX_BUFFER_VIEW vbv{vgpu, (UINT)vbytes, sizeof(Vertex)};
  D3D12_INDEX_BUFFER_VIEW ibv{igpu, (UINT)(idx.size() * 4), DXGI_FORMAT_R32_UINT};
  list_->IASetVertexBuffers(0, 1, &vbv);
  list_->IASetIndexBuffer(&ibv);
  list_->IASetPrimitiveTopology(prim);
  list_->DrawIndexedInstanced((UINT)idx.size(), 1, 0, 0, 0);
}

void D3D12Backend::clear_efb(const EfbCopy& c) {
  float s = (float)scale_;
  D3D12_RECT r{(LONG)(c.src_x * s), (LONG)(c.src_y * s), (LONG)((c.src_x + c.src_w) * s), (LONG)((c.src_y + c.src_h) * s)};
  float color[4] = {((c.clear_color >> 16) & 0xFF) / 255.0f, ((c.clear_color >> 8) & 0xFF) / 255.0f, (c.clear_color & 0xFF) / 255.0f, ((c.clear_color >> 24) & 0xFF) / 255.0f};
  D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart(); rtv.ptr += 3 * rtv_size_;
  list_->ClearRenderTargetView(rtv, color, 1, &r);
  list_->ClearDepthStencilView(dsv_heap_->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 1.0f - (float)c.clear_z / 16777215.0f, 0, 1, &r);
}

void D3D12Backend::execute_copy(const EfbCopy& c) {
  // EFB -> texture at guest address: keep a GPU copy and register it for texture lookups.
  // Like Dolphin with "scaled EFB copies": the copy keeps the internal resolution (native size x scale_).
  // A half-scale copy (GX_TRUE mipmap flag on GXCopyTex) is a filtered 2:1 downscale, drawn with the
  // linear blit pipeline as Dolphin's FromRenderTarget does; a 1:1 copy is an exact texture copy.
  uint32_t w = c.src_w, h = c.src_h;
  if (c.half_scale) { w = std::max(1u, w / 2); h = std::max(1u, h / 2); }
  uint32_t sw = w * scale_, sh = h * scale_;
  TextureEntry& e = efb_copies_[c.dest_addr];
  const D3D12_RESOURCE_STATES dst_state = c.half_scale ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_COPY_DEST;
  const D3D12_RESOURCE_STATES src_state = c.half_scale ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_COPY_SOURCE;
  D3D12_RESOURCE_BARRIER b[2]{};
  b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b[0].Transition = {efb_color_.Get(), 0, D3D12_RESOURCE_STATE_RENDER_TARGET, src_state};
  if (!e.resource || e.width != sw || e.height != sh) {
    if (e.resource) frame_garbage_.push_back(e.resource);
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = sw; rd.Height = sh; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; rd.SampleDesc.Count = 1; rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    check(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, dst_state, nullptr, IID_PPV_ARGS(&e.resource)), "efb copy tex");
    e.width = sw; e.height = sh;
    list_->ResourceBarrier(1, b);
  } else {
    b[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b[1].Transition = {e.resource.Get(), 0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, dst_state};
    list_->ResourceBarrier(2, b);
  }
  e.last_used = frame_counter_;
  if (c.half_scale) {
    if (srv_cursor_ + 1 > 65536) srv_cursor_ = 0;
    uint32_t slot = srv_cursor_++;
    D3D12_CPU_DESCRIPTOR_HANDLE sh_cpu = srv_heap_->GetCPUDescriptorHandleForHeapStart(); sh_cpu.ptr += slot * srv_size_;
    device_->CreateShaderResourceView(efb_color_.Get(), nullptr, sh_cpu);
    D3D12_GPU_DESCRIPTOR_HANDLE sh_gpu = srv_heap_->GetGPUDescriptorHandleForHeapStart(); sh_gpu.ptr += slot * srv_size_;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart(); rtv.ptr += 4 * rtv_size_;  // transient slot
    device_->CreateRenderTargetView(e.resource.Get(), nullptr, rtv);
    list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT vp{0, 0, (float)sw, (float)sh, 0, 1};
    D3D12_RECT sc{0, 0, (LONG)sw, (LONG)sh};
    list_->RSSetViewports(1, &vp);
    list_->RSSetScissorRects(1, &sc);
    list_->SetPipelineState(blit_pso_.Get());
    list_->SetGraphicsRootSignature(blit_root_.Get());
    list_->SetGraphicsRootDescriptorTable(0, sh_gpu);
    float rect[4] = {(float)c.src_w / EFB_WIDTH, (float)c.src_h / EFB_HEIGHT, (float)c.src_x / EFB_WIDTH, (float)c.src_y / EFB_HEIGHT};
    list_->SetGraphicsRoot32BitConstants(1, 4, rect, 0);
    list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list_->DrawInstanced(3, 1, 0, 0);
    // Restore the EFB as the render target; execute_draw re-sets viewport/scissor per draw.
    D3D12_CPU_DESCRIPTOR_HANDLE ertv = rtv_heap_->GetCPUDescriptorHandleForHeapStart(); ertv.ptr += 3 * rtv_size_;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
    list_->OMSetRenderTargets(1, &ertv, FALSE, &dsv);
  } else {
    D3D12_TEXTURE_COPY_LOCATION dst{e.resource.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    D3D12_TEXTURE_COPY_LOCATION src{efb_color_.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    D3D12_BOX box{c.src_x * (UINT)scale_, c.src_y * (UINT)scale_, 0, (c.src_x + c.src_w) * (UINT)scale_, (c.src_y + c.src_h) * (UINT)scale_, 1};
    box.right = std::min<UINT>(box.right, efb_w_); box.bottom = std::min<UINT>(box.bottom, efb_h_);
    list_->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
  }
  D3D12_RESOURCE_BARRIER back[2]{};
  back[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  back[0].Transition = {efb_color_.Get(), 0, src_state, D3D12_RESOURCE_STATE_RENDER_TARGET};
  back[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  back[1].Transition = {e.resource.Get(), 0, dst_state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
  list_->ResourceBarrier(2, back);
}

void D3D12Backend::present_efb(const EfbCopy& c) {
  UINT bb = swapchain_->GetCurrentBackBufferIndex();
  D3D12_RESOURCE_BARRIER b[2]{};
  b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b[0].Transition = {efb_color_.Get(), 0, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
  b[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b[1].Transition = {backbuffers_[bb].Get(), 0, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET};
  list_->ResourceBarrier(2, b);
  // SRV for the EFB
  if (srv_cursor_ + 1 > 65536) srv_cursor_ = 0;
  uint32_t slot = srv_cursor_++;
  D3D12_CPU_DESCRIPTOR_HANDLE h = srv_heap_->GetCPUDescriptorHandleForHeapStart(); h.ptr += slot * srv_size_;
  device_->CreateShaderResourceView(efb_color_.Get(), nullptr, h);
  D3D12_GPU_DESCRIPTOR_HANDLE g = srv_heap_->GetGPUDescriptorHandleForHeapStart(); g.ptr += slot * srv_size_;
  D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart(); rtv.ptr += bb * rtv_size_;
  float border[4] = {0.05f, 0.05f, 0.15f, 1};
  list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  list_->ClearRenderTargetView(rtv, border, 0, nullptr);
  // Letterbox 4:3 output (XFB region c.src_w x lines)
  float src_h_lines = (float)c.src_h * c.y_scale;
  float aspect = (float)c.src_w / std::max(1.0f, src_h_lines) * (480.0f / 528.0f) * (528.0f / 480.0f);
  aspect = 4.0f / 3.0f;
  float ww = (float)client_w_, wh = (float)client_h_;
  float vw = ww, vh = ww / aspect;
  if (vh > wh) { vh = wh; vw = wh * aspect; }
  D3D12_VIEWPORT vp{(ww - vw) * 0.5f, (wh - vh) * 0.5f, vw, vh, 0, 1};
  D3D12_RECT sc{0, 0, client_w_, client_h_};
  list_->RSSetViewports(1, &vp);
  list_->RSSetScissorRects(1, &sc);
  list_->SetPipelineState(blit_pso_.Get());
  list_->SetGraphicsRootSignature(blit_root_.Get());
  list_->SetGraphicsRootDescriptorTable(0, g);
  float rect[4] = {(float)c.src_w / EFB_WIDTH, (float)c.src_h / EFB_HEIGHT, (float)c.src_x / EFB_WIDTH, (float)c.src_y / EFB_HEIGHT};
  list_->SetGraphicsRoot32BitConstants(1, 4, rect, 0);
  list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  list_->DrawInstanced(3, 1, 0, 0);
  D3D12_RESOURCE_BARRIER back[2]{};
  back[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  back[0].Transition = {efb_color_.Get(), 0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET};
  back[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  back[1].Transition = {backbuffers_[bb].Get(), 0, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT};
  list_->ResourceBarrier(2, back);
  // Restore EFB as the render target for any later commands in this frame.
  D3D12_CPU_DESCRIPTOR_HANDLE ertv = rtv_heap_->GetCPUDescriptorHandleForHeapStart(); ertv.ptr += 3 * rtv_size_;
  D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
  list_->OMSetRenderTargets(1, &ertv, FALSE, &dsv);
}

void D3D12Backend::capture_backbuffer() {
  // Read back the last presented back buffer into a PPM (development aid).
  UINT bb = (swapchain_->GetCurrentBackBufferIndex() + 2) % 3;
  ID3D12Resource* src = backbuffers_[bb].Get();
  D3D12_RESOURCE_DESC desc = src->GetDesc();
  UINT pitch = (UINT)((desc.Width * 4 + 255) & ~255ull);
  D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_READBACK};
  D3D12_RESOURCE_DESC rd{};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = (UINT64)pitch * desc.Height; rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
  rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> staging;
  check(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&staging)), "readback");
  allocator_->Reset(); list_->Reset(allocator_.Get(), nullptr);
  D3D12_RESOURCE_BARRIER b{};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition = {src, 0, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE};
  list_->ResourceBarrier(1, &b);
  D3D12_TEXTURE_COPY_LOCATION dst{staging.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
  dst.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, (UINT)desc.Width, desc.Height, 1, pitch};
  D3D12_TEXTURE_COPY_LOCATION s{src, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
  list_->CopyTextureRegion(&dst, 0, 0, 0, &s, nullptr);
  b.Transition = {src, 0, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT};
  list_->ResourceBarrier(1, &b);
  list_->Close();
  ID3D12CommandList* lists[] = {list_.Get()};
  queue_->ExecuteCommandLists(1, lists);
  wait_gpu();
  uint8_t* data; staging->Map(0, nullptr, (void**)&data);
  if (opts_.capture_burst) {
    // Keep burst captures in memory so consecutive presented frames stay microseconds apart; the
    // files are written when the burst ends (or at shutdown).
    PendingCapture pc; pc.path = opts_.capture_path; pc.w = (UINT)desc.Width; pc.h = desc.Height; pc.pitch = pitch; pc.sequence = capture_sequence_;
    pc.data.assign(data, data + (size_t)pitch * desc.Height);
    pending_captures_.push_back(std::move(pc));
    staging->Unmap(0, nullptr);
    if (pending_captures_.size() >= opts_.capture_burst) flush_captures();
    return;
  }
  std::ofstream f(opts_.capture_path, std::ios::binary);
  f << "P6\n" << desc.Width << ' ' << desc.Height << "\n255\n";
  for (UINT y = 0; y < desc.Height; ++y) for (UINT x = 0; x < desc.Width; ++x) f.write((const char*)data + (size_t)y * pitch + x * 4, 3);
  staging->Unmap(0, nullptr);
  host::log("captured %s (sim frame %llu)", opts_.capture_path.c_str(), (unsigned long long)capture_sequence_);
}

void D3D12Backend::flush_captures() {
  for (const PendingCapture& pc : pending_captures_) {
    std::ofstream f(pc.path, std::ios::binary);
    f << "P6\n" << pc.w << ' ' << pc.h << "\n255\n";
    for (UINT y = 0; y < pc.h; ++y) for (UINT x = 0; x < pc.w; ++x) f.write((const char*)pc.data.data() + (size_t)y * pc.pitch + x * 4, 3);
    host::log("captured %s (sim frame %llu)", pc.path.c_str(), (unsigned long long)pc.sequence);
  }
  pending_captures_.clear();
}

static void dump_frame(const Frame& frame, const std::string& path) {
  FILE* f = fopen(path.c_str(), "w");
  if (!f) return;
  fprintf(f, "frame %llu: %zu draws, %zu copies, %zu vertices\n", (unsigned long long)frame.sequence, frame.draws.size(), frame.copies.size(), frame.vertices.size());
  for (const FrameCommand& cmd : frame.commands) {
    if (cmd.kind == FrameCommand::Copy) {
      const EfbCopy& c = frame.copies[cmd.index];
      fprintf(f, "COPY dest=%08X src=%u,%u %ux%u fmt=%u xfb=%d clear=%d color=%08X z=%06X yscale=%.3f\n", c.dest_addr, c.src_x, c.src_y, c.src_w, c.src_h, c.format, c.to_xfb, c.clear, c.clear_color, c.clear_z, c.y_scale);
      continue;
    }
    const DrawCall& d = frame.draws[cmd.index];
    const float* vp = (const float*)&d.xf_regs[0x1A];
    const float* pr = (const float*)&d.xf_regs[0x20];
    fprintf(f, "DRAW prim=%02X n=%u comps=%06X | vp wd=%.1f ht=%.1f zr=%.0f xo=%.1f yo=%.1f fz=%.0f | proj type=%u [%g %g %g %g %g %g] | genmode=%06X zmode=%02X blend=%04X alphacmp=%06X zctl=%02X scissor=%06X/%06X off=%06X | texgens=%u chans=%u mia=%06X\n",
            d.primitive, d.vertex_count, d.components, vp[0], vp[1], vp[2], vp[3], vp[4], vp[5], d.xf_regs[0x26], pr[0], pr[1], pr[2], pr[3], pr[4], pr[5],
            d.bp.reg[0], d.bp.zmode(), d.bp.blendmode(), d.bp.alpha_test(), d.bp.zcontrol(), d.bp.reg[BP_SCISSORTL], d.bp.reg[BP_SCISSORBR], d.bp.reg[BP_SCISSOROFFSET],
            d.xf_regs[0x3F] & 15, d.xf_regs[0x09] & 3, d.matrix_index_a);
    const Vertex& v = frame.vertices[d.first_vertex];
    fprintf(f, "   v0 pos=(%g %g %g) nrm=(%g %g %g) col0=%02X%02X%02X%02X uv0=(%g %g) posmtx=%u | mtx row0=[%g %g %g %g]\n", v.pos[0], v.pos[1], v.pos[2], v.nrm[0], v.nrm[1], v.nrm[2],
            v.col0[0], v.col0[1], v.col0[2], v.col0[3], v.uv[0][0], v.uv[0][1], v.posmtx, d.posMatrices[v.posmtx * 4], d.posMatrices[v.posmtx * 4 + 1], d.posMatrices[v.posmtx * 4 + 2], d.posMatrices[v.posmtx * 4 + 3]);
    {
      const float* nm = &d.normalMatrices[(v.posmtx >= 32 ? v.posmtx - 32 : v.posmtx) * 3];
      const float* pm = &d.posMatrices[v.posmtx * 4];
      fprintf(f, "   posmtx rows: [%g %g %g %g] [%g %g %g %g] [%g %g %g %g] | nrmmtx rows: [%g %g %g] [%g %g %g] [%g %g %g]\n",
              pm[0], pm[1], pm[2], pm[3], pm[4], pm[5], pm[6], pm[7], pm[8], pm[9], pm[10], pm[11], nm[0], nm[1], nm[2], nm[3], nm[4], nm[5], nm[6], nm[7], nm[8]);
    }
    fprintf(f, "   xf: numchan=%u chan0 col=%04X alpha=%04X chan1 col=%04X alpha=%04X amb=%08X/%08X mat=%08X/%08X | tevregs RA/BG: %06X/%06X %06X/%06X %06X/%06X %06X/%06X\n",
            d.xf_regs[0x09] & 3, d.xf_regs[0x0E], d.xf_regs[0x10], d.xf_regs[0x0F], d.xf_regs[0x11], d.xf_regs[0x0A], d.xf_regs[0x0B], d.xf_regs[0x0C], d.xf_regs[0x0D],
            d.bp.reg[0xE0], d.bp.reg[0xE1], d.bp.reg[0xE2], d.bp.reg[0xE3], d.bp.reg[0xE4], d.bp.reg[0xE5], d.bp.reg[0xE6], d.bp.reg[0xE7]);
    for (int i = 0; i < 8; ++i) if (d.textures[i].used)
      fprintf(f, "   tex%d addr=%08X %ux%u fmt=%u tlut=%X/%u mode0=%06X mode1=%06X mips=%u\n", i, d.textures[i].addr, d.textures[i].width, d.textures[i].height, d.textures[i].format, d.textures[i].tlut_addr, d.textures[i].tlut_format, d.textures[i].mode0, d.textures[i].mode1, d.textures[i].mip_levels);
    for (uint32_t s = 0; s <= d.bp.numtevstages(); ++s)
      fprintf(f, "   tev%u cc=%06X ac=%06X order: map=%d coord=%d en=%d chan=%d\n", s, d.bp.tev_color(s), d.bp.tev_alpha(s), d.bp.order_texmap(s), d.bp.order_texcoord(s), d.bp.order_enable(s), d.bp.order_colorchan(s));
  }
  // Every unique shader in the frame, plus the light blocks of lit draws.
  std::unordered_map<uint64_t, bool> seen_vs, seen_ps;
  for (size_t di = 0; di < frame.draws.size(); ++di) {
    const DrawCall& d = frame.draws[di];
    VSUid vu = make_vs_uid(d); PSUid pu = make_ps_uid(d);
    uint64_t vh = vu.hash(), ph = pu.hash();
    if (!seen_vs[vh]) {
      seen_vs[vh] = true;
      fprintf(f, "\n---- vertex shader %016llX (first used by draw %zu) ----\n%s\n", (unsigned long long)vh, di, generate_vertex_shader(vu).c_str());
      if ((d.xf_regs[0x09] & 3) != 0) {
        for (int L = 0; L < 8; ++L) {
          const float* fl = (const float*)(d.lights[L] + 16);
          uint32_t col; memcpy(&col, d.lights[L] + 12, 4);
          fprintf(f, "   light%d color=%08X cosatt=(%g %g %g) distatt=(%g %g %g) pos=(%g %g %g) dir=(%g %g %g)\n", L, col,
                  fl[0], fl[1], fl[2], fl[3], fl[4], fl[5], fl[6], fl[7], fl[8], fl[9], fl[10], fl[11]);
        }
      }
    }
    if (!seen_ps[ph]) {
      seen_ps[ph] = true;
      fprintf(f, "\n---- pixel shader %016llX (first used by draw %zu) ----\n%s\n", (unsigned long long)ph, di, generate_pixel_shader(pu).c_str());
    }
  }
  fclose(f);
}

void D3D12Backend::submit_frame(const Frame& frame, const DrawMatrices* overrides) {
  struct FloatEnvironment {
    unsigned saved = _mm_getcsr();
    FloatEnvironment() { _mm_setcsr(0x1f80); }
    ~FloatEnvironment() { _mm_setcsr(saved); }
  } float_environment;
  ++frame_counter_;
  if (!opts_.dump_path.empty() && frame_counter_ == opts_.dump_frame) dump_frame(frame, opts_.dump_path);
  vertex_ring_.reset(); index_ring_.reset(); constant_ring_.reset(); upload_ring_.reset();
  srv_cursor_ = 0;
  frame_garbage_.clear();
  check(allocator_->Reset(), "allocator reset");
  check(list_->Reset(allocator_.Get(), nullptr), "list reset");
  ID3D12DescriptorHeap* heaps[] = {srv_heap_.Get(), sampler_heap_.Get()};
  list_->SetDescriptorHeaps(2, heaps);
  D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart(); rtv.ptr += 3 * rtv_size_;
  D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
  list_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
  if (have_clear_) { clear_efb(pending_clear_); have_clear_ = false; }
  bool presented = false;
  for (const FrameCommand& cmd : frame.commands) {
    if (cmd.kind == FrameCommand::Draw) {
      execute_draw(frame, frame.draws[cmd.index], overrides ? overrides + cmd.index : nullptr);
    } else {
      const EfbCopy& c = frame.copies[cmd.index];
      if (c.to_xfb) { present_efb(c); presented = true; }
      else execute_copy(c);
      if (c.clear) clear_efb(c);
    }
  }
  check(list_->Close(), "list close");
  ID3D12CommandList* lists[] = {list_.Get()};
  queue_->ExecuteCommandLists(1, lists);
  if (presented) {
    swapchain_->Present(opts_.vsync ? 1 : 0, opts_.vsync ? 0 : DXGI_PRESENT_ALLOW_TEARING);
    ++frames_presented_;
  }
  wait_gpu();
  if (presented && !opts_.capture_path.empty()) {
    capture_sequence_ = frame.sequence;
    if (opts_.capture_sim_frame && !opts_.capture_frame && frame.sequence >= opts_.capture_sim_frame) opts_.capture_frame = frames_presented_;
    bool burst = opts_.capture_burst && opts_.capture_frame && frames_presented_ >= opts_.capture_frame && frames_presented_ < opts_.capture_frame + opts_.capture_burst;
    if (frames_presented_ == opts_.capture_frame && !burst) capture_backbuffer();
    else if (burst || (opts_.capture_every && frames_presented_ % opts_.capture_every == 0)) {
      std::string saved = opts_.capture_path;
      char suffix[32]; snprintf(suffix, sizeof suffix, "_%05u.ppm", frames_presented_);
      opts_.capture_path = saved.substr(0, saved.size() > 4 && saved.compare(saved.size() - 4, 4, ".ppm") == 0 ? saved.size() - 4 : saved.size()) + suffix;
      capture_backbuffer();
      opts_.capture_path = saved;
    }
  }
}

}  // namespace

Backend* create_d3d12_backend(void* hwnd, int w, int h, const D3D12Options& options) {
  return new D3D12Backend((HWND)hwnd, w, h, options);
}
void d3d12_resize(Backend* backend, int w, int h) { static_cast<D3D12Backend*>(backend)->resize(w, h); }
void d3d12_stats(Backend* backend, uint32_t* frames, uint32_t* pipelines, uint32_t* textures) {
  auto* b = static_cast<D3D12Backend*>(backend);
  if (frames) *frames = b->frames_presented();
  if (pipelines) *pipelines = b->pipeline_count();
  if (textures) *textures = b->texture_count();
}

}  // namespace gx
