// Render thread: owns the window and the D3D12 backend, consumes simulation frames from a bounded
// queue, and presents on its own timeline. With a sub-frame mode enabled it renders new frames
// between 60 Hz simulation frames from re-posed geometry (see subframe.h); the simulation is never
// touched. The bounded source queue can back-pressure simulation when rendering is slow.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "threaded_backend.h"
#include "frame_queue.h"
#include "gx_backend.h"
#include "render_options.h"
#include "host.h"
#include "subframe.h"
#include "authored_pose.h"
#include "window.h"
#include "gx_core.h"
#include "replay_control.h"
#include <algorithm>
#include <chrono>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <cmath>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace gx {
namespace {

constexpr double SIM_PERIOD = 1.0 / 60.0;

class ThreadedBackend final : public Backend {
  FrameQueue queue;
  std::thread worker;
  RenderOptions options_;

  // Runs on the render thread.
  void present_loop(Backend* renderer) {
    SubFrameSolver solver;
    std::vector<DrawMatrices> overrides;
    Frame frames[2];             // ring: previous and current simulation frames
    int cur = -1;                // index of the current frame in `frames`, -1 until the first arrives
    bool have_prev = false;
    uint64_t rendered_sequence = 0, submitted = 0, presented = 0, burst_logged = 0;
    bool subframes = options_.subframe != SubFrameMode::Off;
    bool authored = options_.subframe == SubFrameMode::Authored || options_.subframe == SubFrameMode::AuthoredInterpolate;
    bool interpolate = options_.subframe == SubFrameMode::Interpolate || options_.subframe == SubFrameMode::AuthoredInterpolate;
    double cap_period = options_.fps_cap > 0 ? 1.0 / options_.fps_cap : 0.0;
    double refresh_check = 0;
    double render_budget = 0.004;
    // "Uncapped" used to present as fast as this thread could go. Against a GPU that cannot keep
    // up that keeps the GPU's queue full, and a full queue is input latency; it also ran this
    // thread and the solver workers flat out against the simulation and input threads. Uncapped
    // is now held at twice the display's refresh rate, which still shows every sub-frame a monitor
    // can use. MELEE_PRESENT_UNCAPPED=1 restores the old behaviour for comparison runs.
    static const bool present_uncapped = [] { const char* v = std::getenv("MELEE_PRESENT_UNCAPPED"); return v && *v == '1'; }();
    struct Trace {
      FILE* file = nullptr;
      explicit Trace(const std::string& path) {
        if (!path.empty()) {
          file = std::fopen(path.c_str(), "w");
          if (!file) throw std::runtime_error("cannot open frame timing CSV");
          std::setvbuf(file, nullptr, _IOFBF, 1024 * 1024);
          std::fputs("presentation,simulation,phase,source_age_ms,interval_ms,solver_ms,submit_ms,present_wait_ms,authored_draws,paired_draws,sim_ms,draws,missing,hud,state,geometry,projection,state_register,skinned,posed,posed_skinned\n", file);
        }
      }
      ~Trace() { if (file) std::fclose(file); }
    } trace(options_.frame_times);
    double last_submission = 0; uint64_t drained = 0, discontinuities = 0;
    double next_present = host::now_seconds();
    double idle_repaint = 0.0;   // when a standing picture is next submitted again (replay_control::repaint_wanted)
    double stats_time = next_present; uint64_t stats_presented = 0, stats_sim = 0, stats_lines = 0;
    uint32_t phase_bins[5] = {};
    double build_seconds = 0, submit_seconds = 0; uint64_t cost_presented = 0;   // presented phases: [0,.25) [.25,.5) [.5,.75) [.75,1) exactly 1
    // MELEE_TRACE_LATENCY=1: the path of each new simulation frame from its tick to the return of
    // its first Present, in milliseconds, summarised as median and p95 every 600 frames.
    enum { LT_WAKE, LT_PAD, LT_SIM, LT_PICKUP, LT_SUBMIT, LT_WAIT, LT_TOTAL, LT_AGE, LT_COUNT };
    static const char* const kLatencyNames[LT_COUNT] = {"wake late", "pad read after wake", "pad read to queued", "queued to submit", "submit", "present wait", "pad read to Present return", "adapter report age"};
    std::vector<double> latency[LT_COUNT];
    const bool latency_trace = host::latency_trace_enabled();
    uint64_t latency_sequence = 0;
    for (;;) {
      host::window_pump();
      const auto& live_options = render_options(renderer);
      if (live_options.fps_cap > 0) cap_period = 1.0 / live_options.fps_cap;
      else if (live_options.fps_cap == 0 && present_uncapped) cap_period = 0;
      else if (host::now_seconds() >= refresh_check) {
        // -1 follows the monitor; 0 (uncapped) is held at twice the monitor's rate, see above.
        const double refresh = std::max(host::window_refresh_rate(), 1.0);
        cap_period = 1.0 / (live_options.fps_cap < 0 ? refresh : 2.0 * refresh);
        refresh_check = host::now_seconds() + 1.0;
      }
      if (host::window_closed()) { queue.finish(true); break; }
      {
        // The PC settings panel can switch sub-frame animation at run time.
        // Judged on the rate actually being presented, not on the setting: "follow the monitor" on
        // a 60 Hz display presents 60 frames a second, which is the same one-frame-per-tick case as
        // an explicit cap of 60 and has the same problem. Re-posing then buys no smoothness and
        // costs delay and accuracy, which is what the stage glitch reports at 60 were.
        const double presented_rate = cap_period > 0.0 ? 1.0 / cap_period : 0.0;
        const bool rate_worth_it = presented_rate <= 0.0 || presented_rate > 60.5;
        bool now_sub = live_options.subframe != SubFrameMode::Off && rate_worth_it;
        if (now_sub != subframes) {
          subframes = now_sub;
          if (subframes && cur >= 0) solver.set_frames(have_prev ? &frames[cur ^ 1] : nullptr, &frames[cur]);
        }
      }
      authored = live_options.subframe == SubFrameMode::Authored || live_options.subframe == SubFrameMode::AuthoredInterpolate;
      interpolate = live_options.subframe == SubFrameMode::Interpolate || live_options.subframe == SubFrameMode::AuthoredInterpolate;
      set_authored_capture_wanted(subframes && authored);
      // The menus, the character select and the stage select are moved by game code that stops
      // without warning, so predicting ahead overshoots a cursor and snaps back. Interpolating
      // between the last two frames never overshoots, and a menu does not need the frame of
      // latency Predict avoids. Decided from the game's own scene controller, recorded with the
      // frame: major scene 1 is the menus; in the match modes minor scenes 0 and 1 are the
      // character and stage selects and 2 is the match. (An earlier version keyed this on "no
      // skinned draws", which the menus never satisfy: they have skinned draws from frame 4 on.)
      if (authored && subframes && cur >= 0) {
        // Anything that is not certainly a running match counts as a menu. Named the other way
        // round it kept missing screens (the online menus among them), and the two mistakes are not
        // equal: a menu treated as a match flickers, while a match treated as a menu is only
        // slightly less smooth. The scene controller's major scene is the mode and the minor scene
        // is the stage within it; in every mode that plays a match, 0 and 1 are the character and
        // stage selects and the match itself is 2 and up.
        const bool in_menus = !frame_in_match(frames[cur]);
        // A cursor that stops is overshot by prediction and snaps back, which is the menu overshoot
        // reported on the stage select. Interpolating never overshoots and a menu does not need the
        // frame of latency Predict avoids, so menus interpolate whichever mode is selected.
        if (in_menus) interpolate = true;
        solver.set_menu_mode(in_menus);
      }
      // Render every source at least once: EFB resources can depend on earlier commands.
      bool got_new = false;
      Frame incoming;
      if ((cur < 0 || frames[cur].sequence == rendered_sequence) && queue.try_pop(incoming)) {
        int next = cur < 0 ? 0 : cur ^ 1;
        queue.recycle(std::move(frames[next]));   // return the buffers this slot is about to drop
        frames[next] = std::move(incoming);
        // A frame that follows a rollback is not the neighbour of the one before it (Frame::discontinuous),
        // so it is shown as it is, unpaired, and pairing resumes from it on the next frame.
        have_prev = cur >= 0 && !frames[next].discontinuous;
        if (frames[next].discontinuous) ++discontinuities;
        cur = next;
        got_new = true;
        ++submitted; ++stats_sim;
      }
      if (cur < 0) {
        if (queue.drained()) break;
        queue.wait_available(std::chrono::milliseconds(2));
        continue;
      }
      // Pairing must follow every new frame, including drained ones: the index maps this frame's
      // draws onto the previous frame's, and the ring slots are refilled underneath it.
      if (got_new && subframes) solver.set_frames(have_prev ? &frames[cur ^ 1] : nullptr, &frames[cur]);
      // Backlog (the renderer fell behind, or a compile burst): execute older frames without
      // the solver or a present so their EFB copies exist, then catch up to the newest one. The
      // simulation never waits on this.
      if (got_new && queue.size() > 0) {
        renderer->set_skip_present(true);
        renderer->submit_frame(frames[cur]);
        renderer->set_skip_present(false);
        rendered_sequence = frames[cur].sequence;
        ++drained;
        if (drained == 1 || drained % 300 == 0) host::log("renderer: draining a backlog of %zu queued frames (%llu drained so far)", queue.size() + 1, (unsigned long long)drained);
        continue;
      }
      const Frame& current = frames[cur];
      bool should_render;
      double t = 0.0;
      if (!subframes) {
        should_render = current.sequence != rendered_sequence;   // once per simulation frame
        // A paused replay sends no new frames, but its bar and the settings panel still have to be
        // drawn: the frame on screen is submitted again, 60 times a second.
        if (!should_render && !queue.drained() && replay_control::repaint_wanted() && host::now_seconds() >= idle_repaint)
          should_render = true;
        if (!should_render) {
          if (queue.drained()) break;
          queue.wait_available(std::chrono::milliseconds(2));
          continue;
        }
        idle_repaint = host::now_seconds() + SIM_PERIOD;
      } else {
        double now = host::now_seconds();
        t = (now - current.time) / SIM_PERIOD;
        if (interpolate) t = std::min(std::max(t, 0.0), 1.0);
        else t = std::min(std::max(t, 0.0), 1.0);   // never extrapolate more than one frame ahead
        if (options_.pin_phase >= 0) {
          // Development: one presented frame per simulation frame at a fixed phase, so two runs of
          // the same script produce pictures that can be compared one for one.
          t = std::min(options_.pin_phase, 1.0);
          if (current.sequence == rendered_sequence) {
            if (queue.drained()) break;
            queue.wait_available(std::chrono::milliseconds(2));
            continue;
          }
        } else if (cap_period > 0 && now < next_present - render_budget) {
          // Start early enough to finish GPU submission before the presentation deadline.
          double wait = next_present - render_budget - now;
          if (wait > 0.0005) std::this_thread::sleep_for(std::chrono::microseconds((long long)(std::min(wait - 0.0003, 0.001) * 1e6)));
          else std::this_thread::yield();
          continue;
        }
        should_render = true;
        if (queue.drained() && current.sequence == rendered_sequence) break;
      }
      if (options_.capture_burst && options_.capture_sim_frame && current.sequence >= options_.capture_sim_frame && burst_logged < options_.capture_burst) {
        host::log("present %llu: sim frame %llu phase %.3f", presented + 1, (unsigned long long)current.sequence, t);
        ++burst_logged;
      }
      renderer->set_present_deadline(subframes && cap_period > 0 ? next_present : 0);
      solver_pair_counter().store(subframes ? solver.pair_count() : 0, std::memory_order_relaxed);   // for the backends' "gpu:" line
      const double render_start = host::now_seconds();
      double solver_ms = 0;
      if (subframes && have_prev) {
        double t0 = host::now_seconds();
        solver.build(t, interpolate, overrides, authored);
        double t1 = host::now_seconds();
        solver_ms = (t1-t0)*1000.0;
        renderer->submit_frame(current, overrides.data());
        build_seconds += t1 - t0; submit_seconds += host::now_seconds() - t1; ++cost_presented;
      } else {
        double t1 = host::now_seconds();
        renderer->submit_frame(current);
        submit_seconds += host::now_seconds() - t1; ++cost_presented;
      }
      const double render_end = host::now_seconds();
      const double present_wait = renderer->presentation_wait_seconds();
      render_budget = std::max(render_budget * 0.95, render_end-render_start-present_wait+0.0002);
      if (trace.file) std::fprintf(trace.file, "%llu,%llu,%.6f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%u,%.3f,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
          (unsigned long long)(presented+1), (unsigned long long)current.sequence, t,
          (render_start-current.time)*1000.0, last_submission ? (render_end-last_submission)*1000.0 : 0.0,
          solver_ms, (render_end-render_start-present_wait)*1000.0-solver_ms, present_wait*1000.0, solver.stats().authored, solver.stats().paired, host::last_sim_frame_ms(),
          solver.stats().draws, solver.stats().missing, solver.stats().hud, solver.stats().state, solver.stats().geometry,
          solver.stats().projection, solver.stats().state_register, solver.stats().skinned,
          solver.stats().posed, solver.stats().posed_skinned);
      if (latency_trace && current.sequence != latency_sequence && current.tick.pad > 0) {
        latency_sequence = current.sequence;
        const host::TickTiming& k = current.tick;
        const double v[LT_COUNT] = {k.wake - k.target, k.pad - k.wake, current.time - k.pad, render_start - current.time,
                                    render_end - render_start - present_wait, present_wait, render_end - k.pad, k.pad_age / 1000.0};
        for (int i = 0; i < LT_COUNT; ++i) if (i != LT_AGE || k.pad_age >= 0) latency[i].push_back(v[i] * 1000.0);
        if (latency[LT_TOTAL].size() == 600) {
          std::string line = "latency trace (600 frames, median/p95 ms):";
          for (int i = 0; i < LT_COUNT; ++i) {
            auto& s = latency[i];
            if (s.empty()) continue;
            std::sort(s.begin(), s.end());
            char part[96];
            std::snprintf(part, sizeof part, " %s %.2f/%.2f |", kLatencyNames[i], s[s.size() / 2], s[s.size() * 95 / 100]);
            line += part;
            s.clear();
          }
          host::log("%s adapter %.0f Hz", line.c_str(), host::gcadapter_poll_rate_hz());
        }
      }
      last_submission = render_end;
      rendered_sequence = current.sequence;
      ++presented; ++stats_presented;
      ++phase_bins[t >= 1.0 ? 4 : (int)(t * 4.0)];
      if (cap_period > 0) {
        double now = host::now_seconds();
        // Skip missed slots; never emit catch-up bursts after a stall.
        next_present += cap_period;
        if (next_present <= now) next_present += (std::floor((now-next_present)/cap_period)+1.0)*cap_period;
      }
      double now = host::now_seconds();
      if (now - stats_time >= 1.0) {
        const SubFrameStats& s = solver.stats();
        wchar_t title[256];
        _snwprintf_s(title, _TRUNCATE, L"%ls  |  DISPLAY %.0f fps%s  |  game logic %.0f Hz (always 60, like Rivals' physics)  |  %s  |  draws %u paired %u",
                     host::window_title_base().c_str(), stats_presented / (now - stats_time),
                     cap_period <= 0 ? L" (uncapped)" : live_options.fps_cap == 0 ? L" (uncapped, held at 2x refresh)" : L" (capped)", stats_sim / (now - stats_time),
                     !subframes ? L"locked" : authored ? L"authored" : interpolate ? L"interpolate" : L"extrapolate", s.draws, s.paired);
        host::window_set_title(title);
        if (++stats_lines % 5 == 0) {
          // The cap the pacing actually used, not the setting: "uncapped" is held at twice the
          // display's refresh unless MELEE_PRESENT_UNCAPPED=1, and -1 is whatever the monitor runs at.
          char cap_text[64];
          if (cap_period <= 0) std::snprintf(cap_text, sizeof cap_text, "no cap");
          else std::snprintf(cap_text, sizeof cap_text, "cap %.0f%s", 1.0 / cap_period, live_options.fps_cap == 0 ? " = 2x refresh" : live_options.fps_cap < 0 ? " = monitor" : "");
          host::log("display: %.0f fps (%s, sim %.0f Hz, %s, %u draws, %u paired, %u cuts)", stats_presented / (now - stats_time), cap_text, stats_sim / (now - stats_time),
                    !subframes ? "locked" : authored ? "authored" : interpolate ? "interpolate" : "extrapolate", s.draws, s.paired, s.cuts);
          if (subframes) host::log("pair rejection: missing %u, HUD %u, geometry %u, state %u (last BP %02X), projection %u, authored %u, camera-only %u, vertex-blended %u, FLIPS %u | phases <.25:%u <.5:%u <.75:%u <1:%u =1:%u",
                                   s.missing, s.hud, s.geometry, s.state, s.state_register, s.projection, s.authored, s.carried, s.vertex_blended, s.pair_flips, phase_bins[0], phase_bins[1], phase_bins[2], phase_bins[3], phase_bins[4]);
          if (subframes) {
            // What the solver got wrong at the one phase where there is a right answer to compare
            // against. Anything other than zero here is geometry that jumps once per simulation
            // frame, so this is the number to watch when a stage flickers.
            static EndpointStats last_endpoint;
            const EndpointStats& e = subframe_endpoint_stats();
            const uint64_t checked = e.checked - last_endpoint.checked, off = e.off - last_endpoint.off;
            if (checked) host::log("endpoint check: %llu of %llu draws not at the simulation pose at phase 1 (worst %.2f units, identity %016llX) | stage-locked %u",
                                   (unsigned long long)off, (unsigned long long)checked, e.worst, (unsigned long long)e.worst_identity, s.stage_locked);
            last_endpoint = e;
            const_cast<EndpointStats&>(e).worst = 0;
            static PhaseFlipStats last_flips;
            const PhaseFlipStats& f = subframe_phase_flips();
            const uint64_t compared = f.compared - last_flips.compared, flipped = f.flipped - last_flips.flipped,
                           skinned = f.flipped_skinned - last_flips.flipped_skinned;
            if (compared) host::log("phase flips: %llu of %llu draws changed route between two frames of the same tick (%llu of them skinned)",
                                    (unsigned long long)flipped, (unsigned long long)compared, (unsigned long long)skinned);
            last_flips = f;
            const StageSplitAudit& a = subframe_stage_split_audit();
            if (a.checked)
              host::log("stage split audit: %llu of %llu static draws placed off the camera transform, worst %.4f units",
                        (unsigned long long)a.split, (unsigned long long)a.checked, a.worst);
          }
          if (subframes) std::memset(phase_bins, 0, sizeof phase_bins);
          host::log("render cost: solver %.2f ms/frame, submit %.2f ms/frame (%s)", 1000.0 * build_seconds / std::max<uint64_t>(1, cost_presented), 1000.0 * submit_seconds / std::max<uint64_t>(1, cost_presented), render_profile_line(renderer).c_str());
          build_seconds = submit_seconds = 0; cost_presented = 0;
          if (authored) {
            const AuthoredStats& a = authored_stats();
            std::string line = "authored: captured " + std::to_string(a.captured) + " sampled " + std::to_string(a.sampled) + " | capture fails:";
            for (int i = 1; i < 24; ++i) if (a.capture[i]) line += " c" + std::to_string(i) + "=" + std::to_string(a.capture[i]);
            line += " | sample fails:";
            for (int i = 1; i < 32; ++i) if (a.sample[i]) line += " s" + std::to_string(i) + "=" + std::to_string(a.sample[i]);
            // c4 only says "some joint feature has no evaluator". This says which, so the next
            // evaluator to write is chosen by what real matches use rather than by guesswork.
            std::string features;
            for (int i = 0; i < FEAT_COUNT; ++i)
              if (a.feature[i]) features += std::string(" ") + kCaptureFeatureNames[i] + "=" + std::to_string(a.feature[i]);
            if (!features.empty()) line += " | unevaluated joint features:" + features;
            line += " | envelope captured " + std::to_string(a.captured_envelope) + " sampled " + std::to_string(a.sampled_envelope);
            line += " | posed draws " + std::to_string(a.posed_draws) + " (envelope " + std::to_string(a.posed_draws_envelope) + ", skinned draws " + std::to_string(a.skinned_draws) + ")";
            host::log("%s", line.c_str());
          }
        }
        stats_time = now; stats_presented = 0; stats_sim = 0;
      }
    }
    host::log("renderer: %llu simulation frames, %llu presented frames on its own thread, %llu drained without presenting, %llu shown unblended after a rollback",
              submitted, presented, (unsigned long long)drained, (unsigned long long)discontinuities);
  }

 public:
  ThreadedBackend(RenderOptions options, bool visible) : options_(options) {
    std::promise<void> initialized;
    auto ready = initialized.get_future();
    worker = std::thread([this, options, visible, init = std::move(initialized)]() mutable {
      // Above normal so other programs cannot delay presentation; below the simulation thread
      // (main.cpp), which an unlocked renderer would otherwise starve.
      SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
      bool started = false;
      try {
        void* window = host::window_create(options.window_w, options.window_h, host::window_title_base().c_str(), visible);
        if (options.fullscreen && !options.exclusive_fullscreen) host::window_set_fullscreen(true);
        // The swapchain must match the window as it is now (fullscreen covers the monitor, not window_w x window_h).
        int client_w = options.window_w, client_h = options.window_h;
        host::window_client_size(&client_w, &client_h);
        std::unique_ptr<Backend> renderer(create_render_backend(window, std::max(client_w, 1), std::max(client_h, 1), options));
        host::window_set_resize_callback([&renderer](int w, int h) { render_resize(renderer.get(), w, h); });
        init.set_value(); started = true;
        present_loop(renderer.get());
        host::window_set_resize_callback({});
        renderer.reset();
        host::window_destroy();
      } catch (const std::exception& e) {
        host::log("renderer: fatal error on the render thread: %s", e.what());
        if (!started) init.set_exception(std::current_exception());
        else host::request_exit(3);
        queue.finish(true);
      } catch (...) {
        host::log("renderer: fatal error on the render thread");
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
    host::SimCostScope cost(host::SIM_QUEUE);
    if (!queue.push(frame)) throw ExitRequested{host::exit_code()};
  }
  void submit_and_recycle(Frame& frame) override {
    host::SimCostScope cost(host::SIM_QUEUE);
    if (!queue.push_and_recycle(frame)) throw ExitRequested{host::exit_code()};
  }
};
}
std::unique_ptr<Backend> create_threaded_backend(const RenderOptions& options, bool visible) {
  return std::make_unique<ThreadedBackend>(options, visible);
}
}
