// EXPERIMENTAL DLSS 5 Neural Rendering (see gx_dlss5.h).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gx_dlss5.h"
#include "gx_dlss5_scaling.h"
#include "gx_streamline.h"
#include "host.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <limits>

#ifdef GX_DLSS5
#include <nvsdk_ngx.h>
#endif

namespace gx {
namespace dlss5 {

namespace {
std::string g_profile_dir;
std::string clean_profile_name(const std::string& name) {
  std::string out;
  for (char c : name) if (isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_' || c == '.') out += c;
  while (!out.empty() && out.back() == ' ') out.pop_back();
  size_t start = out.find_first_not_of(' ');
  if (start == std::string::npos) return "";
  out = out.substr(start);
  if (out.size() > 40) out.resize(40);
  return out;
}
std::filesystem::path profile_path(const std::string& name) {
  return std::filesystem::path(g_profile_dir) / (clean_profile_name(name) + ".txt");
}
Tuning bounded_tuning(Tuning t) {
  const auto bound = [](float v, float fallback, float lo, float hi) {
    return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
  };
  t.intensity = bound(t.intensity, 1.0f, 0.0f, 10.0f);
  t.detail = bound(t.detail, 1.0f, 0.0f, 10.0f);
  t.tone = bound(t.tone, 1.0f, 0.0f, 10.0f);
  t.skin = t.skin < 0.0f ? -1.0f : bound(t.skin, -1.0f, 0.0f, 10.0f);
  t.style = std::clamp(t.style, 0, 3);
  t.preset = std::clamp(t.preset, 0, 3);
  t.resolution_scale = std::clamp(t.resolution_scale, 25, 100);
  t.downsample_filter = std::clamp(t.downsample_filter, 0, 2);
  t.upsample_filter = std::clamp(t.upsample_filter, 0, 2);
  t.reconstruction = std::clamp(t.reconstruction, 0, 1);
  t.passes = std::clamp(t.passes, 1, 4);
  return t;
}
}  // namespace

void profile_set_folder(const std::string& settings_path) {
  std::filesystem::path p(settings_path);
  g_profile_dir = (p.parent_path() / "Dlss5Profiles").string();
}
std::vector<std::string> profile_list() {
  std::vector<std::string> out;
  if (g_profile_dir.empty()) return out;
  std::error_code ec;
  for (const auto& e : std::filesystem::directory_iterator(g_profile_dir, ec)) {
    if (!e.is_regular_file()) continue;
    const std::filesystem::path& p = e.path();
    if (p.extension() == ".txt") out.push_back(p.stem().string());
  }
  std::sort(out.begin(), out.end());
  return out;
}
bool profile_save(const std::string& name, const Tuning& t) {
  if (g_profile_dir.empty() || clean_profile_name(name).empty()) return false;
  std::error_code ec;
  std::filesystem::create_directories(g_profile_dir, ec);
  std::ofstream f(profile_path(name));
  if (!f) return false;
  const Tuning saved = bounded_tuning(t);
  f << std::setprecision(std::numeric_limits<float>::max_digits10);
  f << "# Melee Unlocked DLSS 5 profile\n"
       "intensity " << saved.intensity << "\ndetail " << saved.detail << "\ntone " << saved.tone << "\nskin " << saved.skin
    << "\nstyle " << saved.style << "\npreset " << saved.preset << "\nautomask " << (saved.auto_mask ? 1 : 0) << "\n";
  f << "resolution " << saved.resolution_scale << "\ndownsample " << saved.downsample_filter
    << "\nupsample " << saved.upsample_filter << "\nreconstruction " << saved.reconstruction
    << "\npasses " << saved.passes << "\n";
  return f.good();
}
bool profile_load(const std::string& name, Tuning& t) {
  if (g_profile_dir.empty()) return false;
  std::ifstream f(profile_path(name));
  if (!f) return false;
  Tuning loaded;
  std::string line;
  while (std::getline(f, line)) {
    std::istringstream fields(line);
    std::string key, value, extra;
    if (!(fields >> key) || key[0] == '#') continue;
    float* number = key == "intensity" ? &loaded.intensity : key == "detail" ? &loaded.detail :
                    key == "tone" ? &loaded.tone : key == "skin" ? &loaded.skin : nullptr;
    int* integer = key == "style" ? &loaded.style : key == "preset" ? &loaded.preset :
                   key == "resolution" ? &loaded.resolution_scale : key == "downsample" ? &loaded.downsample_filter :
                   key == "upsample" ? &loaded.upsample_filter : key == "reconstruction" ? &loaded.reconstruction :
                   key == "passes" ? &loaded.passes : nullptr;
    if (!number && !integer && key != "automask") continue;
    if (!(fields >> value) || ((fields >> extra) && extra[0] != '#')) return false;
    try {
      size_t used = 0;
      if (number) *number = std::stof(value, &used);
      else if (integer) *integer = std::stoi(value, &used);
      else {
        if (value != "0" && value != "1") return false;
        loaded.auto_mask = value == "1"; used = value.size();
      }
      if (used != value.size()) return false;
    } catch (...) { return false; }
  }
  if (f.bad()) return false;
  if (!std::isfinite(loaded.intensity) || !std::isfinite(loaded.detail) ||
      !std::isfinite(loaded.tone) || !std::isfinite(loaded.skin)) return false;
  t = bounded_tuning(loaded);
  return true;
}
bool profile_delete(const std::string& name) {
  if (g_profile_dir.empty()) return false;
  std::error_code ec;
  return std::filesystem::remove(profile_path(name), ec);
}

#ifndef GX_DLSS5
bool evaluate(const Inputs&) { return false; }
bool needs_warmup(uint32_t, uint32_t, const Tuning&) { return false; }
bool running() { return false; }
const char* status() { return "not built into this version"; }
bool model_found() { return false; }
void shutdown() {}
#else

using Microsoft::WRL::ComPtr;

namespace {
using LoadFn = int(__cdecl*)(const wchar_t*);
using InitFn = int(__cdecl*)(unsigned long long, const wchar_t*, void*, int, void*);
using CreateFn = int(__cdecl*)(void*, void*, void**);
using EvaluateFn = int(__cdecl*)(void*, void*, void*);
using ReleaseFn = int(__cdecl*)(void*);

// Same development application id the Streamline integration uses (gx_streamline.cpp).
constexpr unsigned long long kAppId = 231313132ull;

struct Retired { void* feature; ComPtr<ID3D12Resource> resource; uint64_t until; };

struct State {
  bool tried = false, failed = false, ready = false;
  bool tuning_failed = false;
  Tuning failed_tuning;
  uint32_t failed_w = 0, failed_h = 0;
  std::string reason = "off";
  std::string running_line;
  HMODULE forwarder = nullptr;
  LoadFn load = nullptr; InitFn init = nullptr; CreateFn create = nullptr; EvaluateFn eval = nullptr; ReleaseFn release = nullptr;
  ID3D12Device* device = nullptr;   // native (not the Streamline proxy)
  NVSDK_NGX_Parameter* caps = nullptr;
  int float_slot = -1;
  void* feature[4] = {};
  uint32_t feature_w = 0, feature_h = 0;
  Tuning feature_tuning;
  ComPtr<ID3D12Resource> out[2], depth_copy, reduced_input, resolved;
  uint32_t resolved_w = 0, resolved_h = 0;
  Scaling scaling;
  ComPtr<ID3D12Fence> fence;
  uint64_t retire_after = 0;
  bool history_dirty = true;
  uint32_t out_w = 0, out_h = 0, depth_w = 0, depth_h = 0;
  std::vector<Retired> retired;
  int failures = 0;
  uint64_t evaluations = 0;
};
State g;

std::wstring exe_dir() {
  wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
  std::wstring dir(exe); size_t slash = dir.find_last_of(L"\\/"); if (slash != std::wstring::npos) dir.resize(slash);
  return dir;
}
bool file_exists(const std::wstring& p) { DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
std::string narrow(const std::wstring& w) {
  char buf[MAX_PATH * 2]; WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, buf, sizeof buf, nullptr, nullptr); return buf;
}

// The model, in order of preference: beside the executable (a player's own copy), then beside the
// driver's NGX core, then the newest copy anywhere in the NVIDIA driver store.
std::wstring find_model() {
  std::wstring local = exe_dir() + L"\\nvngx_dlssnr.dll";
  if (file_exists(local)) return local;
  if (HMODULE core = GetModuleHandleW(L"_nvngx.dll")) {
    wchar_t path[MAX_PATH]; GetModuleFileNameW(core, path, MAX_PATH);
    std::wstring dir(path); size_t slash = dir.find_last_of(L"\\/"); if (slash != std::wstring::npos) dir.resize(slash);
    if (file_exists(dir + L"\\nvngx_dlssnr.dll")) return dir + L"\\nvngx_dlssnr.dll";
  }
  wchar_t sysroot[MAX_PATH]; GetWindowsDirectoryW(sysroot, MAX_PATH);
  std::wstring repo = std::wstring(sysroot) + L"\\System32\\DriverStore\\FileRepository\\";
  std::wstring best; FILETIME best_time{};
  WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((repo + L"nv*").c_str(), &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
      std::wstring candidate = repo + fd.cFileName + L"\\nvngx_dlssnr.dll";
      WIN32_FILE_ATTRIBUTE_DATA a;
      if (GetFileAttributesExW(candidate.c_str(), GetFileExInfoStandard, &a) && CompareFileTime(&a.ftLastWriteTime, &best_time) > 0) {
        best = candidate; best_time = a.ftLastWriteTime;
      }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
  }
  return best;
}

// The driver's parameter block is driven through its vtable, as the community integrations found:
// 64-bit values (and resources) at slot 0, unsigned at slot 3, floats at a slot found by round trip.
void set_ull(const char* name, unsigned long long v) {
  void** vt = *reinterpret_cast<void***>(g.caps);
  reinterpret_cast<void(*)(void*, const char*, unsigned long long)>(vt[0])(g.caps, name, v);
}
void set_uint(const char* name, unsigned int v) {
  void** vt = *reinterpret_cast<void***>(g.caps);
  reinterpret_cast<void(*)(void*, const char*, unsigned int)>(vt[3])(g.caps, name, v);
}
void set_float_at(int slot, const char* name, float v) {
  void** vt = *reinterpret_cast<void***>(g.caps);
  reinterpret_cast<void(*)(void*, const char*, float)>(vt[slot])(g.caps, name, v);
}
void set_float(const char* name, float v) { if (g.float_slot >= 0) set_float_at(g.float_slot, name, v); }
void set_resource(const char* name, ID3D12Resource* r) { set_ull(name, (unsigned long long)(uintptr_t)r); }

void find_float_slot() {
  static const int candidates[] = {1, 2, 5, 6, 7, 4, 3, 0};
  const float expected = 0.375f;   // exact in binary
  for (int slot : candidates) {
    set_float_at(slot, "DLSSNR.MeleeFloatProbe", expected);
    float back = 0.0f;
    if (g.caps->Get("DLSSNR.MeleeFloatProbe", &back) == NVSDK_NGX_Result_Success && back == expected) {
      g.float_slot = slot;
      host::log("dlss5: float parameters use slot %d", slot);
      return;
    }
  }
  host::log("dlss5: no float parameter slot found");
}

bool fail(const std::string& why, bool tuning_failure = false) {
  g.failed = true; g.reason = why;
  g.tuning_failed = tuning_failure;
  host::log("dlss5: %s; DLSS 5 %s", why.c_str(), tuning_failure ? "disabled for these settings" : "off for this session");
  return false;
}
bool fail_tuning(const std::string& why, uint32_t w, uint32_t h, const Tuning& t) {
  g.failed_w = w; g.failed_h = h; g.failed_tuning = t;
  return fail(why + "; choose different DLSS 5 settings to retry", true);
}
bool can_retry(uint32_t w, uint32_t h, const Tuning& t) {
  return g.tuning_failed && (g.failed_w != w || g.failed_h != h || g.failed_tuning != t);
}

std::string hex(unsigned v) { char b[16]; wsprintfA(b, "0x%08X", v); return b; }

bool ensure_ready(ID3D12Device* proxy_device) {
  if (g.ready) return true;
  if (g.failed) return false;
  g.tried = true;
  const std::wstring dir = exe_dir();
  const std::wstring fwd = dir + L"\\nvngx.dll_meleedlss5.dll";
  g.forwarder = LoadLibraryW(fwd.c_str());
  if (!g.forwarder) return fail("nvngx.dll_meleedlss5.dll is missing from the game folder");
  g.load = (LoadFn)GetProcAddress(g.forwarder, "mdl5_load");
  g.init = (InitFn)GetProcAddress(g.forwarder, "mdl5_init");
  g.create = (CreateFn)GetProcAddress(g.forwarder, "mdl5_create");
  g.eval = (EvaluateFn)GetProcAddress(g.forwarder, "mdl5_evaluate");
  g.release = (ReleaseFn)GetProcAddress(g.forwarder, "mdl5_release");
  if (!g.load || !g.init || !g.create || !g.eval || !g.release) return fail("the forwarder DLL is the wrong version");

  g.device = (ID3D12Device*)streamline::native_interface(proxy_device);
  const std::wstring data = dir + L"\\streamline-logs";
  CreateDirectoryW(data.c_str(), nullptr);
  // NVIDIA's NGX core (DLSS SDK). Streamline has usually brought it up already for DLSS, in which
  // case this returns success without changing anything.
  NVSDK_NGX_Result r = NVSDK_NGX_D3D12_Init(kAppId, data.c_str(), g.device);
  if (NVSDK_NGX_FAILED(r)) return fail("NVIDIA NGX would not start (" + hex((unsigned)r) + ")");
  r = NVSDK_NGX_D3D12_GetCapabilityParameters(&g.caps);
  if (NVSDK_NGX_FAILED(r) || !g.caps) return fail("NVIDIA NGX gave no parameters (" + hex((unsigned)r) + ")");

  const std::wstring model = find_model();
  if (model.empty()) return fail("DLSS 5 model not found in the NVIDIA driver or beside the game executable (nvngx_dlssnr.dll)");
  host::log("dlss5: model %s", narrow(model).c_str());
  if (!g.load(model.c_str())) return fail("nvngx_dlssnr.dll would not load (" + narrow(model) + ")");

  find_float_slot();
  if (g.float_slot < 0) return fail("the model did not accept floating-point controls");
  int ir = g.init(kAppId, data.c_str(), g.device, (int)NVSDK_NGX_Version_API, g.caps);
  if (ir != NVSDK_NGX_Result_Success) return fail("the DLSS 5 model would not initialise (" + hex((unsigned)ir) + ")");
  g.ready = true;
  host::log("dlss5: ready");
  return true;
}

void retire_feature() {
  for (auto& feature : g.feature) {
    if (feature) {
      auto found = std::find_if(g.retired.begin(), g.retired.end(),
        [&](const Retired& retired) { return retired.feature == feature; });
      if (found == g.retired.end()) g.retired.push_back({feature, nullptr, g.retire_after});
      else found->until = std::max(found->until, g.retire_after);
    }
    feature = nullptr;
  }
}
void retire_resource(ComPtr<ID3D12Resource>& r) {
  if (r) g.retired.push_back({nullptr, r, g.retire_after});
  r.Reset();
}
void collect_retired() {
  for (size_t i = 0; i < g.retired.size();) {
    if (!g.fence || g.fence->GetCompletedValue() < g.retired[i].until) { ++i; continue; }
    if (g.retired[i].feature) g.release(g.retired[i].feature);
    g.retired.erase(g.retired.begin() + i);
  }
}

bool make_texture(ComPtr<ID3D12Resource>& t, uint32_t w, uint32_t h, DXGI_FORMAT f, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state, const char* what) {
  D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
  D3D12_RESOURCE_DESC rd{};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = w; rd.Height = h; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
  rd.Format = f; rd.SampleDesc.Count = 1; rd.Flags = flags;
  if (FAILED(g.device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, state, nullptr, IID_PPV_ARGS(&t)))) {
    return fail(std::string("could not create the ") + what + " texture");
  }
  return true;
}

void barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
  if (from == to) return;
  D3D12_RESOURCE_BARRIER b{};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to};
  list->ResourceBarrier(1, &b);
}

void clear_ui_inputs() {
  // The block is shared and outlives everything; a stale pointer here would be a freed resource.
  set_resource("DLSSNR.UI", nullptr); set_resource("DLSSNR.UIAlpha", nullptr); set_resource("DLSSNR.Backbuffer", nullptr);
}

void set_tuning(const Tuning& t) {
  // Written every time, defaults included: the block outlives the feature, so a skipped write
  // would leave the previous value in place.
  set_uint("DLSSNR.Hint.Render.Preset", (unsigned)t.preset);
  set_float("DLSSNR.Intensity", t.intensity);
  set_uint("DLSSNR.Style", (unsigned)t.style);
  set_float("DLSSNR.LocalStructureStrength", t.detail);
  set_float("DLSSNR.LocalToneStrength", t.tone);
  set_float("DLSSNR.SkinStructureStrength", t.skin);
  set_uint("DLSSNR.UseAutoMask", t.auto_mask ? 1 : 0);
}

bool create_feature(ID3D12GraphicsCommandList* list, uint32_t w, uint32_t h, const Tuning& t) {
  retire_feature();
  set_uint("DLSSNR.Enabled", 1);
  set_uint("DLSSNR.Width", w); set_uint("DLSSNR.Height", h);
  set_uint("CreationNodeMask", 1); set_uint("VisibilityNodeMask", 1);
  // Tuning is read when the feature is built, not at evaluate.
  set_tuning(t);
  set_uint("DLSSNR.UICorrection", 0);
  clear_ui_inputs();
  for (int pass = 0; pass < t.passes; ++pass) {
    int r = g.create(list, g.caps, &g.feature[pass]);
    if (r != NVSDK_NGX_Result_Success || !g.feature[pass]) {
      retire_feature(); // Includes initialization work already recorded by earlier passes.
      return fail_tuning("the DLSS 5 feature could not be created (" + hex((unsigned)r) + ")", w, h, t);
    }
    for (int prior = 0; prior < pass; ++prior) if (g.feature[prior] == g.feature[pass]) {
      retire_feature();
      return fail_tuning("the model reused one temporal feature for multiple passes", w, h, t);
    }
  }
  g.feature_w = w; g.feature_h = h; g.feature_tuning = t;
  host::log("dlss5: feature created at %ux%u (intensity %.2f, detail %.2f, tone %.2f, skin %.2f, style %d, preset %d, auto mask %d)",
            w, h, t.intensity, t.detail, t.tone, t.skin, t.style, t.preset, t.auto_mask ? 1 : 0);
  char line[128]; wsprintfA(line, "%ux%u neural resolution (%d%%), %d pass(es)", w, h, t.resolution_scale, t.passes); g.running_line = line;
  return true;
}
}  // namespace

static Tuning clamped(Tuning t) {
  return bounded_tuning(t);
}

bool needs_warmup(uint32_t w, uint32_t h, const Tuning& t) {
  const Tuning tune = clamped(t);
  if (tune.intensity == 0.0f) return false;
  w = std::max(1u, w * (uint32_t)tune.resolution_scale / 100u);
  h = std::max(1u, h * (uint32_t)tune.resolution_scale / 100u);
  if (g.failed && !can_retry(w, h, tune)) return false;
  return !g.feature[0] || g.feature_w != w || g.feature_h != h || g.feature_tuning != tune || g.evaluations == 0;
}

bool evaluate(const Inputs& in) {
  if (g.release) collect_retired();
  if (!in.color || !in.depth || !in.mvec || !in.w || !in.h || !in.guide_w || !in.guide_h) return false;
  const Tuning t = clamped(in.tuning);
  const uint32_t w = std::max(1u, in.w * (uint32_t)t.resolution_scale / 100u);
  const uint32_t h = std::max(1u, in.h * (uint32_t)t.resolution_scale / 100u);
  if (g.failed) {
    if (!can_retry(w, h, t)) return false;
    g.failed = false; g.tuning_failed = false; g.history_dirty = true; g.failures = 0;
  }
  // Exact bypass, including reduced-resolution processed-image mode.
  if (t.intensity == 0.0f) { g.history_dirty = true; return false; }
  if (!in.fence || !in.signal_value) return fail("DLSS 5 submission is missing its completion fence");
  if (g.fence && g.fence.Get() != in.fence) return fail("DLSS 5 queue changed without shutdown");
  g.fence = in.fence; g.retire_after = in.signal_value;
  if (!ensure_ready(in.device)) return false;
  auto* list = (ID3D12GraphicsCommandList*)streamline::native_interface(in.list);
  const bool scaled = w != in.w || h != in.h;
  const auto uav = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  const auto npsr = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

  if (!g.out[0] || g.out_w != w || g.out_h != h) {
    for (auto& out : g.out) retire_resource(out);
    retire_resource(g.reduced_input);
    if (!make_texture(g.out[0], w, h, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, uav, "neural output")) return false;
    g.out_w = w; g.out_h = h;
    g.history_dirty = true;
  }
  if (t.passes > 1 && !g.out[1] && !make_texture(g.out[1], w, h, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, uav, "second neural output")) return false;
  if (scaled) {
    if (!g.reduced_input && !make_texture(g.reduced_input, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, uav, "neural input")) return false;
    if (!g.resolved || g.resolved_w != in.w || g.resolved_h != in.h) {
      retire_resource(g.resolved);
      if (!make_texture(g.resolved, in.w, in.h, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, uav, "neural reconstruction")) return false;
      g.resolved_w = in.w; g.resolved_h = in.h;
    }
  }
  const D3D12_RESOURCE_DESC dd = in.depth->GetDesc();
  if (!g.depth_copy || g.depth_w != (uint32_t)dd.Width || g.depth_h != dd.Height) {
    retire_resource(g.depth_copy);
    if (!make_texture(g.depth_copy, (uint32_t)dd.Width, dd.Height, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE, npsr, "depth copy")) return false;
    g.depth_w = (uint32_t)dd.Width; g.depth_h = dd.Height;
  }
  bool reset = in.reset || in.warm_only || g.history_dirty;
  if (!g.feature[0] || g.feature_w != w || g.feature_h != h || g.feature_tuning != t) {
    if (!create_feature(list, w, h, t)) return false;
    reset = true;
  }
  const auto depth_state = (D3D12_RESOURCE_STATES)in.depth_state, mvec_state = (D3D12_RESOURCE_STATES)in.mvec_state;
  barrier(list, in.color, uav, npsr);
  if (scaled) {
    if (!g.scaling.dispatch(g.device, list, in.fence, in.signal_value, in.color, in.color, in.color, g.reduced_input.Get(), 0, t.downsample_filter)) {
      barrier(list, in.color, npsr, uav);
      return fail(g.scaling.error());
    }
    barrier(list, g.reduced_input.Get(), uav, npsr);
  }
  barrier(list, in.depth, depth_state, D3D12_RESOURCE_STATE_COPY_SOURCE);
  barrier(list, g.depth_copy.Get(), npsr, D3D12_RESOURCE_STATE_COPY_DEST);
  list->CopyResource(g.depth_copy.Get(), in.depth);
  barrier(list, in.depth, D3D12_RESOURCE_STATE_COPY_SOURCE, depth_state);
  barrier(list, g.depth_copy.Get(), D3D12_RESOURCE_STATE_COPY_DEST, npsr);
  barrier(list, in.mvec, mvec_state, npsr);

  int r = NVSDK_NGX_Result_Success;
  for (int pass = 0; pass < t.passes; ++pass) {
    ID3D12Resource* input = pass == 0 ? (scaled ? g.reduced_input.Get() : in.color) : g.out[(pass - 1) % 2].Get();
    if (pass > 0) barrier(list, input, uav, npsr);
    set_resource("DLSSNR.Color", input);
    set_resource("DLSSNR.Depth", g.depth_copy.Get());
    set_resource("DLSSNR.MVec", in.mvec);
    set_resource("DLSSNR.Output", g.out[pass % 2].Get());
    clear_ui_inputs();
    set_uint("DLSSNR.Enabled", 1); set_uint("DLSSNR.UICorrection", 0);
    set_uint("DLSSNR.Width", w); set_uint("DLSSNR.Height", h);
    set_uint("DLSSNR.DepthInverted", 1);
    set_uint("DLSSNR.Reset", reset ? 1 : 0);
    set_uint("DLSSNR.ColorSubrectBaseX", 0); set_uint("DLSSNR.ColorSubrectBaseY", 0);
    set_uint("DLSSNR.ColorSubrectWidth", w); set_uint("DLSSNR.ColorSubrectHeight", h);
    set_uint("DLSSNR.OutputSubrectBaseX", 0); set_uint("DLSSNR.OutputSubrectBaseY", 0);
    set_uint("DLSSNR.OutputSubrectWidth", w); set_uint("DLSSNR.OutputSubrectHeight", h);
    set_uint("DLSSNR.DepthSubrectBaseX", in.guide_x); set_uint("DLSSNR.DepthSubrectBaseY", in.guide_y);
    set_uint("DLSSNR.DepthSubrectWidth", in.guide_w); set_uint("DLSSNR.DepthSubrectHeight", in.guide_h);
    set_uint("DLSSNR.MVecSubrectBaseX", in.guide_x); set_uint("DLSSNR.MVecSubrectBaseY", in.guide_y);
    set_uint("DLSSNR.MVecSubrectWidth", in.guide_w); set_uint("DLSSNR.MVecSubrectHeight", in.guide_h);
    // Each pass owns history from the corresponding pass of the previous frame, so all passes
    // need the frame-to-frame engine vectors. They do not share same-frame temporal history.
    set_float("DLSSNR.MVecScaleX", (float)w / (float)in.guide_w);
    set_float("DLSSNR.MVecScaleY", (float)h / (float)in.guide_h);
    set_tuning(t);
    r = g.eval(list, g.feature[pass], g.caps);
    if (pass > 0) barrier(list, input, npsr, uav);
    if (r != NVSDK_NGX_Result_Success) break;
  }
  barrier(list, in.mvec, npsr, mvec_state);
  bool ok = r == NVSDK_NGX_Result_Success;
  ID3D12Resource* result = g.out[(t.passes - 1) % 2].Get();
  if (ok && scaled && !in.warm_only) {
    barrier(list, result, uav, npsr);
    ok = g.scaling.dispatch(g.device, list, in.fence, in.signal_value, result, g.reduced_input.Get(), in.color,
                            g.resolved.Get(), t.reconstruction == 0 ? 2 : 1, t.upsample_filter);
    barrier(list, result, npsr, uav);
    if (!ok) fail(g.scaling.error());
    result = g.resolved.Get();
  }
  if (scaled) barrier(list, g.reduced_input.Get(), npsr, uav);
  // No partial result: all passes and the resolve must succeed before touching the source.
  if (ok && !in.warm_only) {
    barrier(list, result, uav, D3D12_RESOURCE_STATE_COPY_SOURCE);
    barrier(list, in.color, npsr, D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyResource(in.color, result);
    barrier(list, result, D3D12_RESOURCE_STATE_COPY_SOURCE, uav);
    barrier(list, in.color, D3D12_RESOURCE_STATE_COPY_DEST, uav);
  } else {
    barrier(list, in.color, npsr, uav);
  }
  g.history_dirty = !ok || in.warm_only;
  if (ok) {
    g.failures = 0;
    if (g.evaluations++ == 0) host::log("dlss5: evaluated %d pass(es) at %ux%u%s", t.passes, w, h, in.warm_only ? " (warm-up)" : "");
  } else {
    if (g.failures < 5) host::log("dlss5: evaluate failed (%s)", hex((unsigned)r).c_str());
    if (++g.failures >= 30) fail_tuning("the model keeps failing to evaluate (" + hex((unsigned)r) + ")", w, h, t);
  }
  return ok && !in.warm_only;
}

bool running() { return g.ready && g.feature[0] && !g.failed; }

// The model file is present (beside the game, beside the driver core, or in the driver store).
bool model_found() { return !find_model().empty(); }

const char* status() {
  if (g.failed) return g.reason.c_str();
  if (running()) return g.running_line.c_str();
  return g.tried ? "starting" : "waiting for a match (menus are shown without it)";
}

void shutdown() {
  if (g.release) {
    for (auto& r : g.retired) if (r.feature) g.release(r.feature);
    for (auto* feature : g.feature) if (feature) g.release(feature);
  }
  g.retired.clear();
  for (auto& feature : g.feature) feature = nullptr;
  for (auto& out : g.out) out.Reset();
  g.depth_copy.Reset(); g.reduced_input.Reset(); g.resolved.Reset(); g.scaling.shutdown();
  if (g.caps) { NVSDK_NGX_D3D12_DestroyParameters(g.caps); g.caps = nullptr; }
  g.fence.Reset();
  g.ready = false; g.failed = false; g.tried = false; g.tuning_failed = false;
  g.history_dirty = true; g.evaluations = 0; g.failures = 0;
}
#endif

}  // namespace dlss5
}  // namespace gx
