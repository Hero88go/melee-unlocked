// EXPERIMENTAL DLSS 5 Neural Rendering (see gx_dlss5.h).
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gx_dlss5.h"
#include "gx_streamline.h"
#include "host.h"
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <string>
#include <vector>

#ifdef GX_DLSS5
#include <nvsdk_ngx.h>
#endif

namespace gx {
namespace dlss5 {

#ifndef GX_DLSS5
bool evaluate(const Inputs&) { return false; }
bool running() { return false; }
const char* status() { return "not built into this version"; }
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

struct Retired { void* feature; ComPtr<ID3D12Resource> resource; int frames_left; };

struct State {
  bool tried = false, failed = false, ready = false;
  std::string reason = "off";
  std::string running_line;
  HMODULE forwarder = nullptr;
  LoadFn load = nullptr; InitFn init = nullptr; CreateFn create = nullptr; EvaluateFn eval = nullptr; ReleaseFn release = nullptr;
  ID3D12Device* device = nullptr;   // native (not the Streamline proxy)
  NVSDK_NGX_Parameter* caps = nullptr;
  int float_slot = -1;
  void* feature = nullptr;
  uint32_t feature_w = 0, feature_h = 0;
  Tuning feature_tuning;
  ComPtr<ID3D12Resource> out, depth_copy;
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
  host::log("dlss5: no float slot found; intensity will have no effect");
}

bool fail(const std::string& why) {
  g.failed = true; g.reason = why;
  host::log("dlss5: %s; DLSS 5 off for this session", why.c_str());
  return false;
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
  if (model.empty()) return fail("DLSS 5 model not found: update the NVIDIA driver to 616.64 or newer, or put nvngx_dlssnr.dll next to melee_port.exe");
  host::log("dlss5: model %s", narrow(model).c_str());
  if (!g.load(model.c_str())) return fail("nvngx_dlssnr.dll would not load (" + narrow(model) + ")");

  find_float_slot();
  int ir = g.init(kAppId, data.c_str(), g.device, (int)NVSDK_NGX_Version_API, g.caps);
  if (ir != NVSDK_NGX_Result_Success) return fail("the DLSS 5 model would not initialise (" + hex((unsigned)ir) + ")");
  g.ready = true;
  host::log("dlss5: ready");
  return true;
}

void retire_feature() {
  if (g.feature) g.retired.push_back({g.feature, nullptr, 16});
  g.feature = nullptr;
}
void retire_resource(ComPtr<ID3D12Resource>& r) {
  if (r) g.retired.push_back({nullptr, r, 16});
  r.Reset();
}
void collect_retired() {
  for (size_t i = 0; i < g.retired.size();) {
    if (--g.retired[i].frames_left > 0) { ++i; continue; }
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
  void* feature = nullptr;
  int r = g.create(list, g.caps, &feature);
  if (r != NVSDK_NGX_Result_Success || !feature) return fail("the DLSS 5 feature could not be created (" + hex((unsigned)r) + ")");
  g.feature = feature; g.feature_w = w; g.feature_h = h; g.feature_tuning = t;
  host::log("dlss5: feature created at %ux%u (intensity %.2f, detail %.2f, tone %.2f, skin %.2f, style %d, preset %d, auto mask %d)",
            w, h, t.intensity, t.detail, t.tone, t.skin, t.style, t.preset, t.auto_mask ? 1 : 0);
  char line[96]; wsprintfA(line, "running at %ux%u", w, h); g.running_line = line;
  return true;
}
}  // namespace

bool evaluate(const Inputs& in) {
  if (g.release) collect_retired();
  if (g.failed || !in.color || !in.depth || !in.mvec || !in.w || !in.h || !in.guide_w || !in.guide_h) return false;
  if (!ensure_ready(in.device)) return false;
  auto* list = (ID3D12GraphicsCommandList*)streamline::native_interface(in.list);

  if (!g.out || g.out_w != in.w || g.out_h != in.h) {
    retire_resource(g.out);
    if (!make_texture(g.out, in.w, in.h, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "output")) return false;
    g.out_w = in.w; g.out_h = in.h;
  }
  const D3D12_RESOURCE_DESC dd = in.depth->GetDesc();
  if (!g.depth_copy || g.depth_w != (uint32_t)dd.Width || g.depth_h != dd.Height) {
    retire_resource(g.depth_copy);
    // Our depth buffer is D32_FLOAT, which cannot be read as a texture; the copy is its R32_FLOAT twin.
    if (!make_texture(g.depth_copy, (uint32_t)dd.Width, dd.Height, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, "depth copy")) return false;
    g.depth_w = (uint32_t)dd.Width; g.depth_h = dd.Height;
  }
  Tuning t = in.tuning;
  t.intensity = t.intensity < 0.0f ? 0.0f : t.intensity > 1.0f ? 1.0f : t.intensity;
  bool reset = in.reset;
  if (!g.feature || g.feature_w != in.w || g.feature_h != in.h || g.feature_tuning != t) {
    if (!create_feature(list, in.w, in.h, t)) return false;
    reset = true;
  }

  const auto npsr = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  const auto depth_state = (D3D12_RESOURCE_STATES)in.depth_state, mvec_state = (D3D12_RESOURCE_STATES)in.mvec_state;
  barrier(list, in.depth, depth_state, D3D12_RESOURCE_STATE_COPY_SOURCE);
  barrier(list, g.depth_copy.Get(), npsr, D3D12_RESOURCE_STATE_COPY_DEST);
  list->CopyResource(g.depth_copy.Get(), in.depth);
  barrier(list, in.depth, D3D12_RESOURCE_STATE_COPY_SOURCE, depth_state);
  barrier(list, g.depth_copy.Get(), D3D12_RESOURCE_STATE_COPY_DEST, npsr);
  barrier(list, in.mvec, mvec_state, npsr);
  barrier(list, in.color, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, npsr);

  set_resource("DLSSNR.Color", in.color);
  set_resource("DLSSNR.Depth", g.depth_copy.Get());
  set_resource("DLSSNR.MVec", in.mvec);
  set_resource("DLSSNR.Output", g.out.Get());
  clear_ui_inputs();
  // The block is shared with DLSS, which may overwrite these between frames: set everything again.
  set_uint("DLSSNR.Enabled", 1);
  set_uint("DLSSNR.Width", in.w); set_uint("DLSSNR.Height", in.h);
  set_uint("DLSSNR.DepthInverted", 1);   // reversed Z, as told to DLSS (gx_streamline.cpp)
  set_uint("DLSSNR.Reset", reset ? 1 : 0);
  set_uint("DLSSNR.ColorSubrectBaseX", 0); set_uint("DLSSNR.ColorSubrectBaseY", 0);
  set_uint("DLSSNR.ColorSubrectWidth", in.w); set_uint("DLSSNR.ColorSubrectHeight", in.h);
  set_uint("DLSSNR.OutputSubrectBaseX", 0); set_uint("DLSSNR.OutputSubrectBaseY", 0);
  set_uint("DLSSNR.OutputSubrectWidth", in.w); set_uint("DLSSNR.OutputSubrectHeight", in.h);
  set_uint("DLSSNR.DepthSubrectBaseX", in.guide_x); set_uint("DLSSNR.DepthSubrectBaseY", in.guide_y);
  set_uint("DLSSNR.DepthSubrectWidth", in.guide_w); set_uint("DLSSNR.DepthSubrectHeight", in.guide_h);
  set_uint("DLSSNR.MVecSubrectBaseX", in.guide_x); set_uint("DLSSNR.MVecSubrectBaseY", in.guide_y);
  set_uint("DLSSNR.MVecSubrectWidth", in.guide_w); set_uint("DLSSNR.MVecSubrectHeight", in.guide_h);
  // Our vectors are in render-resolution pixels; the model works at the output size.
  set_float("DLSSNR.MVecScaleX", (float)in.w / (float)in.guide_w);
  set_float("DLSSNR.MVecScaleY", (float)in.h / (float)in.guide_h);
  set_tuning(t);

  const int r = g.eval(list, g.feature, g.caps);
  const bool ok = r == NVSDK_NGX_Result_Success;

  barrier(list, in.mvec, npsr, mvec_state);
  barrier(list, in.color, npsr, ok ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  if (ok) {
    barrier(list, g.out.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    if (in.compare) {
      // Split view: only the right half takes the model's result; the left keeps the DLSS frame.
      D3D12_TEXTURE_COPY_LOCATION dst{in.color, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
      D3D12_TEXTURE_COPY_LOCATION src{g.out.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
      D3D12_BOX box{in.w / 2, 0, 0, in.w, in.h, 1};
      list->CopyTextureRegion(&dst, in.w / 2, 0, 0, &src, &box);
    } else {
      list->CopyResource(in.color, g.out.Get());
    }
    barrier(list, g.out.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    barrier(list, in.color, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    g.failures = 0;
    if (g.evaluations++ == 0) host::log("dlss5: first frame evaluated");
  } else {
    if (g.failures < 5) host::log("dlss5: evaluate failed (%s)", hex((unsigned)r).c_str());
    if (++g.failures >= 30) fail("the model keeps failing to evaluate (" + hex((unsigned)r) + ")");
  }
  return ok;
}

bool running() { return g.ready && g.feature && !g.failed; }

const char* status() {
  if (g.failed) return g.reason.c_str();
  if (running()) return g.running_line.c_str();
  return g.tried ? "starting" : "off";
}

void shutdown() {
  if (g.release) {
    for (auto& r : g.retired) if (r.feature) g.release(r.feature);
    if (g.feature) g.release(g.feature);
  }
  g.retired.clear(); g.feature = nullptr;
  g.out.Reset(); g.depth_copy.Reset();
  if (g.caps) { NVSDK_NGX_D3D12_DestroyParameters(g.caps); g.caps = nullptr; }
  g.ready = false;
}
#endif

}  // namespace dlss5
}  // namespace gx
