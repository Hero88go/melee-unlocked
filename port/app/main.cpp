// Native Melee port entry point.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "host.h"
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

static void usage() {
  std::printf("melee_port --iso <path> [--frames N] [--fast] [--headless] [--scale N] [--vsync]\n"
              "           [--capture out.ppm --capture-frame N] [--trace-calls] [--quiet]\n");
}

int main(int argc, char** argv) {
  host::Options& o = host::options;
  bool headless = false, hidden = false, threaded = false;
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
    else if (a == "--scale") gfx.efb_scale = std::atoi(next());
    else if (a == "--vsync") gfx.vsync = true;
    else if (a == "--capture") gfx.capture_path = next();
    else if (a == "--capture-frame") gfx.capture_frame = (uint32_t)std::strtoul(next(), nullptr, 0);
    else if (a == "--capture-every") gfx.capture_every = (uint32_t)std::strtoul(next(), nullptr, 0);
    else if (a == "--script") { if (!host::input_load_script(next())) { std::fprintf(stderr, "cannot load input script\n"); return 1; } }
    else if (a == "--dump") gfx.dump_path = next();
    else if (a == "--dump-frame") gfx.dump_frame = (uint32_t)std::strtoul(next(), nullptr, 0);
    else if (a == "--trace-calls") o.trace_calls = true;
    else if (a == "--quiet") o.quiet = true;
    else if (a == "--time-base") o.time_base = std::strtoull(next(), nullptr, 0);
    else { usage(); return 2; }
  }
  if (o.iso.empty()) { usage(); return 2; }
  if (gfx.efb_scale < 1) gfx.efb_scale = 1;
  if (!host::disc_open(o.iso)) { std::fprintf(stderr, "cannot open ISO %s\n", o.iso.c_str()); return 1; }

  std::unique_ptr<gx::Backend> backend;
  if (!headless && threaded) {
    backend = gx::create_threaded_backend(gfx, !hidden);
  } else if (!headless) {
    void* hwnd = host::window_create(1280, 960, L"Melee Port (development)", !hidden);
    backend.reset(gx::create_d3d12_backend(hwnd, 1280, 960, gfx));
    host::window_set_resize_callback([renderer = backend.get()](int w, int h) { gx::d3d12_resize(renderer, w, h); });
    host::g_has_window = true;
  }
  gx::init(backend.get());

  ppc::init_dispatch();
  host::boot_setup();
  host::log("boot: entering __start at %08X", 0x8000522Cu);
  try {
    ppc::call(*host::cpu, host::ram, 0x8000522Cu);
  } catch (const ExitRequested& stop) {
    backend.reset();
    return stop.code;
  } catch (const LoadContextUnwind&) {
    host::log("OSLoadContext reached top level");
  }
  host::log("guest returned from __start after %u retraces", host::retrace_count());
  return 0;
}
