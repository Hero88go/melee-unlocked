// macOS's adapter input comes exclusively from the app's report mirror.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "yougame_link.h"
#include "pad_mirror_posix.h"
#include <atomic>
#include <mutex>

namespace host {
namespace {
yougame::pad_mirror::Reader g_app_mirror;
yougame::pad_mirror::Origin g_app_origins[4];
std::mutex g_app_mutex;
std::atomic<double> g_poll_rate_hz{0.0};
void attach_mirror() {
  g_app_mirror.attach_once(yougame::pad_mirror_attachment(),
      [] { for (auto& o : g_app_origins) o.set = false; }, log);
}
bool read_mirror(uint8_t rep[37]) {
  const bool live = g_app_mirror.read(rep);
  g_poll_rate_hz.store(g_app_mirror.rate_hz(), std::memory_order_relaxed);
  return live;
}
bool mirror_live(uint8_t rep[37]) { return g_app_mirror.attached() && read_mirror(rep); }
}
double gcadapter_poll_rate_hz() {
  if (options.no_gc_adapter || !yougame::stdio_bridge()) return 0;
  std::lock_guard<std::mutex> lock(g_app_mutex);
  attach_mirror();
  uint8_t rep[37];
  return mirror_live(rep) ? g_poll_rate_hz.load(std::memory_order_relaxed) : 0;
}
std::string gcadapter_problem() {
  if (!yougame::stdio_bridge()) return "GameCube adapter is unavailable on macOS";
  std::lock_guard<std::mutex> lock(g_app_mutex);
  attach_mirror();
  uint8_t rep[37];
  return mirror_live(rep) ? "Controllers come from the YouGame app" : "No GameCube adapter from the YouGame app";
}
bool gcadapter_reset_pending() { return false; }
bool gcadapter_request_reset() { return false; }
uint32_t gcadapter_poll(PadState out[4]) {
  if (options.no_gc_adapter || !yougame::stdio_bridge()) return 0;
  std::lock_guard<std::mutex> lock(g_app_mutex);
  attach_mirror();
  uint8_t rep[37];
  return mirror_live(rep) ? yougame::pad_mirror::decode(rep, g_app_origins, out) : 0;
}
void gcadapter_recalibrate(int port) {
  std::lock_guard<std::mutex> lock(g_app_mutex);
  if (port < 0 || port > 3) { for (auto& o : g_app_origins) o.set = false; }
  else g_app_origins[port].set = false;
}
void gcadapter_rumble(int port, bool) {
  if (options.no_gc_adapter || port < 0 || port > 3 || !yougame::stdio_bridge()) return;
  static std::once_flag said;
  std::call_once(said, [] { log("gc adapter: rumble unavailable through the YouGame app on this system"); });
}
void gcadapter_shutdown() {
  std::lock_guard<std::mutex> lock(g_app_mutex);
  g_app_mirror.reset();
  g_poll_rate_hz.store(0.0, std::memory_order_relaxed);
}
bool steam_running() { return false; }
}
