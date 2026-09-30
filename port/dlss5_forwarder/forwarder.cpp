// EXPERIMENTAL: calls into NVIDIA's DLSS 5 Neural Rendering model (nvngx_dlssnr.dll) from a module
// the model accepts as a caller.
//
// The model looks up the module that owns its caller's return address and refuses any whose path does
// not contain "nvngx.dll" (the driver's NGX core is _nvngx.dll). This DLL is built as
// nvngx.dll_meleedlss5.dll so its path passes that check. It also owns the NGX SDK core calls,
// keeping the SDK's static library out of the game and test executables.
// NVIDIA publishes no SDK for this feature yet, which is why it is reached this way at all.
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
static_assert(std::is_same_v<decltype(&mdl5_core_init), mdl5_api::CoreInitFn>);
static_assert(std::is_same_v<decltype(&mdl5_core_capabilities), mdl5_api::CoreCapabilitiesFn>);
static_assert(std::is_same_v<decltype(&mdl5_core_destroy), mdl5_api::CoreDestroyFn>);
static_assert(std::is_same_v<decltype(&mdl5_core_shutdown), mdl5_api::CoreShutdownFn>);

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
