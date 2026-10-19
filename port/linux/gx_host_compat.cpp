// The GX decoder is shared with the Windows host; only these machine services
// are needed for native rendering. No translated PPC execution is linked here.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gx_host_compat.h"
#include "gx_core.h"
#include "host_services.h"
#include "host.h"
#include "render_observer.h"
#include "authored_pose.h"
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace host {
uint8_t* ram = reinterpret_cast<uint8_t*>(linux_host::mem1_base);
uint32_t ram_size = linux_host::mem1_bytes;
uint8_t* game_image = reinterpret_cast<uint8_t*>(linux_host::game_image_base);
uint32_t game_image_size = 0;
const double tsc_seconds = 0;
uint8_t* try_ptr(uint32_t address, uint32_t size) { return linux_host::memory_pointer(address, size); }
uint8_t* ptr(uint32_t address, uint32_t size) {
  uint8_t* memory = try_ptr(address, size);
  if (!memory) die("Linux GX address outside game memory: %08X+%X", address, size);
  return memory;
}
uint8_t rd8(uint32_t address) { return *ptr(address, 1); }
uint16_t rd16(uint32_t address) { const auto* p = ptr(address, 2); return (uint16_t(p[0]) << 8) | p[1]; }
uint32_t rd32(uint32_t address) { const auto* p = ptr(address, 4); return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
void wr8(uint32_t address, uint8_t value) { *ptr(address, 1) = value; }
void wr16(uint32_t address, uint16_t value) { auto* p = ptr(address, 2); p[0] = value >> 8; p[1] = value; }
void wr32(uint32_t address, uint32_t value) { auto* p = ptr(address, 4); p[0] = value >> 24; p[1] = value >> 16; p[2] = value >> 8; p[3] = value; }
[[noreturn]] void die(const char* format, ...) {
  char message[2048]; va_list args; va_start(args, format); vsnprintf(message, sizeof message, format, args); va_end(args);
  throw std::runtime_error(message);
}
double now_seconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
TickTiming& tick_timing() { static TickTiming timing; return timing; }
void sim_cost_add(int, double) {}
// mu_gx.c delivers PE interrupts after draining a FIFO batch. Repeating them here
// would deliver the same native game's DrawDone/DrawSync completion twice.
void set_pe_finish_pending() {}
void set_pe_token_pending(uint16_t) {}
void note_frame_submitted() {}
void note_scene_memory() {}
void current_scene(uint32_t* major, uint32_t* minor, uint32_t* match_frame) {
  MuStatePod state{};
  if (linux_host::state_snapshot(&state)) { *major = state.scene_major; *minor = state.scene; *match_frame = state.match_frame; }
  else { *major = 0; *minor = 0; *match_frame = 0; }
}
void input_last_pads(PadState out[4]) { std::memset(out, 0, sizeof(PadState) * 4); }
} // namespace host

namespace ppc {
std::atomic<uint8_t> g_ram_watched[RAM_WATCH_COUNT]{};
std::atomic<uint32_t> g_ram_versions[RAM_WATCH_COUNT]{};
} // namespace ppc
namespace gx {
// These observers are for the translated PPC host. Native GX carries its own
// ownership/identity tokens, and native_pose_bridge handles optional pose data.
uint64_t observed_draw_identity(uint64_t fallback, uint64_t& generation) { generation = 0; return fallback; }
void finish_observed_frame() {}
void set_authored_capture(bool) {}
bool authored_capture_enabled() { return false; }
std::shared_ptr<const AuthoredPose> capture_authored_pose() { return {}; }
void set_owner_tracking(bool) {}
bool owner_tracking_enabled() { return false; }
uint8_t observed_owner() { return 0xFF; }
bool observed_skinned() { return false; }
#ifndef MELEE_LINUX_UNLOCKED
AuthoredStats& authored_stats() { static AuthoredStats stats; return stats; }
#endif
void settings_guest_options_frame(uint8_t, uint16_t, uint32_t) {}
} // namespace gx
namespace replay_control { bool skip_render() { return false; } }

namespace linux_renderer {
void connect_host() {
  host::game_image_size = linux_host::game_image_size();
  gx::set_native_owner_source(true);
}
} // namespace linux_renderer
