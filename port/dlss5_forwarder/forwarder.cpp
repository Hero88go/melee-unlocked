// EXPERIMENTAL: calls into NVIDIA's DLSS 5 Neural Rendering model (nvngx_dlssnr.dll) from a module
// the model accepts as a caller.
//
// The model looks up the module that owns its caller's return address and refuses any whose path does
// not contain "nvngx.dll" (the driver's NGX core is _nvngx.dll). This DLL is built as
// nvngx.dll_meleedlss5.dll so its path passes that check. It also owns the NGX SDK core calls,
// keeping the SDK's static library out of the game and test executables.
// The game first uses NVIDIA's published route (mdl5_core_create below, NVIDIA's RTX Remix 1.6
// SDK); this direct route is the fallback for a model build the NGX core will not load, such as a
// community FP16 build for RTX 20 and 30 series cards.
//
// Every call keeps its result in a volatile local before returning. `return f(...)` may compile to a
// jmp (a tail call), which removes this module's frame and hands the model our caller instead.
// SPDX-License-Identifier: GPL-2.0-or-later
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <nvsdk_ngx.h>
#include <type_traits>
#include "core_api.h"

#pragma optimize("", off)

namespace {
using InitExtFn = int(__cdecl*)(unsigned long long, const wchar_t*, void*, int, const void*);
using CreateFn = int(__cdecl*)(void*, int, const void*, void**);
using EvaluateFn = int(__cdecl*)(void*, const void*, const void*, void*);
using ReleaseFn = int(__cdecl*)(void*);

HMODULE g_model = nullptr;
InitExtFn g_init = nullptr;
CreateFn g_create = nullptr;
EvaluateFn g_evaluate = nullptr;
ReleaseFn g_release = nullptr;
}  // namespace

extern "C" {

// These wrappers keep the SDK's exact calling convention and its default init arguments.
// The caller passes SDK version explicitly so a host/helper mismatch cannot silently change it.
__declspec(dllexport) int __cdecl mdl5_core_init(unsigned long long app_id, const wchar_t* data_path,
                                                void* device, int sdk_version) {
  volatile int result = static_cast<int>(NVSDK_NGX_D3D12_Init(app_id, data_path,
      static_cast<ID3D12Device*>(device), nullptr, static_cast<NVSDK_NGX_Version>(sdk_version)));
  return result;
}
__declspec(dllexport) int __cdecl mdl5_core_capabilities(void** out_parameters) {
  if (!out_parameters) return static_cast<int>(NVSDK_NGX_Result_FAIL_InvalidParameter);
  *out_parameters = nullptr;
  NVSDK_NGX_Parameter* parameters = nullptr;
  volatile int result = static_cast<int>(NVSDK_NGX_D3D12_GetCapabilityParameters(&parameters));
  *out_parameters = parameters;
  return result;
}
__declspec(dllexport) int __cdecl mdl5_core_destroy(void* parameters) {
  volatile int result = static_cast<int>(NVSDK_NGX_D3D12_DestroyParameters(
      static_cast<NVSDK_NGX_Parameter*>(parameters)));
  return result;
}
__declspec(dllexport) int __cdecl mdl5_core_shutdown(void* device) {
  volatile int result = static_cast<int>(NVSDK_NGX_D3D12_Shutdown1(static_cast<ID3D12Device*>(device)));
  return result;
}

// NVIDIA's published DLSS 5 route (RTX Remix 1.6 SDK): the NGX core finds nvngx_dlssnr.dll in the
// listed folders, checks its signature and the GPU, and creates feature 18 itself. Nothing here
// talks to the model directly.
__declspec(dllexport) int __cdecl mdl5_core_init_paths(unsigned long long app_id, const wchar_t* data_path,
                                                      void* device, int sdk_version,
                                                      const wchar_t* const* paths, int path_count) {
  NVSDK_NGX_FeatureCommonInfo info{};
  info.PathListInfo.Path = const_cast<const wchar_t**>(paths);
  info.PathListInfo.Length = path_count > 0 ? static_cast<unsigned int>(path_count) : 0u;
  volatile int result = static_cast<int>(NVSDK_NGX_D3D12_Init(app_id, data_path,
      static_cast<ID3D12Device*>(device), path_count > 0 ? &info : nullptr,
      static_cast<NVSDK_NGX_Version>(sdk_version)));
  return result;
}
__declspec(dllexport) int __cdecl mdl5_core_create(void* list, void* params, void** feature) {
  NVSDK_NGX_Handle* handle = nullptr;
  volatile int result = static_cast<int>(NVSDK_NGX_D3D12_CreateFeature(
      static_cast<ID3D12GraphicsCommandList*>(list), static_cast<NVSDK_NGX_Feature>(18),
      static_cast<NVSDK_NGX_Parameter*>(params), &handle));
  *feature = handle;
  return result;
}
__declspec(dllexport) int __cdecl mdl5_core_evaluate(void* list, void* feature, void* params) {
  volatile int result = static_cast<int>(NVSDK_NGX_D3D12_EvaluateFeature(
      static_cast<ID3D12GraphicsCommandList*>(list), static_cast<const NVSDK_NGX_Handle*>(feature),
      static_cast<const NVSDK_NGX_Parameter*>(params), nullptr));
  return result;
}
__declspec(dllexport) int __cdecl mdl5_core_release(void* feature) {
  if (!feature) return 0;
  volatile int result = static_cast<int>(NVSDK_NGX_D3D12_ReleaseFeature(static_cast<NVSDK_NGX_Handle*>(feature)));
  return result;
}

// Typed parameter access through the SDK's own interface (the type matters to the model).
// type: 0 int, 1 unsigned int, 2 float, 3 pointer or 64-bit value.
__declspec(dllexport) void __cdecl mdl5_param_set(void* params, const char* name, int type, const void* value) {
  auto* p = static_cast<NVSDK_NGX_Parameter*>(params);
  if (!p || !name || !value) return;
  switch (type) {
    case 0: p->Set(name, *static_cast<const int*>(value)); break;
    case 1: p->Set(name, *static_cast<const unsigned int*>(value)); break;
    case 2: p->Set(name, *static_cast<const float*>(value)); break;
    default: p->Set(name, *static_cast<void* const*>(value)); break;
  }
}
__declspec(dllexport) int __cdecl mdl5_param_get(void* params, const char* name, int type, void* out) {
  auto* p = static_cast<NVSDK_NGX_Parameter*>(params);
  if (!p || !name || !out) return static_cast<int>(NVSDK_NGX_Result_FAIL_InvalidParameter);
  NVSDK_NGX_Result r = NVSDK_NGX_Result_Fail;
  switch (type) {
    case 0: r = p->Get(name, static_cast<int*>(out)); break;
    case 1: r = p->Get(name, static_cast<unsigned int*>(out)); break;
    case 2: r = p->Get(name, static_cast<float*>(out)); break;
    case 4: r = p->Get(name, static_cast<unsigned long long*>(out)); break;
    default: r = p->Get(name, static_cast<void**>(out)); break;
  }
  return static_cast<int>(r);
}
// The model's own memory, from its stats callback (NGX_DLSSNR_GET_STATS in NVIDIA's helpers).
__declspec(dllexport) int __cdecl mdl5_core_vram(void* params, unsigned long long* bytes) {
  auto* p = static_cast<NVSDK_NGX_Parameter*>(params);
  if (!p || !bytes) return static_cast<int>(NVSDK_NGX_Result_FAIL_InvalidParameter);
  void* callback = nullptr;
  p->Get("DLSSNRGetStatsCallback", &callback);
  if (!callback) return static_cast<int>(NVSDK_NGX_Result_FAIL_OutOfDate);
  using StatsFn = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter*);
  volatile int result = static_cast<int>(reinterpret_cast<StatsFn>(callback)(p));
  if (NVSDK_NGX_FAILED(static_cast<NVSDK_NGX_Result>(result))) return result;
  *bytes = 0;
  p->Get("SizeInBytes", bytes);
  return result;
}
static_assert(std::is_same_v<decltype(&mdl5_core_init), mdl5_api::CoreInitFn>);
static_assert(std::is_same_v<decltype(&mdl5_core_capabilities), mdl5_api::CoreCapabilitiesFn>);
static_assert(std::is_same_v<decltype(&mdl5_core_destroy), mdl5_api::CoreDestroyFn>);
static_assert(std::is_same_v<decltype(&mdl5_core_shutdown), mdl5_api::CoreShutdownFn>);
static_assert(std::is_same_v<decltype(&mdl5_core_init_paths), mdl5_api::CoreInitPathsFn>);
static_assert(std::is_same_v<decltype(&mdl5_core_create), mdl5_api::CoreCreateFn>);
static_assert(std::is_same_v<decltype(&mdl5_core_evaluate), mdl5_api::CoreEvaluateFn>);
static_assert(std::is_same_v<decltype(&mdl5_core_release), mdl5_api::CoreReleaseFn>);
static_assert(std::is_same_v<decltype(&mdl5_param_set), mdl5_api::ParamSetFn>);
static_assert(std::is_same_v<decltype(&mdl5_param_get), mdl5_api::ParamGetFn>);
static_assert(std::is_same_v<decltype(&mdl5_core_vram), mdl5_api::CoreVramFn>);

// Loads the model from `path`. 1 on success, 0 when it will not load or lacks the D3D12 entry points.
__declspec(dllexport) int mdl5_load(const wchar_t* path) {
  if (!g_model) {
    g_model = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g_model) return 0;
    g_init = (InitExtFn)GetProcAddress(g_model, "NVSDK_NGX_D3D12_Init_Ext");
    g_create = (CreateFn)GetProcAddress(g_model, "NVSDK_NGX_D3D12_CreateFeature");
    g_evaluate = (EvaluateFn)GetProcAddress(g_model, "NVSDK_NGX_D3D12_EvaluateFeature");
    g_release = (ReleaseFn)GetProcAddress(g_model, "NVSDK_NGX_D3D12_ReleaseFeature");
  }
  return g_init && g_create && g_evaluate && g_release ? 1 : 0;
}

__declspec(dllexport) int mdl5_init(unsigned long long app_id, const wchar_t* data_path, void* device, int sdk_version, void* params) {
  if (!g_init) return 0;
  volatile int result = g_init(app_id, data_path, device, sdk_version, params);
  return result;
}

// Feature 18 is Neural Rendering. The create records initialisation work into `list`, so the handle
// must outlive that command list's execution.
__declspec(dllexport) int mdl5_create(void* list, void* params, void** feature) {
  if (!g_create) return 0;
  volatile int result = g_create(list, 18, params, feature);
  return result;
}

__declspec(dllexport) int mdl5_evaluate(void* list, void* feature, void* params) {
  if (!g_evaluate) return 0;
  volatile int result = g_evaluate(list, feature, params, nullptr);
  return result;
}

__declspec(dllexport) int mdl5_release(void* feature) {
  if (!g_release || !feature) return 0;
  volatile int result = g_release(feature);
  return result;
}

}  // extern "C"
