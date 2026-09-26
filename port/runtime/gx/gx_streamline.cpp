// Streamline / DLSS integration (see gx_streamline.h).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gx_streamline.h"
#include "host.h"
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <atomic>
#include <string>

#ifdef GX_STREAMLINE
#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include <sl_dlss_d.h>
#include <sl_dlss_g.h>
#include <sl_reflex.h>
#include <sl_pcl.h>
#include <sl_security.h>
#endif

namespace gx {

const char* dlss_mode_name(DlssMode m) {
  switch (m) {
    case DlssMode::Off: return "Native";
    case DlssMode::DLAA: return "DLAA";
    case DlssMode::Quality: return "DLSS Quality";
    case DlssMode::Balanced: return "DLSS Balanced";
    case DlssMode::Performance: return "DLSS Performance";
    case DlssMode::UltraPerformance: return "DLSS Ultra Performance";
  }
  return "?";
}

namespace streamline {

void jitter(uint32_t index, float* jx, float* jy) {
  auto halton = [](uint32_t i, uint32_t base) { float f = 1.0f, r = 0.0f; while (i > 0) { f /= base; r += f * (i % base); i /= base; } return r; };
  uint32_t i = (index % 32) + 1;
  *jx = halton(i, 2) - 0.5f;
  *jy = halton(i, 3) - 0.5f;
}

#ifndef GX_STREAMLINE
bool init(const std::wstring&) { host::log("dlss: built without the Streamline SDK"); return false; }
void shutdown() {}
void shutdown_for_process_exit() {}
bool available() { return false; }
bool ray_reconstruction_available() { return false; }
long create_dxgi_factory2(uint32_t flags, const void* riid, void** out) { return CreateDXGIFactory2(flags, *(const IID*)riid, out); }
long d3d12_create_device(void* adapter, int fl, const void* riid, void** out) { return D3D12CreateDevice((IUnknown*)adapter, (D3D_FEATURE_LEVEL)fl, *(const IID*)riid, out); }
void set_device(ID3D12Device*) {}
void* native_interface(void* proxy) { return proxy; }
bool dlss_supported(IDXGIAdapter*) { return false; }
bool dlss_optimal_size(DlssMode, uint32_t, uint32_t, uint32_t*, uint32_t*, uint32_t*, uint32_t*, uint32_t*, uint32_t*) { return false; }
bool dlss_set_options(DlssMode, uint32_t, uint32_t, bool) { return false; }
void new_frame(uint32_t) {}
bool set_constants(const FrameConstants&) { return false; }
bool evaluate(ID3D12GraphicsCommandList*, const EvaluateInputs&) { return false; }
bool evaluate_ray_reconstruction(ID3D12GraphicsCommandList*, const RayReconstructionInputs&) { return false; }
bool frame_generation_available() { return false; }
void frame_generation_after_present() {}
bool reflex_available() { return false; }
uint32_t frame_generation_max_multiplier() { return 1; }
bool frame_generation_capabilities_queried() { return false; }
bool frame_generation_dynamic_supported() { return false; }
void set_frame_generation(int, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
void set_reflex(int) {}
float reflex_latency_ms() { return 0.0f; }
ReflexBreakdown reflex_breakdown() { return {}; }
void update_reflex_stats() {}
void pcl_marker(int) {}
void log_frame_generation() {}
#else

namespace {
HMODULE g_module = nullptr;
bool g_ready = false, g_dlss_ok = false, g_fg_ok = false, g_reflex_ok = false,
     g_rr_ok = false, g_fg_on = false;
// Whether the Reflex plugin has been handed options at all (any mode, including Off) and can be
// asked for a report. Separate from whether the low-latency algorithm is actually throttling
// anything (ReflexOptions::mode): the PC Latency markers and the telemetry they produce run
// regardless of that, so the latency reading works at Native too.
bool g_reflex_ready = false;
bool g_rr_evaluation_logged = false;
sl::FrameToken* g_token = nullptr;
sl::ViewportHandle g_viewport{0u};
typedef HRESULT(WINAPI* PFunCreateDXGIFactory2)(UINT, REFIID, void**);
typedef HRESULT(WINAPI* PFunD3D12CreateDevice)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
PFunCreateDXGIFactory2 g_create_factory2 = nullptr;
PFunD3D12CreateDevice g_create_device = nullptr;
sl::DLSSMode g_mode = sl::DLSSMode::eOff;
uint32_t g_out_w = 0, g_out_h = 0;
float g_prev_proj[16] = {};
bool g_have_prev = false;

void log_callback(sl::LogType type, const char* msg) {
  if (type == sl::LogType::eInfo) return;
  static std::atomic<uint64_t> count{0};
  uint64_t n = count.fetch_add(1);
  if (n >= 20 && n % 1000 != 0) return;   // a failing feature logs every frame; keep the log usable
  std::string s(msg);
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
  host::log("streamline: %s", s.c_str());
}

sl::DLSSMode to_sl(DlssMode m) {
  switch (m) {
    case DlssMode::DLAA: return sl::DLSSMode::eDLAA;
    case DlssMode::Quality: return sl::DLSSMode::eMaxQuality;
    case DlssMode::Balanced: return sl::DLSSMode::eBalanced;
    case DlssMode::Performance: return sl::DLSSMode::eMaxPerformance;
    case DlssMode::UltraPerformance: return sl::DLSSMode::eUltraPerformance;
    default: return sl::DLSSMode::eOff;
  }
}

// Row-major 4x4 inverse (general).
bool invert4x4(const float m[16], float out[16]) {
  float inv[16];
  inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
  inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
  inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
  inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
  inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
  inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
  inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
  inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
  inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
  inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
  inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
  inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
  inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
  inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
  inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
  inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
  float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
  if (std::fabs(det) < 1e-20f) return false;
  det = 1.0f / det;
  for (int i = 0; i < 16; ++i) out[i] = inv[i] * det;
  return true;
}
sl::float4x4 to_sl_matrix(const float m[16]) {
  sl::float4x4 r;
  for (int i = 0; i < 4; ++i) r.setRow(i, sl::float4(m[4 * i], m[4 * i + 1], m[4 * i + 2], m[4 * i + 3]));
  return r;
}
void mul4x4(const float a[16], const float b[16], float out[16]) {
  for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) { float s = 0; for (int k = 0; k < 4; ++k) s += a[4 * r + k] * b[4 * k + c]; out[4 * r + c] = s; }
}
}  // namespace

bool init(const std::wstring& exe_dir) {
  std::wstring path = exe_dir + L"\\sl.interposer.dll";
  if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) { host::log("dlss: sl.interposer.dll not found next to the executable; DLSS unavailable"); return false; }
  if (!sl::security::verifyEmbeddedSignature(path.c_str())) { host::log("dlss: sl.interposer.dll signature check failed; DLSS unavailable"); return false; }
  g_module = LoadLibraryW(path.c_str());
  if (!g_module) { host::log("dlss: cannot load sl.interposer.dll (%lu)", GetLastError()); return false; }
  g_create_factory2 = (PFunCreateDXGIFactory2)GetProcAddress(g_module, "CreateDXGIFactory2");
  g_create_device = (PFunD3D12CreateDevice)GetProcAddress(g_module, "D3D12CreateDevice");
  static const wchar_t* plugin_dirs[1];
  static std::wstring dir_copy;
  dir_copy = exe_dir;
  plugin_dirs[0] = dir_copy.c_str();
  static const sl::Feature base_features[] = {
      sl::kFeatureDLSS, sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL};
  static const sl::Feature features_with_rr[] = {
      sl::kFeatureDLSS, sl::kFeatureDLSS_RR, sl::kFeatureDLSS_G,
      sl::kFeatureReflex, sl::kFeaturePCL};
  const bool have_rr_runtime =
      GetFileAttributesW((exe_dir + L"\\sl.dlss_d.dll").c_str()) != INVALID_FILE_ATTRIBUTES &&
      GetFileAttributesW((exe_dir + L"\\nvngx_dlssd.dll").c_str()) != INVALID_FILE_ATTRIBUTES;
  sl::Preferences pref{};
  pref.showConsole = false;
  pref.logLevel = sl::LogLevel::eDefault;
  pref.pathsToPlugins = plugin_dirs;
  pref.numPathsToPlugins = 1;
  static std::wstring logs; logs = exe_dir + L"\\streamline-logs"; CreateDirectoryW(logs.c_str(), nullptr);
  pref.pathToLogsAndData = logs.c_str();
  pref.logMessageCallback = log_callback;
  pref.flags = sl::PreferenceFlags::eDisableCLStateTracking | sl::PreferenceFlags::eUseFrameBasedResourceTagging;
  pref.featuresToLoad = have_rr_runtime ? features_with_rr : base_features;
  pref.numFeaturesToLoad = have_rr_runtime ?
      (uint32_t)(sizeof features_with_rr / sizeof features_with_rr[0]) :
      (uint32_t)(sizeof base_features / sizeof base_features[0]);
  pref.engine = sl::EngineType::eCustom;
  pref.engineVersion = "melee-port";
  pref.applicationId = 231313132;   // NVIDIA sample application id: valid for development builds of non-registered titles
  pref.projectId = "5d3d9a7e-1c2b-4c9e-9a0e-2f6b8c1d4e70";
  pref.renderAPI = sl::RenderAPI::eD3D12;
  sl::Result res = slInit(pref, sl::kSDKVersion);
  if (res != sl::Result::eOk) { host::log("dlss: slInit failed (%d); DLSS unavailable", (int)res); return false; }
  g_ready = true;
  host::log("dlss: Streamline initialised (SDK %llu)", (unsigned long long)sl::kSDKVersion);
  if (have_rr_runtime)
    host::log("dlss-rr: signed plugin and NGX runtime found; waiting for a DXR input path");
  else
    host::log("dlss-rr: plugin/runtime not installed; Ray Reconstruction unavailable");
  return true;
}

void shutdown() {
  if (g_ready) { slShutdown(); g_ready = false; }
}
void shutdown_for_process_exit() {
  // Repeated clean-ISO captures reached slShutdown() and then blocked inside
  // sl.dlss's NGX unload after all GPU work and caches had finished. The backend
  // is destroyed only as this process exits; leaving the signed interposer loaded
  // lets Windows reclaim it without freezing Quit/Restart. Keep shutdown() for a
  // future in-process renderer restart, where an SDK teardown is required.
  g_ready = false;
  g_dlss_ok = g_fg_ok = g_reflex_ok = g_rr_ok = g_fg_on = false;
}
bool available() { return g_ready && g_dlss_ok; }
bool ray_reconstruction_available() { return g_ready && g_rr_ok; }

long create_dxgi_factory2(uint32_t flags, const void* riid, void** out) {
  if (g_ready && g_create_factory2) return g_create_factory2(flags, *(const IID*)riid, out);
  return CreateDXGIFactory2(flags, *(const IID*)riid, out);
}
long d3d12_create_device(void* adapter, int fl, const void* riid, void** out) {
  if (g_ready && g_create_device) return g_create_device((IUnknown*)adapter, (D3D_FEATURE_LEVEL)fl, *(const IID*)riid, out);
  return D3D12CreateDevice((IUnknown*)adapter, (D3D_FEATURE_LEVEL)fl, *(const IID*)riid, out);
}
void set_device(ID3D12Device* device) {
  if (!g_ready) return;
  sl::Result res = slSetD3DDevice(device);
  if (res != sl::Result::eOk) { host::log("dlss: slSetD3DDevice failed (%d)", (int)res); g_dlss_ok = false; return; }
  sl::FeatureRequirements req{};
  g_dlss_ok = slGetFeatureRequirements(sl::kFeatureDLSS, req) == sl::Result::eOk;
  sl::FeatureRequirements fg_req{}, rx_req{};
  g_fg_ok = slGetFeatureRequirements(sl::kFeatureDLSS_G, fg_req) == sl::Result::eOk;
  g_reflex_ok = slGetFeatureRequirements(sl::kFeatureReflex, rx_req) == sl::Result::eOk;
  sl::FeatureRequirements rr_req{};
  const sl::Result rr_requirements = slGetFeatureRequirements(sl::kFeatureDLSS_RR, rr_req);
  g_rr_ok = rr_requirements == sl::Result::eOk;
  if (!g_rr_ok) host::log("dlss-rr: plugin did not initialize (%d)", (int)rr_requirements);
  host::log("dlss: %s", g_dlss_ok ? "available (off until selected under Upscaling in PC settings)" : "feature failed to initialise; native rendering only");
}
void* native_interface(void* proxy) {
  if (!g_ready || !proxy) return proxy;
  void* base = nullptr;
  if (slGetNativeInterface(proxy, &base) != sl::Result::eOk || !base) return proxy;
  // slGetNativeInterface adds a reference; the proxy already holds one for as long as we use it.
  ((IUnknown*)base)->Release();
  return base;
}
bool dlss_supported(IDXGIAdapter* adapter) {
  if (!g_ready) return false;
  DXGI_ADAPTER_DESC desc{};
  if (FAILED(adapter->GetDesc(&desc))) return false;
  sl::AdapterInfo info{};
  info.deviceLUID = (uint8_t*)&desc.AdapterLuid;
  info.deviceLUIDSizeInBytes = sizeof(LUID);
  sl::Result res = slIsFeatureSupported(sl::kFeatureDLSS, info);
  if (res != sl::Result::eOk) {
    const char* why = res == sl::Result::eErrorOSOutOfDate ? "OS out of date" : res == sl::Result::eErrorDriverOutOfDate ? "driver out of date"
                    : res == sl::Result::eErrorNoSupportedAdapterFound || res == sl::Result::eErrorAdapterNotSupported ? "adapter not supported" : "not supported";
    host::log("dlss: not available on this adapter (%s, %d)", why, (int)res);
  }
  g_dlss_ok = res == sl::Result::eOk;
  // Frame generation has its own requirements (RTX 40 or newer, Windows hardware GPU scheduling on).
  if (g_fg_ok) {
    const sl::Result fg = slIsFeatureSupported(sl::kFeatureDLSS_G, info);
    g_fg_ok = fg == sl::Result::eOk;
    host::log("dlss: frame generation %s (%d)", g_fg_ok ? "available" : "not available on this system", (int)fg);
  }
  if (g_reflex_ok) g_reflex_ok = slIsFeatureSupported(sl::kFeatureReflex, info) == sl::Result::eOk;
  if (g_rr_ok) {
    const sl::Result rr = slIsFeatureSupported(sl::kFeatureDLSS_RR, info);
    g_rr_ok = rr == sl::Result::eOk;
    host::log("dlss-rr: %s on this adapter (%d)%s",
              g_rr_ok ? "plugin ready" : "not supported", (int)rr,
              g_rr_ok ? "; traced inputs are still required" : "");
  }
  return g_dlss_ok;
}

bool dlss_optimal_size(DlssMode mode, uint32_t out_w, uint32_t out_h, uint32_t* rw, uint32_t* rh, uint32_t* min_w, uint32_t* min_h, uint32_t* max_w, uint32_t* max_h) {
  if (!available() || mode == DlssMode::Off) return false;
  sl::DLSSOptions o{};
  o.mode = to_sl(mode); o.outputWidth = out_w; o.outputHeight = out_h;
  sl::DLSSOptimalSettings s{};
  if (slDLSSGetOptimalSettings(o, s) != sl::Result::eOk) return false;
  *rw = s.optimalRenderWidth; *rh = s.optimalRenderHeight;
  *min_w = s.renderWidthMin; *min_h = s.renderHeightMin; *max_w = s.renderWidthMax; *max_h = s.renderHeightMax;
  return true;
}

bool dlss_set_options(DlssMode mode, uint32_t out_w, uint32_t out_h, bool color_is_hdr) {
  if (!available()) return false;
  sl::DLSSOptions o{};
  o.mode = to_sl(mode); o.outputWidth = out_w; o.outputHeight = out_h;
  o.colorBuffersHDR = color_is_hdr ? sl::Boolean::eTrue : sl::Boolean::eFalse;
  o.useAutoExposure = sl::Boolean::eTrue;
  // The second-generation transformer L on every mode (Streamline 2.14, DLSS 310.9). Measured on the
  // same match frames at 1080p Quality: L 443, M 380, K 220 (Laplacian variance; native 3x is 405),
  // K visibly soft on text and edges. Melee is light enough that L's extra cost does not matter.
  // MELEE_DLSS_PRESET=K|L|M forces another preset on every mode (for comparison).
  o.dlaaPreset = o.qualityPreset = o.balancedPreset = o.performancePreset = o.ultraPerformancePreset = sl::DLSSPreset::ePresetL;
  if (const char* e = std::getenv("MELEE_DLSS_PRESET")) {
    const sl::DLSSPreset p = *e == 'L' ? sl::DLSSPreset::ePresetL : *e == 'M' ? sl::DLSSPreset::ePresetM : sl::DLSSPreset::ePresetK;
    o.dlaaPreset = o.qualityPreset = o.balancedPreset = o.performancePreset = o.ultraPerformancePreset = p;
  }
  sl::Result res = slDLSSSetOptions(g_viewport, o);
  if (res != sl::Result::eOk) { host::log("dlss: slDLSSSetOptions failed (%d)", (int)res); return false; }
  g_mode = o.mode; g_out_w = out_w; g_out_h = out_h;
  if (mode == DlssMode::Off) g_have_prev = false;
  return true;
}

void new_frame(uint32_t frame_index) {
  if (!available()) return;
  g_token = nullptr;
  if (slGetNewFrameToken(g_token, &frame_index) != sl::Result::eOk) g_token = nullptr;
}

bool set_constants(const FrameConstants& c) {
  if (!available() || !g_token) return false;
  sl::Constants k{};
  float inv[16];
  bool ok = invert4x4(c.projection, inv);
  k.cameraViewToClip = to_sl_matrix(c.projection);
  k.clipToCameraView = to_sl_matrix(ok ? inv : c.projection);
  float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  // Motion vectors carry camera motion (they come from the per-draw model-view matrices), so the
  // clip-to-previous-clip transform only needs to describe the projection change, if any.
  float clip_to_prev[16] = {}, prev_to_clip[16] = {};
  if (g_have_prev && ok) { mul4x4(inv, g_prev_proj, clip_to_prev); if (!invert4x4(clip_to_prev, prev_to_clip)) std::memcpy(prev_to_clip, ident, sizeof ident); }
  else { std::memcpy(clip_to_prev, ident, sizeof ident); std::memcpy(prev_to_clip, ident, sizeof ident); }
  k.clipToPrevClip = to_sl_matrix(clip_to_prev);
  k.prevClipToClip = to_sl_matrix(prev_to_clip);
  k.clipToLensClip = to_sl_matrix(ident);
  k.jitterOffset = sl::float2(c.jitter_x, c.jitter_y);
  k.mvecScale = sl::float2(1.0f / (float)c.render_w, 1.0f / (float)c.render_h);
  k.cameraPinholeOffset = sl::float2(0, 0);
  k.cameraPos = sl::float3(0, 0, 0);
  k.cameraUp = sl::float3(0, 1, 0);
  k.cameraRight = sl::float3(1, 0, 0);
  k.cameraFwd = sl::float3(0, 0, -1);
  k.cameraNear = 1.0f; k.cameraFar = 10000.0f;
  k.cameraFOV = c.orthographic ? 0.0f : 2.0f * std::atan(1.0f / std::max(1e-6f, std::fabs(c.projection[5])));
  k.cameraAspectRatio = std::fabs(c.projection[5]) > 1e-6f ? std::fabs(c.projection[5] / std::max(1e-6f, std::fabs(c.projection[0]))) : 4.0f / 3.0f;
  k.depthInverted = sl::Boolean::eTrue;
  k.cameraMotionIncluded = sl::Boolean::eTrue;
  k.motionVectors3D = sl::Boolean::eFalse;
  k.reset = (c.reset || !g_have_prev) ? sl::Boolean::eTrue : sl::Boolean::eFalse;
  k.orthographicProjection = c.orthographic ? sl::Boolean::eTrue : sl::Boolean::eFalse;
  k.motionVectorsDilated = sl::Boolean::eFalse;
  k.motionVectorsJittered = sl::Boolean::eFalse;
  sl::Result res = slSetConstants(k, *g_token, g_viewport);
  std::memcpy(g_prev_proj, c.projection, sizeof g_prev_proj);
  g_have_prev = true;
  if (res != sl::Result::eOk) { host::log("dlss: slSetConstants failed (%d)", (int)res); return false; }
  return true;
}

bool frame_generation_available() { return g_ready && g_fg_ok; }
bool reflex_available() { return g_ready && g_reflex_ok; }
std::atomic<float> g_latency_ms{0.0f};
void set_reflex(int mode) {
  if (!reflex_available()) return;
  sl::ReflexOptions r{};
  r.mode = mode >= 2 ? sl::ReflexMode::eLowLatencyWithBoost : mode == 1 ? sl::ReflexMode::eLowLatency : sl::ReflexMode::eOff;
  r.useMarkersToOptimize = mode > 0;
  const sl::Result res = slReflexSetOptions(r);
  g_reflex_ready = res == sl::Result::eOk;   // markers/telemetry can flow now, whatever the mode is
  host::log("reflex: %s (%d)", mode >= 2 ? "on + boost" : mode == 1 ? "on" : "off", (int)res);
}
float reflex_latency_ms() { return g_latency_ms.load(std::memory_order_relaxed); }
std::atomic<float> g_sim_ms{0}, g_render_submit_ms{0}, g_driver_ms{0}, g_os_queue_ms{0}, g_gpu_render_ms{0};
ReflexBreakdown reflex_breakdown() {
  ReflexBreakdown b;
  b.sim = g_sim_ms.load(std::memory_order_relaxed); b.render_submit = g_render_submit_ms.load(std::memory_order_relaxed);
  b.driver = g_driver_ms.load(std::memory_order_relaxed); b.os_queue = g_os_queue_ms.load(std::memory_order_relaxed);
  b.gpu_render = g_gpu_render_ms.load(std::memory_order_relaxed);
  return b;
}
void update_reflex_stats() {
  if (!g_reflex_ready) return;
  sl::ReflexState st{};
  if (slReflexGetState(st) != sl::Result::eOk || !st.latencyReportAvailable) return;
  double total = 0, sim = 0, submit = 0, drv = 0, queue = 0, gpu = 0;
  int n = 0, ns = 0, nu = 0, nd = 0, nq = 0, ng = 0;
  auto span = [](uint64_t a, uint64_t b) { return b > a ? (double)(b - a) : -1.0; };
  for (const auto& r : st.frameReport) {
    if (r.simStartTime && r.gpuRenderEndTime > r.simStartTime) { total += (double)(r.gpuRenderEndTime - r.simStartTime); ++n; }
    double v;
    if ((v = span(r.simStartTime, r.simEndTime)) >= 0) { sim += v; ++ns; }
    if ((v = span(r.renderSubmitStartTime, r.renderSubmitEndTime)) >= 0) { submit += v; ++nu; }
    if ((v = span(r.driverStartTime, r.driverEndTime)) >= 0) { drv += v; ++nd; }
    if ((v = span(r.osRenderQueueStartTime, r.osRenderQueueEndTime)) >= 0) { queue += v; ++nq; }
    if ((v = span(r.gpuRenderStartTime, r.gpuRenderEndTime)) >= 0) { gpu += v; ++ng; }
  }
  // Report times are in microseconds.
  if (n) g_latency_ms = (float)(total / n / 1000.0);
  if (ns) g_sim_ms = (float)(sim / ns / 1000.0);
  if (nu) g_render_submit_ms = (float)(submit / nu / 1000.0);
  if (nd) g_driver_ms = (float)(drv / nd / 1000.0);
  if (nq) g_os_queue_ms = (float)(queue / nq / 1000.0);
  if (ng) g_gpu_render_ms = (float)(gpu / ng / 1000.0);
}
// numFramesToGenerateMax is the number of inserted frames, so 5 means a 6x multiplier. Query once
// on the presenting thread, even while generation is off, so settings can show hardware support
// before a match starts.
std::atomic<uint32_t> g_fg_max{1};
std::atomic<bool> g_fg_dynamic_ok{false};
std::atomic<int> g_fg_requested_mode{0};
bool g_fg_queried = false;
std::atomic<uint64_t> g_fg_presented_since_log{0};
std::atomic<uint64_t> g_fg_samples_since_log{0};
std::atomic<uint32_t> g_fg_status{0};
void query_frame_generation_limits() {
  if (g_fg_queried || !frame_generation_available()) return;
  sl::DLSSGState st{};
  if (slDLSSGGetState(g_viewport, st, nullptr) != sl::Result::eOk) return;
  g_fg_queried = true;
  g_fg_max = std::max<uint32_t>(1, st.numFramesToGenerateMax);
  g_fg_dynamic_ok = st.bIsDynamicMFGSupported == sl::Boolean::eTrue;
  host::log("dlss: frame generation up to %ux%s", g_fg_max.load() + 1, g_fg_dynamic_ok.load() ? ", Dynamic available" : "");
  // If a saved Dynamic setting was applied before the first state query, switch from the safe 2x
  // fallback to Dynamic now that support is known. This runs after Present on the same thread.
  if (g_fg_requested_mode.load() == 4 && g_fg_on && g_fg_dynamic_ok.load()) {
    sl::DLSSGOptions o{};
    o.mode = sl::DLSSGMode::eDynamic;
    o.numFramesToGenerate = g_fg_max.load();
    const sl::Result res = slDLSSGSetOptions(g_viewport, o);
    host::log("dlss: frame generation Dynamic refresh (%d)", (int)res);
  }
}
uint32_t frame_generation_max_multiplier() { return g_fg_max.load(); }
bool frame_generation_capabilities_queried() { return g_fg_queried; }
bool frame_generation_dynamic_supported() { return g_fg_dynamic_ok.load(); }
void frame_generation_after_present() {
  if (!frame_generation_available()) return;
  if (!g_fg_queried) query_frame_generation_limits();
  if (!g_fg_on) return;
  // DLSS-G reports a count accumulated since GetState. Query on every Present so the result can
  // be summed over a known number of rendered frames instead of mislabeling a single sample.
  sl::DLSSGState st{};
  if (slDLSSGGetState(g_viewport, st, nullptr) != sl::Result::eOk) return;
  g_fg_presented_since_log.fetch_add(st.numFramesActuallyPresented, std::memory_order_relaxed);
  g_fg_samples_since_log.fetch_add(1, std::memory_order_relaxed);
  g_fg_status.store((uint32_t)st.status, std::memory_order_relaxed);
}

// mode: 0 off, 1–3 fixed 2x–4x, 4 Dynamic, 5 fixed 5x, 6 fixed 6x.
void set_frame_generation(int mode, uint32_t render_w, uint32_t render_h, uint32_t output_w, uint32_t output_h,
                          uint32_t backbuffer_count, uint32_t backbuffer_format, uint32_t motion_format, uint32_t depth_format) {
  if (!frame_generation_available()) return;
  sl::DLSSGOptions o{};
  const bool dynamic = mode == 4 && g_fg_dynamic_ok.load();
  const uint32_t fixed_frames = mode <= 3 ? (uint32_t)mode : mode == 5 ? 4u : mode == 6 ? 5u : 1u;
  o.mode = mode == 0 ? sl::DLSSGMode::eOff : dynamic ? sl::DLSSGMode::eDynamic : sl::DLSSGMode::eOn;
  o.numFramesToGenerate = dynamic ? g_fg_max.load() : std::clamp(fixed_frames, 1u, g_fg_max.load());
  // Streamline can infer these from DXGI, but explicitly passing the swap-chain and input sizes
  // avoids its backbuffer-extent fallback on this manually managed, three-buffer swap chain.
  o.numBackBuffers = backbuffer_count;
  o.mvecDepthWidth = render_w;
  o.mvecDepthHeight = render_h;
  o.colorWidth = output_w;
  o.colorHeight = output_h;
  o.colorBufferFormat = backbuffer_format;
  o.mvecBufferFormat = motion_format;
  o.depthBufferFormat = depth_format;
  const sl::Result res = slDLSSGSetOptions(g_viewport, o);
  g_fg_on = mode != 0 && res == sl::Result::eOk;
  g_fg_requested_mode = mode;
  static const char* names[] = {"off", "2x", "3x", "4x", "dynamic", "5x", "6x"};
  host::log("dlss: frame generation %s (%d)", names[std::clamp(mode, 0, 6)], (int)res);
}
void log_frame_generation() {
  if (!g_fg_on) return;
  const uint64_t presented = g_fg_presented_since_log.exchange(0, std::memory_order_relaxed);
  const uint64_t samples = g_fg_samples_since_log.exchange(0, std::memory_order_relaxed);
  host::log("dlss: frame generation status 0x%X, %llu presented frames across %llu rendered frames (%lld generated)",
            g_fg_status.load(std::memory_order_relaxed), (unsigned long long)presented,
            (unsigned long long)samples, (long long)presented - (long long)samples);
}
void pcl_marker(int marker) {
  if (!g_token || !g_reflex_ready) return;
  slPCLSetMarker((sl::PCLMarker)marker, *g_token);
}

bool evaluate(ID3D12GraphicsCommandList* list, const EvaluateInputs& in) {
  if (!available() || !g_token) return false;
  sl::Resource color_in(sl::ResourceType::eTex2d, in.color_in, in.color_state);
  sl::Resource depth(sl::ResourceType::eTex2d, in.depth, in.depth_state);
  sl::Resource mvec(sl::ResourceType::eTex2d, in.mvec, in.mvec_state);
  sl::Resource color_out(sl::ResourceType::eTex2d, in.color_out, in.out_state);
  sl::Extent render_extent{in.in_top, in.in_left, in.in_w, in.in_h};
  sl::Extent out_extent{0, 0, in.out_w, in.out_h};
  sl::ResourceTag tags[] = {
      sl::ResourceTag(&color_in, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &render_extent),
      sl::ResourceTag(&depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilEvaluate, &render_extent),
      sl::ResourceTag(&mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilEvaluate, &render_extent),
      sl::ResourceTag(&color_out, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &out_extent),
  };
  // No bias-current-colour hint: NVIDIA's guide (3.15) says current models do not use it. The HUD is
  // composited after DLSS in the present blit instead.
  const sl::BaseStructure* inputs[] = {&g_viewport, &tags[0], &tags[1], &tags[2], &tags[3]};
  const uint32_t input_count = 5;
  if (g_fg_on) {
    // Frame generation reads depth and motion vectors at Present, after this call. The game image
    // is composed into the full backbuffer, so there is no special backbuffer sub-rectangle.
    sl::ResourceTag fg_tags[] = {
        sl::ResourceTag(&depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &render_extent),
        sl::ResourceTag(&mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &render_extent),
    };
    const sl::Result tag_res = slSetTagForFrame(*g_token, g_viewport, fg_tags, 2, list);
    if (tag_res != sl::Result::eOk) host::log("dlss: Frame Generation input tags failed (%d)", (int)tag_res);
  }
  sl::Result res = slEvaluateFeature(sl::kFeatureDLSS, *g_token, inputs, input_count, list);
  if (res != sl::Result::eOk) {
    static int logged = 0;
    if (logged++ < 5) host::log("dlss: slEvaluateFeature failed (%d)", (int)res);
    return false;
  }
  return true;
}

bool evaluate_ray_reconstruction(ID3D12GraphicsCommandList* list, const RayReconstructionInputs& in) {
  if (!ray_reconstruction_available() || !g_token || !list || !in.color_in || !in.depth || !in.mvec ||
      !in.albedo || !in.specular_albedo || !in.normal_roughness || !in.color_out ||
      !in.in_w || !in.in_h || !in.out_w || !in.out_h)
    return false;
  sl::DLSSDOptions options{};
  options.mode = g_mode;
  options.outputWidth = in.out_w;
  options.outputHeight = in.out_h;
  options.colorBuffersHDR = sl::Boolean::eTrue;
  options.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::ePacked;
  sl::Result result = slDLSSDSetOptions(g_viewport, options);
  if (result != sl::Result::eOk) {
    host::log("dlss-rr: slDLSSDSetOptions failed (%d)", (int)result);
    return false;
  }
  sl::Resource color(sl::ResourceType::eTex2d, in.color_in, in.color_state);
  sl::Resource depth(sl::ResourceType::eTex2d, in.depth, in.depth_state);
  sl::Resource mvec(sl::ResourceType::eTex2d, in.mvec, in.mvec_state);
  sl::Resource albedo(sl::ResourceType::eTex2d, in.albedo, in.albedo_state);
  sl::Resource specular(sl::ResourceType::eTex2d, in.specular_albedo, in.specular_albedo_state);
  sl::Resource normal(sl::ResourceType::eTex2d, in.normal_roughness, in.normal_roughness_state);
  sl::Resource output(sl::ResourceType::eTex2d, in.color_out, in.out_state);
  sl::Extent render_extent{in.in_top, in.in_left, in.in_w, in.in_h};
  sl::Extent output_extent{0, 0, in.out_w, in.out_h};
  sl::ResourceTag tags[] = {
      sl::ResourceTag(&color, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &render_extent),
      sl::ResourceTag(&depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilEvaluate, &render_extent),
      sl::ResourceTag(&mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilEvaluate, &render_extent),
      sl::ResourceTag(&albedo, sl::kBufferTypeAlbedo, sl::ResourceLifecycle::eValidUntilEvaluate, &render_extent),
      sl::ResourceTag(&specular, sl::kBufferTypeSpecularAlbedo, sl::ResourceLifecycle::eValidUntilEvaluate, &render_extent),
      sl::ResourceTag(&normal, sl::kBufferTypeNormalRoughness, sl::ResourceLifecycle::eValidUntilEvaluate, &render_extent),
      sl::ResourceTag(&output, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilEvaluate, &output_extent),
  };
  const sl::BaseStructure* inputs[] = {&g_viewport, &tags[0], &tags[1], &tags[2], &tags[3],
                                       &tags[4], &tags[5], &tags[6]};
  result = slEvaluateFeature(sl::kFeatureDLSS_RR, *g_token, inputs, (uint32_t)std::size(inputs), list);
  if (result != sl::Result::eOk) {
    static int logged = 0;
    if (logged++ < 5) host::log("dlss-rr: slEvaluateFeature failed (%d)", (int)result);
    return false;
  }
  if (!g_rr_evaluation_logged) {
    host::log("dlss-rr: path-traced HDR frame accepted with depth, motion, diffuse/specular albedo, and packed normal/roughness guides");
    g_rr_evaluation_logged = true;
  }
  return true;
}
#endif

}  // namespace streamline
}  // namespace gx
