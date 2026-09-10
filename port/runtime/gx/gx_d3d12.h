// D3D12 backend for captured GX frames.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include "gx_core.h"

namespace gx {

struct D3D12Options {
  int efb_scale = 2;          // internal resolution multiplier
  bool vsync = false;
  std::string capture_path;   // write a PPM of the presented image at capture_frame
  uint32_t capture_frame = 0;
  uint32_t capture_every = 0;  // if set, capture every N presented frames as <capture_path>_<frame>.ppm
  std::string dump_path;      // write a text dump of draw state + shaders at dump_frame
  uint32_t dump_frame = 0;
};

Backend* create_d3d12_backend(void* hwnd, int client_w, int client_h, const D3D12Options& options);
void d3d12_resize(Backend* backend, int w, int h);
void d3d12_stats(Backend* backend, uint32_t* frames_presented, uint32_t* pipelines, uint32_t* textures);

}  // namespace gx
