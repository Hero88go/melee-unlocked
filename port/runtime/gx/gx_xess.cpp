// SPDX-License-Identifier: GPL-2.0-or-later
#include "gx_xess.h"
#include "host.h"

#include <windows.h>
#include <d3d12.h>
#include "../../third_party/xess/inc/xess.h"
#include "../../third_party/xess/inc/xess_d3d12.h"

namespace gx {
namespace xess {
namespace {

HMODULE g_module = nullptr;
xess_context_handle_t g_ctx = nullptr;
bool g_ok = false, g_initialised = false;
int g_mode = 0; uint32_t g_out_w = 0, g_out_h = 0;

decltype(&xessD3D12CreateContext) p_create = nullptr;
decltype(&xessD3D12Init) p_init = nullptr;
decltype(&xessD3D12Execute) p_execute = nullptr;
decltype(&xessGetOptimalInputResolution) p_optimal = nullptr;
decltype(&xessDestroyContext) p_destroy = nullptr;
decltype(&xessGetVersion) p_version = nullptr;

xess_quality_settings_t quality_of(int mode) {
  switch (mode) {
    case 6: return XESS_QUALITY_SETTING_AA;
    case 7: return XESS_QUALITY_SETTING_ULTRA_QUALITY;
    case 8: return XESS_QUALITY_SETTING_QUALITY;
    case 9: return XESS_QUALITY_SETTING_BALANCED;
    default: return XESS_QUALITY_SETTING_PERFORMANCE;
  }
}

}  // namespace

const char* mode_name(int mode) {
  switch (mode) {
    case 6: return "XeSS AA"; case 7: return "XeSS Ultra Quality"; case 8: return "XeSS Quality";
    case 9: return "XeSS Balanced"; case 10: return "XeSS Performance"; default: return "XeSS";
  }
}

bool init(const std::wstring& exe_dir, ID3D12Device* device) {
  const std::wstring path = exe_dir + L"\\libxess.dll";
  if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) { host::log("xess: libxess.dll not found; XeSS unavailable"); return false; }
  g_module = LoadLibraryW(path.c_str());
  if (!g_module) { host::log("xess: cannot load libxess.dll (%lu)", GetLastError()); return false; }
  p_create = (decltype(p_create))GetProcAddress(g_module, "xessD3D12CreateContext");
  p_init = (decltype(p_init))GetProcAddress(g_module, "xessD3D12Init");
  p_execute = (decltype(p_execute))GetProcAddress(g_module, "xessD3D12Execute");
  p_optimal = (decltype(p_optimal))GetProcAddress(g_module, "xessGetOptimalInputResolution");
  p_destroy = (decltype(p_destroy))GetProcAddress(g_module, "xessDestroyContext");
  p_version = (decltype(p_version))GetProcAddress(g_module, "xessGetVersion");
  if (!p_create || !p_init || !p_execute || !p_optimal || !p_destroy) { host::log("xess: libxess.dll is missing entry points"); return false; }
  const xess_result_t r = p_create(device, &g_ctx);
  if (r != XESS_RESULT_SUCCESS) { host::log("xess: not supported on this GPU (%d)", (int)r); g_ctx = nullptr; return false; }
  g_ok = true;
  xess_version_t v{};
  if (p_version && p_version(&v) == XESS_RESULT_SUCCESS) host::log("xess: available (XeSS %u.%u.%u)", v.major, v.minor, v.patch);
  else host::log("xess: available");
  return true;
}

void shutdown() {
  if (g_ctx && p_destroy) p_destroy(g_ctx);
  g_ctx = nullptr; g_ok = false; g_initialised = false;
}

bool available() { return g_ok; }

bool optimal_size(int mode, uint32_t out_w, uint32_t out_h, uint32_t* rw, uint32_t* rh,
                  uint32_t* min_w, uint32_t* min_h, uint32_t* max_w, uint32_t* max_h) {
  if (!g_ok) return false;
  xess_2d_t out{out_w, out_h}, opt{}, mn{}, mx{};
  if (p_optimal(g_ctx, &out, quality_of(mode), &opt, &mn, &mx) != XESS_RESULT_SUCCESS) return false;
  *rw = opt.x; *rh = opt.y; *min_w = mn.x; *min_h = mn.y; *max_w = mx.x; *max_h = mx.y;
  return true;
}

bool set_options(int mode, uint32_t out_w, uint32_t out_h) {
  if (!g_ok) return false;
  if (g_initialised && g_mode == mode && g_out_w == out_w && g_out_h == out_h) return true;
  xess_d3d12_init_params_t p{};
  p.outputResolution = {out_w, out_h};
  p.qualitySetting = quality_of(mode);
  // Our depth is reversed (near = 1) and the colour is display-ready 8-bit, like the DLSS path.
  p.initFlags = XESS_INIT_FLAG_INVERTED_DEPTH | XESS_INIT_FLAG_LDR_INPUT_COLOR;
  const xess_result_t r = p_init(g_ctx, &p);
  if (r != XESS_RESULT_SUCCESS) { host::log("xess: init failed (%d)", (int)r); g_initialised = false; return false; }
  g_initialised = true; g_mode = mode; g_out_w = out_w; g_out_h = out_h;
  host::log("xess: %s -> output %ux%u", mode_name(mode), out_w, out_h);
  return true;
}

bool evaluate(ID3D12GraphicsCommandList* list, const Inputs& in) {
  if (!g_ok || !g_initialised) return false;
  xess_d3d12_execute_params_t e{};
  e.pColorTexture = in.color; e.pVelocityTexture = in.mvec; e.pDepthTexture = in.depth; e.pOutputTexture = in.out;
  e.jitterOffsetX = in.jitter_x; e.jitterOffsetY = in.jitter_y;
  e.exposureScale = 1.0f;
  e.resetHistory = in.reset ? 1 : 0;
  e.inputWidth = in.in_w; e.inputHeight = in.in_h;
  e.inputColorBase = {in.in_left, in.in_top};
  e.inputMotionVectorBase = {in.in_left, in.in_top};
  e.inputDepthBase = {in.in_left, in.in_top};
  const xess_result_t r = p_execute(g_ctx, list, &e);
  if (r != XESS_RESULT_SUCCESS) {
    static int logged = 0;
    if (logged++ < 5) host::log("xess: execute failed (%d)", (int)r);
    return false;
  }
  return true;
}

}  // namespace xess
}  // namespace gx
