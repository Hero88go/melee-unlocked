// D3D12 backend for captured GX frames.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include "gx_core.h"

namespace gx {

enum class SubFrameMode { Off, Extrapolate, Interpolate, Authored };

struct D3D12Options {
  // Presentation timeline (threaded renderer only). fps_cap 0 = uncapped. With a SubFrameMode other
  // than Off the renderer presents new sub-frames between 60 Hz simulation frames.
  int fps_cap = 60;
  SubFrameMode subframe = SubFrameMode::Off;
  int efb_scale = 2;          // internal resolution multiplier; 0 = auto (integer scale covering the window, like Dolphin "Auto (Window Size)")
  int window_w = 1280, window_h = 960;  // initial client size
  bool vsync = false;
  std::string capture_path;   // write a PPM of the presented image at capture_frame
  uint32_t capture_frame = 0;
  uint32_t capture_every = 0;  // if set, capture every N presented frames as <capture_path>_<frame>.ppm
  uint32_t capture_burst = 0;  // if set, capture this many consecutive presented frames from capture_frame
  uint64_t capture_sim_frame = 0;  // if set, the burst starts at the first presented frame whose simulation sequence >= this
  std::string shader_cache = "shadercache";   // directory for compiled shader blobs and the D3D12 pipeline library
  std::string dump_path;      // write a text dump of draw state + shaders at dump_frame
  uint32_t dump_frame = 0;
};

Backend* create_d3d12_backend(void* hwnd, int client_w, int client_h, const D3D12Options& options);
void d3d12_resize(Backend* backend, int w, int h);
void d3d12_stats(Backend* backend, uint32_t* frames_presented, uint32_t* pipelines, uint32_t* textures);
// Per-section CPU cost of execute_draw since the last call (diagnostics), as a one-line summary.
std::string d3d12_profile_line();

}  // namespace gx
