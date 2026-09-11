// Native Melee port entry point.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "host.h"
#include "render_observer.h"
#include "exi_slippi.h"
#include "slippi_online.h"
#include "slippi_net.h"
#include "audio.h"
#include "functions.h"
#include "guest_symbols.h"
#include "gx_core.h"
#include "gx_d3d12.h"
#include "threaded_backend.h"
#include "window.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

namespace ppc { void init_dispatch(); }
namespace guest {
struct NameEntry { uint32_t addr; const char* name; };
extern const NameEntry name_table[];
extern const size_t name_table_count;
}

static void usage() {
  std::printf("melee_port --iso <path> [--frames N] [--fast] [--headless] [--scale N|auto] [--window WxH] [--vsync]\n"
              "           [--fps N|unlocked] [--frame-mode extrapolate|interpolate|authored|off] [--threaded-renderer]\n"
              "           [--volume 0-100] [--audio-dump out.wav]\n"
              "           [--capture out.ppm --capture-frame N] [--trace-calls] [--quiet]\n");
}

int main(int argc, char** argv) {
  host::Options& o = host::options;
  bool headless = false, hidden = false, threaded = false, fps_requested = false;
  gx::D3D12Options gfx;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> const char* { if (i + 1 >= argc) { usage(); std::exit(2); } return argv[++i]; };
    if (a == "--iso") o.iso = next();
    else if (a == "--state-trace") o.state_trace = next();
    else if (a == "--frames") o.frames = (uint32_t)std::strtoul(next(), nullptr, 0);
    else if (a == "--fast") o.fast = true;
    else if (a == "--headless") headless = true;
    else if (a == "--hidden") hidden = true;
    else if (a == "--threaded-renderer") threaded = true;
    else if (a == "--fps") {   // display rate: N or "unlocked"; enables the render thread
      std::string v = next(); gfx.fps_cap = v == "unlocked" ? 0 : std::atoi(v.c_str());
      if (v != "unlocked" && gfx.fps_cap < 1) { usage(); return 2; }
      threaded = true;
      fps_requested = true;
    }
    else if (a == "--frame-mode") {
      std::string v = next();
      if (v == "extrapolate") gfx.subframe = gx::SubFrameMode::Extrapolate;
      else if (v == "interpolate") gfx.subframe = gx::SubFrameMode::Interpolate;
      else if (v == "authored") gfx.subframe = gx::SubFrameMode::Authored;
      else if (v == "off") gfx.subframe = gx::SubFrameMode::Off;
      else { usage(); return 2; }
      threaded = true;
    }
    else if (a == "--scale") { std::string v = next(); gfx.efb_scale = v == "auto" ? 0 : std::atoi(v.c_str()); if (v != "auto" && gfx.efb_scale < 1) { usage(); return 2; } }
    else if (a == "--window") { if (std::sscanf(next(), "%dx%d", &gfx.window_w, &gfx.window_h) != 2 || gfx.window_w < 320 || gfx.window_h < 240) { usage(); return 2; } }
    else if (a == "--vsync") gfx.vsync = true;
    else if (a == "--capture") gfx.capture_path = next();
    else if (a == "--capture-frame") gfx.capture_frame = (uint32_t)std::strtoul(next(), nullptr, 0);
    else if (a == "--capture-every") gfx.capture_every = (uint32_t)std::strtoul(next(), nullptr, 0);
    else if (a == "--capture-burst") gfx.capture_burst = (uint32_t)std::strtoul(next(), nullptr, 0);
    else if (a == "--capture-sim-frame") gfx.capture_sim_frame = std::strtoull(next(), nullptr, 0);
    else if (a == "--script") { if (!host::input_load_script(next())) { std::fprintf(stderr, "cannot load input script\n"); return 1; } }
    else if (a == "--dump") gfx.dump_path = next();
    else if (a == "--shader-cache") gfx.shader_cache = next();
    else if (a == "--trace-func") { const char* spec = next(); uint32_t addr = (uint32_t)std::strtoul(spec, nullptr, 16); uint32_t limit = 40;
      if (const char* colon = std::strchr(spec, ':')) limit = (uint32_t)std::strtoul(colon + 1, nullptr, 10);
      if (!addr) { std::string name(spec, std::strchr(spec, ':') ? std::strchr(spec, ':') - spec : std::strlen(spec));
        for (size_t i = 0; i < guest::name_table_count; ++i) if (name == guest::name_table[i].name) { addr = guest::name_table[i].addr; break; } }
      if (!addr) { std::fprintf(stderr, "unknown function %s\n", spec); return 2; }
      ppc::add_trace_func(addr, limit); }
    else if (a == "--sys-dir") o.sys_dir = next();
    else if (a == "--replay-dir") o.replay_dir = next();
    else if (a == "--user-dir") slippi::online::config().user_dir = next();
    else if (a == "--online-delay") slippi::online::config().delay = std::atoi(next());
    else if (a == "--chat") { std::string v = next(); slippi::online::config().chat = v == "off" ? 2 : v == "direct" ? 1 : 0; }
    else if (a == "--netplay-port") slippi::Matchmaking::forced_port = (uint16_t)std::atoi(next());
    else if (a == "--local-peer") {
      // idx:local_port:remote_ip:remote_port; two instances peer directly without the matchmaking server.
      std::string v = next(); auto& lp = slippi::Matchmaking::local_peer;
      size_t a1 = v.find(':'), a2 = v.find(':', a1 + 1), a3 = v.find(':', a2 + 1);
      if (a1 == std::string::npos || a2 == std::string::npos || a3 == std::string::npos) { std::fprintf(stderr, "--local-peer idx:port:ip:port"); return 2; }
      lp.enabled = true; lp.local_index = std::atoi(v.substr(0, a1).c_str()); lp.local_port = (uint16_t)std::atoi(v.substr(a1 + 1, a2 - a1 - 1).c_str());
      lp.remote_ip = v.substr(a2 + 1, a3 - a2 - 1); lp.remote_port = (uint16_t)std::atoi(v.substr(a3 + 1).c_str()); }
    else if (a == "--dump-frame") gfx.dump_frame = (uint32_t)std::strtoul(next(), nullptr, 0);
    else if (a == "--trace-calls") o.trace_calls = true;
    else if (a == "--quiet") o.quiet = true;
    else if (a == "--time-base") o.time_base = std::strtoull(next(), nullptr, 0);
    else if (a == "--volume") o.volume = std::atoi(next());
    else if (a == "--hang-watch") o.hang_watch = std::atof(next());
    else if (a == "--audio-dump") o.audio_dump = next();
    else { usage(); return 2; }
  }
  if (fps_requested && gfx.subframe == gx::SubFrameMode::Off) {
    std::fprintf(stderr, "--fps requires explicit experimental --frame-mode interpolate, extrapolate or authored\n");
    return 2;
  }
  if (o.iso.empty()) { usage(); return 2; }
  if (!host::disc_open(o.iso)) { std::fprintf(stderr, "cannot open ISO %s\n", o.iso.c_str()); return 1; }

  std::unique_ptr<gx::Backend> backend;
  if (!headless && threaded) {
    backend = gx::create_threaded_backend(gfx, !hidden);
  } else if (!headless) {
    void* hwnd = host::window_create(gfx.window_w, gfx.window_h, L"Melee Port (development)", !hidden);
    backend.reset(gx::create_d3d12_backend(hwnd, gfx.window_w, gfx.window_h, gfx));
    host::window_set_resize_callback([renderer = backend.get()](int w, int h) { gx::d3d12_resize(renderer, w, h); });
    host::g_has_window = true;
  }
  gx::set_authored_capture(gfx.subframe == gx::SubFrameMode::Authored);
  if (o.hang_watch > 0) ppc::start_hang_watch(host::cpu, o.hang_watch);
  gx::init(backend.get());
  host::audio_open(o.volume, o.audio_dump.c_str(), !headless);

  ppc::init_dispatch();
  host::boot_setup();
  host::log("boot: entering __start at %08X", 0x8000522Cu);
  int code = 0;
  try {
    ppc::call(*host::cpu, host::ram, 0x8000522Cu);
    host::log("guest returned from __start after %u retraces", host::retrace_count());
  } catch (const ExitRequested& stop) {
    backend.reset();
    code = stop.code;
  } catch (const LoadContextUnwind&) {
    host::log("OSLoadContext reached top level");
  }
  host::log("audio: %llu frames played, %llu blocks dropped", (unsigned long long)host::audio_pushed_frames(), (unsigned long long)host::audio_dropped_blocks());
  host::audio_close();
  slippi::shutdown();
  { uint64_t calls = 0, insns = 0; ppc::interpreter_stats(&calls, &insns);
    if (calls) host::log("interpreter: %llu calls into RAM-resident code, %llu instructions", (unsigned long long)calls, (unsigned long long)insns); }
  host::log("slippi: %llu EXI commands, %llu replays written, GCT at %08X", (unsigned long long)slippi::commands_seen(),
            (unsigned long long)slippi::replays_written(), slippi::gct_load_address());
  return code;
}
