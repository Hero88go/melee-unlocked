// Render thread: owns the window and the D3D12 backend, consumes simulation frames from a bounded
// queue, and presents on its own timeline. With a sub-frame mode enabled it renders new frames
// between 60 Hz simulation frames from re-posed geometry (see subframe.h); the simulation is never
// touched. The bounded source queue can back-pressure simulation when rendering is slow.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "threaded_backend.h"
#include "frame_queue.h"
#include "gx_d3d12.h"
#include "host.h"
#include "subframe.h"
#include "authored_pose.h"
#include "window.h"
#include <algorithm>
#include <chrono>
#include <string>
#include <cstdio>
#include <cstring>
#include <future>
#include <thread>
namespace gx {
namespace {

constexpr double SIM_PERIOD = 1.0 / 60.0;

class ThreadedBackend final : public Backend {
  FrameQueue queue;
  std::thread worker;
  D3D12Options options_;

  // Runs on the render thread.
  void present_loop(Backend* renderer) {
    SubFrameSolver solver;
    std::vector<DrawMatrices> overrides;
    Frame frames[2];             // ring: previous and current simulation frames
    int cur = -1;                // index of the current frame in `frames`, -1 until the first arrives
    bool have_prev = false;
    uint64_t rendered_sequence = 0, submitted = 0, presented = 0, burst_logged = 0;
    const bool subframes = options_.subframe != SubFrameMode::Off;
    const bool authored = options_.subframe == SubFrameMode::Authored;
    const bool interpolate = authored || options_.subframe == SubFrameMode::Interpolate;
    const double cap_period = options_.fps_cap > 0 ? 1.0 / options_.fps_cap : 0.0;
    double next_present = host::now_seconds();
    double stats_time = next_present; uint64_t stats_presented = 0, stats_sim = 0, stats_lines = 0;
    uint32_t phase_bins[5] = {};
    double build_seconds = 0, submit_seconds = 0; uint64_t cost_presented = 0;   // presented phases: [0,.25) [.25,.5) [.5,.75) [.75,1) exactly 1
    for (;;) {
      host::window_pump();
      if (host::window_closed()) { queue.finish(true); break; }
      // Render every source at least once: EFB resources can depend on earlier commands.
      bool got_new = false;
      Frame incoming;
      if ((cur < 0 || frames[cur].sequence == rendered_sequence) && queue.try_pop(incoming)) {
        int next = cur < 0 ? 0 : cur ^ 1;
        frames[next] = std::move(incoming);
        have_prev = cur >= 0;
        cur = next;
        got_new = true;
        ++submitted; ++stats_sim;
      }
      if (cur < 0) {
        if (queue.drained()) break;
        queue.wait_available(std::chrono::milliseconds(2));
        continue;
      }
      if (got_new && subframes) solver.set_frames(have_prev ? &frames[cur ^ 1] : nullptr, &frames[cur]);
      const Frame& current = frames[cur];
      bool should_render;
      double t = 0.0;
      if (!subframes) {
        should_render = current.sequence != rendered_sequence;   // once per simulation frame
        if (!should_render) {
          if (queue.drained()) break;
          queue.wait_available(std::chrono::milliseconds(2));
          continue;
        }
      } else {
        double now = host::now_seconds();
        t = (now - current.time) / SIM_PERIOD;
        if (interpolate) t = std::min(std::max(t, 0.0), 1.0);
        else t = std::min(std::max(t, 0.0), 1.0);   // never extrapolate more than one frame ahead
        if (cap_period > 0 && now < next_present) {
          // Wait for the cap deadline, but wake early for a new simulation frame.
          double wait = next_present - now;
          if (wait > 0.0005) queue.wait_available(std::chrono::microseconds((long long)((wait - 0.0003) * 1e6)));
          continue;
        }
        should_render = true;
        if (queue.drained() && current.sequence == rendered_sequence) break;
      }
      if (options_.capture_burst && options_.capture_sim_frame && current.sequence >= options_.capture_sim_frame && burst_logged < options_.capture_burst) {
        host::log("present %llu: sim frame %llu phase %.3f", presented + 1, (unsigned long long)current.sequence, t);
        ++burst_logged;
      }
      if (subframes && have_prev) {
        double t0 = host::now_seconds();
        solver.build(t, interpolate, overrides, authored);
        double t1 = host::now_seconds();
        renderer->submit_frame(current, overrides.data());
        build_seconds += t1 - t0; submit_seconds += host::now_seconds() - t1; ++cost_presented;
      } else {
        double t1 = host::now_seconds();
        renderer->submit_frame(current);
        submit_seconds += host::now_seconds() - t1; ++cost_presented;
      }
      rendered_sequence = current.sequence;
      ++presented; ++stats_presented;
      ++phase_bins[t >= 1.0 ? 4 : (int)(t * 4.0)];
      if (cap_period > 0) {
        double now = host::now_seconds();
        next_present = std::max(next_present + cap_period, now - cap_period);
      }
      double now = host::now_seconds();
      if (now - stats_time >= 1.0) {
        const SubFrameStats& s = solver.stats();
        wchar_t title[160];
        _snwprintf_s(title, _TRUNCATE, L"Melee Port  |  sim %.0f Hz  |  display %.0f fps  |  %s  |  draws %u paired %u",
                     stats_sim / (now - stats_time), stats_presented / (now - stats_time),
                     !subframes ? L"locked" : authored ? L"authored" : interpolate ? L"interpolate" : L"extrapolate", s.draws, s.paired);
        host::window_set_title(title);
        if (++stats_lines % 5 == 0) {
          host::log("display: %.0f fps (sim %.0f Hz, %s, %u draws, %u paired, %u cuts)", stats_presented / (now - stats_time), stats_sim / (now - stats_time),
                    !subframes ? "locked" : authored ? "authored" : interpolate ? "interpolate" : "extrapolate", s.draws, s.paired, s.cuts);
          if (subframes) host::log("pair rejection: missing %u, HUD %u, geometry %u, state %u (last BP %02X), projection %u, authored %u | phases <.25:%u <.5:%u <.75:%u <1:%u =1:%u",
                                   s.missing, s.hud, s.geometry, s.state, s.state_register, s.projection, s.authored, phase_bins[0], phase_bins[1], phase_bins[2], phase_bins[3], phase_bins[4]);
          if (subframes) std::memset(phase_bins, 0, sizeof phase_bins);
          host::log("render cost: solver %.2f ms/frame, submit %.2f ms/frame (%s)", 1000.0 * build_seconds / std::max<uint64_t>(1, cost_presented), 1000.0 * submit_seconds / std::max<uint64_t>(1, cost_presented), d3d12_profile_line().c_str());
          build_seconds = submit_seconds = 0; cost_presented = 0;
          if (authored) {
            const AuthoredStats& a = authored_stats();
            std::string line = "authored: captured " + std::to_string(a.captured) + " sampled " + std::to_string(a.sampled) + " | capture fails:";
            for (int i = 1; i < 24; ++i) if (a.capture[i]) line += " c" + std::to_string(i) + "=" + std::to_string(a.capture[i]);
            line += " | sample fails:";
            for (int i = 1; i < 24; ++i) if (a.sample[i]) line += " s" + std::to_string(i) + "=" + std::to_string(a.sample[i]);
            host::log("%s", line.c_str());
          }
        }
        stats_time = now; stats_presented = 0; stats_sim = 0;
      }
    }
    host::log("renderer: %llu simulation frames, %llu presented frames on its own thread", submitted, presented);
  }

 public:
  ThreadedBackend(D3D12Options options, bool visible) : options_(options) {
    std::promise<void> initialized;
    auto ready = initialized.get_future();
    worker = std::thread([this, options, visible, init = std::move(initialized)]() mutable {
      bool started = false;
      try {
        void* window = host::window_create(options.window_w, options.window_h, L"Melee Port (development)", visible);
        std::unique_ptr<Backend> renderer(create_d3d12_backend(window, options.window_w, options.window_h, options));
        host::window_set_resize_callback([&renderer](int w, int h) { d3d12_resize(renderer.get(), w, h); });
        init.set_value(); started = true;
        present_loop(renderer.get());
        host::window_set_resize_callback({});
        renderer.reset();
        host::window_destroy();
      } catch (...) {
        if (!started) init.set_exception(std::current_exception());
        else host::request_exit(3);
        queue.finish(true);
      }
    });
    try { ready.get(); }
    catch (...) { queue.finish(true); worker.join(); throw; }
  }
  ~ThreadedBackend() override { queue.finish(); worker.join(); }
  void submit_frame(const Frame& frame) override {
    if (!queue.push(frame)) throw ExitRequested{host::exit_code()};
  }
};
}
std::unique_ptr<Backend> create_threaded_backend(const D3D12Options& options, bool visible) {
  return std::make_unique<ThreadedBackend>(options, visible);
}
}
