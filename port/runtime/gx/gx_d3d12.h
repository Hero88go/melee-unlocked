// D3D12 backend for captured GX frames.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include <vector>
#include "gx_core.h"
#include "render_options.h"

namespace gx {
// The option block predates the second backend; this file keeps its old name for it.
using D3D12Options = RenderOptions;

Backend* create_d3d12_backend(void* hwnd, int client_w, int client_h, const D3D12Options& options);
const D3D12Options& d3d12_options(Backend* backend);
void d3d12_resize(Backend* backend, int w, int h);
void d3d12_stats(Backend* backend, uint32_t* frames_presented, uint32_t* pipelines, uint32_t* textures);
// Per-section CPU cost of execute_draw since the last call (diagnostics), as a one-line summary.
std::string d3d12_profile_line();

}  // namespace gx
