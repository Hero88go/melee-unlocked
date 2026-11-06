// GPU readings a renderer publishes for the settings panel and the log, from whichever backend runs.
// The Direct3D 12 renderer keeps its own (gx_d3d12.cpp); the Vulkan renderer writes these, and the
// panel's readers (vram_usage in gx_d3d12.cpp, and its Linux stand-in) fall back to them.
// Header-inline: no Windows or graphics API dependencies, so every platform build can include it.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>

namespace gx::gpu_readings {

// Video memory in use by this process and the card's installed size, in GB. 0 total: not measured.
inline std::atomic<float>& vram_used_gb() { static std::atomic<float> value{0.0f}; return value; }
inline std::atomic<float>& vram_total_gb() { static std::atomic<float> value{0.0f}; return value; }

inline bool vram(float* used_gb, float* total_gb) {
  const float total = vram_total_gb().load(std::memory_order_relaxed);
  if (used_gb) *used_gb = vram_used_gb().load(std::memory_order_relaxed);
  if (total_gb) *total_gb = total;
  return total > 0.0f;
}

}  // namespace gx::gpu_readings
