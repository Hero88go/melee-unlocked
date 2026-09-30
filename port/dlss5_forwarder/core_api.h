// C ABI used by the host to reach the optional NGX core in the helper DLL.
// The SDK types stay inside the helper; pointer arguments are opaque across the boundary.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mdl5_api {
using CoreInitFn = int(__cdecl*)(unsigned long long, const wchar_t*, void*, int);
using CoreCapabilitiesFn = int(__cdecl*)(void**);
using CoreDestroyFn = int(__cdecl*)(void*);
using CoreShutdownFn = int(__cdecl*)(void*);
}  // namespace mdl5_api
