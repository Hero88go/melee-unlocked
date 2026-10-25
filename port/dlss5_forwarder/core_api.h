// C ABI used by the host to reach the optional NGX core in the helper DLL.
// The SDK types stay inside the helper; pointer arguments are opaque across the boundary.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mdl5_api {
using CoreInitFn = int(__cdecl*)(unsigned long long, const wchar_t*, void*, int);
using CoreCapabilitiesFn = int(__cdecl*)(void**);
using CoreDestroyFn = int(__cdecl*)(void*);
using CoreShutdownFn = int(__cdecl*)(void*);
using CoreInitPathsFn = int(__cdecl*)(unsigned long long, const wchar_t*, void*, int, const wchar_t* const*, int);
using CoreCreateFn = int(__cdecl*)(void*, void*, void**);
using CoreEvaluateFn = int(__cdecl*)(void*, void*, void*);
using CoreReleaseFn = int(__cdecl*)(void*);
using ParamSetFn = void(__cdecl*)(void*, const char*, int, const void*);
using ParamGetFn = int(__cdecl*)(void*, const char*, int, void*);
using CoreVramFn = int(__cdecl*)(void*, unsigned long long*);
}  // namespace mdl5_api
