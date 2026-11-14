// Linux: the answers the settings panel and the presentation loop get from the Windows-only GPU
// libraries (NVIDIA Streamline: DLSS frame generation and Reflex; the Direct3D 12 renderer's VRAM and
// pass-cost readings). None of those libraries exists on Linux, so each reports itself
// unavailable, exactly as on a Windows PC without that hardware, and the panel greys those options.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gx_streamline.h"
#include "gx_d3d12.h"
#include "render_options.h"
#include "gpu_readings.h"
#include <atomic>
#include <cstdint>

namespace gx {
// The Vulkan renderer measures it (gpu_readings.h); OpenGL does not, and reads 0.
bool vram_usage(float* used_gb, float* total_gb) { return gpu_readings::vram(used_gb, total_gb); }
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
// No Streamline here; the Vulkan renderer publishes its own reading (VK_NV_low_latency2).
namespace {
std::atomic<float> g_total{0}, g_sim{0}, g_submit{0}, g_driver{0}, g_queue{0}, g_gpu{0};
}
float reflex_latency_ms() { return g_total.load(std::memory_order_relaxed); }
ReflexBreakdown reflex_breakdown() {
  ReflexBreakdown b;
  b.sim = g_sim.load(std::memory_order_relaxed); b.render_submit = g_submit.load(std::memory_order_relaxed);
  b.driver = g_driver.load(std::memory_order_relaxed); b.os_queue = g_queue.load(std::memory_order_relaxed);
  b.gpu_render = g_gpu.load(std::memory_order_relaxed);
  return b;
}
void publish_latency(float total_ms, const ReflexBreakdown& s) {
  g_sim = s.sim; g_submit = s.render_submit; g_driver = s.driver; g_queue = s.os_queue; g_gpu = s.gpu_render; g_total = total_ms;
}
void pcl_marker(int) {}
}  // namespace streamline
}  // namespace gx
