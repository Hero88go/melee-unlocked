// Linux: the answers the settings panel and the presentation loop get from the Windows-only GPU
// libraries (NVIDIA Streamline: DLSS frame generation and Reflex; the Direct3D 12 renderer's VRAM and
// pass-cost readings). None of those libraries exists for the OpenGL renderer, so each reports itself
// unavailable, exactly as on a Windows PC without that hardware, and the panel greys those options.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gx_streamline.h"
#include "gx_d3d12.h"
#include "render_options.h"
#include <atomic>
#include <cstdint>

namespace gx {
bool vram_usage(float* used_gb, float* total_gb) {
  if (used_gb) *used_gb = 0;
  if (total_gb) *total_gb = 0;
  return false;
}
void gpu_pass_cost(float* dlaa_ms, float* neural_ms) {
  if (dlaa_ms) *dlaa_ms = 0;
  if (neural_ms) *neural_ms = 0;
}
// F2: the next frames are captured; the OpenGL renderer reads the shared request count.
namespace { std::atomic<unsigned> g_capture_request{0}; }
unsigned gx_capture_request() { return g_capture_request.load(std::memory_order_relaxed); }
void gx_capture_request_set(unsigned frames) { g_capture_request.store(frames, std::memory_order_relaxed); }
void request_frame_capture(unsigned frames) { gx_capture_request_set(frames); }

namespace streamline {
bool available() { return false; }
bool frame_generation_deferred() { return false; }
bool frame_generation_available() { return false; }
bool reflex_available() { return false; }
uint32_t frame_generation_max_multiplier() { return 1; }
bool frame_generation_capabilities_queried() { return true; }
bool frame_generation_dynamic_supported() { return false; }
float reflex_latency_ms() { return 0.0f; }
ReflexBreakdown reflex_breakdown() { return {}; }
void pcl_marker(int) {}
}  // namespace streamline
}  // namespace gx
