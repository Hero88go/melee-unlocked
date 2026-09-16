// D3D12 backend for captured GX frames.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include "gx_core.h"

namespace gx {

// Authored = predict ahead from the latest game frame (no delay); AuthoredInterpolate = exact
// in-betweens of the last two game frames (one frame of display delay, no overshoot).
enum class SubFrameMode { Off, Extrapolate, Interpolate, Authored, AuthoredInterpolate };

struct D3D12Options {
  // Presentation timeline (threaded renderer only). fps_cap 0 = uncapped. With a SubFrameMode other
  // than Off the renderer presents new sub-frames between 60 Hz simulation frames.
  double fps_cap = 60; // -1 follows the active monitor
  bool fullscreen = false;
  int dlss_mode = 0;              // gx::DlssMode: 0 native, 1 DLAA, 2 quality, 3 balanced, 4 performance, 5 ultra performance
  float dlss_jitter_sign = -1.0f; // calibrated 2026-09-11: -1 reconstructs sharp text, +1 blurs (see PORT_COMPLETION.md)
  bool pc_settings = false, settings_open = false, performance_overlay = false;
  bool input_overlay = false;     // on-screen controller display, for streaming
  int input_overlay_ports = 1;    // bitmask of the controller ports it shows (bit 0 = port 1)
  bool input_overlay_hide_border = false;  // draw the pads with no panel background or resize grip
  // Drop in-world translucent effects to buy frame rate on weak machines: 0 everything, 1 skips
  // effects that do not write depth (sparks, glow, smoke), 2 skips translucent world geometry too.
  // Purely presentational, so unlike a Gecko code it cannot desync and both players may differ.
  int effects_level = 0;
  // Discord Rich Presence, off by default: it tells the player's Discord friends what they are
  // playing. The application id is Melee Unlocked's own, registered once for the whole game rather
  // than per player, and it is not a secret (Rich Presence needs no token). A player can override it
  // in the settings to point the presence at an application of their own.
  bool discord_presence = false;
  std::string discord_app_id = "1549608280949792790";
  std::string settings_path = "port-settings.ini";
  std::string frame_times; // optional buffered CSV of CPU presentation timing
  SubFrameMode subframe = SubFrameMode::Off;
  int efb_scale = 0;          // internal resolution multiplier; 0 = auto (integer scale covering the window, like Dolphin "Auto (Window Size)")
  int window_w = 1280, window_h = 960;  // initial client size
  bool vsync = false;
  bool widescreen = false;    // Slippi Widescreen 16:9 code on (present at 16:9 and tell the game)
  float sharpness = 0.0f;     // 0..1 contrast-adaptive sharpening in the present pass (works with or without DLSS)
  int anisotropy = 16;        // texture anisotropic filtering 1..16
  int ssaa = 1;               // supersampling factor: 1 off, 2 = 4x SSAA (EFB rendered at 2x the chosen scale, box filtered)
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
const D3D12Options& d3d12_options(Backend* backend);
void d3d12_resize(Backend* backend, int w, int h);
void d3d12_stats(Backend* backend, uint32_t* frames_presented, uint32_t* pipelines, uint32_t* textures);
// Per-section CPU cost of execute_draw since the last call (diagnostics), as a one-line summary.
std::string d3d12_profile_line();

}  // namespace gx
