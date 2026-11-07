// RTX Remix bridge (local experiment, never in a release): a match frame's draws, camera and lights
// handed to NVIDIA's RTX Remix runtime through its C API (remix_c.h), which path traces them into a
// window of its own. The runtime is loaded from the Remix folder beside the game (MELEE_REMIX_DIR).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gx {
struct Frame;
struct RtxLightRecord;

class RemixBridge {
 public:
  RemixBridge();
  ~RemixBridge();
  // Loads the runtime and starts it on a window of its own (hidden when `hidden`). False: not usable.
  bool start(int width, int height, bool hidden, std::string* error);
  bool started() const { return started_; }
  // One match frame: the draws of `camera`, `view_to_world` (row major 3x4) and the lights in world space.
  void frame(const Frame& f, int camera, const float view_to_world[12], const std::vector<RtxLightRecord>& lights);
  // Saves Remix's last finished picture as a PPM (development captures).
  bool capture(const std::string& path);
  struct Stats { uint32_t meshes = 0, created = 0, materials = 0, instances = 0, lights = 0; double cpu_ms = 0; };
  const Stats& stats() const { return stats_; }

 private:
  struct Impl;
  Impl* impl_ = nullptr;
  bool started_ = false;
  Stats stats_;
};

// MELEE_REMIX=1: the D3D12 renderer feeds the bridge during matches.
bool remix_requested();

}  // namespace gx
