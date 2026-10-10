// D3D11 backend: replays captured GX frames into an EFB render target and presents XFB copies.
// A mirror of gx_d3d12.cpp for machines where D3D12 is unavailable or slow. The generated HLSL,
// the constant layouts and the blit/sharpen shader are shared with the D3D12 backend, so the
// presented image is the same; DLSS (Streamline) is D3D12 only and is simply not offered here.
//
// The D3D12-specific machinery has idiomatic D3D11 equivalents:
//   upload rings + fences  -> dynamic buffers with MAP_WRITE_DISCARD once per frame
//   root signature + CBVs  -> constant-buffer offsetting (VSSetConstantBuffers1)
//   PSOs                   -> cached blend / depth-stencil / rasterizer / input-layout objects
//   descriptor heaps       -> PSSetShaderResources / PSSetSamplers with a small state cache
//   rect ClearRenderTarget -> a scissored clear quad (D3D11 cannot clear a depth sub-rect)
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_6.h>
#include "gx_adapter.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <intrin.h>
#include <algorithm>
#include <chrono>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <mutex>
#include <deque>
#include <condition_variable>
#include <vector>
#include "gx_d3d11.h"
#include "flicker_scan.h"
#include "gx_backend.h"
#ifdef MELEE_NO_SLIPPI
#include "netplay_state.h"   // the same names, answered from the neutral netplay state
#else
#include "exi_slippi.h"
#endif
#include "gx_shader.h"
#include "crt_shader.h"
#include "user_shader.h"
#include "gx_texture.h"
#include "texture_pack.h"
#include "video_background.h"
#include "video_background_learning.h"
#include "host.h"
#include "window.h"
#include "gecko_data.h"
#ifdef GX_PC_SETTINGS
#include "pc_settings.h"
#include "lab_view.h"
#endif

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

namespace gx {
namespace {

// Topology codes stored in the shared shader-cache recipe file; the values are D3D12's
// D3D12_PRIMITIVE_TOPOLOGY_TYPE so recipes.bin stays readable by both backends.
enum : uint32_t { TOPO_LINE = 2, TOPO_TRIANGLE = 3 };

// The vertex shader never reads past unjittered_projection without motion vectors, and D3D11
// has no DLSS path, so only this much of the block is uploaded per draw.
constexpr size_t VS_CONSTANT_BYTES = offsetof(VSConstants, prev_projection);

double g_prof[8]; uint64_t g_prof_draws = 0, g_pipe_hits = 0, g_pipe_lookups = 0, g_pipe_creates = 0, g_pipe_skips = 0;
// Sampling an address the game has copied the EFB into: found in the copy registry, or not found
// and therefore decoded from guest RAM, which for a render target holds nothing the GPU wrote.
uint64_t g_efb_tex_hit = 0, g_efb_tex_miss = 0;
struct Stopwatch {
  static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
  double t = now(); double lap() { double n = now(), d = n - t; t = n; return d; }
};

void check(HRESULT hr, const char* what) { if (FAILED(hr)) host::die("D3D11: %s failed (%08X)", what, (unsigned)hr); }
// Startup steps that may legitimately fail on an old machine: the caller falls back to D3D12.
void require(HRESULT hr, const char* what) {
  if (FAILED(hr)) { char buf[160]; snprintf(buf, sizeof buf, "%s failed (%08X)", what, (unsigned)hr); throw std::runtime_error(buf); }
}

struct PipeKey {
  uint64_t vs, ps;
  uint32_t blend, zmode, cull, topology, pixel_format;
  bool operator==(const PipeKey& o) const {
    return vs == o.vs && ps == o.ps && blend == o.blend && zmode == o.zmode &&
        cull == o.cull && topology == o.topology && pixel_format == o.pixel_format;
  }
};
struct PipeKeyHash { size_t operator()(const PipeKey& k) const {
  const uint64_t fields[] = {k.vs, k.ps, k.blend, k.zmode, k.cull, k.topology, k.pixel_format};
  return (size_t)hash_bytes(fields, sizeof fields);
} };

// What a draw binds. Held by pointer in DrawCall::cached_pipeline, so the map owns them
// through unique_ptr and never moves them.
struct Pipeline {
  ComPtr<ID3D11VertexShader> vs;
  ComPtr<ID3D11PixelShader> ps;
  ID3D11BlendState* blend = nullptr;            // owned by the state caches below
  ID3D11DepthStencilState* depth = nullptr;
  ID3D11RasterizerState* raster = nullptr;
  D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
};

struct TextureEntry {
  ComPtr<ID3D11Texture2D> resource;
  ComPtr<ID3D11ShaderResourceView> srv;
  ComPtr<ID3D11RenderTargetView> rtv;   // EFB copies only
  uint32_t width = 0, height = 0, levels = 1;
  uint64_t last_used = 0;
  uint64_t replacement_bytes = 0;   // what a pack replacement charged to the replacement budget, given back when evicted
};
// Video memory an RGBA8 texture takes, near enough for keeping the cache inside its budget.
uint64_t texture_bytes(uint32_t w, uint32_t h, uint32_t levels) {
  const uint64_t base = (uint64_t)w * h * 4;
  return levels > 1 ? base + base / 3 : base;
}

struct SamplerKey {
  uint32_t mode0, mode1, anisotropy;
  bool operator==(const SamplerKey& o) const { return mode0 == o.mode0 && mode1 == o.mode1 && anisotropy == o.anisotropy; }
};
struct SamplerKeyHash { size_t operator()(const SamplerKey& k) const { return (size_t)hash_bytes(&k, sizeof k); } };

struct PipelineRecipe {   // binary-compatible with the D3D12 backend's recipes.bin
  uint32_t topology = 0, components = 0;
  BPMemory bp{};
  uint32_t xf[0x58]{};
};

// ---------------------------------------------------------------- dynamic buffers
// One buffer per kind, filled once per frame with MAP_WRITE_DISCARD. Unlike D3D12 there is no
// fence to respect: DISCARD renames the allocation, so draws already recorded keep the old one.
class DynamicBuffer {
 public:
  bool init(ID3D11Device* dev, ID3D11DeviceContext* ctx, size_t bytes, UINT bind) {
    dev_ = dev; ctx_ = ctx; bind_ = bind;
    return grow(bytes);
  }
  bool grow(size_t bytes) {
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = (UINT)((bytes + 65535) & ~size_t(65535));
    bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = bind_; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ComPtr<ID3D11Buffer> replacement;
    if (FAILED(dev_->CreateBuffer(&bd, nullptr, &replacement))) return false;
    buffer_ = std::move(replacement); size_ = bd.ByteWidth;
    return true;
  }
  // Maps the whole buffer for a frame's worth of data, growing first when it does not fit.
  uint8_t* begin(size_t bytes) {
    if (!bytes) return nullptr;
    if (bytes > size_ && !grow(bytes * 2)) return nullptr;
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx_->Map(buffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return nullptr;
    mapped_ = true;
    return (uint8_t*)m.pData;
  }
  void end() { if (mapped_) { ctx_->Unmap(buffer_.Get(), 0); mapped_ = false; } }
  ID3D11Buffer* get() const { return buffer_.Get(); }
  size_t size() const { return size_; }
 private:
  ID3D11Device* dev_ = nullptr; ID3D11DeviceContext* ctx_ = nullptr;
  ComPtr<ID3D11Buffer> buffer_;
  size_t size_ = 0; UINT bind_ = 0; bool mapped_ = false;
};

// Per-draw constant blocks. D3D11 constant buffers hold at most 4096 float4s, so a pool of
// 64 KB buffers is carved into fixed-size slots and bound with VSSetConstantBuffers1's
// first/num constant window. Without that D3D11.1 capability each slot gets its own buffer.
class ConstantPool {
 public:
  void init(ID3D11Device* dev, ID3D11DeviceContext* ctx, size_t block_bytes, bool offsetting) {
    dev_ = dev; ctx_ = ctx;
    stride_ = (UINT)((block_bytes + 255) & ~size_t(255));   // FirstConstant must be a multiple of 16 float4s
    constants_ = stride_ / 16;
    per_buffer_ = offsetting ? std::max<UINT>(1, 65536 / stride_) : 1;
    // FirstConstant + NumConstants must stay inside the 4096-constant window.
    while (per_buffer_ > 1 && (per_buffer_ - 1) * constants_ + constants_ > 4096) --per_buffer_;
    buffer_bytes_ = per_buffer_ * stride_;
  }
  UINT stride() const { return stride_; }
  bool reserve(UINT slots) {
    size_t needed = (slots + per_buffer_ - 1) / per_buffer_;
    while (buffers_.size() < needed) {
      D3D11_BUFFER_DESC bd{};
      bd.ByteWidth = buffer_bytes_; bd.Usage = D3D11_USAGE_DYNAMIC;
      bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
      ComPtr<ID3D11Buffer> b;
      if (FAILED(dev_->CreateBuffer(&bd, nullptr, &b))) return false;
      buffers_.push_back(std::move(b));
    }
    return true;
  }
  // Slots must be written in increasing order: only the buffer being filled is mapped.
  uint8_t* slot(UINT index) {
    UINT b = index / per_buffer_;
    if (b >= buffers_.size() && !reserve(index + 1)) return nullptr;
    if (b != current_) {
      end();
      D3D11_MAPPED_SUBRESOURCE m{};
      if (FAILED(ctx_->Map(buffers_[b].Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return nullptr;
      mapped_ = (uint8_t*)m.pData; current_ = b;
    }
    return mapped_ + (index % per_buffer_) * stride_;
  }
  void end() {
    if (mapped_) { ctx_->Unmap(buffers_[current_].Get(), 0); mapped_ = nullptr; }
    current_ = (UINT)-1;
  }
  void bind(UINT index, ID3D11Buffer** buffer, UINT* first_constant, UINT* num_constants) const {
    *buffer = buffers_[index / per_buffer_].Get();
    *first_constant = (index % per_buffer_) * constants_;
    *num_constants = constants_;
  }
  bool windowed() const { return per_buffer_ > 1; }
 private:
  ID3D11Device* dev_ = nullptr; ID3D11DeviceContext* ctx_ = nullptr;
  std::vector<ComPtr<ID3D11Buffer>> buffers_;
  UINT stride_ = 0, constants_ = 0, per_buffer_ = 1, buffer_bytes_ = 0, current_ = (UINT)-1;
  uint8_t* mapped_ = nullptr;
};

// D3D11 backends never share DrawCall::cached_pipeline with a D3D12 one: the id spaces are disjoint.
static std::atomic<uint64_t> next_backend_id{0x100000000ull};

class D3D11Backend : public Backend {
 public:
  D3D11Backend(HWND hwnd, int w, int h, const RenderOptions& o) : hwnd_(hwnd), opts_(o), client_w_(w), client_h_(h) {
    init();
    try {
      start_shader_workers();
      prewarm_pipelines();
#ifdef GX_PC_SETTINGS
      if (opts_.pc_settings) settings_ui_ = std::make_unique<PcSettingsUID3D11>(hwnd, device_.Get(), context_.Get(), opts_);
#endif
      set_hud_scales(opts_.stock_hud_scale, opts_.damage_hud_scale, gecko::option_pal_stock_icons);
      set_low_poly_fighters(opts_.low_poly_fighters != 0);
      set_player_tags_always(opts_.player_tags_always != 0);
    } catch (...) { stop_shader_workers(); throw; }   // the worker threads must not outlive a failed start
    starting_ = false;
  }
  ~D3D11Backend() override {
    user_shader::d3d11_release();
    if (swapchain_) swapchain_->SetFullscreenState(FALSE, nullptr);
    texpack::report();   // last word on how many of the pack's textures the game actually drew
#ifdef GX_PC_SETTINGS
    settings_ui_.reset();
#endif
    stop_shader_workers();
    integrate_compiled_pipelines();
    flush_captures();
    save_pipeline_recipes();
    if (context_) context_->ClearState();
    if (present_timer_) CloseHandle(present_timer_);
  }
  const RenderOptions& options() const { return opts_; }
  const RenderOptions* presentation_options() const override { return &opts_; }
  void presentation_stats(uint32_t* frames, uint32_t* pipelines, uint32_t* textures) const override {
    if (frames) *frames = frames_presented();
    if (pipelines) *pipelines = pipeline_count();
    if (textures) *textures = texture_count();
  }
  std::string profile_line() const override { return d3d11_profile_line(); }
  void set_present_deadline(double deadline) override { present_deadline_ = deadline; }
  double presentation_wait_seconds() const override { return present_wait_; }
  void submit_frame(const Frame& frame) override { submit_frame(frame, nullptr); }
  void submit_frame(const Frame& frame, const DrawMatrices* overrides) override;
  void set_skip_present(bool skip) override { skip_present_ = skip; }
  void resize(int w, int h) override {
    if (w == client_w_ && h == client_h_) return;
    client_w_ = w; client_h_ = h;
    create_swapchain_targets(true);
    if (opts_.efb_scale == 0 && pick_scale() != scale_) { host::log("d3d11: dropping %zu EFB copy textures, internal scale %d -> %d", efb_copies_.size(), scale_, pick_scale()); efb_copies_.clear(); create_efb(); }
  }
  uint32_t frames_presented() const { return frames_presented_; }
  uint32_t pipeline_count() const { return (uint32_t)pipelines_.size(); }
  uint32_t texture_count() const { return (uint32_t)textures_.size(); }

 private:
  const uint64_t backend_id_ = next_backend_id.fetch_add(1);
#ifdef GX_PC_SETTINGS
  std::unique_ptr<PcSettingsUID3D11> settings_ui_;
#endif
  bool lab_skip_scene_ = false;   // the Lab view covers this frame and its scene is not drawn (submit_frame)
  // During construction a failure means "this machine cannot run D3D11": throw so the caller can
  // fall back to D3D12. Afterwards (a resolution change mid-session) it is a genuine fatal error.
  bool starting_ = true;
  // Whether presented_aspect's widen actually reaches anything on screen: a mode's character/stage
  // select through its results screen, not the bare 2D menu shell around it (see
  // frame_has_widenable_scene in gx_core.h). Starts true so a fresh window is never letterboxed to
  // 73:60 for the one frame before the first submit_frame sets it for real.
  bool widenable_scene_ = true;
  void fail(HRESULT hr, const char* what) const { if (SUCCEEDED(hr)) return; if (starting_) require(hr, what); check(hr, what); }
  void init();
  void create_swapchain_targets(bool resize);
  void apply_fullscreen_mode();
  void create_efb();
  int pick_scale() const;
  float output_aspect() const;
  void output_size(int* vw, int* vh) const;
  void bind_efb_targets();
  void unbind_shader_resources();

  // ---- pipelines ----
  struct ShaderJob { PipeKey key; VSUid vsu; PSUid psu; PipelineRecipe recipe; };
  struct ShaderResult { PipeKey key; ComPtr<ID3DBlob> vs, ps; PipelineRecipe recipe; };
  Pipeline* get_pipeline(const DrawCall& dc, uint32_t topo_type, D3D11_PRIMITIVE_TOPOLOGY topology);
  Pipeline* fallback_pipeline(const PipeKey& key, const DrawCall& dc, D3D11_PRIMITIVE_TOPOLOGY topology);
  Pipeline* build_pipeline(const PipeKey& key, ID3DBlob* vs, ID3DBlob* ps, D3D11_PRIMITIVE_TOPOLOGY topology);
  ID3D11BlendState* blend_state(uint32_t blend, uint32_t pixel_format);
  ID3D11DepthStencilState* depth_state(uint32_t zmode);
  ID3D11RasterizerState* raster_state(uint32_t cull);
  void ensure_input_layout(ID3DBlob* vs);
  void shader_worker();
  void integrate_compiled_pipelines();
  void start_shader_workers();
  void stop_shader_workers();
  void prewarm_pipelines();
  void save_pipeline_recipes();
  bool compile_shaders(const PipeKey& key, const VSUid& vsu, const PSUid& psu, ComPtr<ID3DBlob>& vs, ComPtr<ID3DBlob>& ps);
  bool load_shader_blob(const std::string& path, ComPtr<ID3DBlob>& blob);
  void save_shader_blob(const std::string& path, ID3DBlob* blob);

  // ---- resources ----
  TextureEntry* update_video_texture(const std::shared_ptr<const video_bg::Frame>& frame,
                                     int video_slot);
  void draw_video_background(const std::shared_ptr<const video_bg::Frame>& frame,
                             int video_slot, const EfbCopy& screen);
  TextureEntry* get_texture(const TextureRef& t);
  ID3D11SamplerState* get_sampler(uint32_t mode0, uint32_t mode1);
  void bind_textures(const DrawCall& dc);
  void execute_copy(const EfbCopy& copy);
  void present_efb(const EfbCopy& copy);
  void clear_efb(const EfbCopy& copy);
  // crt: eight more constants for the CRT display model (crt_shader.h); null draws the plain picture.
  void blit(ID3D11ShaderResourceView* src, const float rect[16], ID3D11ShaderResourceView* depth = nullptr, const float* crt = nullptr);
  void capture_backbuffer();
  void write_capture(const std::string& path, uint64_t sequence);
  void flush_captures();
  struct PendingCapture { std::string path; UINT w, h, pitch; uint64_t sequence; std::vector<uint8_t> data; };
  std::vector<PendingCapture> pending_captures_;
  uint64_t capture_sequence_ = 0;

  HWND hwnd_;
  RenderOptions opts_;
  int client_w_, client_h_;
  int scale_ = 1;
  int efb_w_ = EFB_WIDTH, efb_h_ = EFB_HEIGHT;
  bool skip_present_ = false;
  bool widescreen_sent_ = false;
  bool fod_reflections_sent_ = false;
  int anisotropy_applied_ = 0, ssaa_applied_ = 0;
  bool allow_tearing_ = false;
  bool constant_offsetting_ = false;

  ComPtr<ID3D11Device> device_;
  ComPtr<ID3D11DeviceContext> context_;
  ComPtr<ID3D11DeviceContext1> context1_;
  ComPtr<IDXGISwapChain1> swapchain_;
  ComPtr<ID3D11RenderTargetView> backbuffer_rtv_;
  ComPtr<ID3D11Texture2D> efb_color_, efb_depth_;
  ComPtr<ID3D11RenderTargetView> efb_rtv_;
  ComPtr<ID3D11ShaderResourceView> efb_srv_;
  ComPtr<ID3D11ShaderResourceView> efb_depth_srv_;
  ComPtr<ID3D11DepthStencilView> efb_dsv_;
  ComPtr<ID3D11InputLayout> layout_;
  ComPtr<ID3D11VertexShader> blit_vs_, clear_vs_;
  ComPtr<ID3D11PixelShader> blit_ps_, crt_ps_, clear_ps_;
  // A player-chosen shader preset reads the picture at the console's own size from this texture.
  ComPtr<ID3D11Texture2D> shader_source_;
  ComPtr<ID3D11ShaderResourceView> shader_source_srv_;
  ComPtr<ID3D11RenderTargetView> shader_source_rtv_;
  uint32_t shader_source_w_ = 0, shader_source_h_ = 0;
  ComPtr<ID3D11Buffer> blit_cb_, clear_cb_;
  ComPtr<ID3D11SamplerState> blit_sampler_;
  ComPtr<ID3D11BlendState> opaque_blend_;
  ComPtr<ID3D11DepthStencilState> no_depth_, clear_depth_;
  ComPtr<ID3D11RasterizerState> blit_raster_;
  ComPtr<ID3D11Texture2D> capture_staging_;

  DynamicBuffer vertex_buffer_, index_buffer_;
  ConstantPool vs_constants_, ps_constants_;

  std::string shader_cache_root_;
  std::vector<PipelineRecipe> pipeline_recipes_;
  bool prewarming_ = false;
  std::unordered_map<PipeKey, std::unique_ptr<Pipeline>, PipeKeyHash> pipelines_;
  std::unordered_set<PipeKey, PipeKeyHash> pipelines_pending_;
  struct FallbackKey { uint32_t blend, zmode, cull, topology, pixel_format, variant;
    bool operator==(const FallbackKey& o) const { return blend == o.blend && zmode == o.zmode && cull == o.cull && topology == o.topology && pixel_format == o.pixel_format && variant == o.variant; } };
  struct FallbackKeyHash { size_t operator()(const FallbackKey& k) const { return (size_t)hash_bytes(&k, sizeof k); } };
  std::unordered_map<FallbackKey, std::unique_ptr<Pipeline>, FallbackKeyHash> fallback_pipelines_;
  std::unordered_map<uint64_t, ComPtr<ID3DBlob>> vs_blobs_, ps_blobs_;
  std::unordered_map<uint32_t, ComPtr<ID3D11BlendState>> blend_states_;
  std::unordered_map<uint32_t, ComPtr<ID3D11DepthStencilState>> depth_states_;
  std::unordered_map<uint32_t, ComPtr<ID3D11RasterizerState>> raster_states_;
  std::unordered_map<SamplerKey, ComPtr<ID3D11SamplerState>, SamplerKeyHash> samplers_;
  std::unordered_map<uint64_t, TextureEntry> textures_;
  TextureEntry video_textures_[video_bg::kVideoSlots];
  uint64_t video_serial_[video_bg::kVideoSlots]{};
  bool video_layer_logged_[2]{};
  int draw_video_slot_ = -1;   // video target sampled by the draw currently being submitted
  std::unordered_map<uint32_t, TextureEntry> efb_copies_;
  // Diagnostic for the Fountain of Dreams reflection, which exists only as an EFB copy: the stage
  // renders a mirrored camera pass, copies it into an 80x60 image, and the water samples that
  // address. If the lookup below ever misses, the water samples whatever stale bytes guest RAM
  // holds at that address instead, which is a frame with no reflection in it.
  std::unordered_set<uint32_t> copy_dests_;      // every address ever used as a copy destination

  // ---- flicker scan (--flicker-scan) ----
  // A one-frame glitch is invisible to a screenshot key and too rare to catch by dumping frames: at
  // an unlocked rate a few thousand dumped frames cover a fraction of a second. So the check runs in
  // the renderer instead, on a 16x16 downsample of every presented frame, and only says something
  // when a frame pops out and comes back.
  static constexpr int kScanGrid = FlickerScanner::kGrid;   // coarse, but enough to recognise what changed on a hit
  static constexpr int kScanRing = 3;   // readback is mapped two frames later, so it never stalls
  void flicker_scan();
  ComPtr<ID3D11Texture2D> scan_rt_, scan_staging_[kScanRing];
  ComPtr<ID3D11RenderTargetView> scan_rtv_;
  uint32_t scan_ring_ = 0, scan_ready_ = 0;
  FlickerScanner scanner_;   // the analysis, shared with D3D12 (flicker_scan.cpp)


  std::mutex shader_mutex_;
  std::condition_variable shader_cv_, shader_done_cv_;
  std::deque<ShaderJob> shader_jobs_;
  std::vector<ShaderResult> shader_done_;
  std::vector<std::thread> shader_threads_;
  bool shader_quit_ = false;
  int shader_wait_budget_us_ = 0;
  bool capturing_efb_ = false;

  // ---- per-frame plan (built before any GPU work, see submit_frame) ----
  struct DrawPlan {
    uint32_t first_index = 0, index_count = 0, base_vertex = 0;
    uint32_t vs_slot = 0, ps_slot = 0;
    uint32_t topo_type = TOPO_TRIANGLE;
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    bool valid = false;
  };
  std::vector<DrawPlan> plans_;
  std::vector<uint32_t> index_scratch_;
  std::vector<uint8_t> decode_scratch_;
  void execute_draw(const DrawCall& dc, const DrawPlan& plan);

  // ---- redundant-state filter (D3D11 state calls are not free) ----
  struct Bound {
    ID3D11VertexShader* vs = nullptr; ID3D11PixelShader* ps = nullptr;
    ID3D11BlendState* blend = nullptr; ID3D11DepthStencilState* depth = nullptr; ID3D11RasterizerState* raster = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11ShaderResourceView* srv[8] = {};
    ID3D11SamplerState* samp[8] = {};
    ID3D11Buffer* vb = nullptr;
    float viewport[6] = {};
    LONG scissor[4] = {};
  } bound_;
  void reset_bound() { bound_ = Bound{}; }

  HANDLE present_timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0x2 /* high resolution */, TIMER_ALL_ACCESS);
  double present_deadline_ = 0, present_wait_ = 0;
  uint64_t frame_counter_ = 0;
  // Replacement textures are decoded PNGs and far bigger than the originals, so they get a budget
  // rather than being allowed to fill video memory. Same figures as the D3D12 backend.
  uint64_t replacement_bytes_ = 0, replacement_budget_ = 512ull * 1024 * 1024;
  // The whole game texture cache has a budget too (a quarter of the adapter's dedicated memory,
  // floor 256 MB, ceiling 2 GB, set in init): textures_ used to keep every texture the game had
  // ever shown, so a long session only ever grew. trim_textures evicts the least recently drawn
  // above it, once a second at most.
  uint64_t texture_budget_bytes_ = 512ull * 1024 * 1024;
  double texture_trim_time_ = 0;
  void trim_textures();
  // GPU time per frame and GPU queue depth for the "gpu:" diagnostic line. A disjoint query plus
  // two timestamps bracket each frame's work; an event query marks its end. Results are read back
  // kTimerRing frames later and only if ready (never a stall), and the events still outstanding at
  // the start of a frame are the frames the GPU has not finished.
  static constexpr int kTimerRing = 4;
  struct GpuTimer {
    ComPtr<ID3D11Query> disjoint, start, end, done;
    bool timing_pending = false, done_pending = false;
  };
  GpuTimer gpu_timers_[kTimerRing];
  int gpu_timer_slot_ = 0;
  double gpu_frame_ms_sum_ = 0; uint32_t gpu_frame_ms_samples_ = 0;   // since the last "gpu:" line
  void gpu_frame_begin();
  void gpu_frame_end();
  unsigned gpu_frames_queued();
  uint64_t texpack_report_frame_ = ~0ull;
  uint32_t frames_presented_ = 0;
  bool capture_pending_ = false;
  std::string capture_pending_path_;
};

// ---------------------------------------------------------------- setup
void D3D11Backend::init() {
  ComPtr<IDXGIFactory2> factory;
  require(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "dxgi factory");
  ComPtr<IDXGIAdapter1> adapter, chosen;
  for (UINT i = 0; enum_adapter_fastest_first(factory.Get(), i, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 desc; adapter->GetDesc1(&desc);
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
    chosen = adapter;
    break;
  }
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  UINT flags = 0;
#ifdef _DEBUG
  flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
  D3D_FEATURE_LEVEL level{};
  HRESULT hr = D3D11CreateDevice(chosen.Get(), chosen ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                 levels, _countof(levels), D3D11_SDK_VERSION, &device_, &level, &context_);
  if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {   // no debug layer installed
    flags &= ~(UINT)D3D11_CREATE_DEVICE_DEBUG;
    hr = D3D11CreateDevice(chosen.Get(), chosen ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                           levels, _countof(levels), D3D11_SDK_VERSION, &device_, &level, &context_);
  }
  require(hr, "device creation");
  if (chosen) {
    DXGI_ADAPTER_DESC1 desc; chosen->GetDesc1(&desc);
    char name[128]; WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof name, nullptr, nullptr);
    host::log("d3d11: using %s (feature level %d.%d)", name, (level >> 12) & 0xF, (level >> 8) & 0xF);
    // A quarter of the adapter, floor 256 MB, ceiling 2 GB, the same share the D3D12 backend uses:
    // bounds the texture cache on every GPU instead of letting it grow for the whole session.
    texture_budget_bytes_ = std::clamp<uint64_t>((uint64_t)desc.DedicatedVideoMemory / 4,
                                                 256ull * 1024 * 1024, 2048ull * 1024 * 1024);
  }
  context_.As(&context1_);
  {
    // Frame timing queries (see gpu_frame_begin). A device that refuses any of them simply reports
    // 0 ms and 0 queued in the diagnostic line.
    bool ok = true;
    for (GpuTimer& t : gpu_timers_) {
      D3D11_QUERY_DESC qd{};
      qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT; ok = ok && SUCCEEDED(device_->CreateQuery(&qd, &t.disjoint));
      qd.Query = D3D11_QUERY_TIMESTAMP; ok = ok && SUCCEEDED(device_->CreateQuery(&qd, &t.start)) && SUCCEEDED(device_->CreateQuery(&qd, &t.end));
      qd.Query = D3D11_QUERY_EVENT; ok = ok && SUCCEEDED(device_->CreateQuery(&qd, &t.done));
    }
    if (!ok) {
      for (GpuTimer& t : gpu_timers_) { t.disjoint.Reset(); t.start.Reset(); t.end.Reset(); t.done.Reset(); }
      host::log("d3d11: GPU timestamp queries unavailable; the gpu: line reports 0 ms");
    }
  }
  {
    D3D11_FEATURE_DATA_D3D11_OPTIONS o{};
    if (context1_ && SUCCEEDED(device_->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &o, sizeof o)))
      constant_offsetting_ = o.ConstantBufferOffsetting != FALSE;
    host::log("d3d11: constant buffer offsetting %s", constant_offsetting_ ? "supported" : "unavailable (one buffer per draw)");
  }
  {
    ComPtr<IDXGIFactory5> factory5;
    BOOL tearing = FALSE;
    if (SUCCEEDED(factory.As(&factory5)) && SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof tearing)))
      allow_tearing_ = tearing != FALSE;
  }
  DXGI_SWAP_CHAIN_DESC1 sd{};
  sd.Width = client_w_; sd.Height = client_h_; sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.SampleDesc.Count = 1;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 3; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  // No DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH: with it, going exclusive changes the display mode
  // to match the window, so the desktop resolution became whatever "Window size" was set to.
  // Without it exclusive fullscreen keeps the desktop's own mode and refresh rate.
  sd.Flags = allow_tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
  if (FAILED(factory->CreateSwapChainForHwnd(device_.Get(), hwnd_, &sd, nullptr, nullptr, &swapchain_))) {
    // Pre-Windows-10 runtimes have no flip-discard: fall back to the blit model.
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD; sd.BufferCount = 1; sd.Flags = 0; allow_tearing_ = false;
    require(factory->CreateSwapChainForHwnd(device_.Get(), hwnd_, &sd, nullptr, nullptr, &swapchain_), "swapchain");
  }
  factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
  // With V-Sync on, the default lets three finished frames wait in line for the display. After a
  // stall the game runs slightly fast to catch up and that line filled, and it stayed filled:
  // up to 50 ms of extra delay for the rest of the session. One frame in line is enough.
  {
    ComPtr<IDXGIDevice1> dxgi_device;
    if (SUCCEEDED(device_.As(&dxgi_device))) dxgi_device->SetMaximumFrameLatency(1);
  }

  create_swapchain_targets(false);
  create_efb();
  if (opts_.exclusive_fullscreen) apply_fullscreen_mode();

  // The shader cache directory is shared with the D3D12 backend: vs_5_0/ps_5_0 bytecode is identical.
  CreateDirectoryA(opts_.shader_cache.c_str(), nullptr);
  shader_cache_root_ = opts_.shader_cache;
  opts_.shader_cache += "/" GX_SHADER_CACHE_VERSION;
  CreateDirectoryA(opts_.shader_cache.c_str(), nullptr);

  if (!vertex_buffer_.init(device_.Get(), context_.Get(), 8 << 20, D3D11_BIND_VERTEX_BUFFER)) host::die("D3D11: vertex buffer");
  if (!index_buffer_.init(device_.Get(), context_.Get(), 4 << 20, D3D11_BIND_INDEX_BUFFER)) host::die("D3D11: index buffer");
  // Only the head of VSBlock is uploaded and bound: everything past unjittered_projection is read
  // solely by the motion-vector shader variant, which is DLSS-only and never generated here.
  // Constants outside the bound window read as zero, so the tail costs nothing.
  vs_constants_.init(device_.Get(), context_.Get(), VS_CONSTANT_BYTES, constant_offsetting_);
  ps_constants_.init(device_.Get(), context_.Get(), sizeof(PSConstants), constant_offsetting_);

  // Blit (EFB -> back buffer, and the half-scale EFB copy). Identical HLSL to the D3D12 backend,
  // with the root constants replaced by a constant buffer.
  const char* blit = R"(
Texture2D src : register(t0); Texture2D<float> efb_depth : register(t3); SamplerState samp : register(s0);
cbuffer C : register(b0) { float4 rect; float4 sharp; float4 box; float4 color; float4 crt; float4 crt2; };  // color: brightness, contrast, vibrance, SSAO strength
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
float ao(float2 uv) {
  if (color.w <= 0.0) return 1.0;
  float d = efb_depth.Sample(samp, uv).r;
  if (d <= 0.00001) return 1.0;
  uint depth_w, depth_h;
  efb_depth.GetDimensions(depth_w, depth_h);
  float2 px = 2.0 / max(float2(depth_w, depth_h), 1.0);
  float2 dirs[8] = {float2(-1,-1), float2(0,-1), float2(1,-1), float2(-1,0), float2(1,0), float2(-1,1), float2(0,1), float2(1,1)};
  float occ = 0.0;
  float bias = max(0.0015, d * 0.012);
  [unroll] for (int k = 0; k < 8; ++k) {
    float nd = efb_depth.Sample(samp, uv + dirs[k] * px).r;
    occ += nd > d + bias ? 1.0 : 0.0;
  }
  return saturate(1.0 - (occ * 0.125) * color.w);
}
float4 PS(O i) : SV_Target {
  if (color.w < -1.5) {
    uint z = min((uint)((1.0 - src.Sample(samp, i.uv).r) * 16777216.0), 0xFFFFFFu);
    return float4(float3((z >> 16) & 255u, (z >> 8) & 255u, z & 255u) / 255.0, 1.0);
  }
  float4 c = downsample(i.uv);
  // Copies from an EFB without alpha read as opaque on hardware.
  if (color.w < 0.0) return float4(c.rgb, 1.0);
  c.rgb *= ao(i.uv);
  if (sharp.z <= 0.0) {
    float3 rgb = (c.rgb * color.x - 0.5) * color.y + 0.5;
    float luma = dot(rgb, float3(0.2126, 0.7152, 0.0722));
    return float4(saturate(lerp(luma.xxx, rgb, color.z)), c.a);
  }
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
  r = (saturate(r) * color.x - 0.5) * color.y + 0.5;
  float luma = dot(r, float3(0.2126, 0.7152, 0.0722));
  return float4(saturate(lerp(luma.xxx, r, color.z)), c.a);
})";
  ComPtr<ID3DBlob> bvs, bps, err;
  fail(D3DCompile(blit, strlen(blit), nullptr, nullptr, nullptr, "VS", "vs_5_0", 0, 0, &bvs, &err), "blit vs");
  fail(D3DCompile(blit, strlen(blit), nullptr, nullptr, nullptr, "PS", "ps_5_0", 0, 0, &bps, &err), "blit ps");
  fail(device_->CreateVertexShader(bvs->GetBufferPointer(), bvs->GetBufferSize(), nullptr, &blit_vs_), "blit vs object");
  fail(device_->CreatePixelShader(bps->GetBufferPointer(), bps->GetBufferSize(), nullptr, &blit_ps_), "blit ps object");
  // The CRT display model as a second pixel shader over the same inputs (see the D3D12 backend).
  const std::string crt_src = std::string(blit) + R"(
float3 crt_tap(float2 uv) { return src.SampleLevel(samp, uv, 0).rgb; }
)" + kCrtShader + R"(
float4 PS_CRT(O i) : SV_Target {
  float2 picture_uv;
  float3 c = crt_pixel(i.pos.xy, i.uv, picture_uv);
  c *= ao(picture_uv);
  c = (c * color.x - 0.5) * color.y + 0.5;
  float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
  return float4(saturate(lerp(luma.xxx, c, color.z)), 1.0);
})";
  ComPtr<ID3DBlob> crt_blob;
  fail(D3DCompile(crt_src.data(), crt_src.size(), nullptr, nullptr, nullptr, "PS_CRT", "ps_5_0", 0, 0, &crt_blob, &err), "crt ps");
  fail(device_->CreatePixelShader(crt_blob->GetBufferPointer(), crt_blob->GetBufferSize(), nullptr, &crt_ps_), "crt ps object");

  // D3D11 cannot clear a sub-rectangle of a depth buffer, so the EFB clear is a scissored quad
  // that writes the clear colour and depth (what ClearRenderTargetView/ClearDepthStencilView with
  // a rect do on D3D12).
  const char* clear_src = R"(
cbuffer C : register(b0) { float4 clear_color; float4 clear_depth; };
float4 VS(uint id : SV_VertexID) : SV_Position { float2 p = float2((id << 1) & 2, id & 2); return float4(p * float2(2,-2) + float2(-1,1), clear_depth.x, 1); }
float4 PS() : SV_Target { return clear_color; })";
  ComPtr<ID3DBlob> cvs, cps;
  fail(D3DCompile(clear_src, strlen(clear_src), nullptr, nullptr, nullptr, "VS", "vs_5_0", 0, 0, &cvs, &err), "clear vs");
  fail(D3DCompile(clear_src, strlen(clear_src), nullptr, nullptr, nullptr, "PS", "ps_5_0", 0, 0, &cps, &err), "clear ps");
  fail(device_->CreateVertexShader(cvs->GetBufferPointer(), cvs->GetBufferSize(), nullptr, &clear_vs_), "clear vs object");
  fail(device_->CreatePixelShader(cps->GetBufferPointer(), cps->GetBufferSize(), nullptr, &clear_ps_), "clear ps object");

  D3D11_BUFFER_DESC cbd{};
  cbd.ByteWidth = 96; cbd.Usage = D3D11_USAGE_DYNAMIC; cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  fail(device_->CreateBuffer(&cbd, nullptr, &blit_cb_), "blit cb");
  cbd.ByteWidth = 32;
  fail(device_->CreateBuffer(&cbd, nullptr, &clear_cb_), "clear cb");

  D3D11_SAMPLER_DESC ss{};
  ss.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  ss.AddressU = ss.AddressV = ss.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  ss.ComparisonFunc = D3D11_COMPARISON_NEVER; ss.MaxLOD = D3D11_FLOAT32_MAX; ss.MaxAnisotropy = 1;
  fail(device_->CreateSamplerState(&ss, &blit_sampler_), "blit sampler");

  D3D11_BLEND_DESC bd{};
  bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  fail(device_->CreateBlendState(&bd, &opaque_blend_), "opaque blend");
  D3D11_DEPTH_STENCIL_DESC dd{};
  dd.DepthEnable = FALSE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
  fail(device_->CreateDepthStencilState(&dd, &no_depth_), "blit depth state");
  dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
  fail(device_->CreateDepthStencilState(&dd, &clear_depth_), "clear depth state");
  D3D11_RASTERIZER_DESC rs{};
  rs.FillMode = D3D11_FILL_SOLID; rs.CullMode = D3D11_CULL_NONE; rs.DepthClipEnable = TRUE; rs.ScissorEnable = TRUE;
  fail(device_->CreateRasterizerState(&rs, &blit_raster_), "blit raster");
}

void D3D11Backend::create_swapchain_targets(bool resize) {
  backbuffer_rtv_.Reset();
  capture_staging_.Reset();
  if (resize) {
    context_->ClearState(); reset_bound();
    context_->Flush();
    DXGI_SWAP_CHAIN_DESC1 sd{}; swapchain_->GetDesc1(&sd);
    // A resize that runs out of memory (reported on integrated graphics, with the internal
    // resolution going up at the same moment) used to close the game. It is tried again with one
    // buffer fewer, and failing that the old size is kept: a picture scaled to the window is
    // better than a crash report.
    HRESULT hr = swapchain_->ResizeBuffers(sd.BufferCount, client_w_, client_h_, DXGI_FORMAT_R8G8B8A8_UNORM, sd.Flags);
    if (FAILED(hr) && sd.BufferCount > 2) {
      host::log("d3d11: resize to %ux%u failed (%08X); trying with %u buffers", client_w_, client_h_, (unsigned)hr, sd.BufferCount - 1);
      context_->Flush();
      hr = swapchain_->ResizeBuffers(sd.BufferCount - 1, client_w_, client_h_, DXGI_FORMAT_R8G8B8A8_UNORM, sd.Flags);
    }
    if (FAILED(hr)) {
      host::log("d3d11: resize to %ux%u failed (%08X); keeping %ux%u", client_w_, client_h_, (unsigned)hr, sd.Width, sd.Height);
      hr = swapchain_->ResizeBuffers(sd.BufferCount, sd.Width, sd.Height, DXGI_FORMAT_R8G8B8A8_UNORM, sd.Flags);
      if (SUCCEEDED(hr)) { client_w_ = sd.Width; client_h_ = sd.Height; }   // drawn at the old size, stretched by DXGI
    }
    fail(hr, "resize");
  }
  ComPtr<ID3D11Texture2D> back;
  fail(swapchain_->GetBuffer(0, IID_PPV_ARGS(&back)), "backbuffer");
  fail(device_->CreateRenderTargetView(back.Get(), nullptr, &backbuffer_rtv_), "backbuffer rtv");
}

void D3D11Backend::apply_fullscreen_mode() {
  if (!swapchain_) return;
  BOOL was_exclusive = FALSE;
  swapchain_->GetFullscreenState(&was_exclusive, nullptr);
  const bool want_exclusive = opts_.exclusive_fullscreen;
  if ((was_exclusive != FALSE) == want_exclusive) {
    if (!want_exclusive) host::window_set_fullscreen(opts_.fullscreen);
    return;
  }
  context_->ClearState(); context_->Flush();
  backbuffer_rtv_.Reset(); capture_staging_.Reset();
  if (was_exclusive) {
    const HRESULT hr = swapchain_->SetFullscreenState(FALSE, nullptr);
    if (FAILED(hr)) host::log("d3d11: leaving exclusive fullscreen failed (0x%08X)", (unsigned)hr);
  }
  if (host::window_is_fullscreen()) host::window_set_fullscreen(false);
  if (want_exclusive) {
    ComPtr<IDXGIOutput> output;
    DXGI_OUTPUT_DESC desc{};
    if (SUCCEEDED(swapchain_->GetContainingOutput(&output)) && SUCCEEDED(output->GetDesc(&desc))) {
      DXGI_MODE_DESC mode{};
      mode.Width = (UINT)(desc.DesktopCoordinates.right - desc.DesktopCoordinates.left);
      mode.Height = (UINT)(desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top);
      mode.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      if (mode.Width >= 320 && mode.Height >= 240) swapchain_->ResizeTarget(&mode);
    }
    const HRESULT hr = swapchain_->SetFullscreenState(TRUE, nullptr);
    if (FAILED(hr)) {
      host::log("d3d11: exclusive fullscreen unavailable (0x%08X); using borderless fullscreen", (unsigned)hr);
      opts_.exclusive_fullscreen = false;
      opts_.fullscreen = true;
      host::window_set_fullscreen(true);
      RECT client{}; if (GetClientRect(hwnd_, &client)) { client_w_ = client.right; client_h_ = client.bottom; }
    } else {
      ComPtr<IDXGIOutput> output;
      DXGI_OUTPUT_DESC desc{};
      if (SUCCEEDED(swapchain_->GetContainingOutput(&output)) && SUCCEEDED(output->GetDesc(&desc))) {
        client_w_ = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
        client_h_ = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
      } else host::window_client_size(&client_w_, &client_h_);
    }
  } else {
    host::window_set_fullscreen(opts_.fullscreen);
    RECT client{}; if (GetClientRect(hwnd_, &client)) { client_w_ = client.right; client_h_ = client.bottom; }
  }
  client_w_ = std::max(client_w_, 1); client_h_ = std::max(client_h_, 1);
  create_swapchain_targets(true);
  if (pick_scale() != scale_) {
    host::log("d3d11: fullscreen transition changes internal scale %d -> %d", scale_, pick_scale());
    efb_color_.Reset(); efb_depth_.Reset(); efb_rtv_.Reset(); efb_srv_.Reset(); efb_depth_srv_.Reset(); efb_dsv_.Reset();
    create_efb();
  }
}

// Mirrors the D3D12 backend (and Dolphin's CalculateTargetSize).
int D3D11Backend::pick_scale() const {
  constexpr int max_scale = 16384 / EFB_WIDTH;
  const int ssaa = std::clamp(opts_.ssaa, 1, 2);
  if (opts_.efb_scale > 0) return std::clamp(opts_.efb_scale * ssaa, 1, max_scale);
  float ww = (float)std::max(client_w_, 1), wh = (float)std::max(client_h_, 1);
  float aspect = output_aspect();
  float vw = ww, vh = ww / aspect;
  if (vh > wh) { vh = wh; vw = wh * aspect; }
  int s = std::max((int)std::ceil(vw / (480.0f * aspect)), (int)std::ceil(vh / 480.0f));
  return std::clamp(s * ssaa, 1, max_scale);
}

// Melee's camera asks for a 73:60 frustum, which the Slippi widescreen code widens to exactly
// 16:9. See presented_aspect in gx_d3d12.h for the evidence and the player's override.
float D3D11Backend::output_aspect() const { return presented_aspect(opts_, client_w_, client_h_, widenable_scene_); }

void D3D11Backend::output_size(int* vw, int* vh) const {
  float ww = (float)std::max(client_w_, 1), wh = (float)std::max(client_h_, 1);
  float aspect = output_aspect();
  float w = ww, h = ww / aspect;
  if (h > wh) { h = wh; w = wh * aspect; }
  *vw = std::max(1, (int)w); *vh = std::max(1, (int)h);
}

void D3D11Backend::create_efb() {
  scale_ = pick_scale();
  efb_w_ = EFB_WIDTH * scale_; efb_h_ = EFB_HEIGHT * scale_;
  host::log("d3d11: internal resolution %dx%d (EFB x%d, window %dx%d)", efb_w_, efb_h_, scale_, client_w_, client_h_);
  efb_rtv_.Reset(); efb_srv_.Reset(); efb_depth_srv_.Reset(); efb_dsv_.Reset(); efb_color_.Reset(); efb_depth_.Reset();
  context_->ClearState(); reset_bound();
  D3D11_TEXTURE2D_DESC td{};
  td.Width = efb_w_; td.Height = efb_h_; td.MipLevels = 1; td.ArraySize = 1;
  td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
  td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  fail(device_->CreateTexture2D(&td, nullptr, &efb_color_), "efb color");
  fail(device_->CreateRenderTargetView(efb_color_.Get(), nullptr, &efb_rtv_), "efb rtv");
  fail(device_->CreateShaderResourceView(efb_color_.Get(), nullptr, &efb_srv_), "efb srv");
  td.Format = DXGI_FORMAT_R32_TYPELESS; td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
  fail(device_->CreateTexture2D(&td, nullptr, &efb_depth_), "efb depth");
  D3D11_DEPTH_STENCIL_VIEW_DESC dsv{}; dsv.Format = DXGI_FORMAT_D32_FLOAT; dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
  fail(device_->CreateDepthStencilView(efb_depth_.Get(), &dsv, &efb_dsv_), "efb dsv");
  D3D11_SHADER_RESOURCE_VIEW_DESC depth_srv{}; depth_srv.Format = DXGI_FORMAT_R32_FLOAT; depth_srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  depth_srv.Texture2D.MipLevels = 1;
  fail(device_->CreateShaderResourceView(efb_depth_.Get(), &depth_srv, &efb_depth_srv_), "efb depth srv");
  const float black[4] = {0, 0, 0, 0};
  context_->ClearRenderTargetView(efb_rtv_.Get(), black);
  context_->ClearDepthStencilView(efb_dsv_.Get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
}

void D3D11Backend::unbind_shader_resources() {
  ID3D11ShaderResourceView* none[8] = {};
  context_->PSSetShaderResources(0, 8, none);
  for (auto& s : bound_.srv) s = nullptr;
}

void D3D11Backend::bind_efb_targets() {
  unbind_shader_resources();   // the EFB itself may still be bound as a source
  ID3D11RenderTargetView* rtv = efb_rtv_.Get();
  context_->OMSetRenderTargets(1, &rtv, efb_dsv_.Get());
}

// ---------------------------------------------------------------- state objects
ID3D11BlendState* D3D11Backend::blend_state(uint32_t blend, uint32_t pixel_format) {
  uint32_t key = blend | (pixel_format << 16);
  auto it = blend_states_.find(key);
  if (it != blend_states_.end()) return it->second.Get();
  static const D3D11_BLEND src_factors[] = {D3D11_BLEND_ZERO, D3D11_BLEND_ONE, D3D11_BLEND_DEST_COLOR, D3D11_BLEND_INV_DEST_COLOR,
                                            D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_DEST_ALPHA, D3D11_BLEND_INV_DEST_ALPHA};
  static const D3D11_BLEND dst_factors[] = {D3D11_BLEND_ZERO, D3D11_BLEND_ONE, D3D11_BLEND_SRC_COLOR, D3D11_BLEND_INV_SRC_COLOR,
                                            D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_DEST_ALPHA, D3D11_BLEND_INV_DEST_ALPHA};
  D3D11_BLEND_DESC bd{};
  auto& rt = bd.RenderTarget[0];
  const bool alpha_in_efb = pixel_format == 1;   // RGBA6_Z24
  uint32_t sf = bits(blend, 8, 3), df = bits(blend, 5, 3);
  D3D11_BLEND s = src_factors[sf], d = dst_factors[df];
  if (!alpha_in_efb) {
    if (s == D3D11_BLEND_DEST_ALPHA) s = D3D11_BLEND_ONE; if (s == D3D11_BLEND_INV_DEST_ALPHA) s = D3D11_BLEND_ZERO;
    if (d == D3D11_BLEND_DEST_ALPHA) d = D3D11_BLEND_ONE; if (d == D3D11_BLEND_INV_DEST_ALPHA) d = D3D11_BLEND_ZERO;
  }
  rt.BlendEnable = bits(blend, 0, 1) != 0;
  rt.SrcBlend = s; rt.DestBlend = d; rt.BlendOp = bits(blend, 11, 1) ? D3D11_BLEND_OP_REV_SUBTRACT : D3D11_BLEND_OP_ADD;
  rt.SrcBlendAlpha = D3D11_BLEND_ONE; rt.DestBlendAlpha = D3D11_BLEND_ZERO; rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
  rt.RenderTargetWriteMask = (UINT8)((bits(blend, 3, 1) ? (D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE) : 0) |
                                     (bits(blend, 4, 1) ? D3D11_COLOR_WRITE_ENABLE_ALPHA : 0));
  ComPtr<ID3D11BlendState> state;
  if (FAILED(device_->CreateBlendState(&bd, &state))) return opaque_blend_.Get();
  ID3D11BlendState* raw = state.Get();
  blend_states_[key] = std::move(state);
  return raw;
}

ID3D11DepthStencilState* D3D11Backend::depth_state(uint32_t zmode) {
  auto it = depth_states_.find(zmode);
  if (it != depth_states_.end()) return it->second.Get();
  // Reversed depth range (GC near = 1.0), as in the D3D12 backend.
  static const D3D11_COMPARISON_FUNC cmp[] = {D3D11_COMPARISON_NEVER, D3D11_COMPARISON_GREATER, D3D11_COMPARISON_EQUAL,
                                              D3D11_COMPARISON_GREATER_EQUAL, D3D11_COMPARISON_LESS, D3D11_COMPARISON_NOT_EQUAL,
                                              D3D11_COMPARISON_LESS_EQUAL, D3D11_COMPARISON_ALWAYS};
  D3D11_DEPTH_STENCIL_DESC dd{};
  dd.DepthEnable = bits(zmode, 0, 1) != 0;
  dd.DepthWriteMask = (bits(zmode, 0, 1) && bits(zmode, 4, 1)) ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
  dd.DepthFunc = bits(zmode, 0, 1) ? cmp[bits(zmode, 1, 3)] : D3D11_COMPARISON_ALWAYS;
  ComPtr<ID3D11DepthStencilState> state;
  if (FAILED(device_->CreateDepthStencilState(&dd, &state))) return no_depth_.Get();
  ID3D11DepthStencilState* raw = state.Get();
  depth_states_[zmode] = std::move(state);
  return raw;
}

ID3D11RasterizerState* D3D11Backend::raster_state(uint32_t cull) {
  cull &= 3;
  auto it = raster_states_.find(cull);
  if (it != raster_states_.end()) return it->second.Get();
  static const D3D11_CULL_MODE cull_modes[] = {D3D11_CULL_NONE, D3D11_CULL_BACK, D3D11_CULL_FRONT, D3D11_CULL_BACK};
  D3D11_RASTERIZER_DESC rd{};
  rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = cull_modes[cull];
  rd.FrontCounterClockwise = FALSE; rd.DepthClipEnable = TRUE; rd.ScissorEnable = TRUE;
  ComPtr<ID3D11RasterizerState> state;
  if (FAILED(device_->CreateRasterizerState(&rd, &state))) return blit_raster_.Get();
  ID3D11RasterizerState* raw = state.Get();
  raster_states_[cull] = std::move(state);
  return raw;
}

void D3D11Backend::ensure_input_layout(ID3DBlob* vs) {
  if (layout_) return;
  static const D3D11_INPUT_ELEMENT_DESC layout[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 2, DXGI_FORMAT_R32G32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 3, DXGI_FORMAT_R32G32_FLOAT, 0, 56, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 4, DXGI_FORMAT_R32G32_FLOAT, 0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 5, DXGI_FORMAT_R32G32_FLOAT, 0, 72, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 6, DXGI_FORMAT_R32G32_FLOAT, 0, 80, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 7, DXGI_FORMAT_R32G32_FLOAT, 0, 88, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 96, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"BLENDINDICES", 1, DXGI_FORMAT_R8G8B8A8_UINT, 0, 100, D3D11_INPUT_PER_VERTEX_DATA, 0},
    // Vertex::texmtx[7] at offset 104. Without it texture generator 7 read generator 6's index.
    {"BLENDINDICES", 2, DXGI_FORMAT_R8_UINT, 0, 104, D3D11_INPUT_PER_VERTEX_DATA, 0},
  };
  check(device_->CreateInputLayout(layout, _countof(layout), vs->GetBufferPointer(), vs->GetBufferSize(), &layout_), "input layout");
}

// ---------------------------------------------------------------- shader compilation
bool D3D11Backend::load_shader_blob(const std::string& path, ComPtr<ID3DBlob>& blob) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
  if (n <= 0 || n > (16 << 20) || FAILED(D3DCreateBlob((SIZE_T)n, &blob))) { std::fclose(f); return false; }
  bool ok = std::fread(blob->GetBufferPointer(), 1, (size_t)n, f) == (size_t)n;
  std::fclose(f);
  if (!ok) blob.Reset();
  return ok;
}

static void write_cache_file(const std::string& path, const void* data, size_t size) {
  std::string temporary = path + "." + std::to_string(GetCurrentProcessId()) + ".tmp";
  FILE* file = std::fopen(temporary.c_str(), "wb");
  if (!file) return;
  bool ok = std::fwrite(data, 1, size, file) == size;
  ok = std::fclose(file) == 0 && ok;
  if (ok) ok = MoveFileExA(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
  if (!ok) DeleteFileA(temporary.c_str());
}

void D3D11Backend::save_shader_blob(const std::string& path, ID3DBlob* blob) {
  write_cache_file(path, blob->GetBufferPointer(), blob->GetBufferSize());
}

bool D3D11Backend::compile_shaders(const PipeKey& key, const VSUid& vsu, const PSUid& psu, ComPtr<ID3DBlob>& vs, ComPtr<ID3DBlob>& ps) {
  char vs_name[64], ps_name[64];
  snprintf(vs_name, sizeof vs_name, "vs_%016llX.dxbc", (unsigned long long)key.vs);
  snprintf(ps_name, sizeof ps_name, "ps_%016llX.dxbc", (unsigned long long)key.ps);
  if (!vs && !load_shader_blob(opts_.shader_cache + "/" + vs_name, vs)) {
    std::string src = generate_vertex_shader(vsu);
    ComPtr<ID3DBlob> err;
    if (FAILED(D3DCompile(src.c_str(), src.size(), "vs", nullptr, nullptr, "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs, &err))) {
      host::log("d3d11: vertex shader compile failed:\n%s", err ? (const char*)err->GetBufferPointer() : "?");
      return false;
    }
    save_shader_blob(opts_.shader_cache + "/" + vs_name, vs.Get());
  }
  if (!ps && !load_shader_blob(opts_.shader_cache + "/" + ps_name, ps)) {
    std::string src = generate_pixel_shader(psu);
    ComPtr<ID3DBlob> err;
    if (FAILED(D3DCompile(src.c_str(), src.size(), "ps", nullptr, nullptr, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, &err))) {
      host::log("d3d11: pixel shader compile failed:\n%s", err ? (const char*)err->GetBufferPointer() : "?");
      return false;
    }
    save_shader_blob(opts_.shader_cache + "/" + ps_name, ps.Get());
  }
  return vs && ps;
}

// Shader objects and state objects are created on the render thread (ID3D11Device creation is
// free-threaded, but keeping it here means the caches need no locking).
Pipeline* D3D11Backend::build_pipeline(const PipeKey& key, ID3DBlob* vs, ID3DBlob* ps, D3D11_PRIMITIVE_TOPOLOGY topology) {
  auto pipeline = std::make_unique<Pipeline>();
  if (FAILED(device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &pipeline->vs))) return nullptr;
  if (FAILED(device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &pipeline->ps))) return nullptr;
  ensure_input_layout(vs);
  pipeline->blend = blend_state(key.blend, key.pixel_format);
  pipeline->depth = depth_state(key.zmode);
  pipeline->raster = raster_state(key.cull);
  pipeline->topology = topology;
  Pipeline* raw = pipeline.get();
  pipelines_[key] = std::move(pipeline);
  return raw;
}

void D3D11Backend::shader_worker() {
  for (;;) {
    ShaderJob job;
    {
      std::unique_lock<std::mutex> lk(shader_mutex_);
      shader_cv_.wait(lk, [&] { return shader_quit_ || !shader_jobs_.empty(); });
      if (shader_quit_) return;
      job = shader_jobs_.front(); shader_jobs_.pop_front();
    }
    ShaderResult r; r.key = job.key; r.recipe = job.recipe;
    compile_shaders(job.key, job.vsu, job.psu, r.vs, r.ps);
    { std::lock_guard<std::mutex> lk(shader_mutex_); shader_done_.push_back(std::move(r)); }
    shader_done_cv_.notify_all();
  }
}

void D3D11Backend::integrate_compiled_pipelines() {
  std::vector<ShaderResult> done;
  { std::lock_guard<std::mutex> lk(shader_mutex_); done.swap(shader_done_); }
  for (auto& r : done) {
    pipelines_pending_.erase(r.key);
    if (!r.vs || !r.ps) continue;
    if (!vs_blobs_[r.key.vs]) vs_blobs_[r.key.vs] = r.vs;
    if (!ps_blobs_[r.key.ps]) ps_blobs_[r.key.ps] = r.ps;
    if (pipelines_.count(r.key)) continue;
    D3D11_PRIMITIVE_TOPOLOGY topology = r.key.topology == TOPO_LINE ? D3D11_PRIMITIVE_TOPOLOGY_LINELIST : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    if (build_pipeline(r.key, r.vs.Get(), r.ps.Get(), topology) && pipeline_recipes_.size() < 16384)
      pipeline_recipes_.push_back(r.recipe);
  }
}

void D3D11Backend::start_shader_workers() {
  int workers = std::clamp((int)std::thread::hardware_concurrency() - 2, 2, 6);
  for (int i = 0; i < workers; ++i) shader_threads_.emplace_back([this] { shader_worker(); });
}

void D3D11Backend::stop_shader_workers() {
  { std::lock_guard<std::mutex> lk(shader_mutex_); shader_quit_ = true; }
  shader_cv_.notify_all();
  for (auto& t : shader_threads_) t.join();
  shader_threads_.clear();
}

// While the real shaders compile, draw with a generic pipeline (position, vertex colour,
// texture 0) instead of dropping the draw, exactly as the D3D12 backend does.
Pipeline* D3D11Backend::fallback_pipeline(const PipeKey& key, const DrawCall& dc, D3D11_PRIMITIVE_TOPOLOGY topology) {
  const bool textured = (dc.xf_regs[0x3F] & 15) != 0 && (dc.components & VB_HAS_UV0);
  const bool colored = (dc.components & VB_HAS_COL0) != 0;
  FallbackKey fk{key.blend, key.zmode, key.cull, key.topology, key.pixel_format, (uint32_t)textured | ((uint32_t)colored << 1)};
  auto it = fallback_pipelines_.find(fk);
  if (it != fallback_pipelines_.end()) return it->second.get();
  static const char* vs_src = R"(
cbuffer VSBlock : register(b0) {
float4 projection[4]; float4 depthparams; float4 viewparams; float4 materials[4]; float4 lights[40];
float4 texmatrices[24]; float4 transformmatrices[64]; float4 normalmatrices[32]; float4 posttransformmatrices[64]; };
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
void main(out float4 ocol0 : SV_Target0, in float4 rawpos : SV_Position, in float4 color : COLOR0, in float2 uv : TEXCOORD0) {
  float4 c = color;
  TEX_EXPR
  if (c.a <= 0.0) discard;
  ocol0 = c;
})";
  std::string vs = vs_src, ps = ps_src;
  vs.replace(vs.find("COLOR_EXPR"), 10, colored ? "color0" : "float4(1.0, 1.0, 1.0, 1.0)");
  ps.replace(ps.find("TEX_EXPR"), 8, textured ? "c *= Tex[0].Sample(samp[0], uv);" : "");
  ComPtr<ID3DBlob> vsb, psb, err;
  if (FAILED(D3DCompile(vs.c_str(), vs.size(), "fallback_vs", nullptr, nullptr, "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsb, &err)) ||
      FAILED(D3DCompile(ps.c_str(), ps.size(), "fallback_ps", nullptr, nullptr, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psb, &err))) {
    host::log("d3d11: fallback shader compile failed: %s", err ? (const char*)err->GetBufferPointer() : "?");
    fallback_pipelines_[fk] = nullptr; return nullptr;
  }
  auto pipeline = std::make_unique<Pipeline>();
  if (FAILED(device_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &pipeline->vs)) ||
      FAILED(device_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &pipeline->ps))) {
    fallback_pipelines_[fk] = nullptr; return nullptr;
  }
  ensure_input_layout(vsb.Get());
  pipeline->blend = blend_state(key.blend, key.pixel_format);
  pipeline->depth = depth_state(key.zmode);
  pipeline->raster = raster_state(key.cull);
  pipeline->topology = topology;
  Pipeline* raw = pipeline.get();
  fallback_pipelines_[fk] = std::move(pipeline);
  return raw;
}

Pipeline* D3D11Backend::get_pipeline(const DrawCall& dc, uint32_t topo_type, D3D11_PRIMITIVE_TOPOLOGY topology) {
  if (dc.cached_pipeline && dc.cached_pipeline_owner == backend_id_) { ++g_pipe_hits; return (Pipeline*)dc.cached_pipeline; }
  Stopwatch sw;
  VSUid vsu = make_vs_uid(dc);
  PSUid psu = make_ps_uid(dc);
  uint64_t vh = vsu.hash(), ph = psu.hash();
  PipeKey key{vh, ph, dc.bp.blendmode() & 0xFFFF, dc.bp.zmode() & 0x1F, dc.bp.cullmode(), topo_type, dc.bp.zcontrol() & 7};
  g_prof[6] += sw.lap(); ++g_pipe_lookups;
  auto it = pipelines_.find(key);
  g_prof[7] += sw.lap();
  if (it != pipelines_.end()) { dc.cached_pipeline_owner = backend_id_; dc.cached_pipeline = it->second.get(); return it->second.get(); }
  PipelineRecipe recipe{}; recipe.topology = topo_type; recipe.components = dc.components;
  recipe.bp = dc.bp; std::memcpy(recipe.xf, dc.xf_regs, sizeof(recipe.xf));
  if (!prewarming_ && !shader_threads_.empty()) {
    if (pipelines_pending_.insert(key).second) {
      ++g_pipe_creates;
      std::lock_guard<std::mutex> lk(shader_mutex_);
      shader_jobs_.push_front(ShaderJob{key, vsu, psu, recipe});
      shader_cv_.notify_one();
    }
    const bool exact = capturing_efb_ || (dc.bp.ztex2() & 12);
    while (shader_wait_budget_us_ > 0 || exact) {
      Stopwatch wait_sw;
      {
        std::unique_lock<std::mutex> lk(shader_mutex_);
        shader_done_cv_.wait_for(lk, std::chrono::microseconds(exact ? 2000 : std::min(shader_wait_budget_us_, 2000)), [&] { return !shader_done_.empty(); });
      }
      shader_wait_budget_us_ -= (int)(wait_sw.lap() * 1e6);
      integrate_compiled_pipelines();
      auto ready = pipelines_.find(key);
      if (ready != pipelines_.end()) { dc.cached_pipeline_owner = backend_id_; dc.cached_pipeline = ready->second.get(); return ready->second.get(); }
      if (!pipelines_pending_.count(key)) break; // compile failure
    }
    ++g_pipe_skips;
    return fallback_pipeline(key, dc, topology);
  }
  ++g_pipe_creates;
  ComPtr<ID3DBlob>& vs = vs_blobs_[vh];
  ComPtr<ID3DBlob>& ps = ps_blobs_[ph];
  if (!compile_shaders(key, vsu, psu, vs, ps)) return nullptr;
  Pipeline* pipeline = build_pipeline(key, vs.Get(), ps.Get(), topology);
  if (!pipeline) return nullptr;
  if (!prewarming_ && pipeline_recipes_.size() < 4096) pipeline_recipes_.push_back(recipe);
  dc.cached_pipeline_owner = backend_id_; dc.cached_pipeline = pipeline;
  return pipeline;
}

// ---------------------------------------------------------------- textures
TextureEntry* D3D11Backend::update_video_texture(
    const std::shared_ptr<const video_bg::Frame>& frame, int video_slot) {
  if (!frame || video_slot < 0 || video_slot >= video_bg::kVideoSlots || frame->bgra.empty()) return nullptr;
  TextureEntry& e = video_textures_[video_slot];
  if (!e.resource || e.width != frame->width || e.height != frame->height) {
    e = TextureEntry{};
    D3D11_TEXTURE2D_DESC td{};
    td.Width = frame->width; td.Height = frame->height; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &e.resource)) ||
        FAILED(device_->CreateShaderResourceView(e.resource.Get(), nullptr, &e.srv))) {
      char message[96];
      std::snprintf(message, sizeof message, "D3D11 video texture creation failed (%ux%u)",
                    frame->width, frame->height);
      video_bg::report_backend_failure(video_slot, message);
      e = TextureEntry{};
    } else {
      e.width = frame->width; e.height = frame->height; e.levels = 1;
      video_serial_[video_slot] = 0;
    }
  }
  if (e.resource && video_serial_[video_slot] != frame->serial) {
    context_->UpdateSubresource(e.resource.Get(), 0, nullptr, frame->bgra.data(),
                                frame->width * 4, 0);
    video_serial_[video_slot] = frame->serial;
  }
  if (!e.resource) return nullptr;
  e.last_used = frame_counter_;
  return &e;
}

void D3D11Backend::draw_video_background(
    const std::shared_ptr<const video_bg::Frame>& frame, int video_slot,
    const EfbCopy& screen) {
  TextureEntry* video = update_video_texture(frame, video_slot);
  if (!video) return;
  unbind_shader_resources();
  ID3D11RenderTargetView* rtv = efb_rtv_.Get();
  context_->OMSetRenderTargets(1, &rtv, nullptr);
  const float s = (float)scale_;
  D3D11_VIEWPORT vp{screen.src_x * s, screen.src_y * s,
                    screen.src_w * s, screen.src_h * s, 0, 1};
  D3D11_RECT sc{(LONG)(screen.src_x * scale_), (LONG)(screen.src_y * scale_),
                (LONG)((screen.src_x + screen.src_w) * scale_),
                (LONG)((screen.src_y + screen.src_h) * scale_)};
  context_->RSSetViewports(1, &vp);
  context_->RSSetScissorRects(1, &sc);
  // Media Foundation's decoded rows are top-down while this EFB blit samples V in the opposite
  // direction. Flip only the presentation layer; guest textures and ordinary EFB blits retain
  // their existing orientation.
  const float rect[16] = {1, -1, 0, 1,
                          1.0f / frame->width, 1.0f / frame->height, 0, 0,
                          1, 1, 0, 0,
                          1, 1, 1, 0};
  blit(video->srv.Get(), rect);
  bind_efb_targets();
}

TextureEntry* D3D11Backend::get_texture(const TextureRef& t) {
  auto ec = efb_copies_.find(t.addr);
  if (ec != efb_copies_.end() && ec->second.resource) {
    ec->second.last_used = frame_counter_;
    if (copy_dests_.count(t.addr)) ++g_efb_tex_hit;
    return &ec->second;
  }
  // An address the game has copied the EFB into, being sampled without the copy being found. The
  // fallback below decodes guest RAM, which for a render target holds nothing the GPU ever wrote.
  if (copy_dests_.count(t.addr)) ++g_efb_tex_miss;
  if (!t.data) return nullptr;
  // Dynamic menu video is checked before the immutable texture cache. A target may be learned
  // after the vanilla texture was cached, and subsequent frames still need to switch immediately.
  std::string video_name;
  if (video_bg::wants_texture_names()) {
    video_name = texpack::base_name(t, *t.data);
    int video_slot = -1;
    std::shared_ptr<const video_bg::Frame> frame =
        video_bg::lookup(video_name, t.width, t.height, &video_slot);
    if (frame && video_slot >= 0 && video_slot < 2 && !frame->bgra.empty()) {
      if (TextureEntry* video = update_video_texture(frame, video_slot)) {
        draw_video_slot_ = video_slot;
        return video;
      }
    }
  }
  const uint32_t meta[] = {t.width, t.height, t.format, t.mip_levels, t.tlut_format};
  uint64_t key = t.data->hash ^ hash_bytes(meta, sizeof meta);
  auto it = textures_.find(key);
  if (it != textures_.end()) { it->second.last_used = frame_counter_; return &it->second; }

  // Custom texture pack or launch-scoped cosmetic companion, exactly as the D3D12 backend does it:
  // Dolphin's name for this texture, then its PNG if either source has one. Both happen once per
  // unique texture on this cache-miss path, never per draw. Cosmetic companions stay active
  // independently of the general-purpose texture-pack toggle.
  std::string pack_name;
  std::unique_ptr<texpack::Replacement> replacement;
  if (texpack::enabled() || texpack::dumping() || texpack::cosmetics_enabled()) {
    pack_name = video_name.empty() ? texpack::base_name(t, *t.data) : video_name;
    if (texpack::has(pack_name)) {
      replacement = texpack::load(pack_name, replacement_bytes_ < replacement_budget_
                                                 ? replacement_budget_ - replacement_bytes_ : 0);
      texpack::note_lookup(replacement != nullptr);
    }
  }

  // Decode every level into one scratch block so the texture can be created with its full
  // contents (paletted formats decode through the snapshot's TLUT copy, as on D3D12).
  uint32_t levels = std::max(1u, t.mip_levels);
  std::vector<uint8_t> image;
  std::array<D3D11_SUBRESOURCE_DATA, 16> initial{};
  std::array<size_t, 16> offsets{};
  std::array<uint32_t, 16> widths{}, heights{};
  const uint8_t* level_src = t.data->image.data();
  if (replacement) levels = std::max(1u, replacement->levels);
  uint32_t lw = replacement ? replacement->width : t.width;
  uint32_t lh = replacement ? replacement->height : t.height;
  uint32_t actual = 0;
  for (uint32_t l = 0; l < levels && l < 16 && lw && lh; ++l) {
    const uint8_t* rgba;
    if (replacement) {
      // A replacement arrives already RGBA8, so it skips the GX decoder.
      rgba = replacement->pixels.data() + replacement->level_offset[l];
      lw = replacement->level_width[l]; lh = replacement->level_height[l];
    } else {
      decode_texture(level_src, lw, lh, t.format, t.data->palette.data(), t.tlut_format, decode_scratch_);
      rgba = decode_scratch_.data();
      if (texpack::dumping()) texpack::dump_level(pack_name, l, rgba, lw, lh);
    }
    offsets[l] = image.size();
    widths[l] = lw; heights[l] = lh;
    image.insert(image.end(), rgba, rgba + (size_t)lw * lh * 4);
    if (!replacement) level_src += texture_level_bytes(lw, lh, t.format);
    lw = std::max(1u, lw / 2); lh = std::max(1u, lh / 2);
    ++actual;
  }
  if (replacement) replacement_bytes_ += image.size();
  if (!actual) return nullptr;
  for (uint32_t l = 0; l < actual; ++l) {
    initial[l].pSysMem = image.data() + offsets[l];
    initial[l].SysMemPitch = widths[l] * 4;
    initial[l].SysMemSlicePitch = widths[l] * heights[l] * 4;
  }
  TextureEntry e;
  e.width = widths[0]; e.height = heights[0]; e.levels = actual; e.last_used = frame_counter_;
  if (replacement) e.replacement_bytes = image.size();   // so an eviction gives the budget back
  D3D11_TEXTURE2D_DESC td{};
  td.Width = widths[0]; td.Height = heights[0]; td.MipLevels = actual; td.ArraySize = 1;
  td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  if (FAILED(device_->CreateTexture2D(&td, initial.data(), &e.resource))) { host::log("d3d11: texture creation failed (%ux%u fmt %u)", t.width, t.height, t.format); return nullptr; }
  if (FAILED(device_->CreateShaderResourceView(e.resource.Get(), nullptr, &e.srv))) return nullptr;
  auto inserted = textures_.emplace(key, std::move(e));
  return &inserted.first->second;
}

ID3D11SamplerState* D3D11Backend::get_sampler(uint32_t mode0, uint32_t mode1) {
  SamplerKey key{mode0, mode1, (uint32_t)std::clamp(opts_.anisotropy, 1, 16)};
  auto it = samplers_.find(key);
  if (it != samplers_.end()) return it->second.Get();
  static const D3D11_TEXTURE_ADDRESS_MODE wrap[] = {D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_MIRROR, D3D11_TEXTURE_ADDRESS_WRAP};
  D3D11_SAMPLER_DESC sd{};
  sd.AddressU = wrap[bits(mode0, 0, 2)]; sd.AddressV = wrap[bits(mode0, 2, 2)]; sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  bool mag_linear = bits(mode0, 4, 1) != 0;
  uint32_t minf = bits(mode0, 5, 3);
  bool min_linear = (minf & 4) != 0;
  uint32_t mip = minf & 3;   // 0 none, 1 point, 2 linear
  D3D11_FILTER_TYPE mn = min_linear ? D3D11_FILTER_TYPE_LINEAR : D3D11_FILTER_TYPE_POINT;
  D3D11_FILTER_TYPE mg = mag_linear ? D3D11_FILTER_TYPE_LINEAR : D3D11_FILTER_TYPE_POINT;
  D3D11_FILTER_TYPE mp = mip == 2 ? D3D11_FILTER_TYPE_LINEAR : D3D11_FILTER_TYPE_POINT;
  sd.Filter = D3D11_ENCODE_BASIC_FILTER(mn, mg, mp, D3D11_FILTER_REDUCTION_TYPE_STANDARD);
  if (key.anisotropy > 1 && min_linear && mag_linear) sd.Filter = D3D11_FILTER_ANISOTROPIC;
  sd.MipLODBias = (float)(int32_t)sbits(mode0, 9, 8) / 32.0f;
  sd.MinLOD = bits(mode1, 0, 8) / 16.0f;
  sd.MaxLOD = mip ? bits(mode1, 8, 8) / 16.0f : 0.0f;
  sd.MaxAnisotropy = key.anisotropy;
  sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
  ComPtr<ID3D11SamplerState> state;
  if (FAILED(device_->CreateSamplerState(&sd, &state))) return blit_sampler_.Get();
  ID3D11SamplerState* raw = state.Get();
  samplers_[key] = std::move(state);
  return raw;
}

void D3D11Backend::bind_textures(const DrawCall& dc) {
  ID3D11ShaderResourceView* srvs[8] = {};
  ID3D11SamplerState* samps[8] = {};
  bool srv_changed = false, samp_changed = false;
  for (int i = 0; i < 8; ++i) {
    if (dc.textures[i].used) {
      TextureEntry* e = get_texture(dc.textures[i]);
      srvs[i] = e ? e->srv.Get() : nullptr;
      samps[i] = get_sampler(dc.textures[i].mode0, dc.textures[i].mode1);
    }
    if (srvs[i] != bound_.srv[i]) srv_changed = true;
    if (samps[i] != bound_.samp[i]) samp_changed = true;
  }
  if (srv_changed) { context_->PSSetShaderResources(0, 8, srvs); std::memcpy(bound_.srv, srvs, sizeof srvs); }
  if (samp_changed) { context_->PSSetSamplers(0, 8, samps); std::memcpy(bound_.samp, samps, sizeof samps); }
}

// ---------------------------------------------------------------- draws
void D3D11Backend::execute_draw(const DrawCall& dc, const DrawPlan& plan) {
  Stopwatch sw;
  ++g_prof_draws;
  Pipeline* pipeline = get_pipeline(dc, plan.topo_type, plan.topology);
  g_prof[3] += sw.lap();
  if (!pipeline) return;
  bind_textures(dc);
  g_prof[4] += sw.lap();

  // Viewport / scissor (identical arithmetic to the D3D12 backend).
  const float* vp = (const float*)&dc.xf_regs[0x1A];
  float s = (float)scale_;
  float X = (vp[3] - vp[0] - 342.0f) * s, Y = (vp[4] + vp[1] - 342.0f) * s, W = 2.0f * vp[0] * s, H = -2.0f * vp[1] * s;
  if (W < 0) { X += W; W = -W; }
  if (H < 0) { Y += H; H = -H; }
  float min_depth = 1.0f - vp[5] / 16777216.0f, max_depth = 1.0f - (vp[5] - vp[2]) / 16777216.0f;
  min_depth = std::clamp(min_depth, 0.0f, 1.0f); max_depth = std::clamp(max_depth, 0.0f, 1.0f);
  if (max_depth < min_depth) std::swap(min_depth, max_depth);
  uint32_t tl = dc.bp.reg[BP_SCISSORTL], br = dc.bp.reg[BP_SCISSORBR], so = dc.bp.reg[BP_SCISSOROFFSET];
  int xoff = (int)bits(so, 0, 10) * 2 - 342, yoff = (int)bits(so, 10, 10) * 2 - 342;
  int sl = (int)bits(tl, 12, 12) - xoff - 342, st = (int)bits(tl, 0, 12) - yoff - 342;
  int sr = (int)bits(br, 12, 12) - xoff - 341, sb = (int)bits(br, 0, 12) - yoff - 341;
  sl = std::clamp(sl, 0, EFB_WIDTH); sr = std::clamp(sr, 0, EFB_WIDTH); st = std::clamp(st, 0, EFB_HEIGHT); sb = std::clamp(sb, 0, EFB_HEIGHT);
  if (sr <= sl || sb <= st) return;

  const float viewport[6] = {X, Y, std::max(W, 1.0f), std::max(H, 1.0f), min_depth, max_depth};
  if (std::memcmp(viewport, bound_.viewport, sizeof viewport) != 0) {
    D3D11_VIEWPORT v{viewport[0], viewport[1], viewport[2], viewport[3], viewport[4], viewport[5]};
    context_->RSSetViewports(1, &v);
    std::memcpy(bound_.viewport, viewport, sizeof viewport);
  }
  const LONG scissor[4] = {(LONG)(sl * scale_), (LONG)(st * scale_), (LONG)(sr * scale_), (LONG)(sb * scale_)};
  if (std::memcmp(scissor, bound_.scissor, sizeof scissor) != 0) {
    D3D11_RECT r{scissor[0], scissor[1], scissor[2], scissor[3]};
    context_->RSSetScissorRects(1, &r);
    std::memcpy(bound_.scissor, scissor, sizeof scissor);
  }

  if (pipeline->vs.Get() != bound_.vs) { context_->VSSetShader(pipeline->vs.Get(), nullptr, 0); bound_.vs = pipeline->vs.Get(); }
  if (pipeline->ps.Get() != bound_.ps) { context_->PSSetShader(pipeline->ps.Get(), nullptr, 0); bound_.ps = pipeline->ps.Get(); }
  if (pipeline->blend != bound_.blend) { context_->OMSetBlendState(pipeline->blend, nullptr, 0xFFFFFFFFu); bound_.blend = pipeline->blend; }
  if (pipeline->depth != bound_.depth) { context_->OMSetDepthStencilState(pipeline->depth, 0); bound_.depth = pipeline->depth; }
  if (pipeline->raster != bound_.raster) { context_->RSSetState(pipeline->raster); bound_.raster = pipeline->raster; }
  if (pipeline->topology != bound_.topology) { context_->IASetPrimitiveTopology(pipeline->topology); bound_.topology = pipeline->topology; }

  ID3D11Buffer* cb; UINT first, count;
  vs_constants_.bind(plan.vs_slot, &cb, &first, &count);
  if (vs_constants_.windowed()) context1_->VSSetConstantBuffers1(0, 1, &cb, &first, &count);
  else context_->VSSetConstantBuffers(0, 1, &cb);
  ps_constants_.bind(plan.ps_slot, &cb, &first, &count);
  if (ps_constants_.windowed()) context1_->PSSetConstantBuffers1(1, 1, &cb, &first, &count);
  else context_->PSSetConstantBuffers(1, 1, &cb);

  context_->DrawIndexed(plan.index_count, plan.first_index, (INT)plan.base_vertex);
  g_prof[5] += sw.lap();
}

void D3D11Backend::clear_efb(const EfbCopy& c) {
  // Presenting an XFB binds the backbuffer without a depth target. The copy's
  // clear still applies to the EFB, including the depth used by portrait copies.
  bind_efb_targets();
  float s = (float)scale_;
  D3D11_RECT r{(LONG)(c.src_x * s), (LONG)(c.src_y * s), (LONG)((c.src_x + c.src_w) * s), (LONG)((c.src_y + c.src_h) * s)};
  r.right = std::min<LONG>(r.right, efb_w_); r.bottom = std::min<LONG>(r.bottom, efb_h_);
  if (r.right <= r.left || r.bottom <= r.top) return;
  struct { float color[4]; float depth[4]; } cb{};
  cb.color[0] = ((c.clear_color >> 16) & 0xFF) / 255.0f;
  cb.color[1] = ((c.clear_color >> 8) & 0xFF) / 255.0f;
  cb.color[2] = (c.clear_color & 0xFF) / 255.0f;
  cb.color[3] = ((c.clear_color >> 24) & 0xFF) / 255.0f;
  cb.depth[0] = 1.0f - (float)c.clear_z / 16777215.0f;
  D3D11_MAPPED_SUBRESOURCE m{};
  if (FAILED(context_->Map(clear_cb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
  std::memcpy(m.pData, &cb, sizeof cb);
  context_->Unmap(clear_cb_.Get(), 0);
  ID3D11Buffer* buffer = clear_cb_.Get();
  D3D11_VIEWPORT v{0, 0, (float)efb_w_, (float)efb_h_, 0.0f, 1.0f};
  context_->RSSetViewports(1, &v);
  context_->RSSetScissorRects(1, &r);
  context_->IASetInputLayout(nullptr);
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_->VSSetShader(clear_vs_.Get(), nullptr, 0);
  context_->PSSetShader(clear_ps_.Get(), nullptr, 0);
  context_->VSSetConstantBuffers(0, 1, &buffer);
  context_->PSSetConstantBuffers(0, 1, &buffer);
  context_->OMSetBlendState(opaque_blend_.Get(), nullptr, 0xFFFFFFFFu);
  context_->OMSetDepthStencilState(clear_depth_.Get(), 0);
  context_->RSSetState(blit_raster_.Get());
  context_->Draw(3, 0);
  reset_bound();
  context_->IASetInputLayout(layout_.Get());
}

// EFB -> texture at a guest address. Kept at the internal resolution, like Dolphin's scaled EFB copies.
void D3D11Backend::execute_copy(const EfbCopy& c) {
  unbind_shader_resources();   // the destination may still be bound as a texture from an earlier draw
  uint32_t w = c.src_w, h = c.src_h;
  if (c.half_scale) { w = std::max(1u, w / 2); h = std::max(1u, h / 2); }
  uint32_t sw = w * scale_, sh = h * scale_;
  if (copy_dests_.insert(c.dest_addr).second)
    host::log("efb copy: new destination %08X, %ux%u from (%u,%u) %ux%u, half %d, y_scale %.3f, format %u",
              c.dest_addr, sw, sh, c.src_x, c.src_y, c.src_w, c.src_h, (int)c.half_scale, c.y_scale, c.format);
  TextureEntry& e = efb_copies_[c.dest_addr];
  if (!e.resource || e.width != sw || e.height != sh) {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = sw; td.Height = sh; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    e.srv.Reset(); e.rtv.Reset(); e.resource.Reset();
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &e.resource))) { host::log("d3d11: EFB copy texture %ux%u failed", sw, sh); return; }
    check(device_->CreateShaderResourceView(e.resource.Get(), nullptr, &e.srv), "efb copy srv");
    check(device_->CreateRenderTargetView(e.resource.Get(), nullptr, &e.rtv), "efb copy rtv");
    e.width = sw; e.height = sh;
  }
  e.last_used = frame_counter_;
  const bool opaque = !c.efb_alpha && !c.is_depth;
  if (c.half_scale || opaque || c.is_depth) {
    unbind_shader_resources();
    ID3D11RenderTargetView* rtv = e.rtv.Get();
    context_->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp{0, 0, (float)sw, (float)sh, 0, 1};
    D3D11_RECT sc{0, 0, (LONG)sw, (LONG)sh};
    context_->RSSetViewports(1, &vp);
    context_->RSSetScissorRects(1, &sc);
    const float rect[16] = {(float)c.src_w / EFB_WIDTH, (float)c.src_h / EFB_HEIGHT, (float)c.src_x / EFB_WIDTH, (float)c.src_y / EFB_HEIGHT,
                            0, 0, 0, 0, 1.0f, 1.0f, 0, 0, 1.0f, 1.0f, 1.0f, c.is_depth ? -2.0f : (opaque ? -1.0f : 0.0f)};
    blit(c.is_depth ? efb_depth_srv_.Get() : efb_srv_.Get(), rect);
    bind_efb_targets();
  } else {
    D3D11_BOX box{c.src_x * (UINT)scale_, c.src_y * (UINT)scale_, 0, (c.src_x + c.src_w) * (UINT)scale_, (c.src_y + c.src_h) * (UINT)scale_, 1};
    box.right = std::min<UINT>(box.right, (UINT)efb_w_); box.bottom = std::min<UINT>(box.bottom, (UINT)efb_h_);
    if (box.right > box.left && box.bottom > box.top)
      context_->CopySubresourceRegion(e.resource.Get(), 0, 0, 0, 0, efb_color_.Get(), 0, &box);
  }
}

// One full-screen triangle through the shared blit/sharpen shader. The caller has already set
// the render target, viewport and scissor.
void D3D11Backend::blit(ID3D11ShaderResourceView* src, const float rect[16], ID3D11ShaderResourceView* depth, const float* crt) {
  D3D11_MAPPED_SUBRESOURCE m{};
  if (FAILED(context_->Map(blit_cb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
  std::memcpy(m.pData, rect, 64);
  if (crt) std::memcpy(static_cast<uint8_t*>(m.pData) + 64, crt, 32); else std::memset(static_cast<uint8_t*>(m.pData) + 64, 0, 32);
  context_->Unmap(blit_cb_.Get(), 0);
  ID3D11Buffer* buffer = blit_cb_.Get();
  ID3D11SamplerState* sampler = blit_sampler_.Get();
  context_->IASetInputLayout(nullptr);
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_->VSSetShader(blit_vs_.Get(), nullptr, 0);
  context_->PSSetShader(crt ? crt_ps_.Get() : blit_ps_.Get(), nullptr, 0);
  context_->VSSetConstantBuffers(0, 1, &buffer);
  context_->PSSetConstantBuffers(0, 1, &buffer);
  context_->PSSetShaderResources(0, 1, &src);
  context_->PSSetShaderResources(3, 1, &depth);
  context_->PSSetSamplers(0, 1, &sampler);
  context_->OMSetBlendState(opaque_blend_.Get(), nullptr, 0xFFFFFFFFu);
  context_->OMSetDepthStencilState(no_depth_.Get(), 0);
  context_->RSSetState(blit_raster_.Get());
  context_->Draw(3, 0);
  reset_bound();
  context_->IASetInputLayout(layout_.Get());
}

void D3D11Backend::present_efb(const EfbCopy& c) {
  unbind_shader_resources();
  ID3D11RenderTargetView* rtv = backbuffer_rtv_.Get();
  context_->OMSetRenderTargets(1, &rtv, nullptr);
  const float border[4] = {0, 0, 0, 1};   // letterbox/pillarbox bars
  context_->ClearRenderTargetView(rtv, border);
  // Border art fills the window first; the picture is drawn over it (see the D3D12 backend).
  video_bg::set_border(opts_.border_art);
  bool art_reversed = false;
  if (const auto art = video_bg::border_frame(&art_reversed)) {
    if (TextureEntry* texture = update_video_texture(art, video_bg::kBorderSlot)) {
      const float window = (float)client_w_ / std::max((float)client_h_, 1.0f), shape = (float)art->width / std::max((float)art->height, 1.0f);
      const float sx = shape > window ? window / shape : 1.0f, sy = shape > window ? 1.0f : shape / window;
      const float art_rect[16] = {sx, art_reversed ? -sy : sy, (1.0f - sx) * 0.5f, art_reversed ? 1.0f - (1.0f - sy) * 0.5f : (1.0f - sy) * 0.5f,
                                  1.0f / art->width, 1.0f / art->height, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0};
      D3D11_VIEWPORT art_vp{0, 0, (float)client_w_, (float)client_h_, 0, 1};
      D3D11_RECT art_sc{0, 0, client_w_, client_h_};
      context_->RSSetViewports(1, &art_vp);
      context_->RSSetScissorRects(1, &art_sc);
      blit(texture->srv.Get(), art_rect);
      context_->OMSetRenderTargets(1, &rtv, nullptr);
    }
  }
  if (opts_.widescreen != widescreen_sent_) { widescreen_sent_ = opts_.widescreen; slippi::request_widescreen(opts_.widescreen); }
  if (opts_.fod_reflections != fod_reflections_sent_) { fod_reflections_sent_ = opts_.fod_reflections; slippi::request_fod_reflections(opts_.fod_reflections); }
  // The Gecko code wins if both are somehow set, so the two can never widen the same frame twice.
  set_true_widescreen(opts_.true_widescreen && !opts_.widescreen);
  float aspect = output_aspect();
  float ww = (float)client_w_, wh = (float)client_h_;
  float vw = ww, vh = ww / aspect;
  if (vh > wh) { vh = wh; vw = wh * aspect; }
  const CrtLook look = crt_look(opts_.crt_filter);
  const float lines = std::max((float)c.src_h * c.y_scale, 1.0f);
  // A player-chosen shader preset takes the place of the built-in CRT model (see the D3D12 backend):
  // the plain present goes into a console-sized texture and the preset draws that to the window.
  bool shader_on = user_shader::d3d11_prepare(device_.Get(), opts_.shader_preset);
  const uint32_t shader_w = (uint32_t)std::max(c.src_w, 1u), shader_h = (uint32_t)std::max(std::lround(lines), 1L);
  if (shader_on && (!shader_source_ || shader_source_w_ != shader_w || shader_source_h_ != shader_h)) {
    shader_source_.Reset(); shader_source_srv_.Reset(); shader_source_rtv_.Reset();
    D3D11_TEXTURE2D_DESC td{};
    td.Width = shader_w; td.Height = shader_h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &shader_source_)) ||
        FAILED(device_->CreateShaderResourceView(shader_source_.Get(), nullptr, &shader_source_srv_)) ||
        FAILED(device_->CreateRenderTargetView(shader_source_.Get(), nullptr, &shader_source_rtv_))) {
      shader_source_.Reset(); shader_on = false;
      host::log("shaders: the picture texture could not be made; the preset is off this frame");
    } else { shader_source_w_ = shader_w; shader_source_h_ = shader_h; }
  }
  const bool crt_on = opts_.crt_filter && !shader_on;
  CrtFit fit{vh, 0.0f};
  if (crt_on || shader_on) { fit = crt_fit(vh, lines); vh = fit.height; vw = vh * aspect; }
  D3D11_VIEWPORT vp{(ww - vw) * 0.5f, (wh - vh) * 0.5f, vw, vh, 0, 1};
  if (crt_on || shader_on) { vp.TopLeftX = std::floor(vp.TopLeftX); vp.TopLeftY = std::floor(vp.TopLeftY); }   // whole pixels: the raster is laid on the pixel grid
  D3D11_RECT sc{0, 0, client_w_, client_h_};
  if (shader_on) {
    ID3D11RenderTargetView* picture_rtv = shader_source_rtv_.Get();
    context_->OMSetRenderTargets(1, &picture_rtv, nullptr);
    D3D11_VIEWPORT picture{0, 0, (float)shader_w, (float)shader_h, 0, 1};
    D3D11_RECT picture_sc{0, 0, (LONG)shader_w, (LONG)shader_h};
    context_->RSSetViewports(1, &picture);
    context_->RSSetScissorRects(1, &picture_sc);
  } else {
    context_->RSSetViewports(1, &vp);
    context_->RSSetScissorRects(1, &sc);
  }
  float rect[16] = {(float)c.src_w / EFB_WIDTH, (float)c.src_h / EFB_HEIGHT, (float)c.src_x / EFB_WIDTH, (float)c.src_y / EFB_HEIGHT,
                    1.0f / std::max((float)efb_w_, 1.0f), 1.0f / std::max((float)efb_h_, 1.0f), std::clamp(opts_.sharpness, 0.0f, 1.0f), 0.0f,
                    1.0f, 1.0f, 0.0f, 0.0f, opts_.brightness, opts_.contrast, opts_.vibrance, opts_.screen_space_ao};
  // Averaging box when the rendered image is larger than the output (see the D3D12 backend).
  rect[8] = (float)std::clamp((int)std::lround((double)c.src_w * scale_ / std::max(vw, 1.0f)), 1, 4);
  rect[9] = (float)std::clamp((int)std::lround((double)c.src_h * scale_ / std::max(vh, 1.0f)), 1, 4);
  const float crt[8] = {look.scan, look.mask, look.curve, lines, (float)c.src_w, vh / lines, look.halation, fit.shift};
  if (shader_on) {   // the averaging box for the console-sized texture, and no sharpening before the preset
    rect[6] = 0.0f;
    rect[8] = (float)std::clamp((int)std::lround((double)c.src_w * scale_ / shader_w), 1, 4);
    rect[9] = (float)std::clamp((int)std::lround((double)c.src_h * scale_ / shader_h), 1, 4);
  }
  blit(efb_srv_.Get(), rect, efb_depth_srv_.Get(), crt_on ? crt : nullptr);
  if (shader_on) {
    context_->OMSetRenderTargets(1, &rtv, nullptr);
    const user_shader::Target where{vp.TopLeftX, vp.TopLeftY, (uint32_t)std::max(vw, 1.0f), (uint32_t)std::max(vh, 1.0f)};
    user_shader::d3d11_draw(context_.Get(), shader_source_srv_.Get(), rtv, where, (size_t)frame_counter_);
    // The preset's passes leave their own state on the context.
    reset_bound();
    context_->IASetInputLayout(layout_.Get());
    context_->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT whole{0, 0, (float)client_w_, (float)client_h_, 0, 1};
    context_->RSSetViewports(1, &whole);
    context_->RSSetScissorRects(1, &sc);
  }
#ifdef GX_PC_SETTINGS
  if (settings_ui_) { settings_ui_->draw(); reset_bound(); context_->IASetInputLayout(layout_.Get()); }
#endif
  bind_efb_targets();   // later commands in this frame render to the EFB again
}

// ---------------------------------------------------------------- captures
void D3D11Backend::capture_backbuffer() {
  ComPtr<ID3D11Texture2D> back;
  if (FAILED(swapchain_->GetBuffer(0, IID_PPV_ARGS(&back)))) return;
  D3D11_TEXTURE2D_DESC desc{}; back->GetDesc(&desc);
  if (!capture_staging_) {
    D3D11_TEXTURE2D_DESC sd = desc;
    sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.MiscFlags = 0;
    if (FAILED(device_->CreateTexture2D(&sd, nullptr, &capture_staging_))) return;
  }
  context_->CopyResource(capture_staging_.Get(), back.Get());
}

// Downsamples the presented image to a 16x16 signature and compares the frame before last against
// its two neighbours. A frame in normal motion sits between them: the distance from each neighbour
// is about half the distance between the neighbours themselves. A frame that is wrong sits outside
// both, so min(to previous, to next) exceeds the distance between previous and next. That ratio is
// the test, and it does not care whether the frame is too bright, too dark or in the wrong place.
void D3D11Backend::flicker_scan() {
  if (!scan_rt_) {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = kScanGrid; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &scan_rt_))) return;
    if (FAILED(device_->CreateRenderTargetView(scan_rt_.Get(), nullptr, &scan_rtv_))) { scan_rt_.Reset(); return; }
    D3D11_TEXTURE2D_DESC sd = td;
    sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    for (int i = 0; i < kScanRing; ++i)
      if (FAILED(device_->CreateTexture2D(&sd, nullptr, &scan_staging_[i]))) { scan_rt_.Reset(); return; }
  }
  // Shrink the EFB through the blit shader rather than the swapchain: the back buffer has already
  // been letterboxed, and the bars would dominate a 16x16 average.
  unbind_shader_resources();
  ID3D11RenderTargetView* rtv = scan_rtv_.Get();
  context_->OMSetRenderTargets(1, &rtv, nullptr);
  D3D11_VIEWPORT vp{0, 0, (float)kScanGrid, (float)kScanGrid, 0, 1};
  D3D11_RECT sc{0, 0, kScanGrid, kScanGrid};
  context_->RSSetViewports(1, &vp);
  context_->RSSetScissorRects(1, &sc);
  const float rect[16] = {1, 1, 0, 0, 0, 0, 0, 0, 1.0f, 1.0f, 0, 0, 1.0f, 1.0f, 1.0f, 0};
  blit(efb_srv_.Get(), rect);
  context_->CopyResource(scan_staging_[scan_ring_].Get(), scan_rt_.Get());
  scan_ring_ = (scan_ring_ + 1) % kScanRing;
  if (scan_ready_ < kScanRing) { ++scan_ready_; bind_efb_targets(); return; }

  // Map the oldest copy in the ring, which the GPU finished two frames ago.
  D3D11_MAPPED_SUBRESOURCE m{};
  if (SUCCEEDED(context_->Map(scan_staging_[scan_ring_].Get(), 0, D3D11_MAP_READ, 0, &m))) {
    static float grid[FlickerScanner::kGrid * FlickerScanner::kGrid];
    const uint8_t* src = (const uint8_t*)m.pData;
    for (int y = 0; y < kScanGrid; ++y)
      for (int x = 0; x < kScanGrid; ++x) {
        const uint8_t* p = src + (size_t)y * m.RowPitch + (size_t)x * 4;
        grid[y * kScanGrid + x] = p[0] * 0.25f + p[1] * 0.6f + p[2] * 0.15f;
      }
    context_->Unmap(scan_staging_[scan_ring_].Get(), 0);
    scanner_.push(grid, frames_presented_ - kScanRing);   // this copy was made kScanRing frames ago
  }
  bind_efb_targets();
}

void D3D11Backend::write_capture(const std::string& path, uint64_t sequence) {
  if (!capture_staging_) return;
  D3D11_TEXTURE2D_DESC desc{}; capture_staging_->GetDesc(&desc);
  D3D11_MAPPED_SUBRESOURCE m{};
  if (FAILED(context_->Map(capture_staging_.Get(), 0, D3D11_MAP_READ, 0, &m))) return;
  const uint8_t* data = (const uint8_t*)m.pData;
  if (opts_.capture_burst) {
    PendingCapture pc; pc.path = path; pc.w = desc.Width; pc.h = desc.Height; pc.pitch = m.RowPitch; pc.sequence = sequence;
    pc.data.assign(data, data + (size_t)m.RowPitch * desc.Height);
    pending_captures_.push_back(std::move(pc));
    context_->Unmap(capture_staging_.Get(), 0);
    if (pending_captures_.size() >= opts_.capture_burst) flush_captures();
    return;
  }
  std::ofstream f(path, std::ios::binary);
  f << "P6\n" << desc.Width << ' ' << desc.Height << "\n255\n";
  for (UINT y = 0; y < desc.Height; ++y) for (UINT x = 0; x < desc.Width; ++x) f.write((const char*)data + (size_t)y * m.RowPitch + x * 4, 3);
  context_->Unmap(capture_staging_.Get(), 0);
  host::log("captured %s (sim frame %llu)", path.c_str(), (unsigned long long)sequence);
}

void D3D11Backend::flush_captures() {
  for (const PendingCapture& pc : pending_captures_) {
    std::ofstream f(pc.path, std::ios::binary);
    f << "P6\n" << pc.w << ' ' << pc.h << "\n255\n";
    for (UINT y = 0; y < pc.h; ++y) for (UINT x = 0; x < pc.w; ++x) f.write((const char*)pc.data.data() + (size_t)y * pc.pitch + x * 4, 3);
    host::log("captured %s (sim frame %llu)", pc.path.c_str(), (unsigned long long)pc.sequence);
  }
  pending_captures_.clear();
}

// Once a second at most: keeps the game texture cache inside texture_budget_bytes_ by dropping the
// textures drawn longest ago first, never one this frame has used, down to a tenth under the budget
// so it does not run every second. D3D11 keeps a released texture alive for as long as recorded
// work still names it, so nothing has to wait for the GPU here; a texture drawn again later is
// simply decoded and uploaded again. The resize out-of-memory retry stays as it was.
void D3D11Backend::trim_textures() {
  const double now = Stopwatch::now();
  if (now - texture_trim_time_ < 1.0) return;
  texture_trim_time_ = now;
  // EFB copies are kept by the guest address they were copied to and nothing took one out again,
  // so every address any scene had copied to held a texture of up to the whole internal frame for
  // the rest of the session, outside the budget below. Once more than a few exist, a copy nothing
  // has written or sampled for a minute goes; a draw that samples the address later falls back to
  // the game's own texture data until the game copies there again (as the D3D12 backend does).
  constexpr uint64_t kCopyIdleFrames = 3600;
  constexpr size_t kCopiesKept = 16;
  if (efb_copies_.size() > kCopiesKept) {
    uint32_t copies = 0;
    for (auto it = efb_copies_.begin(); it != efb_copies_.end();) {
      if (it->second.last_used + kCopyIdleFrames >= frame_counter_) { ++it; continue; }
      it = efb_copies_.erase(it); ++copies;
    }
    if (copies) {
      reset_bound();   // the redundant-state filter remembers views by pointer
      host::log("d3d11: %u idle EFB copy textures freed, %zu kept", copies, efb_copies_.size());
    }
  }
  uint64_t cached = 0;
  for (const auto& entry : textures_) cached += texture_bytes(entry.second.width, entry.second.height, entry.second.levels);
  if (cached <= texture_budget_bytes_) return;
  std::vector<std::pair<uint64_t, uint64_t>> order;   // (last_used, key)
  order.reserve(textures_.size());
  for (const auto& entry : textures_)
    if (entry.second.last_used != frame_counter_) order.emplace_back(entry.second.last_used, entry.first);
  std::sort(order.begin(), order.end());
  const uint64_t target = texture_budget_bytes_ - texture_budget_bytes_ / 10;
  uint64_t freed = 0;
  uint32_t dropped = 0;
  for (const auto& candidate : order) {
    if (cached - freed <= target) break;
    auto it = textures_.find(candidate.second);
    if (it == textures_.end()) continue;
    freed += texture_bytes(it->second.width, it->second.height, it->second.levels);
    replacement_bytes_ -= std::min(replacement_bytes_, it->second.replacement_bytes);
    textures_.erase(it);
    ++dropped;
  }
  if (!dropped) return;
  reset_bound();   // the redundant-state filter remembers views by pointer, and a new texture may reuse one
  host::log("d3d11: texture cache %.0f of %.0f MB budget; %u textures freed (%.0f MB), %zu kept",
            cached / 1048576.0, texture_budget_bytes_ / 1048576.0, dropped, freed / 1048576.0, textures_.size());
}

// Reads back the timing of the frame that used this ring slot kTimerRing frames ago, if the GPU
// has finished it (a result that is not ready is skipped, never waited for), then starts timing
// this frame.
void D3D11Backend::gpu_frame_begin() {
  GpuTimer& t = gpu_timers_[gpu_timer_slot_];
  if (!t.disjoint) return;
  if (t.timing_pending) {
    t.timing_pending = false;
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
    UINT64 start = 0, end = 0;
    if (context_->GetData(t.disjoint.Get(), &dj, sizeof dj, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK && !dj.Disjoint && dj.Frequency &&
        context_->GetData(t.start.Get(), &start, sizeof start, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
        context_->GetData(t.end.Get(), &end, sizeof end, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK && end > start) {
      gpu_frame_ms_sum_ += (double)(end - start) * 1000.0 / (double)dj.Frequency;
      ++gpu_frame_ms_samples_;
    }
  }
  context_->Begin(t.disjoint.Get());
  context_->End(t.start.Get());
}

// Closes this frame's timing and marks its end with an event, then moves to the next ring slot.
void D3D11Backend::gpu_frame_end() {
  GpuTimer& t = gpu_timers_[gpu_timer_slot_];
  if (!t.disjoint) return;
  context_->End(t.end.Get());
  context_->End(t.disjoint.Get());
  context_->End(t.done.Get());
  t.timing_pending = true; t.done_pending = true;
  gpu_timer_slot_ = (gpu_timer_slot_ + 1) % kTimerRing;
}

// Submitted frames the GPU has not finished: the end-of-frame events still outstanding. D3D11 has
// no fence value to compare, and SetMaximumFrameLatency(1) in init is what keeps this small.
unsigned D3D11Backend::gpu_frames_queued() {
  unsigned queued = 0;
  for (GpuTimer& t : gpu_timers_) {
    if (!t.done_pending || !t.done) continue;
    BOOL finished = FALSE;
    const HRESULT hr = context_->GetData(t.done.Get(), &finished, sizeof finished, D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if (hr == S_FALSE) ++queued;
    else t.done_pending = false;
  }
  return queued;
}

// ---------------------------------------------------------------- frame
void D3D11Backend::submit_frame(const Frame& frame, const DrawMatrices* overrides) {
  video_bg::set_enabled(opts_.video_backgrounds);
  video_bg::begin_frame(frame.scene_major, frame.scene_minor);
  widenable_scene_ = frame_has_widenable_scene(frame);
  integrate_compiled_pipelines();
  if (opts_.anisotropy != anisotropy_applied_) { anisotropy_applied_ = opts_.anisotropy; samplers_.clear(); reset_bound(); }
  if (opts_.ssaa != ssaa_applied_ || pick_scale() != scale_) {
    ssaa_applied_ = opts_.ssaa;
    if (pick_scale() != scale_) { host::log("d3d11: dropping %zu EFB copy textures, internal scale %d -> %d", efb_copies_.size(), scale_, pick_scale()); efb_copies_.clear(); create_efb(); host::log("d3d11: internal resolution now EFB x%d", scale_); }
  }
  struct FloatEnvironment {
    unsigned saved = _mm_getcsr();
    FloatEnvironment() { _mm_setcsr(0x1f80); }
    ~FloatEnvironment() { _mm_setcsr(saved); }
  } float_environment;
#ifdef GX_PC_SETTINGS
  if (settings_ui_) { settings_set_game_aspect(output_aspect()); settings_set_hud_snapshot(frame); }
  if (settings_ui_ && settings_ui_->begin(opts_)) {
    apply_fullscreen_mode();
    if (pick_scale() != scale_) { host::log("d3d11: dropping %zu EFB copy textures, internal scale %d -> %d", efb_copies_.size(), scale_, pick_scale()); efb_copies_.clear(); create_efb(); }
  }
  // After the panel, where the Lab view decided whether it covers this frame (see gx_d3d12.cpp).
  lab_skip_scene_ = settings_ui_ && opts_.lab_skip_scene && frame_in_match(frame) && lab::covering();
#endif
  // Switching packs on or off in the panel invalidates every cached texture, because each was
  // built with or without its replacement. D3D11 holds its own references until the cache is
  // cleared, so there is no explicit GPU wait to do here.
  if (texpack::configure(opts_.custom_textures, opts_.dump_textures)) {
    textures_.clear();
    replacement_bytes_ = 0;
    texpack_report_frame_ = frame_counter_ + 600;
  }
  // A skin picked on the character select screen brought other portraits or stock icons: the same.
  if (texpack::take_cosmetics_changed()) {
    textures_.clear();
    replacement_bytes_ = 0;
  }
  if (texpack::enabled() && frame_counter_ >= texpack_report_frame_) {
    texpack::report();
    texpack_report_frame_ = frame_counter_ + 3600;
  }
  if (host::window_take_fullscreen_toggle()) {   // Alt+Enter
    if (opts_.exclusive_fullscreen) opts_.exclusive_fullscreen = false;
    else opts_.fullscreen = !host::window_is_fullscreen();
    apply_fullscreen_mode();
    if (pick_scale() != scale_) { host::log("d3d11: dropping %zu EFB copy textures, internal scale %d -> %d", efb_copies_.size(), scale_, pick_scale()); efb_copies_.clear(); create_efb(); }
  }
  shader_wait_budget_us_ = 12000;
  capturing_efb_ = std::any_of(frame.copies.begin(),frame.copies.end(),[](const EfbCopy& c) { return !c.to_xfb; });
  ++frame_counter_;
  trim_textures();
  // Queue depth before this frame's work is issued, then this frame's timing starts (and the
  // frame from kTimerRing ago is read back). The line a "gets laggier the longer I play" report
  // needs: if the delay grows over a session, one of these grows with it.
  const unsigned frames_queued = gpu_frames_queued();
  gpu_frame_begin();
  if (frame_counter_ % 300 == 0) {
    host::log("gpu: %.2f ms/frame, %u frames queued, solver pairs %zu, textures %zu",
              gpu_frame_ms_samples_ ? gpu_frame_ms_sum_ / gpu_frame_ms_samples_ : 0.0, frames_queued,
              solver_pair_counter().load(std::memory_order_relaxed), textures_.size());
    gpu_frame_ms_sum_ = 0; gpu_frame_ms_samples_ = 0;
  }

  // ---- plan the frame before touching the GPU: indices and constants are built in one sweep
  // over the draw list (each DrawCall is ~5 KB, so a second sweep would miss cache on all of it),
  // then uploaded as whole mapped blocks instead of D3D12's per-draw ring allocations.
  Stopwatch sw;
  plans_.assign(frame.draws.size(), DrawPlan{});
  index_scratch_.clear();
  const EffectsFilter effects = effects_filter(frame, opts_.effects_level);
  uint32_t extra_vertices = 0, vs_slots = 0, ps_slots = 0;
  for (const FrameCommand& cmd : frame.commands) {
    if (cmd.kind != FrameCommand::Draw || cmd.index >= frame.draws.size()) continue;
    const DrawCall& dc = frame.draws[cmd.index];
    DrawPlan& plan = plans_[cmd.index];
    if (lab_skip_scene_) continue;   // the Lab view covers this frame; plan.valid stays false
    if (skip_for_effects(effects, dc)) continue;   // "Visual effects"; plan.valid stays false
    const uint32_t n = dc.vertex_count;
    const uint32_t first = (uint32_t)index_scratch_.size();
    auto& idx = index_scratch_;
    auto append_indices = [&](uint32_t primitive, uint32_t count, uint32_t base) {
      if (append_segment_indices(idx, primitive, count, base) == DrawTopology::Lines) {
        plan.topo_type = TOPO_LINE;
        plan.topology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
      }
    };
    if (dc.segment_count && dc.first_segment <= frame.segments.size() &&
        dc.segment_count <= frame.segments.size() - dc.first_segment) {
      for (uint32_t s = 0; s < dc.segment_count; ++s) {
        const DrawSegment& segment = frame.segments[dc.first_segment + s];
        append_indices(segment.primitive, segment.vertex_count, segment.first_vertex - dc.first_vertex);
      }
    } else {
      append_indices(dc.primitive, dc.vertex_count, 0);
    }
    if (index_scratch_.size() == first) continue;
    plan.first_index = first;
    plan.index_count = (uint32_t)(index_scratch_.size() - first);
    const DrawMatrices* over = overrides ? overrides + cmd.index : nullptr;
    if (over && over->vertices) { plan.base_vertex = (uint32_t)frame.vertices.size() + extra_vertices; extra_vertices += n; }
    else plan.base_vertex = dc.first_vertex;
    plan.vs_slot = vs_slots++;
    plan.ps_slot = ps_slots++;
    plan.valid = true;
    if (uint8_t* dst = vs_constants_.slot(plan.vs_slot)) {
      VSConstants vs_constants;
      fill_vs_constants(dc, vs_constants, scale_, over);
      std::memcpy(dst, &vs_constants, VS_CONSTANT_BYTES);
    }
    if (uint8_t* dst = ps_constants_.slot(plan.ps_slot)) {
      PSConstants ps_constants;
      fill_ps_constants(dc, ps_constants, scale_);
      std::memcpy(dst, &ps_constants, sizeof(PSConstants));
    }
  }
  vs_constants_.end();
  ps_constants_.end();
  g_prof[0] += sw.lap();   // index generation, planning and constants

  const size_t vertex_bytes = (frame.vertices.size() + extra_vertices) * sizeof(Vertex);
  if (uint8_t* dst = vertex_buffer_.begin(vertex_bytes)) {
    if (!frame.vertices.empty()) std::memcpy(dst, frame.vertices.data(), frame.vertices.size() * sizeof(Vertex));
    size_t at = frame.vertices.size();
    for (const FrameCommand& cmd : frame.commands) {
      if (cmd.kind != FrameCommand::Draw || cmd.index >= frame.draws.size() || !plans_[cmd.index].valid) continue;
      const DrawMatrices* over = overrides ? overrides + cmd.index : nullptr;
      if (!over || !over->vertices) continue;
      std::memcpy(dst + at * sizeof(Vertex), over->vertices, (size_t)frame.draws[cmd.index].vertex_count * sizeof(Vertex));
      at += frame.draws[cmd.index].vertex_count;
    }
    vertex_buffer_.end();
  } else if (vertex_bytes) {
    host::log("d3d11: vertex buffer allocation failed (%zu bytes)", vertex_bytes);
    return;
  }
  if (uint8_t* dst = index_buffer_.begin(index_scratch_.size() * 4)) {
    std::memcpy(dst, index_scratch_.data(), index_scratch_.size() * 4);
    index_buffer_.end();
  } else if (!index_scratch_.empty()) {
    host::log("d3d11: index buffer allocation failed");
    return;
  }
  g_prof[1] += sw.lap();   // vertex/index upload

  // ---- record the frame
  reset_bound();
  bind_efb_targets();
  ID3D11Buffer* vb = vertex_buffer_.get();
  UINT stride = sizeof(Vertex), offset = 0;
  context_->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
  context_->IASetIndexBuffer(index_buffer_.get(), DXGI_FORMAT_R32_UINT, 0);
  context_->IASetInputLayout(layout_.Get());

  const EfbCopy* screen = nullptr;
  for (const EfbCopy& copy : frame.copies) if (copy.to_xfb) screen = &copy;
  int fullscreen_slot = -1;
  std::shared_ptr<const video_bg::Frame> fullscreen = video_bg::fullscreen_frame(&fullscreen_slot);
  const bool css_fullscreen = fullscreen && fullscreen_slot == 0 && screen;
  unsigned leading_untextured_draws = 0;
  bool background_drawn = false;
  bool presented = false;
  // A backlog frame (skip_present_) needs only the draws that feed its texture copies; see the same
  // rule in gx_d3d12.cpp. Not when the screen copy keeps the EFB for the next frame.
  const size_t needed_commands = [&] {
    static const bool full_backlog = std::getenv("MELEE_BACKLOG_FULL") != nullptr;   // comparison switch: the pre-0.8.5 full draw
    if (!skip_present_ || !screen || !screen->clear || full_backlog) return frame.commands.size();
    size_t end = 0;
    for (size_t i = 0; i < frame.commands.size(); ++i)
      if (frame.commands[i].kind != FrameCommand::Draw && !frame.copies[frame.commands[i].index].to_xfb) end = i + 1;
    return end;
  }();
  for (size_t command_index = 0; command_index < frame.commands.size(); ++command_index) {
    const FrameCommand& cmd = frame.commands[command_index];
    if (cmd.kind == FrameCommand::Draw) {
      if (command_index < needed_commands && cmd.index < frame.draws.size()) {
        const DrawCall& dc = frame.draws[cmd.index];
        if (css_fullscreen && !background_drawn) {
          bool textured = false;
          for (const TextureRef& texture : dc.textures) textured |= texture.used;
          if (!textured) {
            ++leading_untextured_draws;
          } else if (video_bg::learning::should_insert_fullscreen_layer(
                         fullscreen_slot, leading_untextured_draws, textured)) {
            draw_video_background(fullscreen, fullscreen_slot, *screen);
            background_drawn = true;
            if (!video_layer_logged_[fullscreen_slot]) {
              host::log("video backgrounds: CSS full-screen layer inserted after %u hardcoded background draws (D3D11)",
                        leading_untextured_draws);
              video_layer_logged_[fullscreen_slot] = true;
            }
          }
        }
        draw_video_slot_ = -1;
        if (plans_[cmd.index].valid) execute_draw(dc, plans_[cmd.index]);
        if (fullscreen && screen && fullscreen_slot == 1 && draw_video_slot_ == 1 && !background_drawn) {
          draw_video_background(fullscreen, fullscreen_slot, *screen);
          background_drawn = true;
          if (!video_layer_logged_[fullscreen_slot]) {
            host::log("video backgrounds: SSS full-screen layer composited after learned backdrop draw (D3D11)");
            video_layer_logged_[fullscreen_slot] = true;
          }
        }
      }
    } else {
      const EfbCopy& c = frame.copies[cmd.index];
      // The EFB holds the finished image at the copy to the display buffer, which is the moment the
      // frame is what the player will see and before anything clears it for the next one.
      if (c.to_xfb) { if (!skip_present_) { if (opts_.flicker_scan) flicker_scan(); present_efb(c); presented = true; } }
      else execute_copy(c);
      if (c.clear) clear_efb(c);
    }
  }

  present_wait_ = 0;
  if (presented) {
    bool capture = false;
    std::string path;
    if (!opts_.capture_path.empty()) {
      const uint32_t next_presented = frames_presented_ + 1;
      if (opts_.capture_sim_frame && !opts_.capture_frame && frame.sequence >= opts_.capture_sim_frame) opts_.capture_frame = next_presented;
      bool burst = opts_.capture_burst && opts_.capture_frame && next_presented >= opts_.capture_frame && next_presented < opts_.capture_frame + opts_.capture_burst;
      if (next_presented == opts_.capture_frame && !burst) { capture = true; path = opts_.capture_path; }
      else if (burst || (opts_.capture_every && next_presented % opts_.capture_every == 0)) {
        // A pinned-phase run names each picture by its simulation frame, so two runs line up by
        // name even if one of them skipped a present while draining a backlog.
        char suffix[48];
        if (opts_.pin_phase >= 0) snprintf(suffix, sizeof suffix, "_s%06llu.ppm", (unsigned long long)frame.sequence);
        else snprintf(suffix, sizeof suffix, "_%05u.ppm", next_presented);
        const std::string& base = opts_.capture_path;
        path = base.substr(0, base.size() > 4 && base.compare(base.size() - 4, 4, ".ppm") == 0 ? base.size() - 4 : base.size()) + suffix;
        capture = true;
      }
    }
    // F2: write the next N presented frames out, whatever the capture options say. This backend
    // needs its own copy of the hook: the first version only had it in D3D12, so pressing F2 on a
    // D3D11 machine logged the request and produced nothing.
    if (unsigned want = gx_capture_request()) {
      gx_capture_request_set(want - 1);
      CreateDirectoryA("capture", nullptr);
      char req[64];
      snprintf(req, sizeof req, "capture\\blink_%05u.ppm", frames_presented_ + 1);
      const std::string saved = opts_.capture_path;
      opts_.capture_path = req;
      capture_backbuffer();
      write_capture(req, frame.sequence);
      opts_.capture_path = saved;
      if (want == 1) host::log("capture: wrote the requested frames into capture\\");
      capture = false;   // already written for this frame
    }
    // The flip model discards the back buffer on Present, so a capture is copied out first.
    if (capture) capture_backbuffer();
    const double wait_start = Stopwatch::now();
    for (;;) {
      double remaining = present_deadline_ - Stopwatch::now();
      if (remaining <= 0) break;
      if (present_timer_ && remaining > 0.0004) {
        LARGE_INTEGER due; due.QuadPart = -(LONGLONG)((remaining - 0.0002) * 1e7);
        if (SetWaitableTimer(present_timer_, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(present_timer_, INFINITE);
        else SwitchToThread();
      } else YieldProcessor();
    }
    present_wait_ = Stopwatch::now() - wait_start;
    swapchain_->Present(opts_.vsync ? 1 : 0, (!opts_.vsync && allow_tearing_ && !opts_.exclusive_fullscreen) ? DXGI_PRESENT_ALLOW_TEARING : 0);
    ++frames_presented_;
    if (capture) { capture_sequence_ = frame.sequence; write_capture(path, frame.sequence); }
  }
  gpu_frame_end();
}

// ---------------------------------------------------------------- pipeline prewarm
static bool read_recipe_file(const std::string& path, std::vector<PipelineRecipe>& out) {
  std::ifstream file(path, std::ios::binary);
  uint64_t header[3]{};
  if (!file.read((char*)header, sizeof(header)) || header[0] != 0x3150535247505847ull || header[1] > 16384) return false;
  std::vector<PipelineRecipe> recipes((size_t)header[1]);
  if (!file.read((char*)recipes.data(), recipes.size() * sizeof(PipelineRecipe)) ||
      hash_bytes(recipes.data(), recipes.size() * sizeof(PipelineRecipe)) != header[2]) return false;
  out.insert(out.end(), recipes.begin(), recipes.end());
  return true;
}

void D3D11Backend::prewarm_pipelines() {
  std::vector<PipelineRecipe> recipes;
  read_recipe_file(shader_cache_root_ + "/recipes.bin", recipes);
  std::error_code ec;
  for (auto& entry : std::filesystem::directory_iterator(shader_cache_root_, ec))
    if (entry.is_directory(ec)) read_recipe_file(entry.path().string() + "/recipes.bin", recipes);
  if (recipes.empty()) return;
  std::unordered_set<std::string> seen;
  std::unordered_set<PipeKey, PipeKeyHash> keys_seen;
  Stopwatch timer;
  size_t queued = 0;
  for (const auto& recipe : recipes) {
    if ((recipe.topology != TOPO_TRIANGLE && recipe.topology != TOPO_LINE) ||
        (recipe.xf[0x3F] & 15) > 8 || (recipe.xf[9] & 3) > 2) continue;
    if (!seen.insert(std::string((const char*)&recipe, sizeof recipe)).second) continue;
    if (pipeline_recipes_.size() >= 16384) break;
    DrawCall draw{}; draw.components = recipe.components; draw.bp = recipe.bp;
    std::memcpy(draw.xf_regs, recipe.xf, sizeof(recipe.xf));
    VSUid vsu = make_vs_uid(draw); PSUid psu = make_ps_uid(draw);
    PipeKey key{vsu.hash(), psu.hash(), draw.bp.blendmode() & 0xFFFF, draw.bp.zmode() & 0x1F, draw.bp.cullmode(), recipe.topology, draw.bp.zcontrol() & 7};
    if (pipelines_.count(key)) { if (keys_seen.insert(key).second) pipeline_recipes_.push_back(recipe); continue; }
    if (!pipelines_pending_.insert(key).second) continue;
    keys_seen.insert(key);
    { std::lock_guard<std::mutex> lk(shader_mutex_); shader_jobs_.push_back(ShaderJob{key, vsu, psu, recipe}); }
    ++queued;
  }
  shader_cv_.notify_all();
  const size_t total = pipelines_.size() + queued;
  // A cache that is already warm finishes in a moment; the progress panel only appears once the
  // wait is long enough to notice, so a normal launch never flashes it.
  const double panel_after = host::now_seconds() + 0.4;
  for (;;) {
    integrate_compiled_pipelines();
    size_t pending = pipelines_pending_.size();
    wchar_t title[192]; swprintf_s(title, L"%ls  |  compiling shaders %zu / %zu", host::window_title_base().c_str(), total - pending, total);
    host::window_set_title(title);
    if (pending && host::now_seconds() >= panel_after)
      host::loading_show(L"Compiling shaders (first launch of this version)", total - pending, total);
    if (!pending) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  host::loading_close();
  host::window_set_title(host::window_title_base().c_str());
  host::log("d3d11: prewarmed %zu pipelines from %zu recipes before guest startup in %.1f ms", pipelines_.size(), pipeline_recipes_.size(), timer.lap() * 1000.0);
}

void D3D11Backend::save_pipeline_recipes() {
  if (pipeline_recipes_.empty()) return;
  std::unordered_set<std::string> seen;
  std::vector<PipelineRecipe> unique;
  for (const auto& r : pipeline_recipes_) if (seen.insert(std::string((const char*)&r, sizeof r)).second) unique.push_back(r);
  uint64_t header[3] = {0x3150535247505847ull, unique.size(), hash_bytes(unique.data(), unique.size() * sizeof(PipelineRecipe))};
  std::vector<uint8_t> data(sizeof(header) + unique.size() * sizeof(PipelineRecipe));
  std::memcpy(data.data(), header, sizeof(header));
  std::memcpy(data.data() + sizeof(header), unique.data(), data.size() - sizeof(header));
  write_cache_file(shader_cache_root_ + "/recipes.bin", data.data(), data.size());
}

}  // namespace

std::string d3d11_profile_line() {
  char buf[512];
  double n = (double)std::max<uint64_t>(1, g_prof_draws);
  double l = (double)std::max<uint64_t>(1, g_pipe_lookups);
  snprintf(buf, sizeof buf, "%llu draws: plan+constants %.2f, upload %.2f, pipeline %.2f, bind %.2f, draw %.2f us/draw | pipeline cache hits %llu, lookups %llu (uid %.2f + map %.2f us), created %llu, draws on the fallback pipeline while compiling %llu | EFB-copy textures: found %llu, MISSED %llu",
           (unsigned long long)g_prof_draws, 1e6 * g_prof[0] / n, 1e6 * g_prof[1] / n, 1e6 * g_prof[3] / n, 1e6 * g_prof[4] / n, 1e6 * g_prof[5] / n,
           (unsigned long long)g_pipe_hits, (unsigned long long)g_pipe_lookups, 1e6 * g_prof[6] / l, 1e6 * g_prof[7] / l, (unsigned long long)g_pipe_creates, (unsigned long long)g_pipe_skips,
           (unsigned long long)g_efb_tex_hit, (unsigned long long)g_efb_tex_miss);
  std::memset(g_prof, 0, sizeof g_prof); g_prof_draws = g_pipe_hits = g_pipe_lookups = g_pipe_creates = g_pipe_skips = 0;
  g_efb_tex_hit = g_efb_tex_miss = 0;
  return buf;
}

Backend* create_d3d11_backend(void* hwnd, int w, int h, const RenderOptions& options) {
  try {
    return new D3D11Backend((HWND)hwnd, w, h, options);
  } catch (const std::exception& e) {
    host::log("d3d11: backend unavailable (%s)", e.what());
    return nullptr;
  }
}
const RenderOptions& d3d11_options(Backend* backend) { return static_cast<D3D11Backend*>(backend)->options(); }
void d3d11_resize(Backend* backend, int w, int h) { static_cast<D3D11Backend*>(backend)->resize(w, h); }
void d3d11_stats(Backend* backend, uint32_t* frames, uint32_t* pipelines, uint32_t* textures) {
  auto* b = static_cast<D3D11Backend*>(backend);
  if (frames) *frames = b->frames_presented();
  if (pipelines) *pipelines = b->pipeline_count();
  if (textures) *textures = b->texture_count();
}

}  // namespace gx
