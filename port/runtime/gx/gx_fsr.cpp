// AMD FSR upscaling through AMD's signed DLLs (see gx_fsr.h).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gx_fsr.h"
#include "host.h"

#include <windows.h>
#include <d3d12.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <string>
#include "../../third_party/fsr/api/include/ffx_api.h"
#include "../../third_party/fsr/api/include/dx12/ffx_api_dx12.h"
#include "../../third_party/fsr/upscalers/include/ffx_upscale.h"
#include "../../third_party/fsr/framegeneration/include/ffx_framegeneration.h"

namespace gx {
namespace fsr {
namespace {

HMODULE g_module = nullptr;
PfnFfxCreateContext p_create = nullptr;
PfnFfxDestroyContext p_destroy = nullptr;
PfnFfxConfigure p_configure = nullptr;
PfnFfxQuery p_query = nullptr;
PfnFfxDispatch p_dispatch = nullptr;
ID3D12Device* g_device = nullptr;
ffxContext g_ctx = nullptr;
bool g_ok = false;
int g_mode = 0;
uint32_t g_out_w = 0, g_out_h = 0;
std::string g_provider;
// Frame generation: its own context, the picture size it was made for, and the frame number AMD's
// library wants raised by exactly one per frame.
ffxContext g_fg_ctx = nullptr;
bool g_fg_ok = false;
uint32_t g_fg_w = 0, g_fg_h = 0;
uint64_t g_fg_frame = 0;
std::string g_fg_provider;

uint32_t quality_of(int mode) {
  switch (mode) {
    case 11: return FFX_UPSCALE_QUALITY_MODE_NATIVEAA;
    case 12: return FFX_UPSCALE_QUALITY_MODE_QUALITY;
    case 13: return FFX_UPSCALE_QUALITY_MODE_BALANCED;
    case 14: return FFX_UPSCALE_QUALITY_MODE_PERFORMANCE;
    default: return FFX_UPSCALE_QUALITY_MODE_ULTRA_PERFORMANCE;
  }
}
// AMD's per-axis ratios for the modes (ffx_upscale.h), the same as its own ratio query returns.
float ratio_of(int mode) {
  switch (mode) {
    case 11: return 1.0f; case 12: return 1.5f; case 13: return 1.7f; case 14: return 2.0f; default: return 3.0f;
  }
}

void destroy_context() {
  if (g_ctx && p_destroy) p_destroy(&g_ctx, nullptr);
  g_ctx = nullptr;
}

// A context for this output size. The upscale context sizes its own resources from the largest render
// and output sizes it will see, so a new output size makes a new one.
bool create_context(uint32_t out_w, uint32_t out_h) {
  destroy_context();
  ffxCreateBackendDX12Desc backend{};
  backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
  backend.device = g_device;
  ffxCreateContextDescUpscale desc{};
  desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
  desc.header.pNext = &backend.header;
  // Our depth is reversed (near = 1, as XeSS is told) and the colour is display-ready 8-bit, gamma encoded.
  desc.flags = FFX_UPSCALE_ENABLE_DEPTH_INVERTED | FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
  desc.maxRenderSize = {out_w, out_h};
  desc.maxUpscaleSize = {out_w, out_h};
  const ffxReturnCode_t r = p_create(&g_ctx, &desc.header, nullptr);
  if (r != FFX_API_RETURN_OK) { host::log("fsr: context for %ux%u refused (%u)", out_w, out_h, (unsigned)r); g_ctx = nullptr; return false; }
  ffxQueryGetProviderVersion version{};
  version.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
  if (p_query(&g_ctx, &version.header) == FFX_API_RETURN_OK && version.versionName) g_provider = version.versionName;
  return true;
}

// Test switches for the conventions, measured on captures before the defaults were chosen.
float env_float(const char* name, float fallback) {
  const char* v = std::getenv(name);
  return v && *v ? (float)std::atof(v) : fallback;
}

}  // namespace

const char* mode_name(int mode) {
  switch (mode) {
    case 11: return "FSR Native AA"; case 12: return "FSR Quality"; case 13: return "FSR Balanced";
    case 14: return "FSR Performance"; case 15: return "FSR Ultra Performance"; default: return "FSR";
  }
}

bool init(const std::wstring& exe_dir, ID3D12Device* device) {
  const std::wstring loader = exe_dir + L"\\amd_fidelityfx_loader_dx12.dll", upscaler = exe_dir + L"\\amd_fidelityfx_upscaler_dx12.dll";
  if (GetFileAttributesW(loader.c_str()) == INVALID_FILE_ATTRIBUTES || GetFileAttributesW(upscaler.c_str()) == INVALID_FILE_ATTRIBUTES) {
    host::log("fsr: AMD FSR is not installed (PC settings > Video has the download); FSR unavailable");
    return false;
  }
  g_module = LoadLibraryW(loader.c_str());
  if (!g_module) { host::log("fsr: cannot load amd_fidelityfx_loader_dx12.dll (%lu)", GetLastError()); return false; }
  p_create = (PfnFfxCreateContext)(void*)GetProcAddress(g_module, "ffxCreateContext");
  p_destroy = (PfnFfxDestroyContext)(void*)GetProcAddress(g_module, "ffxDestroyContext");
  p_configure = (PfnFfxConfigure)(void*)GetProcAddress(g_module, "ffxConfigure");
  p_query = (PfnFfxQuery)(void*)GetProcAddress(g_module, "ffxQuery");
  p_dispatch = (PfnFfxDispatch)(void*)GetProcAddress(g_module, "ffxDispatch");
  if (!p_create || !p_destroy || !p_query || !p_dispatch) { host::log("fsr: the FSR loader is missing entry points"); return false; }
  g_device = device;
  // A small context proves the upscaler runs on this GPU and names the one AMD's library chose.
  if (!create_context(1280, 720)) { host::log("fsr: not supported on this GPU"); return false; }
  destroy_context();
  g_ok = true;
  host::log("fsr: available (%s)", g_provider.empty() ? "provider not reported" : g_provider.c_str());
  // Frame generation, the same way, when its DLL is there too.
  const std::wstring framegen = exe_dir + L"\\amd_fidelityfx_framegeneration_dx12.dll";
  if (p_configure && GetFileAttributesW(framegen.c_str()) != INVALID_FILE_ATTRIBUTES) {
    g_fg_ok = true;   // fg_create checks it
    if (fg_create(1280, 720, 1280, 720)) host::log("fsr: frame generation available (%s)", g_fg_provider.empty() ? "provider not reported" : g_fg_provider.c_str());
    else { g_fg_ok = false; host::log("fsr: frame generation is not supported on this GPU"); }
    fg_destroy();
  }
  return true;
}

void shutdown() {
  fg_destroy();
  destroy_context();
  g_ok = false; g_fg_ok = false; g_mode = 0; g_out_w = g_out_h = 0;
}

bool available() { return g_ok; }
std::string provider() { return g_provider; }

bool optimal_size(int mode, uint32_t out_w, uint32_t out_h, uint32_t* rw, uint32_t* rh,
                  uint32_t* min_w, uint32_t* min_h, uint32_t* max_w, uint32_t* max_h) {
  if (!g_ok || !is_fsr_mode(mode)) return false;
  const float k = ratio_of(mode);
  *rw = (uint32_t)std::lround(out_w / k); *rh = (uint32_t)std::lround(out_h / k);
  // Any render size up to the output works (the context is made for the output size).
  *min_w = 0; *min_h = 0; *max_w = out_w; *max_h = out_h;
  return true;
}

bool set_options(int mode, uint32_t out_w, uint32_t out_h) {
  if (!g_ok) return false;
  if (g_ctx && g_out_w == out_w && g_out_h == out_h) { g_mode = mode; return true; }   // the quality mode only moves the render size
  if (!create_context(out_w, out_h)) return false;
  g_mode = mode; g_out_w = out_w; g_out_h = out_h;
  host::log("fsr: %s (%s) -> output %ux%u", mode_name(mode), g_provider.empty() ? "AMD FSR" : g_provider.c_str(), out_w, out_h);
  return true;
}

bool evaluate(ID3D12GraphicsCommandList* list, const Inputs& in) {
  if (!g_ok || !g_ctx) return false;
  if (in.in_left || in.in_top) {
    static bool logged = false;
    if (!logged) { logged = true; host::log("fsr: the picture starts at %u,%u in the EFB; FSR reads from 0,0, presenting natively", in.in_left, in.in_top); }
    return false;
  }
  static const float mv_scale = env_float("MELEE_FSR_MV_SCALE", 1.0f);
  ffxDispatchDescUpscale d{};
  d.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
  d.commandList = list;
  d.color = ffxApiGetResourceDX12(in.color, FFX_API_RESOURCE_STATE_COMPUTE_READ);
  d.depth = ffxApiGetResourceDX12(in.depth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
  d.motionVectors = ffxApiGetResourceDX12(in.mvec, FFX_API_RESOURCE_STATE_COMPUTE_READ);
  d.output = ffxApiGetResourceDX12(in.out, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
  d.jitterOffset = {in.jitter_x, in.jitter_y};
  d.motionVectorScale = {mv_scale, mv_scale};   // the motion vectors are in render pixels already
  d.renderSize = {in.in_w, in.in_h};
  d.upscaleSize = {g_out_w, g_out_h};
  d.enableSharpening = false;   // the present pass has its own sharpening
  d.frameTimeDelta = in.frame_ms > 0.0f && in.frame_ms < 100.0f ? in.frame_ms : 1000.0f / 60.0f;
  d.preExposure = 1.0f;
  d.reset = in.reset;
  // Melee's match cameras: a near plane about 1 unit away, scenery out to some thousands, a narrow
  // vertical field of view. FSR uses these only to weigh depth between frames.
  d.cameraNear = 1.0f;
  d.cameraFar = 20000.0f;
  d.cameraFovAngleVertical = 0.5f;
  d.viewSpaceToMetersFactor = 1.0f;
  d.flags = FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;
  const ffxReturnCode_t r = p_dispatch(&g_ctx, &d.header);
  if (r != FFX_API_RETURN_OK) {
    static int logged = 0;
    if (logged++ < 5) host::log("fsr: dispatch failed (%u)", (unsigned)r);
    return false;
  }
  return true;
}

bool fg_available() { return g_ok && g_fg_ok; }
std::string fg_provider() { return g_fg_provider; }
bool fg_ready() { return g_fg_ctx != nullptr; }

void fg_destroy() {
  if (g_fg_ctx && p_destroy) p_destroy(&g_fg_ctx, nullptr);
  g_fg_ctx = nullptr; g_fg_w = g_fg_h = 0;
}

bool fg_create(uint32_t w, uint32_t h, uint32_t max_render_w, uint32_t max_render_h) {
  if (!g_fg_ok || !g_device) return false;
  if (g_fg_ctx && g_fg_w == w && g_fg_h == h) return true;
  fg_destroy();
  ffxCreateBackendDX12Desc backend{};
  backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
  backend.device = g_device;
  // The version link enables the current interface (the PrepareV2 and generation dispatches below).
  ffxCreateContextDescFrameGenerationVersion version{};
  version.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION;
  version.version = FFX_FRAMEGENERATION_VERSION;
  version.header.pNext = &backend.header;
  ffxCreateContextDescFrameGeneration desc{};
  desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
  desc.header.pNext = &version.header;
  // Reversed depth and render-resolution motion vectors, as the upscaler is given them.
  desc.flags = FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED;
  desc.displaySize = {w, h};
  desc.maxRenderSize = {std::max(max_render_w, 1u), std::max(max_render_h, 1u)};
  desc.backBufferFormat = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
  const ffxReturnCode_t r = p_create(&g_fg_ctx, &desc.header, nullptr);
  if (r != FFX_API_RETURN_OK) { host::log("fsr: frame generation context for %ux%u refused (%u)", w, h, (unsigned)r); g_fg_ctx = nullptr; return false; }
  ffxQueryGetProviderVersion pv{};
  pv.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
  if (p_query(&g_fg_ctx, &pv.header) == FFX_API_RETURN_OK && pv.versionName) g_fg_provider = pv.versionName;
  g_fg_w = w; g_fg_h = h;
  return true;
}

bool fg_generate(ID3D12GraphicsCommandList* list, const FrameInputs& in) {
  if (!g_fg_ctx || !p_configure) return false;
  static const float mv_scale = env_float("MELEE_FSR_MV_SCALE", 1.0f);
  const uint64_t id = ++g_fg_frame;
  const FfxApiRect2D whole{0, 0, (int32_t)g_fg_w, (int32_t)g_fg_h};
  auto failed = [](const char* what, ffxReturnCode_t r) {
    static int logged = 0;
    if (logged++ < 5) host::log("fsr: frame generation %s failed (%u)", what, (unsigned)r);
    return false;
  };
  // No swap chain of AMD's: only generate, leave presenting to the renderer.
  ffxConfigureDescFrameGeneration config{};
  config.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
  config.swapChain = nullptr;
  config.frameGenerationEnabled = true;
  config.allowAsyncWorkloads = false;
  config.flags = FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;
  config.onlyPresentGenerated = false;
  config.generationRect = whole;
  config.frameID = id;
  if (const ffxReturnCode_t r = p_configure(&g_fg_ctx, &config.header); r != FFX_API_RETURN_OK) return failed("configure", r);
  // This frame's depth and motion: what the next generated frame is made from.
  ffxDispatchDescFrameGenerationPrepareV2 prepare{};
  prepare.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;
  prepare.frameID = id;
  prepare.flags = 0;
  prepare.commandList = list;
  prepare.renderSize = {in.render_w, in.render_h};
  prepare.jitterOffset = {in.jitter_x, in.jitter_y};
  prepare.motionVectorScale = {mv_scale, mv_scale};   // render pixels, as for the upscaler
  prepare.frameTimeDelta = in.frame_ms > 0.0f && in.frame_ms < 100.0f ? in.frame_ms : 1000.0f / 60.0f;
  prepare.reset = in.reset;
  // The camera as the upscaler is told it (see evaluate). Melee's camera position and axes are not
  // tracked here; a camera at the origin looking down -z is the neutral answer.
  prepare.cameraNear = 1.0f;
  prepare.cameraFar = 20000.0f;
  prepare.cameraFovAngleVertical = 0.5f;
  prepare.viewSpaceToMetersFactor = 1.0f;
  prepare.depth = ffxApiGetResourceDX12(in.depth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
  prepare.motionVectors = ffxApiGetResourceDX12(in.mvec, FFX_API_RESOURCE_STATE_COMPUTE_READ);
  prepare.cameraUp[1] = 1.0f; prepare.cameraRight[0] = 1.0f; prepare.cameraForward[2] = -1.0f;
  if (const ffxReturnCode_t r = p_dispatch(&g_fg_ctx, &prepare.header); r != FFX_API_RETURN_OK) return failed("prepare", r);
  // The frame between the previous picture and this one.
  ffxDispatchDescFrameGeneration generate{};
  generate.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;
  generate.commandList = list;
  generate.presentColor = ffxApiGetResourceDX12(in.color, FFX_API_RESOURCE_STATE_COMPUTE_READ);
  generate.outputs[0] = ffxApiGetResourceDX12(in.out, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
  generate.numGeneratedFrames = 1;
  generate.reset = in.reset;
  generate.backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
  generate.minMaxLuminance[0] = 0.0f; generate.minMaxLuminance[1] = 1.0f;
  generate.generationRect = whole;
  generate.frameID = id;
  if (const ffxReturnCode_t r = p_dispatch(&g_fg_ctx, &generate.header); r != FFX_API_RETURN_OK) return failed("dispatch", r);
  return !in.reset;
}

}  // namespace fsr
}  // namespace gx
