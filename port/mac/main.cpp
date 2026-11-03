// Experimental macOS offline host; shared native game services and GX renderer.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_services.h"
#include "platform_paths.h"
#include <exception>
#include <filesystem>
#ifdef MELEE_LINUX_GRAPHICS
#include "renderer_gl.h"
#include "gx_host_compat.h"
#include "renderer_presenter.h"
#include <SDL.h>
#endif
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
#include <cpuid.h>
#include <sys/sysctl.h>
#include "cpu_check.h"
#include <csignal>
#include <execinfo.h>
#include <unistd.h>
namespace {
void crash_trace(int signal) {
  static constexpr char message[] = "Mac host: fatal signal; host backtrace follows\n";
  write(STDERR_FILENO, message, sizeof(message)-1);
  void* frames[48];
  const int count = backtrace(frames, 48);
  backtrace_symbols_fd(frames, count, STDERR_FILENO);
  _Exit(128 + signal);
}
#ifdef MELEE_LINUX_GRAPHICS
std::filesystem::path shader_cache_override;   // --shader-cache, from the YouGame launch spec
constexpr bool threaded_presenter = false;   // see the presentation.open call in main
std::filesystem::path shader_cache() {
  if (!shader_cache_override.empty()) return shader_cache_override;
  if (const char* cache = std::getenv("XDG_CACHE_HOME"); cache && *cache && std::filesystem::path(cache).is_absolute())
    return std::filesystem::path(cache) / "melee-unlocked/shaders";
  if (const char* home = std::getenv("HOME"); home && *home)
    return std::filesystem::path(home) / ".cache/melee-unlocked/shaders";
  return std::filesystem::temp_directory_path() / "melee-unlocked-shaders";
}
struct Presentation {
  SDL_Window* window = nullptr;
  std::unique_ptr<gx::Backend> backend;
  ~Presentation() { close(); }
  void close() {
    gx::init(nullptr);
    backend.reset();
    if (window) SDL_DestroyWindow(window);
    window = nullptr;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
  }
  void open_window(bool hidden) {
    std::fprintf(stderr, "Mac video: initializing SDL (hidden=%d)\n", hidden);
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    window = SDL_CreateWindow("Melee Unlocked " MELEE_PORT_VERSION " - Experimental, not tested yet",
       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 720,
       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | (hidden ? SDL_WINDOW_HIDDEN : 0));
    if (!window) throw std::runtime_error(SDL_GetError());
  }
  void open(bool hidden, bool threaded = false, double fps = 120) {
    open_window(hidden);
    backend = threaded ? linux_renderer::create_threaded(window, shader_cache(), 1, fps, false)
                       : linux_renderer::create(window, shader_cache(), 1, !hidden);
    if (!backend) throw std::runtime_error("OpenGL renderer initialization failed");
    linux_renderer::connect_host();
    gx::init(backend.get());
  }
};
std::exception_ptr video_failure;
void fifo(const uint8_t* bytes, uint32_t size, void*) {
  try { gx::write_fifo_bytes(bytes, size); }
  catch (...) { video_failure = std::current_exception(); linux_host::request_stop(1); }
}
#endif
uint64_t number(const char* text) {
  if (!text || !*text || *text=='-') throw std::runtime_error("invalid unsigned number");
  char* end=nullptr; errno=0; auto n=std::strtoull(text,&end,0);
  if (errno || *end) throw std::runtime_error("invalid unsigned number");
  return n;
}
bool cpu_ok() { return mac_host::cpu_ok(); }

struct Observation {
  std::ofstream digest;
#ifdef MELEE_LINUX_GRAPHICS
  gx::Backend* backend = nullptr;
  std::filesystem::path capture_dir;
  unsigned capture_index = 0;
#endif
};
void observe(const MuStatePod& state,const linux_host::Counters& counts,void* user) {
  auto& observation = *static_cast<Observation*>(user);
  if (observation.digest.is_open()) {
    auto& out = observation.digest;
    out<<counts.retraces<<','<<state.scene_major<<','<<state.scene<<','<<state.match_frame<<','<<state.rng;
    for (const auto& f:state.player)
      out<<','<<f.present<<','<<f.stocks<<','<<f.action<<','<<f.anim_frame<<','<<f.pos_x<<','<<f.pos_y<<','<<f.pos_z<<','<<f.vel_x<<','<<f.vel_y<<','<<f.vel_z<<','<<f.percent<<','<<f.facing;
    out<<'\n';
  }
#ifdef MELEE_LINUX_GRAPHICS
  constexpr unsigned frames[] = {30, 300, 600, 900};
  if (observation.backend && !observation.capture_dir.empty() && observation.capture_index < 4 &&
      state.scene_major == 2 && state.match_frame >= frames[observation.capture_index]) {
    try {
      const auto path = observation.capture_dir / ("match-" + std::to_string(state.match_frame) + ".ppm");
      linux_renderer::capture_efb(*observation.backend, path);
      std::fprintf(stderr, "Mac capture: match %u, retrace %u: %s\n", state.match_frame, counts.retraces, path.c_str());
      ++observation.capture_index;
    } catch (...) { video_failure = std::current_exception(); linux_host::request_stop(1); }
  }
#endif
}
}
int main(int argc,char** argv) {
  const char* dev_spike = std::getenv("MELEE_MAC_DEV_SPIKE");
  if (!dev_spike || std::string(dev_spike) != "1") {
    std::fputs("Mac dev spike: set MELEE_MAC_DEV_SPIKE=1 to run this development-only host\n", stderr);
    return 2;
  }
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--vanilla-only") {
      std::fputs("Mac dev spike: --vanilla-only is unsupported; use the build_full.py host\n", stderr);
      return 2;
    }
  }
  std::signal(SIGABRT, crash_trace);
  try {
    if (!cpu_ok()) throw std::runtime_error("Requires Rosetta on macOS 15+, or an Intel Mac with AVX2/FMA and OS AVX support");
    linux_host::Config cfg;
    const auto resources = mac_host::resources();
    cfg.game_path = (resources / "melee_game.so").string();
    const auto coefficients = resources / "Sys/GC/dsp_coef.bin";
    if (!std::getenv("MELEE_DSP_COEF") && std::filesystem::is_regular_file(coefficients))
      setenv("MELEE_DSP_COEF", coefficients.c_str(), 0);
    cfg.no_gc_adapter=true;
    std::string digest_path, graphics_capture, capture_path, capture_dir;
    bool presenter_test = false, hidden = false;
    double display_fps = 60;
    for (int i=1;i<argc;++i) {
      const std::string arg=argv[i];
      auto next=[&]() { if (++i>=argc) throw std::runtime_error("missing value after "+arg); return argv[i]; };
      if (arg=="--help" || arg=="-h") {
        std::puts("Melee Unlocked - Experimental, not tested yet\n"
          "MeleeUnlocked --iso <disc> [--hidden] [--headless] [--frames N] [--fast]\n"
          "  [--script file] [--card-dir path] [--log-file path] [--state-digest path]\n"
          "  [--capture file.ppm] [--capture-frames directory] [--fps 60..1000]\n"
          "Keyboard port 1: arrows stick; Z/X/C/V A/B/X/Y; Enter Start; Q/E L/R; S Z; I/J/K/L C-stick.\n"
          "SDL game controllers: first connected controller is port 1. Close the window to exit.");
        return 0;
      }
      if (arg=="--version") { std::puts("Melee Unlocked " MELEE_PORT_VERSION " macOS x86-64 - Experimental, not tested yet"); return 0; }
      if (arg=="--memory-self-test") {
        std::string error;
        if (!linux_host::reserve_memory(&error)) throw std::runtime_error(error);
        auto* p=reinterpret_cast<volatile uint8_t*>(linux_host::mem1_base);
        p[0]=0x5a; p[linux_host::mem1_bytes-1]=0xa5;
        if (p[0]!=0x5a || p[linux_host::mem1_bytes-1]!=0xa5) throw std::runtime_error("MEM1 write failed");
        linux_host::shutdown(); std::puts("Mac fixed memory: PASS"); return 0;
      }
      if (arg=="--host-self-test") {
        std::string error;
        if (!linux_host::service_self_test(next(),&error)) throw std::runtime_error(error);
        return 0;
      }
      if (arg=="--iso") cfg.iso_path=next();
      else if (arg=="--game-library") cfg.game_path=next();
      else if (arg=="--headless") cfg.headless=true;
      else if (arg=="--hidden") hidden=true;
      else if (arg=="--graphics-self-test") graphics_capture=next();
      else if (arg=="--presenter-self-test") presenter_test=true;
      else if (arg=="--capture") capture_path=next();
      else if (arg=="--capture-frames") capture_dir=next();
      else if (arg=="--fps") {
        display_fps=number(next());
        if (display_fps<60 || display_fps>1000) throw std::runtime_error("--fps must be 60..1000");
      }
      else if (arg=="--fast") cfg.pace=false;
      else if (arg=="--vanilla-game") cfg.vanilla_game=true;
      else if (arg=="--no-gc-adapter") cfg.no_gc_adapter=true;
      else if (arg=="--script" || arg=="--input-script") cfg.input_script=next();
      else if (arg=="--card-dir") cfg.card_dir=next();
      else if (arg=="--log-file") cfg.log_path=next();
#ifdef MELEE_LINUX_GRAPHICS
      else if (arg=="--shader-cache") shader_cache_override=next();
#endif
      // The YouGame launch spec is shared with the Windows build. This offline Mac host has no settings
      // panel, Slippi, replays or mods yet, so it accepts those options and ignores them.
      else if (arg=="--threaded-renderer") {}
      else if (arg=="--settings-path" || arg=="--sys-dir" || arg=="--user-dir" || arg=="--replay-dir") next();
      else if (arg=="--trace-state") cfg.trace_state=true;
      else if (arg=="--state-digest") digest_path=next();
      else if (arg=="--frames") {
        auto n=number(next()); if (n>UINT32_MAX) throw std::runtime_error("frames overflow"); cfg.max_frames=n;
      } else if (arg=="--rng-seed") {
        auto n=number(next()); if (n>UINT32_MAX) throw std::runtime_error("seed overflow"); cfg.has_rng_seed=true; cfg.rng_seed=n;
      } else if (arg=="--time-base") { cfg.has_time_base=true; cfg.time_base=number(next()); }
      else throw std::runtime_error("unknown option: "+arg);
    }
#ifdef MELEE_LINUX_GRAPHICS
    if (presenter_test) {
      Presentation presentation;
      presentation.open_window(true);
      if (!linux_renderer::presenter_self_test(presentation.window, shader_cache() / "presenter-test"))
        throw std::runtime_error("Mac presenter self-test failed");
      std::puts("Mac presenter: PASS"); return 0;
    }
    if (!graphics_capture.empty()) {
      std::string error;
      if (!linux_host::reserve_memory(&error)) throw std::runtime_error(error);
      Presentation presentation;
      presentation.open(true);
      if (!linux_renderer::smoke_test(*presentation.backend, graphics_capture))
        throw std::runtime_error("Mac graphics self-test failed");
      std::puts("Mac graphics: PASS"); return 0;
    }
#else
    if (!cfg.headless) throw std::runtime_error("build has no graphics; use --headless");
#endif
    if (cfg.iso_path.empty()) throw std::runtime_error("requires --iso <NTSC 1.02 disc>");
#ifdef MELEE_LINUX_GRAPHICS
    // macOS: bring up SDL video (AppKit) before the host starts SDL events, controllers and audio;
    // the other order hangs inside SDL_InitSubSystem(SDL_INIT_VIDEO).
    if (!cfg.headless && SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) throw std::runtime_error(SDL_GetError());
#endif
    std::string error;
    if (!linux_host::initialize(cfg,&error)) throw std::runtime_error(error);
#ifdef MELEE_LINUX_GRAPHICS
    Presentation presentation;
    if (!cfg.headless) {
      // The threaded presenter creates its GL context on a render thread, which deadlocks against
      // AppKit on macOS; the experimental build presents from the main thread at 60 Hz.
      presentation.open(hidden, threaded_presenter, display_fps);
      linux_host::set_video_sinks(fifo, nullptr, nullptr);
    }
#endif
    Observation observation;
    if (!digest_path.empty()) {
      observation.digest.open(digest_path);
      if (!observation.digest) throw std::runtime_error("cannot open digest");
    }
#ifdef MELEE_LINUX_GRAPHICS
    observation.backend = presentation.backend.get();
    observation.capture_dir = capture_dir;
    if (!capture_dir.empty()) std::filesystem::create_directories(capture_dir);
#endif
    linux_host::set_frame_observer(observe, &observation);
    auto start=std::chrono::steady_clock::now();
    int result=linux_host::run();
#ifdef MELEE_LINUX_GRAPHICS
    if (video_failure) std::rethrow_exception(video_failure);
    if (!capture_path.empty() && presentation.backend)
      linux_renderer::capture_efb(*presentation.backend, capture_path);
    if (presentation.backend && threaded_presenter) {   // only the threaded presenter keeps these counts
      const auto stats = linux_renderer::presenter_stats(*presentation.backend);
      std::printf("Mac display: %llu source, %llu display, %llu authored subframes, %llu posed draws\n",
        (unsigned long long)stats.simulation, (unsigned long long)stats.presentation,
        (unsigned long long)stats.subframes, (unsigned long long)stats.posed);
    }
    presentation.close();
#endif
    const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    const auto stats=linux_host::counters();
    std::printf("Mac host: %u retraces; loop wall %.6f s, %.6f ms/retrace (includes callbacks, disc I/O, digest)\n",stats.retraces,seconds,stats.retraces ? seconds*1000/stats.retraces : 0);
    std::printf("Mac host: %llu disc reads, %llu FIFO bytes, %llu audio blocks\n",static_cast<unsigned long long>(stats.disc_reads),static_cast<unsigned long long>(stats.fifo_bytes),static_cast<unsigned long long>(stats.audio_blocks));
    linux_host::set_frame_observer(nullptr,nullptr);
    linux_host::shutdown();
    return result;
  } catch (const std::exception& e) {
    std::fprintf(stderr,"Mac host: %s\n",e.what()); linux_host::shutdown(); return 1;
  }
}
