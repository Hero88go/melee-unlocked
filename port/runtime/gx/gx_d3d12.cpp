// D3D12 backend: replays captured GX frames into an EFB render target and presents XFB copies.
// SPDX-License-Identifier: GPL-2.0-or-later
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <mutex>
#include <deque>
#include <condition_variable>
#include <vector>
#include "gx_d3d12.h"
#include "exi_slippi.h"
#include "gx_shader.h"
#include "gx_texture.h"
#include "gx_streamline.h"
#include "host.h"
#include "window.h"   // fullscreen toggling lives on the window, not the settings panel
#ifdef GX_PC_SETTINGS
#include "pc_settings.h"
#endif

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

namespace gx {

namespace {

#ifndef GX_SRV_HEAP_SIZE
#define GX_SRV_HEAP_SIZE 65536
#endif
#ifndef GX_SAMPLER_HEAP_SIZE
#define GX_SAMPLER_HEAP_SIZE 2048
#endif
#ifndef GX_CONSTANT_PAGE_SIZE
#define GX_CONSTANT_PAGE_SIZE (32 << 20)
#endif

// execute_draw section costs (seconds) and draw count since the last profile line.
double g_prof[8]; uint64_t g_prof_draws = 0, g_pso_hits = 0, g_pso_lookups = 0, g_pso_creates = 0, g_pso_skips = 0;
struct Stopwatch {
  static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
  double t = now(); double lap() { double n = now(), d = n - t; t = n; return d; }
};

void check(HRESULT hr, const char* what) { if (FAILED(hr)) host::die("D3D12: %s failed (%08X)", what, (unsigned)hr); }
template <class T> const IID& IID_PPV_ARGS_Helper_IID() { return __uuidof(T); }

struct PsoKey {
  uint64_t vs, ps;
  uint32_t blend, zmode, cull, topology, pixel_format, mvec;
  bool operator==(const PsoKey& o) const {
    return vs == o.vs && ps == o.ps && blend == o.blend && zmode == o.zmode &&
        cull == o.cull && topology == o.topology && pixel_format == o.pixel_format && mvec == o.mvec;
  }
};
struct PsoKeyHash { size_t operator()(const PsoKey& k) const {
  const uint64_t fields[] = {k.vs, k.ps, k.blend, k.zmode, k.cull, k.topology, k.pixel_format, k.mvec};
  return (size_t)hash_bytes(fields, sizeof fields);
} };

struct TextureEntry {
  ComPtr<ID3D12Resource> resource;
  uint32_t width = 0, height = 0, levels = 1;
  uint64_t last_used = 0;
};

using TextureSetKey = std::array<ID3D12Resource*, 8>;
struct TextureSetHash {
  size_t operator()(const TextureSetKey& k) const { return (size_t)hash_bytes(k.data(), sizeof(ID3D12Resource*) * k.size()); }
};

struct SamplerSetKey {
  uint32_t mode0[8], mode1[8];
  bool operator==(const SamplerSetKey& o) const { return memcmp(this, &o, sizeof *this) == 0; }
};
struct SamplerSetHash { size_t operator()(const SamplerSetKey& k) const { return (size_t)hash_bytes(&k, sizeof k); } };

// Upload pages remain alive until the frame fence completes. Grow instead of
// dropping draws or exposing an incompletely uploaded texture when a page fills.
class Ring {
  struct Page {
    ComPtr<ID3D12Resource> buffer;
    uint8_t* cpu = nullptr;
    size_t size = 0, offset = 0;
  };
  ID3D12Device* device_ = nullptr;
  size_t page_size_ = 0, current_ = 0, slot_ = 0;
  std::vector<Page> slots_[3];      // one page set per frame in flight
  std::vector<Page>& pages() { return slots_[slot_]; }
  Page make_page(size_t size) {
    Page p; p.size = size;
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_UPLOAD};
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = size; rd.Height = 1;
    rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    check(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&p.buffer)), "upload page");
    check(p.buffer->Map(0, nullptr, (void**)&p.cpu), "upload page map");
    return p;
  }
 public:
  void init(ID3D12Device* dev, size_t size) {
    device_ = dev; page_size_ = size;
    for (auto& set : slots_) set.push_back(make_page(size));
  }
  // Selects the page set of frame slot `slot` (whose previous GPU work has completed).
  void reset(size_t slot) {
    slot_ = slot; current_ = 0;
    for (auto& p : pages()) p.offset = 0;
    // Retire exceptional overflow pages now that this slot's fence has passed.
    if (pages().size() > 1) pages().resize(1);
  }
  bool alloc(size_t bytes, size_t align, uint8_t** cpu, D3D12_GPU_VIRTUAL_ADDRESS* gpu) {
    size_t off = (pages()[current_].offset + align - 1) & ~(align - 1);
    if (off > pages()[current_].size || bytes > pages()[current_].size - off) {
      ++current_;
      pages().push_back(make_page(std::max(page_size_, (bytes + align - 1) & ~(align - 1))));
      off = 0;
    }
    auto& p = pages()[current_];
    *cpu = p.cpu + off; *gpu = p.buffer->GetGPUVirtualAddress() + off;
    p.offset = off + bytes;
    return true;
  }
  ID3D12Resource* resource() { return pages()[current_].buffer.Get(); }
};

struct PipelineRecipe {
  uint32_t topology = 0, components = 0;
  BPMemory bp{};
  uint32_t xf[0x58]{};
};
static std::atomic<uint64_t> next_backend_id{1};
class D3D12Backend : public Backend {
 public:
  D3D12Backend(HWND hwnd, int w, int h, const D3D12Options& o) : hwnd_(hwnd), opts_(o), client_w_(w), client_h_(h) { init(); start_pso_workers(); prewarm_pipelines();
#ifdef GX_PC_SETTINGS
    if (opts_.pc_settings) settings_ui_ = std::make_unique<PcSettingsUI>(hwnd, device_.Get(), queue_.Get(), opts_);
#endif
  }
  ~D3D12Backend() override { wait_gpu();
#ifdef GX_PC_SETTINGS
    settings_ui_.reset();
#endif
 stop_pso_workers(); integrate_compiled_psos(); flush_captures(); save_pipeline_recipes(); save_pipeline_library(); if (fence_event_) CloseHandle(fence_event_); if (present_timer_) CloseHandle(present_timer_);
    last_poses_.clear(); mvec_.Reset(); dlss_out_.Reset(); streamline::shutdown(); }
  const D3D12Options& options() const { return opts_; }
  void set_present_deadline(double deadline) override { present_deadline_ = deadline; }
  double presentation_wait_seconds() const override { return present_wait_; }
  void submit_frame(const Frame& frame) override { submit_frame(frame, nullptr); }
  void submit_frame(const Frame& frame, const DrawMatrices* overrides) override;
  void resize(int w, int h) {
    wait_gpu(); client_w_ = w; client_h_ = h; create_swapchain_targets(true);
    // Auto scale follows the window like Dolphin's "Auto (Window Size)" integral mode: the EFB is
    // re-created at the new multiplier and scaled EFB-copy textures are dropped (their size changed).
    if (opts_.efb_scale == 0 && pick_scale() != scale_) { efb_copies_.clear(); create_efb(); }
  }
  int scale() const { return scale_; }
  void set_skip_present(bool skip) override { skip_present_ = skip; }
  bool skip_present_ = false;
  uint32_t frames_presented() const { return frames_presented_; }
  uint32_t pipeline_count() const { return (uint32_t)psos_.size(); }
  uint32_t texture_count() const { return (uint32_t)textures_.size(); }

 private:
  const uint64_t backend_id_ = next_backend_id.fetch_add(1);
#ifdef GX_PC_SETTINGS
  std::unique_ptr<PcSettingsUI> settings_ui_;
#endif
  // DLSS: motion-vector target at EFB resolution, upscaled output at the letterboxed window size,
  // previous presented pose per draw identity for motion vectors, per-frame jitter.
  bool dlss_active_ = false;
  // DLAA renders and outputs at the same size, so it anti-aliases the EFB in place and the ordinary
  // present blit letterboxes the result, instead of DLSS producing a window-sized image.
  bool dlss_in_place_ = false;
  int dlss_mode_active_ = 0;
  bool widescreen_sent_ = false;
  int dlss_failures_ = 0;
  int anisotropy_applied_ = 0, ssaa_applied_ = 0;
  int forced_scale_ = 0;
  ComPtr<ID3D12Resource> mvec_, dlss_out_;
  uint32_t dlss_out_w_ = 0, dlss_out_h_ = 0;
  float jitter_x_ = 0, jitter_y_ = 0;
  bool dlss_reset_ = true;
  struct LastPose { float pos[256]; float proj[16]; uint64_t frame; };
  std::unordered_map<uint64_t, LastPose> last_poses_;
  void configure_dlss();
  void bind_efb_targets();
  void output_size(int* vw, int* vh) const;
  HANDLE present_timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0x2 /* high resolution */, TIMER_ALL_ACCESS);
  double present_deadline_ = 0, present_wait_ = 0;
  std::vector<PipelineRecipe> pipeline_recipes_;
  std::string shader_cache_root_;   // recipes.bin lives here, above the per-shader-version namespace
  bool prewarming_ = false;
  // Asynchronous pipeline creation (see get_pso).
  struct PsoJob { PsoKey key; VSUid vsu; PSUid psu; D3D12_PRIMITIVE_TOPOLOGY_TYPE topo; PipelineRecipe recipe; };
  struct PsoResult { PsoKey key; ComPtr<ID3D12PipelineState> pso; ComPtr<ID3DBlob> vs, ps; PipelineRecipe recipe; };
  std::unordered_set<PsoKey, PsoKeyHash> psos_pending_;
  struct FallbackKey { uint32_t blend, zmode, cull, topology, pixel_format, mvec, variant;
    bool operator==(const FallbackKey& o) const { return blend == o.blend && zmode == o.zmode && cull == o.cull && topology == o.topology && pixel_format == o.pixel_format && mvec == o.mvec && variant == o.variant; } };
  struct FallbackKeyHash { size_t operator()(const FallbackKey& k) const { return hash_bytes(&k, sizeof k); } };
  std::unordered_map<FallbackKey, ComPtr<ID3D12PipelineState>, FallbackKeyHash> fallback_psos_;
  ID3D12PipelineState* fallback_pso(const PsoKey& key, const DrawCall& dc, D3D12_PRIMITIVE_TOPOLOGY_TYPE topo);
  std::mutex pso_mutex_, pipeline_library_mutex_;
  std::condition_variable pso_cv_, pso_done_cv_;
  int pso_wait_budget_us_ = 0;   // per presented frame: how long draws may wait for their real pipeline
  std::deque<PsoJob> pso_jobs_;
  std::vector<PsoResult> pso_done_;
  std::vector<std::thread> pso_threads_;
  bool pso_quit_ = false;
  ComPtr<ID3D12PipelineState> build_pso(const PsoKey& key, const VSUid& vsu, const PSUid& psu, D3D12_PRIMITIVE_TOPOLOGY_TYPE topo,
                                        ComPtr<ID3DBlob>& vs, ComPtr<ID3DBlob>& ps);
  void pso_worker();
  void integrate_compiled_psos();
  void start_pso_workers() {
    // Many pipelines appear together at match start; spread the compiles over the spare cores.
    int workers = std::clamp((int)std::thread::hardware_concurrency() - 2, 2, 6);
    for (int i = 0; i < workers; ++i) pso_threads_.emplace_back([this] { pso_worker(); });
  }
  void stop_pso_workers() {
    { std::lock_guard<std::mutex> lk(pso_mutex_); pso_quit_ = true; }
    pso_cv_.notify_all();
    for (auto& t : pso_threads_) t.join();
    pso_threads_.clear();
  }
  void prewarm_pipelines();
  void save_pipeline_recipes();
  std::vector<uint32_t> index_scratch_;
  void init();
  void create_swapchain_targets(bool resize);
  void create_efb();
  int pick_scale() const;
  float output_aspect() const;
  void wait_gpu();
  uint32_t reserve_srvs(uint32_t count);
  void rotate_heap(D3D12_DESCRIPTOR_HEAP_TYPE type);
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
  // Persistent caches (opts_.shader_cache): compiled shader blobs as files, pipelines in a D3D12
  // pipeline library serialized at shutdown. First sessions compile; later ones load instantly.
  ComPtr<ID3D12PipelineLibrary> pipeline_library_;
  std::vector<uint8_t> pipeline_library_data_;
  bool pipeline_library_dirty_ = false;
  void open_pipeline_library();
  void save_pipeline_library();
  bool load_shader_blob(const std::string& path, ComPtr<ID3DBlob>& blob);
  void save_shader_blob(const std::string& path, ID3DBlob* blob);
  ComPtr<ID3D12CommandQueue> queue_;
  ComPtr<IDXGISwapChain3> swapchain_;
  ComPtr<ID3D12DescriptorHeap> rtv_heap_, dsv_heap_, srv_heap_, sampler_heap_;
  ComPtr<ID3D12Resource> backbuffers_[3];
  ComPtr<ID3D12Resource> efb_color_, efb_depth_;
  // Frames in flight: each slot owns a command allocator, upload rings and the resources it
  // retired; a slot is reused only after the fence value recorded at its submission completes.
  static constexpr int FRAME_SLOTS = 3;
  ComPtr<ID3D12CommandAllocator> allocators_[FRAME_SLOTS];
  uint64_t slot_fence_[FRAME_SLOTS] = {};
  int slot_ = 0;
  ComPtr<ID3D12GraphicsCommandList> list_;
  ComPtr<ID3D12Fence> fence_;
  HANDLE fence_event_ = nullptr;
  uint64_t fence_value_ = 0;
  void wait_fence(uint64_t value) {
    if (value && fence_->GetCompletedValue() < value) { fence_->SetEventOnCompletion(value, fence_event_); WaitForSingleObject(fence_event_, INFINITE); }
  }
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
  std::unordered_map<TextureSetKey, uint32_t, TextureSetHash> texture_sets_;
  std::vector<ComPtr<ID3D12DescriptorHeap>> descriptor_garbage_[FRAME_SLOTS];
  uint64_t frame_counter_ = 0;
  uint32_t frames_presented_ = 0;
  bool efb_is_rt_ = true;
  bool have_clear_ = false;
  EfbCopy pending_clear_{};
  std::vector<ComPtr<ID3D12Resource>> frame_garbage_[FRAME_SLOTS];
  std::vector<uint8_t> decode_scratch_;
};

void D3D12Backend::init() {
#ifdef _DEBUG
  { ComPtr<ID3D12Debug> dbg; if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) dbg->EnableDebugLayer(); }
#endif
  if (opts_.pc_settings || opts_.dlss_mode != 0) {
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe); size_t slash = dir.find_last_of(L"\\/"); if (slash != std::wstring::npos) dir.resize(slash);
    streamline::init(dir);
  }
  ComPtr<IDXGIFactory4> factory;
  check(streamline::create_dxgi_factory2(0, &IID_PPV_ARGS_Helper_IID<IDXGIFactory4>(), (void**)factory.GetAddressOf()), "factory");
  ComPtr<IDXGIAdapter1> adapter;
  for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 desc; adapter->GetDesc1(&desc);
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
    if (SUCCEEDED(streamline::d3d12_create_device(adapter.Get(), D3D_FEATURE_LEVEL_11_0, &IID_PPV_ARGS_Helper_IID<ID3D12Device>(), (void**)device_.GetAddressOf()))) {
      char name[128]; WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof name, nullptr, nullptr);
      host::log("d3d12: using %s", name);
      streamline::set_device(device_.Get());
      streamline::dlss_supported(adapter.Get());
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
  hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = GX_SRV_HEAP_SIZE; hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  check(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&srv_heap_)), "srv heap");
  hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER; hd.NumDescriptors = GX_SAMPLER_HEAP_SIZE;
  check(device_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&sampler_heap_)), "sampler heap");
  rtv_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  srv_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  sampler_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

  for (auto& a : allocators_) check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&a)), "allocator");
  check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators_[0].Get(), nullptr, IID_PPV_ARGS(&list_)), "list");
  list_->Close();
  check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "fence");
  fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);

  create_swapchain_targets(false);
  create_efb();
  open_pipeline_library();
  vertex_ring_.init(device_.Get(), 48 << 20);
  index_ring_.init(device_.Get(), 24 << 20);
  constant_ring_.init(device_.Get(), GX_CONSTANT_PAGE_SIZE);
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
  bp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; bp[1].Constants.Num32BitValues = 12; bp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC brsd{2, bp, 1, &ss, D3D12_ROOT_SIGNATURE_FLAG_NONE};
  check(D3D12SerializeRootSignature(&brsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err), "blit root");
  check(device_->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&blit_root_)), "blit root sig");
  const char* blit = R"(
Texture2D src : register(t0); SamplerState samp : register(s0);
cbuffer C : register(b0) { float4 rect; float4 sharp; float4 box; };  // rect: xy = uv scale, zw = uv offset; sharp: xy = texel size, z = amount; box: xy = taps per axis
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O VS(uint id : SV_VertexID) { O o; float2 p = float2((id << 1) & 2, id & 2); o.pos = float4(p * float2(2,-2) + float2(-1,1), 0, 1); o.uv = p * rect.xy + rect.zw; return o; }
// Downsampling: average the whole footprint of one output pixel (a box of taps x taps bilinear
// samples) instead of one bilinear sample, which at 3x or more would skip most of the rendered
// pixels (shimmering, jagged edges). This is what makes a 4x or 6x internal resolution look
// supersampled on a 1080p screen.
float4 downsample(float2 uv) {
  int nx = (int)box.x, ny = (int)box.y;
  if (nx <= 1 && ny <= 1) return src.Sample(samp, uv);
  float2 foot = sharp.xy * float2(nx, ny);   // footprint of one output pixel in uv
  float4 acc = 0;
  for (int y = 0; y < ny; ++y)
    for (int x = 0; x < nx; ++x)
      acc += src.Sample(samp, uv + (float2(x, y) + 0.5) / float2(nx, ny) * foot - 0.5 * foot);
  return acc / (nx * ny);
}
float4 PS(O i) : SV_Target {
  float4 c = downsample(i.uv);
  if (sharp.z <= 0.0) return c;
  // Contrast-adaptive sharpening (AMD CAS style): sharpen where local contrast allows it,
  // over neighbours one output pixel away.
  float2 step = sharp.xy * max(box.xy, 1.0);
  float3 n = src.Sample(samp, i.uv + float2(0, -step.y)).rgb, s = src.Sample(samp, i.uv + float2(0, step.y)).rgb;
  float3 w = src.Sample(samp, i.uv + float2(-step.x, 0)).rgb, e = src.Sample(samp, i.uv + float2(step.x, 0)).rgb;
  float3 mn = min(min(min(n, s), min(w, e)), c.rgb), mx = max(max(max(n, s), max(w, e)), c.rgb);
  float3 amp = sqrt(saturate(min(mn, 1.0 - mx) / max(mx, 1e-4)));
  float peak = -1.0 / lerp(8.0, 5.0, saturate(sharp.z));
  float3 wgt = amp * peak;
  float3 r = (c.rgb + (n + s + w + e) * wgt) / (1.0 + 4.0 * wgt);
  return float4(saturate(r), c.a);
})";
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
  if (forced_scale_ > 0) return std::clamp(forced_scale_, 1, max_scale);
  const int ssaa = std::clamp(opts_.ssaa, 1, 2);
  if (opts_.efb_scale > 0) return std::clamp(opts_.efb_scale * ssaa, 1, max_scale);
  float ww = (float)std::max(client_w_, 1), wh = (float)std::max(client_h_, 1);
  float aspect = output_aspect();
  float vw = ww, vh = ww / aspect;
  if (vh > wh) { vh = wh; vw = wh * aspect; }
  int s = std::max((int)std::ceil(vw / (480.0f * aspect)), (int)std::ceil(vh / 480.0f));
  return std::clamp(s * ssaa, 1, max_scale);
}

// The game renders the same 640x480 field either way, but its camera asks for a 73:60 frustum, and
// Slippi's widescreen code widens that to exactly 16:9. See presented_aspect in gx_d3d12.h.
float D3D12Backend::output_aspect() const { return presented_aspect(opts_, client_w_, client_h_); }

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
  rd.Format = DXGI_FORMAT_R16G16_FLOAT; rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  D3D12_CLEAR_VALUE mv{DXGI_FORMAT_R16G16_FLOAT, {0, 0, 0, 0}};
  check(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET, &mv, IID_PPV_ARGS(&mvec_)), "motion vectors");
  D3D12_CPU_DESCRIPTOR_HANDLE mrtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart(); mrtv.ptr += 5 * rtv_size_;
  device_->CreateRenderTargetView(mvec_.Get(), nullptr, mrtv);
  last_poses_.clear(); dlss_reset_ = true;
  efb_is_rt_ = true;
}

void D3D12Backend::bind_efb_targets() {
  D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2];
  rtvs[0] = rtv_heap_->GetCPUDescriptorHandleForHeapStart(); rtvs[0].ptr += 3 * rtv_size_;
  rtvs[1] = rtv_heap_->GetCPUDescriptorHandleForHeapStart(); rtvs[1].ptr += 5 * rtv_size_;
  D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
  list_->OMSetRenderTargets(dlss_active_ ? 2 : 1, rtvs, FALSE, &dsv);
}

void D3D12Backend::output_size(int* vw, int* vh) const {
  float ww = (float)std::max(client_w_, 1), wh = (float)std::max(client_h_, 1);
  float aspect = output_aspect();
  float w = ww, h = ww / aspect;
  if (h > wh) { h = wh; w = wh * aspect; }
  *vw = std::max(1, (int)w); *vh = std::max(1, (int)h);
}

// Reconciles the DLSS mode with the window: picks the integer EFB scale whose render size fits
// the mode's optimal/allowed range, allocates the output texture and tells Streamline.
void D3D12Backend::configure_dlss() {
  int vw, vh; output_size(&vw, &vh);
  bool want = opts_.dlss_mode != 0 && streamline::available();
  if (!want) {
    if (dlss_active_ || forced_scale_) {
      wait_gpu(); dlss_active_ = false; dlss_in_place_ = false; dlss_mode_active_ = 0; forced_scale_ = 0; dlss_out_.Reset(); dlss_out_w_ = dlss_out_h_ = 0;
      streamline::dlss_set_options(DlssMode::Off, vw, vh);
      if (pick_scale() != scale_) { efb_copies_.clear(); create_efb(); }
      host::log("dlss: off (native rendering at EFB x%d)", scale_);
    }
    return;
  }
  const bool in_place = (DlssMode)opts_.dlss_mode == DlssMode::DLAA;
  // DLAA only accepts a render size equal to its output size. The EFB is always a 640x480 multiple,
  // which can never equal a 16:9 window, so asking for a window-sized DLAA output left the render
  // larger than the mode's own maximum and it produced an empty image (a black screen). Anti-alias
  // the EFB in place instead, at the scale the player chose, and let the present blit letterbox it.
  int out_w = vw, out_h = vh, in_place_scale = 0;
  if (in_place) {
    // pick_scale() reports any scale a previous mode forced, so clear it first to get the EFB scale
    // the player actually chose (switching straight from DLSS Quality to DLAA would otherwise size
    // DLAA from the scale Quality had forced).
    const int saved = forced_scale_;
    forced_scale_ = 0;
    in_place_scale = pick_scale();
    forced_scale_ = saved;
    out_w = EFB_WIDTH * in_place_scale; out_h = EFB_HEIGHT * in_place_scale;
  }
  if (dlss_active_ && dlss_mode_active_ == opts_.dlss_mode && (int)dlss_out_w_ == out_w && (int)dlss_out_h_ == out_h) return;
  uint32_t rw = 0, rh = 0, min_w = 0, min_h = 0, max_w = 0, max_h = 0;
  if (!streamline::dlss_optimal_size((DlssMode)opts_.dlss_mode, (uint32_t)out_w, (uint32_t)out_h, &rw, &rh, &min_w, &min_h, &max_w, &max_h)) {
    host::log("dlss: optimal settings unavailable, staying native"); opts_.dlss_mode = 0; return;
  }
  int scale;
  if (in_place) {
    // DLAA is handed the whole EFB and writes an image the same size, so the render size is the EFB
    // itself (640x528 per step), not the 640x480 visible region the upscaling modes feed it. Running
    // the multiplier arithmetic below on a 480-tall assumption made the height constraint
    // unsatisfiable and DLAA never ran at all; the scale is simply the one the player chose.
    scale = in_place_scale;
    if ((min_w && (uint32_t)out_w < min_w) || (min_h && (uint32_t)out_h < min_h) ||
        (max_w && (uint32_t)out_w > max_w) || (max_h && (uint32_t)out_h > max_h)) {
      host::log("dlss: DLAA cannot render %dx%d (it accepts %ux%u to %ux%u); staying native",
                out_w, out_h, min_w, min_h, max_w, max_h);
      opts_.dlss_mode = 0; return;
    }
  } else {
    // Smallest integer EFB multiplier whose 640x480 visible region reaches the optimal size, within
    // the allowed range. The upscaling modes are fed that region, not the full EFB.
    scale = std::max(1, (int)std::ceil(std::max(rw / 640.0, rh / 480.0)));
    while (scale > 1 && ((max_w && 640u * scale > max_w) || (max_h && 480u * scale > max_h))) --scale;
    if (min_w && 640u * scale < min_w) scale = (int)((min_w + 639) / 640);
    if (min_h && 480u * scale < min_h) scale = std::max(scale, (int)((min_h + 479) / 480));
    // Raising the scale to reach the minimum can push it back past the maximum: when no multiplier
    // satisfies both, the mode cannot run at this output size, so stay native rather than render blind.
    if ((max_w && 640u * scale > max_w) || (max_h && 480u * scale > max_h) ||
        640u * scale > (uint32_t)out_w || 480u * scale > (uint32_t)out_h) {
      host::log("dlss: %s needs a render size between %ux%u and %ux%u for a %dx%d output and no EFB multiple fits; staying native",
                dlss_mode_name((DlssMode)opts_.dlss_mode), min_w, min_h, max_w, max_h, out_w, out_h);
      opts_.dlss_mode = 0; return;
    }
  }
  wait_gpu();
  forced_scale_ = scale;
  if (pick_scale() != scale_) { efb_copies_.clear(); create_efb(); }
  if (!dlss_out_ || (int)dlss_out_w_ != out_w || (int)dlss_out_h_ != out_h) {
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = out_w; rd.Height = out_h; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; rd.SampleDesc.Count = 1; rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    dlss_out_.Reset();
    check(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&dlss_out_)), "dlss output");
    dlss_out_w_ = out_w; dlss_out_h_ = out_h;
  }
  if (!streamline::dlss_set_options((DlssMode)opts_.dlss_mode, (uint32_t)out_w, (uint32_t)out_h)) { opts_.dlss_mode = 0; forced_scale_ = 0; dlss_active_ = false; return; }
  dlss_active_ = true; dlss_in_place_ = in_place; dlss_mode_active_ = opts_.dlss_mode; dlss_reset_ = true; last_poses_.clear();
  host::log("dlss: %s, render %dx%d (EFB x%d, optimal %ux%u) -> %s %dx%d%s", dlss_mode_name((DlssMode)opts_.dlss_mode),
            in_place ? out_w : 640 * scale, in_place ? out_h : 480 * scale, scale, rw, rh,
            in_place ? "anti-aliased in place at" : "output", out_w, out_h, in_place ? ", letterboxed to the window by the present blit" : "");
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
// Pipeline creation (shader compile + CreateGraphicsPipelineState) can take tens of milliseconds,
// so outside the startup prewarm it runs on worker threads: the draw that needs a new pipeline is
// skipped until it is ready (a few presented frames) instead of stalling the renderer and, behind
// it, the simulation and audio.
// Everything of a pipeline description that does not depend on the shaders' contents.
static void describe_pipeline(D3D12_GRAPHICS_PIPELINE_STATE_DESC& pd, const PsoKey& key, D3D12_PRIMITIVE_TOPOLOGY_TYPE topo,
                              ID3D12RootSignature* root, ID3DBlob* vs, ID3DBlob* ps) {
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
    // Vertex::texmtx[7] at offset 104. Without it texture generator 7 read generator 6's index.
    {"BLENDINDICES", 2, DXGI_FORMAT_R8_UINT, 0, 104, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };
  pd.pRootSignature = root;
  pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
  pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
  pd.InputLayout = {layout, _countof(layout)};
  pd.SampleMask = UINT_MAX;
  // Blend
  uint32_t bm = key.blend;
  bool alpha_in_efb = key.pixel_format == 1;  // RGBA6_Z24
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
  pd.RasterizerState.CullMode = cull_modes[key.cull & 3];
  pd.RasterizerState.FrontCounterClockwise = FALSE;
  pd.RasterizerState.DepthClipEnable = TRUE;
  // Depth (reversed range: GC near = 1.0)
  uint32_t zm = key.zmode;
  static const D3D12_COMPARISON_FUNC cmp[] = {D3D12_COMPARISON_FUNC_NEVER, D3D12_COMPARISON_FUNC_GREATER, D3D12_COMPARISON_FUNC_EQUAL,
                                              D3D12_COMPARISON_FUNC_GREATER_EQUAL, D3D12_COMPARISON_FUNC_LESS, D3D12_COMPARISON_FUNC_NOT_EQUAL,
                                              D3D12_COMPARISON_FUNC_LESS_EQUAL, D3D12_COMPARISON_FUNC_ALWAYS};
  pd.DepthStencilState.DepthEnable = bits(zm, 0, 1);
  pd.DepthStencilState.DepthWriteMask = (bits(zm, 0, 1) && bits(zm, 4, 1)) ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
  pd.DepthStencilState.DepthFunc = bits(zm, 0, 1) ? cmp[bits(zm, 1, 3)] : D3D12_COMPARISON_FUNC_ALWAYS;
  pd.PrimitiveTopologyType = topo;
  pd.NumRenderTargets = key.mvec ? 2 : 1; pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; pd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
  if (key.mvec) {
    pd.RTVFormats[1] = DXGI_FORMAT_R16G16_FLOAT;
    pd.BlendState.IndependentBlendEnable = TRUE;
    pd.BlendState.RenderTarget[1] = D3D12_RENDER_TARGET_BLEND_DESC{};
    pd.BlendState.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN;
  }
  pd.SampleDesc.Count = 1;
}

ComPtr<ID3D12PipelineState> D3D12Backend::build_pso(const PsoKey& key, const VSUid& vsu, const PSUid& psu, D3D12_PRIMITIVE_TOPOLOGY_TYPE topo,
                                                    ComPtr<ID3DBlob>& vs, ComPtr<ID3DBlob>& ps) {
  char vs_name[64], ps_name[64];
  snprintf(vs_name, sizeof vs_name, "vs_%016llX.dxbc", (unsigned long long)key.vs);
  snprintf(ps_name, sizeof ps_name, "ps_%016llX.dxbc", (unsigned long long)key.ps);
  if (!vs && !load_shader_blob(opts_.shader_cache + "/" + vs_name, vs)) {
    std::string src = generate_vertex_shader(vsu);
    ComPtr<ID3DBlob> err;
    if (FAILED(D3DCompile(src.c_str(), src.size(), "vs", nullptr, nullptr, "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs, &err))) {
      host::log("vertex shader compile failed:\n%s\n%s", err ? (const char*)err->GetBufferPointer() : "?", src.c_str());
      host::die("vertex shader compile failed");
    }
    save_shader_blob(opts_.shader_cache + "/" + vs_name, vs.Get());
  }
  if (!ps && !load_shader_blob(opts_.shader_cache + "/" + ps_name, ps)) {
    std::string src = generate_pixel_shader(psu);
    ComPtr<ID3DBlob> err;
    if (FAILED(D3DCompile(src.c_str(), src.size(), "ps", nullptr, nullptr, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, &err))) {
      host::log("pixel shader compile failed:\n%s\n%s", err ? (const char*)err->GetBufferPointer() : "?", src.c_str());
      host::die("pixel shader compile failed");
    }
    save_shader_blob(opts_.shader_cache + "/" + ps_name, ps.Get());
  }
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
  describe_pipeline(pd, key, topo, root_.Get(), vs.Get(), ps.Get());
  ComPtr<ID3D12PipelineState> pso;
  wchar_t pso_name[96];
  swprintf_s(pso_name, L"%016llX-%016llX-%X-%X-%X-%X-%X-%X", (unsigned long long)key.vs, (unsigned long long)key.ps, key.blend, key.zmode, key.cull, key.topology, key.pixel_format, key.mvec);
  std::lock_guard<std::mutex> lk(pipeline_library_mutex_);
  if (!pipeline_library_ || FAILED(pipeline_library_->LoadGraphicsPipeline(pso_name, &pd, IID_PPV_ARGS(&pso)))) {
    check(device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso)), "pso");
    if (pipeline_library_ && SUCCEEDED(pipeline_library_->StorePipeline(pso_name, pso.Get()))) pipeline_library_dirty_ = true;
  }
  return pso;
}

void D3D12Backend::pso_worker() {
  for (;;) {
    PsoJob job;
    {
      std::unique_lock<std::mutex> lk(pso_mutex_);
      pso_cv_.wait(lk, [&] { return pso_quit_ || !pso_jobs_.empty(); });
      if (pso_quit_) return;
      job = pso_jobs_.front(); pso_jobs_.pop_front();
    }
    PsoResult r; r.key = job.key; r.recipe = job.recipe;
    r.pso = build_pso(job.key, job.vsu, job.psu, job.topo, r.vs, r.ps);
    { std::lock_guard<std::mutex> lk(pso_mutex_); pso_done_.push_back(std::move(r)); }
    pso_done_cv_.notify_all();
  }
}

void D3D12Backend::integrate_compiled_psos() {
  std::vector<PsoResult> done;
  { std::lock_guard<std::mutex> lk(pso_mutex_); done.swap(pso_done_); }
  for (auto& r : done) {
    psos_[r.key] = r.pso;
    psos_pending_.erase(r.key);
    if (r.vs && !vs_blobs_[r.key.vs]) vs_blobs_[r.key.vs] = r.vs;
    if (r.ps && !ps_blobs_[r.key.ps]) ps_blobs_[r.key.ps] = r.ps;
    if (r.key.mvec == 0 && pipeline_recipes_.size() < 16384) pipeline_recipes_.push_back(r.recipe);
  }
}


// While a draw's real pipeline compiles on a worker, draw it with a generic one (position, vertex
// colour, texture 0) instead of skipping it: a few frames of approximate shading beat objects
// popping in and out. Built synchronously (the shaders are tiny), one per raster state.
ID3D12PipelineState* D3D12Backend::fallback_pso(const PsoKey& key, const DrawCall& dc, D3D12_PRIMITIVE_TOPOLOGY_TYPE topo) {
  const bool textured = (dc.xf_regs[0x3F] & 15) != 0 && (dc.components & VB_HAS_UV0);
  const bool colored = (dc.components & VB_HAS_COL0) != 0;
  FallbackKey fk{key.blend, key.zmode, key.cull, key.topology, key.pixel_format, key.mvec, (uint32_t)textured | ((uint32_t)colored << 1)};
  auto it = fallback_psos_.find(fk);
  if (it != fallback_psos_.end()) return it->second.Get();
  static const char* vs_src = R"(
cbuffer VSBlock : register(b0) {
float4 projection[4]; float4 depthparams; float4 viewparams; float4 materials[4]; float4 lights[40];
float4 texmatrices[24]; float4 transformmatrices[64]; float4 normalmatrices[32]; float4 posttransformmatrices[64];
float4 unjittered_projection[4]; float4 prev_projection[4]; float4 prev_transformmatrices[64]; };
struct VS_OUTPUT { float4 pos : SV_Position; float4 color : COLOR0; float2 uv : TEXCOORD0; };
VS_OUTPUT main(float3 rawpos : POSITION, float3 rawnorm0 : NORMAL0, float4 color0 : COLOR0, float4 color1 : COLOR1,
  float2 rawtex0 : TEXCOORD0, float2 rawtex1 : TEXCOORD1, float2 rawtex2 : TEXCOORD2, float2 rawtex3 : TEXCOORD3,
  float2 rawtex4 : TEXCOORD4, float2 rawtex5 : TEXCOORD5, float2 rawtex6 : TEXCOORD6, float2 rawtex7 : TEXCOORD7,
  uint4 blend_indices : BLENDINDICES, uint4 blend_indices2 : BLENDINDICES1) {
  VS_OUTPUT o;
  int posmtx = int(blend_indices.x);
  float4 rawpos4 = float4(rawpos, 1.0);
  float4 pos = float4(dot(transformmatrices[posmtx], rawpos4), dot(transformmatrices[posmtx+1], rawpos4), dot(transformmatrices[posmtx+2], rawpos4), 1);
  o.pos = float4(dot(projection[0], pos), dot(projection[1], pos), dot(projection[2], pos), dot(projection[3], pos));
  o.color = COLOR_EXPR;
  float4 coord = float4(rawtex0, 1.0, 1.0);
  o.uv = float2(dot(coord, texmatrices[0]), dot(coord, texmatrices[1]));
  o.pos.z = o.pos.w * depthparams.x - o.pos.z * depthparams.y;
  o.pos.xy *= sign(depthparams.zw * float2(-1.0, 1.0));
  o.pos.xy = o.pos.xy + o.pos.w * depthparams.zw;
  if (o.pos.w == 1.0) { o.pos.xy = round(o.pos.xy * viewparams.xy) * viewparams.zw; }
  return o;
})";
  static const char* ps_src = R"(
SamplerState samp[8] : register(s0); Texture2D Tex[8] : register(t0);
void main(out float4 ocol0 : SV_Target0 MVEC_OUT, in float4 rawpos : SV_Position, in float4 color : COLOR0, in float2 uv : TEXCOORD0) {
  float4 c = color;
  TEX_EXPR
  if (c.a <= 0.0) discard;
  ocol0 = c;
  MVEC_WRITE
})";
  std::string vs = vs_src, ps = ps_src;
  vs.replace(vs.find("COLOR_EXPR"), 10, colored ? "color0" : "float4(1.0, 1.0, 1.0, 1.0)");
  ps.replace(ps.find("TEX_EXPR"), 8, textured ? "c *= Tex[0].Sample(samp[0], uv);" : "");
  ps.replace(ps.find("MVEC_OUT"), 8, key.mvec ? ", out float2 omv : SV_Target1" : "");
  ps.replace(ps.find("MVEC_WRITE"), 10, key.mvec ? "omv = float2(0.0, 0.0);" : "");
  ComPtr<ID3DBlob> vsb, psb, err;
  if (FAILED(D3DCompile(vs.c_str(), vs.size(), "fallback_vs", nullptr, nullptr, "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsb, &err)) ||
      FAILED(D3DCompile(ps.c_str(), ps.size(), "fallback_ps", nullptr, nullptr, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psb, &err))) {
    host::log("d3d12: fallback shader compile failed: %s", err ? (const char*)err->GetBufferPointer() : "?");
    fallback_psos_[fk] = nullptr; return nullptr;
  }
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
  describe_pipeline(pd, key, topo, root_.Get(), vsb.Get(), psb.Get());
  ComPtr<ID3D12PipelineState> pso;
  if (FAILED(device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso)))) { host::log("d3d12: fallback pipeline creation failed"); fallback_psos_[fk] = nullptr; return nullptr; }
  fallback_psos_[fk] = pso;
  return pso.Get();
}

ID3D12PipelineState* D3D12Backend::get_pso(const DrawCall& dc, D3D12_PRIMITIVE_TOPOLOGY_TYPE topo) {
  // The motion-vector variant is part of the real pipeline key: it changes shader generation and the
  // render target count. Caching on the backend alone handed a draw the other variant's pipeline
  // the moment DLSS was switched on or off, so the variant belongs in the cached owner as well.
  // Doubling keeps these ids far below the D3D11 backend's id base, so the two spaces stay disjoint.
  const uint64_t owner = backend_id_ * 2 + (dlss_active_ ? 1u : 0u);
  if (dc.cached_pipeline && dc.cached_pipeline_owner == owner) { ++g_pso_hits; return (ID3D12PipelineState*)dc.cached_pipeline; }
  Stopwatch sw;
  VSUid vsu = make_vs_uid(dc);
  PSUid psu = make_ps_uid(dc);
  vsu.motion_vectors = psu.motion_vectors = dlss_active_ ? 1u : 0u;
  uint64_t vh = vsu.hash(), ph = psu.hash();
  PsoKey key{vh, ph, dc.bp.blendmode() & 0xFFFF, dc.bp.zmode() & 0x1F, dc.bp.cullmode(), (uint32_t)topo, dc.bp.zcontrol() & 7, dlss_active_ ? 1u : 0u};
  g_prof[6] += sw.lap(); ++g_pso_lookups;   // uid build + hash
  auto it = psos_.find(key);
  g_prof[7] += sw.lap();                    // map lookup
  if (it != psos_.end()) { dc.cached_pipeline_owner = owner; dc.cached_pipeline = it->second.Get(); return it->second.Get(); }
  PipelineRecipe recipe{}; recipe.topology = (uint32_t)topo; recipe.components = dc.components;
  recipe.bp = dc.bp; std::memcpy(recipe.xf, dc.xf_regs, sizeof(recipe.xf));
  if (!prewarming_ && !pso_threads_.empty()) {
    if (psos_pending_.insert(key).second) {
      ++g_pso_creates;
      std::lock_guard<std::mutex> lk(pso_mutex_);
      pso_jobs_.push_front(PsoJob{key, vsu, psu, topo, recipe});   // a draw is waiting on it: ahead of prewarm work
      pso_cv_.notify_one();
    }
    // Give the workers a bounded slice of this presented frame to deliver the real pipeline (a
    // compile is usually 2 to 10 ms). Only past the budget does the draw fall back to the generic
    // pipeline, so a new matchup costs a short display hitch instead of black or missing surfaces.
    while (pso_wait_budget_us_ > 0) {
      Stopwatch wait_sw;
      {
        std::unique_lock<std::mutex> lk(pso_mutex_);
        pso_done_cv_.wait_for(lk, std::chrono::microseconds(std::min(pso_wait_budget_us_, 2000)), [&] { return !pso_done_.empty(); });
      }
      pso_wait_budget_us_ -= (int)(wait_sw.lap() * 1e6);
      integrate_compiled_psos();
      auto ready = psos_.find(key);
      if (ready != psos_.end()) { dc.cached_pipeline_owner = owner; dc.cached_pipeline = ready->second.Get(); return ready->second.Get(); }
    }
    ++g_pso_skips;
    return fallback_pso(key, dc, topo);   // approximate shading until the worker delivers the pipeline
  }
  ++g_pso_creates;
  ComPtr<ID3DBlob>& vs = vs_blobs_[vh];
  ComPtr<ID3DBlob>& ps = ps_blobs_[ph];
  ComPtr<ID3D12PipelineState> pso = build_pso(key, vsu, psu, topo, vs, ps);
  psos_[key] = pso;
  if (!prewarming_ && pipeline_recipes_.size() < 4096) pipeline_recipes_.push_back(recipe);
  dc.cached_pipeline_owner = owner; dc.cached_pipeline = pso.Get();
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

// Recorded descriptor tables stay valid until the GPU finishes this frame.
// Never wrap and overwrite descriptors referenced by an earlier draw.
void D3D12Backend::rotate_heap(D3D12_DESCRIPTOR_HEAP_TYPE type) {
  const bool sampler = type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
  auto& heap = sampler ? sampler_heap_ : srv_heap_;
  descriptor_garbage_[slot_].push_back(heap);
  D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
  ComPtr<ID3D12DescriptorHeap> replacement;
  check(device_->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&replacement)), "descriptor rollover");
  heap = std::move(replacement);
  if (sampler) { sampler_sets_.clear(); sampler_slots_used_ = 0; }
  else { srv_cursor_ = 0; texture_sets_.clear(); }
  ID3D12DescriptorHeap* heaps[] = {srv_heap_.Get(), sampler_heap_.Get()};
  list_->SetDescriptorHeaps(2, heaps);
}

uint32_t D3D12Backend::reserve_srvs(uint32_t count) {
  if (srv_cursor_ + count > GX_SRV_HEAP_SIZE) rotate_heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  uint32_t base = srv_cursor_; srv_cursor_ += count;
  return base;
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12Backend::bind_textures(const DrawCall& dc) {
  TextureSetKey resources{};
  for (int i = 0; i < 8; ++i) {
    uint32_t w = 1, h = 1;
    if (dc.textures[i].used) resources[i] = get_texture(dc.textures[i], &w, &h);
  }
  auto existing = texture_sets_.find(resources);
  uint32_t base;
  if (existing != texture_sets_.end()) base = existing->second;
  else {
    base = reserve_srvs(8);
    texture_sets_[resources] = base;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = srv_heap_->GetCPUDescriptorHandleForHeapStart();
    for (int i = 0; i < 8; ++i) {
      D3D12_CPU_DESCRIPTOR_HANDLE h = cpu; h.ptr += (base + i) * srv_size_;
      D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
      sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      sd.Texture2D.MipLevels = resources[i] ? -1 : 1;
      device_->CreateShaderResourceView(resources[i], &sd, h);
    }
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
    if (sampler_slots_used_ + 8 > GX_SAMPLER_HEAP_SIZE) rotate_heap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
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
      // Anisotropic filtering where the game asked for linear sampling (Dolphin's "Anisotropic Filtering").
      if (opts_.anisotropy > 1 && min_linear && mag_linear) sd.Filter = D3D12_FILTER_ANISOTROPIC;
      sd.MipLODBias = (float)(int32_t)((int32_t)sbits(m0, 9, 8)) / 32.0f;
      sd.MinLOD = bits(m1, 0, 8) / 16.0f;
      sd.MaxLOD = mip ? bits(m1, 8, 8) / 16.0f : 0.0f;
      sd.MaxAnisotropy = (UINT)std::clamp(opts_.anisotropy, 1, 16);
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
  // Optional quality reduction, for machines that cannot hold the frame rate. Submitting draws is the
  // largest cost per frame, so dropping decorative ones is the most direct saving available. Only
  // world-space draws qualify: HUD and menus use a different projection (xf_regs[0x26]) and are left
  // alone, so percentages, stocks and the timer are never affected. This changes only what is drawn,
  // never guest memory, so it cannot desync and two players may run different settings.
  static uint64_t g_effects_submitted = 0;   // world draws that survived the effects filter
  if (opts_.effects_level > 0 && dc.xf_regs[0x26] == 0 && (dc.bp.blendmode() & 1)) {
    const uint32_t blend = dc.bp.blendmode();
    const bool writes_depth = (dc.bp.zmode() & 0x10) != 0;
    // Additive blending (destination factor ONE) is what glow, sparks and flashes use. Matching any
    // blending at all removed about 830 of 1936 draws per frame, most of the translucent stage, which
    // is a different setting from the one intended.
    const uint32_t dst_factor = (blend >> 5) & 7;
    const bool additive = dst_factor == 1;
    // Level 2 used to skip every blended draw that does not write depth, which in Melee is most of
    // the world: it removed the entire scene and left a blank screen. A draw that neither writes nor
    // tests depth cannot be part of the scene's geometry, so that is the safe wider category.
    const bool tests_depth = (dc.bp.zmode() & 1) != 0;
    const bool overlay = !writes_depth && !tests_depth;
    if ((additive && !writes_depth) || (opts_.effects_level >= 2 && overlay)) {
      // Counted so the setting can be shown to do something: a filter that silently matches nothing
      // looks exactly like one that works but is lost in frame-rate noise.
      static uint64_t skipped = 0;
      if (++skipped % 20000 == 0) host::log("effects: skipped %llu draws of %llu submitted at level %d",
                                            (unsigned long long)skipped, (unsigned long long)g_effects_submitted, opts_.effects_level);
      return;
    }
    ++g_effects_submitted;
  }
  // Build index list (triangle list / line list) from the GX primitive.
  Stopwatch sw;
  auto& idx = index_scratch_; idx.clear();
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
  ++g_prof_draws; g_prof[0] += sw.lap();   // index generation
  // Vertices
  uint8_t* vcpu; D3D12_GPU_VIRTUAL_ADDRESS vgpu;
  size_t vbytes = (size_t)n * sizeof(Vertex);
  if (!vertex_ring_.alloc(vbytes, 16, &vcpu, &vgpu)) { host::log("d3d12: vertex ring full"); return; }
  const Vertex* vsrc = (override_matrices && override_matrices->vertices) ? override_matrices->vertices : &frame.vertices[dc.first_vertex];
  memcpy(vcpu, vsrc, vbytes);
  uint8_t* icpu; D3D12_GPU_VIRTUAL_ADDRESS igpu;
  if (!index_ring_.alloc(idx.size() * 4, 4, &icpu, &igpu)) { host::log("d3d12: index ring full"); return; }
  memcpy(icpu, idx.data(), idx.size() * 4);
  g_prof[1] += sw.lap();   // vertex/index upload
  // Constants
  uint8_t* ccpu; D3D12_GPU_VIRTUAL_ADDRESS vs_gpu, ps_gpu;
  // Only the part of the block this draw's shader can read is uploaded: the post-transform and
  // previous-pose matrices sit at the end and are conditional, so a typical draw sends 2.7 KB
  // instead of 4.9 KB.
  const size_t vs_bytes = vs_constants_bytes(dc, dlss_active_);
  if (!constant_ring_.alloc(vs_bytes, 256, &ccpu, &vs_gpu)) { host::log("d3d12: constant ring full"); return; }
  // Upload heaps can be write-combined: build scattered constants in normal
  // CPU memory, then copy contiguously rather than touching the mapped heap
  // repeatedly with partial writes.
  VSConstants vs_constants;
  if (dlss_active_) {
    MotionInfo motion; motion.jitter_x = jitter_x_; motion.jitter_y = jitter_y_;
    LastPose& last = last_poses_[dc.identity];
    bool had = last.frame != 0 && last.frame + 2 >= frame_counter_;
    if (had) { motion.prev_pos = last.pos; motion.prev_proj = last.proj; }
    fill_vs_constants(dc, vs_constants, scale_, override_matrices, &motion);
    std::memcpy(last.pos, override_matrices ? override_matrices->pos : dc.posMatrices, sizeof last.pos);
    std::memcpy(last.proj, vs_constants.unjittered_projection, sizeof last.proj);
    last.frame = frame_counter_;
  } else {
    fill_vs_constants(dc, vs_constants, scale_, override_matrices);
  }
  std::memcpy(ccpu, &vs_constants, vs_bytes);
  if (!constant_ring_.alloc(sizeof(PSConstants), 256, &ccpu, &ps_gpu)) return;
  PSConstants ps_constants;
  fill_ps_constants(dc, ps_constants, scale_);
  std::memcpy(ccpu, &ps_constants, sizeof(ps_constants));
  g_prof[2] += sw.lap();   // constants
  // Pipeline
  ID3D12PipelineState* pso = get_pso(dc, topo);
  g_prof[3] += sw.lap();   // pso
  if (!pso) return;        // being compiled on a worker thread
  D3D12_GPU_DESCRIPTOR_HANDLE srvs = bind_textures(dc);
  D3D12_GPU_DESCRIPTOR_HANDLE samps = bind_samplers(dc);
  g_prof[4] += sw.lap();   // textures + samplers
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
  g_prof[5] += sw.lap();   // state + draw calls
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
    if (e.resource) frame_garbage_[slot_].push_back(e.resource);
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
    uint32_t slot = reserve_srvs(1);
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
    float rect[12] = {(float)c.src_w / EFB_WIDTH, (float)c.src_h / EFB_HEIGHT, (float)c.src_x / EFB_WIDTH, (float)c.src_y / EFB_HEIGHT,
                      0, 0, 0, 0, 1.0f, 1.0f, 0, 0};   // no sharpening or averaging on this path
    list_->SetGraphicsRoot32BitConstants(1, 12, rect, 0);
    list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list_->DrawInstanced(3, 1, 0, 0);
    // Restore the EFB as the render target; execute_draw re-sets viewport/scissor per draw.
    bind_efb_targets();
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
  // DLSS: upscale the jittered EFB region (with depth and motion vectors) into the output texture first.
  bool upscaled = false;
  if (dlss_active_ && dlss_out_) {
    streamline::EvaluateInputs in{};
    in.color_in = efb_color_.Get(); in.color_state = D3D12_RESOURCE_STATE_RENDER_TARGET;
    in.depth = efb_depth_.Get(); in.depth_state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    in.mvec = mvec_.Get(); in.mvec_state = D3D12_RESOURCE_STATE_RENDER_TARGET;
    in.color_out = dlss_out_.Get(); in.out_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    // In place (DLAA) the render and output sizes must match exactly, so feed the whole EFB and let
    // the present blit pick the displayed region out of the result, exactly as it does without DLSS.
    if (dlss_in_place_) { in.in_left = 0; in.in_top = 0; in.in_w = efb_w_; in.in_h = efb_h_; }
    else {
      in.in_left = c.src_x * scale_; in.in_top = c.src_y * scale_;
      in.in_w = std::min<uint32_t>(c.src_w * scale_, efb_w_ - in.in_left); in.in_h = std::min<uint32_t>(c.src_h * scale_, efb_h_ - in.in_top);
    }
    in.out_w = dlss_out_w_; in.out_h = dlss_out_h_;
    upscaled = streamline::evaluate(list_.Get(), in);
    if (!upscaled && ++dlss_failures_ >= 30) {
      host::log("dlss: evaluation keeps failing; switching Upscaling back to Native");
      opts_.dlss_mode = 0; dlss_failures_ = 0;
    } else if (upscaled) dlss_failures_ = 0;
    ID3D12DescriptorHeap* heaps[] = {srv_heap_.Get(), sampler_heap_.Get()};
    list_->SetDescriptorHeaps(2, heaps);
  }
  ID3D12Resource* source = upscaled ? dlss_out_.Get() : efb_color_.Get();
  D3D12_RESOURCE_STATES source_state = upscaled ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_RENDER_TARGET;
  D3D12_RESOURCE_BARRIER b[2]{};
  b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b[0].Transition = {source, 0, source_state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
  b[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b[1].Transition = {backbuffers_[bb].Get(), 0, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET};
  list_->ResourceBarrier(2, b);
  // SRV for the presented image
  uint32_t slot = reserve_srvs(1);
  D3D12_CPU_DESCRIPTOR_HANDLE h = srv_heap_->GetCPUDescriptorHandleForHeapStart(); h.ptr += slot * srv_size_;
  device_->CreateShaderResourceView(source, nullptr, h);
  D3D12_GPU_DESCRIPTOR_HANDLE g = srv_heap_->GetGPUDescriptorHandleForHeapStart(); g.ptr += slot * srv_size_;
  D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart(); rtv.ptr += bb * rtv_size_;
  float border[4] = {0, 0, 0, 1};   // letterbox/pillarbox bars (black, as on Dolphin and a TV)
  list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  list_->ClearRenderTargetView(rtv, border, 0, nullptr);
  // Letterbox the output at the game's aspect (XFB region c.src_w x lines); the widescreen
  // setting also drives the Slippi code on the simulation side.
  if (opts_.widescreen != widescreen_sent_) { widescreen_sent_ = opts_.widescreen; slippi::request_widescreen(opts_.widescreen); }
  float src_h_lines = (float)c.src_h * c.y_scale;
  float aspect = output_aspect();
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
  // In-place DLAA leaves a full-size EFB image, so it is presented exactly like the un-upscaled EFB:
  // same displayed sub-region, same box filter when the render is larger than the window.
  const bool fills_output = upscaled && !dlss_in_place_;
  float src_w = upscaled ? (float)dlss_out_w_ : (float)efb_w_, src_h = upscaled ? (float)dlss_out_h_ : (float)efb_h_;
  float rect[12] = {(float)c.src_w / EFB_WIDTH, (float)c.src_h / EFB_HEIGHT, (float)c.src_x / EFB_WIDTH, (float)c.src_y / EFB_HEIGHT,
                    1.0f / std::max(src_w, 1.0f), 1.0f / std::max(src_h, 1.0f), std::clamp(opts_.sharpness, 0.0f, 1.0f), 0.0f,
                    1.0f, 1.0f, 0.0f, 0.0f};
  // Averaging box when the rendered image is larger than the output. The two axes shrink by
  // different amounts (the picture is letterboxed to 16:9 inside the window), so they get their
  // own tap counts; using the horizontal count for both left vertical edges aliasing.
  if (!fills_output) {
    rect[8] = (float)std::clamp((int)std::lround((double)c.src_w * scale_ / std::max(vw, 1.0f)), 1, 4);
    rect[9] = (float)std::clamp((int)std::lround((double)c.src_h * scale_ / std::max(vh, 1.0f)), 1, 4);
  }
  if (fills_output) { rect[0] = 1.0f; rect[1] = 1.0f; rect[2] = 0.0f; rect[3] = 0.0f; }
  list_->SetGraphicsRoot32BitConstants(1, 12, rect, 0);
  list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  list_->DrawInstanced(3, 1, 0, 0);
#ifdef GX_PC_SETTINGS
  if (settings_ui_) {
    settings_ui_->draw(list_.Get());
    ID3D12DescriptorHeap* heaps[] = {srv_heap_.Get(), sampler_heap_.Get()};
    list_->SetDescriptorHeaps(2, heaps);
  }
#endif
  D3D12_RESOURCE_BARRIER back[2]{};
  back[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  back[0].Transition = {source, 0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, source_state};
  back[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  back[1].Transition = {backbuffers_[bb].Get(), 0, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT};
  list_->ResourceBarrier(2, back);
  // Restore EFB as the render target for any later commands in this frame.
  bind_efb_targets();
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
  wait_gpu();   // the readback reuses this slot's allocator: every in-flight frame must be done
  allocators_[slot_]->Reset(); list_->Reset(allocators_[slot_].Get(), nullptr);
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
  integrate_compiled_psos();
  if (opts_.anisotropy != anisotropy_applied_) { anisotropy_applied_ = opts_.anisotropy; wait_gpu(); sampler_sets_.clear(); }
  if (opts_.ssaa != ssaa_applied_ || (!dlss_active_ && pick_scale() != scale_)) {
    ssaa_applied_ = opts_.ssaa;
    if (pick_scale() != scale_) { wait_gpu(); efb_copies_.clear(); create_efb(); host::log("d3d12: internal resolution now EFB x%d", scale_); }
  }
  struct FloatEnvironment {
    unsigned saved = _mm_getcsr();
    FloatEnvironment() { _mm_setcsr(0x1f80); }
    ~FloatEnvironment() { _mm_setcsr(saved); }
  } float_environment;
#ifdef GX_PC_SETTINGS
  if (settings_ui_ && settings_ui_->begin(opts_)) {
    host::window_set_fullscreen(opts_.fullscreen);
    if (pick_scale() != scale_) { wait_gpu(); efb_copies_.clear(); create_efb(); }
  }
#endif
  if (host::window_take_fullscreen_toggle()) {   // Alt+Enter
    opts_.fullscreen = !opts_.fullscreen;
    host::window_set_fullscreen(opts_.fullscreen);
    if (pick_scale() != scale_) { wait_gpu(); efb_copies_.clear(); create_efb(); }
  }
  configure_dlss();
  pso_wait_budget_us_ = 12000;
  ++frame_counter_;
  if (!opts_.dump_path.empty() && frame_counter_ == opts_.dump_frame) dump_frame(frame, opts_.dump_path);
  slot_ = (int)(frame_counter_ % FRAME_SLOTS);
  wait_fence(slot_fence_[slot_]);   // this slot's previous frame (FRAME_SLOTS frames ago) is complete
  vertex_ring_.reset(slot_); index_ring_.reset(slot_); constant_ring_.reset(slot_); upload_ring_.reset(slot_);
  frame_garbage_[slot_].clear();
  descriptor_garbage_[slot_].clear();
  check(allocators_[slot_]->Reset(), "allocator reset");
  check(list_->Reset(allocators_[slot_].Get(), nullptr), "list reset");
  ID3D12DescriptorHeap* heaps[] = {srv_heap_.Get(), sampler_heap_.Get()};
  list_->SetDescriptorHeaps(2, heaps);
  bind_efb_targets();
  if (dlss_active_) {
    D3D12_CPU_DESCRIPTOR_HANDLE mrtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart(); mrtv.ptr += 5 * rtv_size_;
    const float zero[4] = {0, 0, 0, 0};
    list_->ClearRenderTargetView(mrtv, zero, 0, nullptr);
    streamline::new_frame((uint32_t)frame_counter_);
    streamline::jitter(frames_presented_, &jitter_x_, &jitter_y_);
    // The main camera projection: the first perspective draw of the frame (menus and HUD are orthographic).
    streamline::FrameConstants fc{};
    bool found = false;
    for (const DrawCall& d : frame.draws) if (d.xf_regs[0x26] == 0) { build_projection(d, fc.projection); found = true; break; }
    if (!found && !frame.draws.empty()) { build_projection(frame.draws[0], fc.projection); fc.orthographic = true; }
    fc.jitter_x = jitter_x_ * opts_.dlss_jitter_sign; fc.jitter_y = jitter_y_ * opts_.dlss_jitter_sign;
    fc.render_w = 640 * scale_; fc.render_h = 480 * scale_;
    fc.reset = dlss_reset_; dlss_reset_ = false;
    streamline::set_constants(fc);
    if (frame_counter_ % 600 == 0) for (auto it = last_poses_.begin(); it != last_poses_.end();) { if (it->second.frame + 4 < frame_counter_) it = last_poses_.erase(it); else ++it; }
  }
  if (have_clear_) { clear_efb(pending_clear_); have_clear_ = false; }
  bool presented = false;
  for (const FrameCommand& cmd : frame.commands) {
    if (cmd.kind == FrameCommand::Draw) {
      execute_draw(frame, frame.draws[cmd.index], overrides ? overrides + cmd.index : nullptr);
    } else {
      const EfbCopy& c = frame.copies[cmd.index];
      if (c.to_xfb) { if (!skip_present_) { present_efb(c); presented = true; } }
      else execute_copy(c);
      if (c.clear) clear_efb(c);
    }
  }
  check(list_->Close(), "list close");
  ID3D12CommandList* lists[] = {list_.Get()};
  queue_->ExecuteCommandLists(1, lists);
  present_wait_ = 0;
  if (presented) {
    const double wait_start = Stopwatch::now();
    for (;;) {
      double remaining = present_deadline_ - Stopwatch::now();
      if (remaining <= 0) break;
      if (present_timer_ && remaining > 0.0004) {
        LARGE_INTEGER due; due.QuadPart = -(LONGLONG)((remaining-0.0002)*1e7);
        if (SetWaitableTimer(present_timer_, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(present_timer_, INFINITE);
        else SwitchToThread();
      } else YieldProcessor();
    }
    present_wait_ = Stopwatch::now()-wait_start;
    swapchain_->Present(opts_.vsync ? 1 : 0, opts_.vsync ? 0 : DXGI_PRESENT_ALLOW_TEARING);
    ++frames_presented_;
  }
  // Signal, do not wait: the next frames render while the GPU finishes this one.
  ++fence_value_;
  queue_->Signal(fence_.Get(), fence_value_);
  slot_fence_[slot_] = fence_value_;
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

// Atomically publish complete cache files. Concurrent instances may replace one
// another's cache, but cannot expose a truncated file to a reader.
static void write_cache(const std::string& path, const void* data, size_t size) {
  std::string temporary = path + "." + std::to_string(GetCurrentProcessId()) + ".tmp";
  FILE* file = std::fopen(temporary.c_str(), "wb");
  if (!file) return;
  bool ok = std::fwrite(data, 1, size, file) == size;
  ok = std::fclose(file) == 0 && ok;
  if (ok) ok = MoveFileExA(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
  if (!ok) DeleteFileA(temporary.c_str());
}

// Pipeline recipes (the game-side state a pipeline was built from) do not depend on the shader
// sources, so they live at the cache root and survive every shader/namespace change; older
// per-namespace files are merged in. This is what lets a fresh build precompile before boot.
static bool read_recipe_file(const std::string& path, std::vector<PipelineRecipe>& out) {
  std::ifstream file(path, std::ios::binary);
  uint64_t header[3]{};
  if (!file.read((char*)header, sizeof(header)) || header[0] != 0x3150535247505847ull || header[1] > 16384) return false;
  std::vector<PipelineRecipe> recipes((size_t)header[1]);
  if (!file.read((char*)recipes.data(), recipes.size()*sizeof(PipelineRecipe)) ||
      hash_bytes(recipes.data(), recipes.size()*sizeof(PipelineRecipe)) != header[2]) return false;
  out.insert(out.end(), recipes.begin(), recipes.end());
  return true;
}

void D3D12Backend::prewarm_pipelines() {
  std::vector<PipelineRecipe> recipes;
  read_recipe_file(shader_cache_root_ + "/recipes.bin", recipes);
  std::error_code ec;
  for (auto& entry : std::filesystem::directory_iterator(shader_cache_root_, ec))
    if (entry.is_directory(ec)) read_recipe_file(entry.path().string() + "/recipes.bin", recipes);
  if (recipes.empty()) return;
  std::unordered_set<std::string> seen;
  std::unordered_set<PsoKey, PsoKeyHash> keys_seen;
  Stopwatch timer;
  // All recipes go to the compile workers at once (the pipeline library makes known ones cheap);
  // the window title shows progress while the game waits to boot.
  size_t queued = 0;
  for (const auto& recipe : recipes) {
    if ((recipe.topology != D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE && recipe.topology != D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE) ||
        (recipe.xf[0x3F] & 15) > 8 || (recipe.xf[9] & 3) > 2) continue;
    if (!seen.insert(std::string((const char*)&recipe, sizeof recipe)).second) continue;
    if (pipeline_recipes_.size() >= 16384) break;
    DrawCall draw{}; draw.components = recipe.components; draw.bp = recipe.bp;
    std::memcpy(draw.xf_regs, recipe.xf, sizeof(recipe.xf));
    auto topo = (D3D12_PRIMITIVE_TOPOLOGY_TYPE)recipe.topology;
    // Both pipeline variants: plain, and with motion vectors for DLSS (otherwise the first match
    // with DLSS on would compile everything again and skip draws meanwhile).
    const int variants = streamline::available() ? 2 : 1;
    for (int mvec = 0; mvec < variants; ++mvec) {
      VSUid vsu = make_vs_uid(draw); PSUid psu = make_ps_uid(draw);
      vsu.motion_vectors = psu.motion_vectors = (uint32_t)mvec;
      PsoKey key{vsu.hash(), psu.hash(), draw.bp.blendmode() & 0xFFFF, draw.bp.zmode() & 0x1F, draw.bp.cullmode(), (uint32_t)topo, draw.bp.zcontrol() & 7, (uint32_t)mvec};
      // One recipe per pipeline: recipes that only differ in state the pipeline key ignores are dropped.
      if (psos_.count(key)) { if (mvec == 0 && keys_seen.insert(key).second) pipeline_recipes_.push_back(recipe); continue; }
      if (!psos_pending_.insert(key).second) continue;
      if (mvec == 0) keys_seen.insert(key);
      { std::lock_guard<std::mutex> lk(pso_mutex_); pso_jobs_.push_back(PsoJob{key, vsu, psu, topo, recipe}); }
      ++queued;
    }
  }
  pso_cv_.notify_all();
  const size_t total = psos_.size() + queued;
  for (;;) {
    integrate_compiled_psos();
    size_t pending = psos_pending_.size();
    wchar_t title[128]; swprintf_s(title, L"Melee Unlocked  |  compiling shaders %zu / %zu", total - pending, total);
    host::window_set_title(title);
    if (!pending) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  host::log("d3d12: prewarmed %zu pipelines from %zu recipes before guest startup in %.1f ms", psos_.size(), pipeline_recipes_.size(), timer.lap()*1000.0);
}

void D3D12Backend::save_pipeline_recipes() {
  if (pipeline_recipes_.empty()) return;
  std::unordered_set<std::string> seen;
  std::vector<PipelineRecipe> unique;
  for (const auto& r : pipeline_recipes_) if (seen.insert(std::string((const char*)&r, sizeof r)).second) unique.push_back(r);
  uint64_t header[3] = {0x3150535247505847ull, unique.size(), hash_bytes(unique.data(), unique.size()*sizeof(PipelineRecipe))};
  std::vector<uint8_t> data(sizeof(header)+unique.size()*sizeof(PipelineRecipe));
  std::memcpy(data.data(), header, sizeof(header));
  std::memcpy(data.data()+sizeof(header), unique.data(), data.size()-sizeof(header));
  write_cache(shader_cache_root_+"/recipes.bin", data.data(), data.size());
}

void D3D12Backend::open_pipeline_library() {
  CreateDirectoryA(opts_.shader_cache.c_str(), nullptr);
  shader_cache_root_ = opts_.shader_cache;
  opts_.shader_cache += "/" GX_SHADER_CACHE_VERSION;
  CreateDirectoryA(opts_.shader_cache.c_str(), nullptr);
  ComPtr<ID3D12Device1> device1;
  if (FAILED(device_.As(&device1))) { host::log("d3d12: pipeline library unsupported"); return; }
  std::string path = opts_.shader_cache + "/pipelines.bin";
  FILE* f = std::fopen(path.c_str(), "rb");
  if (f) {
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    if (n > (256 << 20)) n = 0;
    pipeline_library_data_.resize(n > 0 ? (size_t)n : 0);
    if (n > 0 && std::fread(pipeline_library_data_.data(), 1, (size_t)n, f) != (size_t)n) pipeline_library_data_.clear();
    std::fclose(f);
  }
  HRESULT hr = pipeline_library_data_.empty() ? E_FAIL : device1->CreatePipelineLibrary(pipeline_library_data_.data(), pipeline_library_data_.size(), IID_PPV_ARGS(&pipeline_library_));
  if (FAILED(hr)) {   // absent, or built by another driver/adapter version: start a fresh library
    pipeline_library_data_.clear();
    if (FAILED(device1->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&pipeline_library_)))) { pipeline_library_.Reset(); host::log("d3d12: pipeline library unavailable"); return; }
    host::log("d3d12: new pipeline library in %s", opts_.shader_cache.c_str());
  } else {
    host::log("d3d12: pipeline library loaded from %s (%zu bytes)", path.c_str(), pipeline_library_data_.size());
  }
}

void D3D12Backend::save_pipeline_library() {
  if (!pipeline_library_ || !pipeline_library_dirty_) return;
  size_t size = pipeline_library_->GetSerializedSize();
  std::vector<uint8_t> data(size);
  if (FAILED(pipeline_library_->Serialize(data.data(), size))) return;
  std::string path = opts_.shader_cache + "/pipelines.bin";
  write_cache(path, data.data(), size);
  host::log("d3d12: pipeline library saved (%zu bytes, %zu pipelines)", size, psos_.size());
  pipeline_library_dirty_ = false;
}

bool D3D12Backend::load_shader_blob(const std::string& path, ComPtr<ID3DBlob>& blob) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
  if (n <= 0 || n > (16 << 20) || FAILED(D3DCreateBlob((SIZE_T)n, &blob))) { std::fclose(f); return false; }
  bool ok = std::fread(blob->GetBufferPointer(), 1, (size_t)n, f) == (size_t)n;
  std::fclose(f);
  if (!ok) blob.Reset();
  return ok;
}

void D3D12Backend::save_shader_blob(const std::string& path, ID3DBlob* blob) {
  write_cache(path, blob->GetBufferPointer(), blob->GetBufferSize());
}

std::string d3d12_profile_line() {
  char buf[512];
  double n = (double)std::max<uint64_t>(1, g_prof_draws);
  double l = (double)std::max<uint64_t>(1, g_pso_lookups);
  snprintf(buf, sizeof buf, "%llu draws: index %.2f, upload %.2f, constants %.2f, pso %.2f, bind %.2f, draw %.2f us/draw | pso cache hits %llu, lookups %llu (uid %.2f + map %.2f us), created %llu, draws on the fallback pipeline while compiling %llu",
           (unsigned long long)g_prof_draws, 1e6 * g_prof[0] / n, 1e6 * g_prof[1] / n, 1e6 * g_prof[2] / n, 1e6 * g_prof[3] / n, 1e6 * g_prof[4] / n, 1e6 * g_prof[5] / n,
           (unsigned long long)g_pso_hits, (unsigned long long)g_pso_lookups, 1e6 * g_prof[6] / l, 1e6 * g_prof[7] / l, (unsigned long long)g_pso_creates, (unsigned long long)g_pso_skips);
  std::memset(g_prof, 0, sizeof g_prof); g_prof_draws = g_pso_hits = g_pso_lookups = g_pso_creates = g_pso_skips = 0;
  return buf;
}

Backend* create_d3d12_backend(void* hwnd, int w, int h, const D3D12Options& options) {
  return new D3D12Backend((HWND)hwnd, w, h, options);
}
const D3D12Options& d3d12_options(Backend* backend) { return static_cast<D3D12Backend*>(backend)->options(); }
void d3d12_resize(Backend* backend, int w, int h) { static_cast<D3D12Backend*>(backend)->resize(w, h); }
void d3d12_stats(Backend* backend, uint32_t* frames, uint32_t* pipelines, uint32_t* textures) {
  auto* b = static_cast<D3D12Backend*>(backend);
  if (frames) *frames = b->frames_presented();
  if (pipelines) *pipelines = b->pipeline_count();
  if (textures) *textures = b->texture_count();
}

}  // namespace gx
