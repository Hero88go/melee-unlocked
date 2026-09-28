// D3D11 backend for captured GX frames: the same renderer as gx_d3d12.cpp on Direct3D 11,
// for machines whose driver or GPU cannot start D3D12. No DLSS (Streamline is D3D12 only).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include "gx_core.h"
#include "render_options.h"

namespace gx {

// Returns nullptr (after logging why) when D3D11 cannot be started, so the caller can fall back.
Backend* create_d3d11_backend(void* hwnd, int client_w, int client_h, const RenderOptions& options);
const RenderOptions& d3d11_options(Backend* backend);
void d3d11_resize(Backend* backend, int w, int h);
void d3d11_stats(Backend* backend, uint32_t* frames_presented, uint32_t* pipelines, uint32_t* textures);
std::string d3d11_profile_line();

}  // namespace gx
