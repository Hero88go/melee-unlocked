// Picks the render backend (D3D12 by default, D3D11 with --backend d3d11) and forwards the
// handful of calls the rest of the port makes into whichever one is running.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include "gx_core.h"
#include "render_options.h"

namespace gx {

// Creates the requested backend; falls back to D3D12 if D3D11 cannot start.
Backend* create_render_backend(void* hwnd, int client_w, int client_h, const RenderOptions& options);
const RenderOptions& render_options(Backend* backend);
void render_resize(Backend* backend, int w, int h);
void render_stats(Backend* backend, uint32_t* frames_presented, uint32_t* pipelines, uint32_t* textures);
std::string render_profile_line();
// Called by the D3D11 backend as it is destroyed so its address is never confused with a later one.
void render_backend_unregister(Backend* backend);
// True when this machine's driver can create a Direct3D 11 hardware device, so offering the D3D11
// backend to the player is honest. Probed once on first use and cached.
bool d3d11_available();

}  // namespace gx
