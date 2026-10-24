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
#include "vrr_detect.h"
#include "replay_control.h"
#include <algorithm>
#include <vector>
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
    double last_refresh = 60.0;   // the monitor's rate as last read, for Reflex's limit
    int probe_state = 0;          // the variable refresh check below: 0 not run, 1 running, 2 done
    double probe_until = 0;
    bool reflex_slept = false;    // Reflex has already waited for the frame about to be drawn
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
    double build_seconds = 0, submit_seconds = 0; uint64_t cost_presented = 0;
    // Frame pacing for "it feels choppy" reports: the time between two frames handed to the display,
    // summarised every five seconds (median, p99, worst, how many ran long) with each spike logged
    // as it happens. The average fps above cannot show uneven frames; this can.
    std::vector<float> pacing_intervals; pacing_intervals.reserve(4096);
    double pacing_spike_logged = 0; uint32_t pacing_spikes_quiet = 0;   // presented phases: [0,.25) [.25,.5) [.5,.75) [.75,1) exactly 1
    // MELEE_TRACE_LATENCY=1: the path of each new simulation frame from its tick to the return of
    // its first Present, in milliseconds, summarised as median and p95 every 600 frames.
    enum { LT_WAKE, LT_PAD, LT_SIM, LT_POP, LT_PAIR, LT_SLOT, LT_PICKUP, LT_SUBMIT, LT_WAIT, LT_TOTAL, LT_AGE, LT_COUNT };
    static const char* const kLatencyNames[LT_COUNT] = {"wake late", "pad read after wake", "pad read to queued", "queued to pop", "pairing", "pop to render start", "queued to submit", "submit", "present wait", "pad read to Present return", "adapter report age"};
    double frame_popped = 0.0, frame_pair_s = 0.0;   // of the frame in frames[cur]: when the render thread took it, and its pairing time
    std::vector<double> latency[LT_COUNT];
    const bool latency_trace = host::latency_trace_enabled();
    uint64_t latency_sequence = 0;
    // Predict phases from the tick's schedule (above, where t is set). Measured 2026-10-23 on the
    // same scripted match back to back: steps in the shown time over 2 ms fell from 262 to 15 per
    // 40 s, over 1 ms from 1032 to 230, with latency, pair rejection and the endpoint check
    // unchanged. MELEE_PHASE_TIMELINE=handover (tests) keeps the hand-over time instead.
    static const bool phase_timeline = [] { const char* v = std::getenv("MELEE_PHASE_TIMELINE"); return !(v && v[0] == 'h'); }();
    double typical_span = 0.0, early_span = 0.0; uint64_t timeline_sequence = 0;
    // Present on hand-over (variable refresh only): a new game frame is drawn the moment it arrives
    // instead of waiting for the next slot of the cap's grid; the in-between presents of that tick are
    // then spaced evenly up to the predicted next hand-over, so the next one never meets the display's
    // minimum interval. A fixed-refresh display keeps the plain grid (uneven presents judder there).
    static const bool handover_env = [] { const char* v = std::getenv("MELEE_PRESENT_ON_HANDOVER"); return v && *v == '1'; }();
    // Frame-locked grid (MELEE_PRESENT_LOCK=1): on a variable refresh display the grid period is a
    // whole fraction of the game frame and a new frame is presented on arrival (below).
    static const bool present_lock = [] { const char* v = std::getenv("MELEE_PRESENT_LOCK"); return v && *v == '1'; }();
    bool handover_present = handover_env;
    int lock_logged = 0;   // off: about 1 ms less delay, but less even motion (measured 10-23)
    double handover_span = 0.0; uint64_t handover_span_seq = 0;
    bool handover_now = false; double handover_due = 0.0, submit_typical = 0.0, submit_first_typical = 0.0;
    // An in-between present whose game frame is superseded while its re-pose is being built is not
    // submitted: the new frame takes that slot instead of waiting a whole period for the next one.
    // MELEE_STALE_DISCARD=0 keeps the old behaviour (measurement only).
    static const bool stale_discard = [] { const char* v = std::getenv("MELEE_STALE_DISCARD"); return !(v && *v == '0'); }();
    uint64_t stale_discards = 0;
    static const bool scanline_sync = [] { const char* v = std::getenv("MELEE_SCANLINE_SYNC"); return v && *v == '1'; }();
    bool scanline_active = false, scanline_logged_on = false, scanline_failed = false;
    double last_scanline_flip = 0.0, scanline_flip = 0.0, scanline_period = 0.0;
    int scanline_misses = 0;
    std::string scanline_why;
    enum { KF_DEFAULT, KF_OFF, KF_ANCHOR_ALL, KF_SNAP };
    static const int keyframe_mode = [] {
      const char* m = std::getenv("MELEE_KEYFRAME_MODE");
      if (!m || !*m) return (int)KF_DEFAULT;
      const std::string s(m);
      return s == "off" ? (int)KF_OFF : s == "anchor-all" ? (int)KF_ANCHOR_ALL : s == "snap" ? (int)KF_SNAP : (int)KF_DEFAULT;
    }();
    host::log("presentation: phase timeline %s, keyframe %s", phase_timeline ? "tick" : "hand-over",
              keyframe_mode == KF_OFF ? "off" : keyframe_mode == KF_ANCHOR_ALL ? "anchor (all modes)" : keyframe_mode == KF_SNAP ? "snap" : "off");
    for (;;) {
      host::window_pump();
      const auto& live_options = render_options(renderer);
      // A fixed cap the monitor's rate is not a whole multiple of (60 on 144 or 175 Hz) shows each
      // frame for an uneven number of refreshes, which reads as choppy, unless variable refresh is
      // on. Reports of "60 feels like 30" give no monitor rate, so it is logged once, and again
      // when the cap or the monitor changes.
      {
        static int told_cap = -2; static long told_hz = -1;
        const long hz = std::lround(host::window_refresh_rate());
        if (live_options.fps_cap > 0 && hz > 0 && (live_options.fps_cap != told_cap || hz != told_hz)) {
          told_cap = live_options.fps_cap; told_hz = hz;
          const double ratio = (double)hz / live_options.fps_cap;
          if (live_options.fps_cap < hz && std::fabs(ratio - std::round(ratio)) > 0.05)
            host::log("display: monitor %ld Hz, frame cap %d: %.2f refreshes a frame, so frames are shown for uneven times "
                      "(choppy) unless G-Sync or FreeSync is on; the Monitor cap or a cap that divides %ld is even",
                      hz, live_options.fps_cap, ratio, hz);
          else host::log("display: monitor %ld Hz, frame cap %d", hz, live_options.fps_cap);
        }
      }
      if (host::now_seconds() >= refresh_check) {
        last_refresh = std::max(host::window_refresh_rate(), 1.0);
        refresh_check = host::now_seconds() + 1.0;
      }
      // The rate asked for: the cap, the monitor (-1), or uncapped (0), held at twice the monitor's
      // rate (see above) unless MELEE_PRESENT_UNCAPPED=1.
      double wanted = live_options.fps_cap > 0 ? live_options.fps_cap
                    : live_options.fps_cap < 0 ? last_refresh
                    : present_uncapped ? 0.0 : 2.0 * last_refresh;
      // A display that follows the frames (G-Sync, FreeSync, read by the renderer from the display's
      // own counts) is fed just under its refresh and synced, whatever the cap and V-Sync say: above
      // the refresh it stops following and tears, under it syncing costs nothing. A 0.8.99 player on
      // a 240 Hz G-Sync screen saw tearing with V-Sync off, and again after turning it on and off.
      int vrr = renderer->variable_refresh();
      // Measurement only: hidden test windows read as fixed refresh; MELEE_ASSUME_VRR=1 treats the
      // display as variable refresh so the hand-over present path can be timed there.
      static const bool assume_vrr = [] { const char* v = std::getenv("MELEE_ASSUME_VRR"); return v && *v == '1'; }();
      if (assume_vrr) vrr = 1;
      const double vrr_cap = vrr::under_refresh(last_refresh);
      // Frames as fast as the refresh look alike on both kinds of display, so the counts cannot tell
      // them apart. Once, for four seconds at the start, frames are held to 90% of the refresh to
      // find out. Melee's own frames stay 60 a second; only the in-between frames thin out.
      if (vrr < 0 && probe_state == 0 && renderer->display_counts_readable() && last_refresh >= 75.0 &&
          (wanted <= 0.0 || wanted > 0.96 * last_refresh)) {
        probe_state = 1;
        probe_until = host::now_seconds() + 4.0;
      }
      if (probe_state == 1) {
        if (vrr < 0 && host::now_seconds() < probe_until) wanted = 0.9 * last_refresh;
        else probe_state = 2;
      }
      if (vrr == 1 && (wanted <= 0.0 || wanted > vrr_cap)) wanted = vrr_cap;
      renderer->set_display_refresh(last_refresh, vrr == 1);
      cap_period = wanted > 0.0 ? 1.0 / wanted : 0.0;
      // With Reflex on, its own limiter holds the rate (in its sleep, at the start of each frame) and
      // this thread's timer stands aside, so the two never wait on each other. "Follow the monitor"
      // with V-Sync off sits where NVIDIA puts it for G-Sync, refresh * (1 - refresh / 3600): 225 at
      // 240 Hz, 189 at 200 Hz, inside the variable refresh range so G-Sync never tears at the top.
      // With V-Sync on it is left to the display: the driver applies that same limit itself when
      // G-Sync is active, and a display without it is paced by V-Sync at its full refresh (a fixed
      // 240 Hz screen fed 225 frames would show some of them twice).
      // Locked (no sub-frames) keeps the old path: one present per game frame, Reflex markers only.
      // On a display known to follow the frames Reflex holds the same rate as above; on one known to
      // refresh at a fixed rate, "follow the monitor" is its full refresh (V-Sync, when on, paces it).
      const double reflex_hz = vrr == 1 || probe_state == 1 ? wanted
                             : live_options.fps_cap > 0 ? live_options.fps_cap
                             : live_options.fps_cap < 0 ? (live_options.vsync ? 0.0 : vrr == 0 ? last_refresh : vrr_cap)
                             : present_uncapped ? 0.0 : 2.0 * last_refresh;
      const bool reflex_pacing = renderer->reflex_pacing(subframes ? reflex_hz : 0.0) && subframes;
      handover_present = handover_env;
      if (present_lock && vrr == 1 && !reflex_pacing && cap_period > 0.0) {
        const double n = std::max(2.0, std::floor(SIM_PERIOD / cap_period + 1e-6));
        cap_period = SIM_PERIOD / n;
        handover_present = true;
        if (lock_logged != (int)n) {
          lock_logged = (int)n;
          host::log("pacing: frame-locked grid, %d presents per game frame (%.1f Hz), new frames presented on arrival", (int)n, n / SIM_PERIOD);
        }
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
      // Tear-free without V-Sync's queue, for displays that refresh at a fixed rate (G-Sync and
      // FreeSync off): one frame per refresh. Each frame is started just after a vertical blank,
      // shows the moment of the following blank, and is presented synced so it flips at that blank:
      // a whole refresh for drawing it, never a tear, never more than one frame queued. It costs up
      // to one refresh of display delay against tearing. If two presents in a row wait more than
      // 1.5 refreshes (the frame missed its blank), it stops for the session and the frame cap clock
      // paces. MELEE_SCANLINE_SYNC=1 turns it on.
      scanline_active = false;
      if (scanline_sync && !scanline_failed && subframes && vrr != 1 && probe_state != 1 && !live_options.vsync && !reflex_pacing &&
          options_.pin_phase < 0 && (live_options.fps_cap <= 0 || live_options.fps_cap >= 0.98 * last_refresh)) {
        std::string why;
        if (scanline_flip <= 0.0) {
          double blank = 0, period = 0;
          if (renderer->vblank_next(host::now_seconds(), last_refresh, &blank, &period, &why)) {
            double flip = blank + period;   // drawn after the coming blank, flipped at the one after
            if (flip < last_scanline_flip + 0.5 * period) flip += period;   // that refresh already has its frame
            scanline_flip = flip; scanline_period = period;
          }
        }
        if (scanline_flip > 0.0) {
          next_present = scanline_flip - scanline_period + 0.0003 + render_budget;   // the loop starts render_budget before next_present
          cap_period = scanline_period;
          scanline_active = true;
          if (!scanline_logged_on) {
            host::log("pacing: tear-free on, one frame per vertical blank (every %.3f ms), drawn the refresh before", scanline_period * 1000.0);
            scanline_logged_on = true; scanline_why.clear();
          }
        } else if (why != scanline_why) {
          host::log("pacing: tear-free waiting (%s); the frame cap clock paces meanwhile", why.c_str());
          scanline_why = why; scanline_logged_on = false;
        }
      }
      renderer->set_display_refresh(last_refresh, vrr == 1 || scanline_active);
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
        // A frame that follows a rollback (Frame::discontinuous) is not the neighbour of the one
        // before it, but the solver still pairs it against that previous frame: SubFrameSolver::set_frames
        // holds world/stage draws at their exact pose across the discontinuity while still letting a
        // fighter's authored pose interpolate through the correction.
        have_prev = cur >= 0 && !frames[next].hard_cut;
        if (frames[next].discontinuous) ++discontinuities;
        cur = next;
        got_new = true;
        frame_popped = host::now_seconds(); frame_pair_s = 0.0;
        ++submitted; ++stats_sim;
      }
      if (cur < 0) {
        if (queue.drained()) break;
        queue.wait_available(std::chrono::milliseconds(2));
        continue;
      }
      // Pairing must follow every new frame, including drained ones: the index maps this frame's
      // draws onto the previous frame's, and the ring slots are refilled underneath it.
      if (got_new && subframes) {
        const double p0 = host::now_seconds();
        solver.set_frames(have_prev ? &frames[cur ^ 1] : nullptr, &frames[cur]);
        frame_pair_s = host::now_seconds() - p0;
      }
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
        if (reflex_pacing && !reflex_slept && options_.pin_phase < 0) {
          // Reflex's wait, then back to the top for a game frame that arrived during it, so the
          // moment shown below is chosen after the wait, not before.
          renderer->reflex_sleep();
          reflex_slept = true;
          continue;
        }
        double now = host::now_seconds();
        // The moment a game frame stands for. Its hand-over time moves with how long the game took
        // to compute it (2 to 5 ms from frame to frame), so phases measured from it make motion
        // speed up and slow down by that much. The tick's own scheduled start is even; adding the
        // usual compute time keeps the average where it was. Predict only: Interpolate shows the
        // previous frame at phase 0, which must never be shown before the new one exists.
        double base = current.time;
        static const bool interp_timeline = [] { const char* v = std::getenv("MELEE_INTERP_TIMELINE"); return !(v && *v == '0'); }();
        if (phase_timeline && (!interpolate || interp_timeline) && current.tick.target > 0.0) {
          const double span = current.time - current.tick.target;
          if (span > 0.0 && span < SIM_PERIOD) {
            if (current.sequence != timeline_sequence) {
              timeline_sequence = current.sequence;
              typical_span = typical_span > 0.0 ? typical_span + 0.05 * (span - typical_span) : span;
              // Presented on arrival, a frame handed over earlier than usual would need a phase
              // under 0 and is clamped, which is a visible step: the base follows the early edge.
              early_span = early_span > 0.0 ? early_span + (span < early_span ? 0.3 : 0.01) * (span - early_span) : span;
            }
            const bool on_arrival = handover_present && vrr == 1 && !reflex_pacing && cap_period > 0;
            base = current.tick.target + (on_arrival && early_span > 0.0 ? early_span : typical_span);
          }
        }
        handover_now = handover_present && vrr == 1 && !reflex_pacing && cap_period > 0 && current.sequence != rendered_sequence;
        if (current.tick.target > 0.0 && current.sequence != handover_span_seq) {
          handover_span_seq = current.sequence;
          const double span = current.time - current.tick.target;
          if (span > 0.0 && span < SIM_PERIOD) handover_span = handover_span > 0.0 ? handover_span + 0.05 * (span - handover_span) : span;
        }
        // The phase of a present whose render starts at `when`.
        // With the hand-over lever every present takes its phase from the moment it is expected to be
        // shown, less one typical submit, so the two kinds of present agree: a grid present is shown
        // at its slot; a hand-over present one submit after now, or at its floor (half a period after
        // the last present) if that is later. Taken at the render start instead, a present that then
        // waited inside Present showed a phase that was too old by that wait: the measured steps.
        // Every simulation frame shown once exactly as the game drew it (MELEE_KEYFRAME_MODE=anchor-all,
        // tests): the first present of a new frame at phase 0. Off by default in both modes: anchored,
        // the first present of each frame jumps back to 0 and motion stalls on every game frame
        // (measured 10-23 in Interpolate: steps over 2 ms 164 -> 3291 a match against 0.9.17, which
        // never anchored). snap: at a cap that is a whole multiple of 60, phases rounded to the
        // present grid so one present lands on 0 every frame.
        const auto phase_for = [&](double when) {
          double ph = (when - base) / SIM_PERIOD;
          if (handover_present && vrr == 1 && !reflex_pacing && cap_period > 0 && submit_typical > 0.0) {
            // A new frame's first present submits more (textures, EFB copies) than an in-between one,
            // so it has its own typical.
            const double first_typical = submit_first_typical > 0.0 ? submit_first_typical : submit_typical;
            const double shown = handover_now ? std::max(when + first_typical, last_submission > 0 ? last_submission + 0.5 * cap_period : 0.0)
                                              : next_present;
            ph = (shown - submit_typical - base) / SIM_PERIOD;
          }
          if (scanline_active) ph = (scanline_flip - base) / SIM_PERIOD;   // shown from that blank on
          ph = std::min(std::max(ph, 0.0), 1.0);   // never extrapolate more than one frame ahead
          if (options_.pin_phase < 0) {
            if (keyframe_mode == KF_ANCHOR_ALL && current.sequence != rendered_sequence) ph = 0.0;
            if (keyframe_mode == KF_SNAP && live_options.fps_cap > 0) {
              const double k = std::round(live_options.fps_cap / 60.0);
              if (k >= 2 && std::fabs(live_options.fps_cap - k * 60.0) < 1.0) ph = std::round(ph * k) / k;
            }
          }
          return ph;
        };
        t = phase_for(now);
        if (options_.pin_phase >= 0) {
          // Development: one presented frame per simulation frame at a fixed phase, so two runs of
          // the same script produce pictures that can be compared one for one.
          t = std::min(options_.pin_phase, 1.0);
          if (current.sequence == rendered_sequence) {
            if (queue.drained()) break;
            queue.wait_available(std::chrono::milliseconds(2));
            continue;
          }
        } else if (!handover_now && !reflex_pacing && cap_period > 0 && now < next_present - render_budget) {
          // Start early enough to finish GPU submission before the presentation deadline.
          double wait = next_present - render_budget - now;
          if (wait > 0.0005 + host::sleep_slack()) std::this_thread::sleep_for(std::chrono::microseconds((long long)(std::min(wait - 0.0003, 0.001) * 1e6)));   // not on a system whose sleeps overshoot
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
      // A hand-over present goes out now: the grid slot it replaces is no deadline for it, only half
      // a period after the last present, so two presents never queue back to back.
      renderer->set_present_deadline(!(subframes && cap_period > 0 && !reflex_pacing) || scanline_active ? 0
                                     : handover_now ? (last_submission > 0 ? last_submission + 0.5 * cap_period : 0) : next_present);
      solver_pair_counter().store(subframes ? solver.pair_count() : 0, std::memory_order_relaxed);   // for the backends' "gpu:" line
      const double render_start = host::now_seconds();
      double solver_ms = 0;
      if (subframes && have_prev) {
        double t0 = host::now_seconds();
        // A repeated present's re-pose stops the moment a newer game frame is pushed (polled by the
        // solver's workers), and is dropped: the newer frame takes this slot.
        const uint64_t pushed_at_start = queue.pushed();
        const bool repeat = current.sequence == rendered_sequence && options_.pin_phase < 0 && stale_discard;
        const std::function<bool()> newer = [&] { return queue.pushed() != pushed_at_start; };
        const bool built = solver.build(t, interpolate, overrides, authored, repeat ? &newer : nullptr);
        double t1 = host::now_seconds();
        if (repeat && (!built || queue.pushed() != pushed_at_start)) {
          build_seconds += t1 - t0; ++stale_discards;
          continue;   // the newer frame is picked up at the top and presented in this slot
        }
        solver_ms = (t1-t0)*1000.0;
        renderer->submit_frame(current, overrides.data());
        build_seconds += t1 - t0; submit_seconds += host::now_seconds() - t1; ++cost_presented;
      } else {
        double t1 = host::now_seconds();
        renderer->submit_frame(current);
        submit_seconds += host::now_seconds() - t1; ++cost_presented;
      }
      const double render_end = host::now_seconds();
      reflex_slept = false;
      const double present_wait = renderer->presentation_wait_seconds();
      render_budget = std::max(render_budget * 0.95, render_end-render_start-present_wait+0.0002);
      { const double s = std::min(render_end - render_start - present_wait, 0.010);
        if (current.sequence != rendered_sequence) submit_first_typical = submit_first_typical > 0.0 ? submit_first_typical + 0.05 * (s - submit_first_typical) : s;
        else submit_typical = submit_typical > 0.0 ? submit_typical + 0.02 * (s - submit_typical) : s; }
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
        const double v[LT_COUNT] = {k.wake - k.target, k.pad - k.wake, current.time - k.pad,
                                    frame_popped - current.time, frame_pair_s, render_start - frame_popped - frame_pair_s, render_start - current.time,
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
      if (last_submission > 0) {
        const double interval = render_end - last_submission;
        pacing_intervals.push_back((float)(interval * 1000.0));
        // A spike: well past this cap's frame time (or 50 ms when uncapped). One line a second at most.
        const double expected = cap_period > 0 ? cap_period : 1.0 / 60.0;
        if (interval > std::max(0.050, 3.0 * expected)) {
          if (render_end - pacing_spike_logged >= 1.0) {
            // Where the time went: waiting for the next simulated frame, drawing it, or inside the
            // swap chain's Present (Windows taking the window back from the game, an overlay).
            host::log("pacing: a frame took %.1f ms to reach the display (expected %.1f), sim frame %llu, simulation %.1f ms; "
                      "%.1f ms waiting for the frame, %.1f drawing, %.1f in Present%s",
                      interval * 1000.0, expected * 1000.0, (unsigned long long)current.sequence, host::last_sim_frame_ms(),
                      (render_start - last_submission) * 1000.0, (render_end - render_start - present_wait) * 1000.0,
                      present_wait * 1000.0, pacing_spikes_quiet ? " (more since the last line)" : "");
            pacing_spike_logged = render_end; pacing_spikes_quiet = 0;
          } else {
            ++pacing_spikes_quiet;
          }
        }
      }
      last_submission = render_end;
      rendered_sequence = current.sequence;
      ++presented; ++stats_presented;
      ++phase_bins[t >= 1.0 ? 4 : (int)(t * 4.0)];
      if (scanline_active) {
        last_scanline_flip = scanline_flip;   // the next one is taken from the vertical blank again
        scanline_flip = 0.0;
        scanline_misses = present_wait > 1.5 * scanline_period ? scanline_misses + 1 : 0;
        if (scanline_misses >= 2) {
          scanline_failed = true;
          host::log("pacing: tear-free stopped: two presents in a row waited %.1f ms (refresh %.3f ms); the frame cap clock paces from now on",
                    present_wait * 1000.0, scanline_period * 1000.0);
        }
      } else if (handover_now) {
        // Restart the grid from this present; the next hand-over's present is predicted from the
        // usual tick-start-to-hand-over time.
        // Measured, not render_budget: that holds a slow present's time for a while and pushed the
        // grid 10 ms or more out after every hitch.
        const double presented_at = render_end;
        next_present = presented_at + cap_period;
        handover_due = current.tick.target > 0.0 && handover_span > 0.0
                     ? current.tick.target + SIM_PERIOD + handover_span + std::min(render_end - render_start, 0.006) : 0.0;
        if (next_present <= host::now_seconds()) next_present = host::now_seconds() + cap_period;
      } else if (cap_period > 0) {
        double now = host::now_seconds();
        // Skip missed slots; never emit catch-up bursts after a stall.
        next_present += cap_period;
        if (next_present <= now) next_present += (std::floor((now-next_present)/cap_period)+1.0)*cap_period;
      }
      // A slot landing just before the predicted hand-over would hold that present back by the
      // display's minimum interval: skip it (5, 5, 6.7 ms at 200 Hz instead of 5, 5, 5, 1.7).
      if (handover_present && vrr == 1 && handover_due > 0.0 && !scanline_active &&
          next_present > handover_due - 0.6 * cap_period && next_present < handover_due + 0.5 * cap_period)
        next_present += cap_period;
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
          if (reflex_pacing && reflex_hz > 0)
            std::snprintf(cap_text, sizeof cap_text, "cap %.0f by Reflex%s", reflex_hz,
                          vrr == 1 ? ", synced under the G-Sync or FreeSync refresh" : live_options.fps_cap < 0 ? ", the monitor's G-Sync limit" : "");
          else if (vrr == 1 && cap_period > 0)
            std::snprintf(cap_text, sizeof cap_text, "cap %.0f, synced under the G-Sync or FreeSync refresh", 1.0 / cap_period);
          else if (reflex_pacing) std::snprintf(cap_text, sizeof cap_text, "%s, Reflex", live_options.vsync ? "V-Sync" : "no cap");
          else if (cap_period <= 0) std::snprintf(cap_text, sizeof cap_text, "no cap");
          else std::snprintf(cap_text, sizeof cap_text, "cap %.0f%s", 1.0 / cap_period, live_options.fps_cap == 0 ? " = 2x refresh" : live_options.fps_cap < 0 ? " = monitor" : "");
          host::log("display: %.0f fps (%s, sim %.0f Hz, %s, %u draws, %u paired, %u cuts)", stats_presented / (now - stats_time), cap_text, stats_sim / (now - stats_time),
                    !subframes ? "locked" : authored ? "authored" : interpolate ? "interpolate" : "extrapolate", s.draws, s.paired, s.cuts);
          if (subframes) host::log("pair rejection: missing %u, HUD %u, geometry %u, state %u (last BP %02X), projection %u, rollback %u, authored %u, camera-only %u, vertex-blended %u, FLIPS %u | phases <.25:%u <.5:%u <.75:%u <1:%u =1:%u",
                                   s.missing, s.hud, s.geometry, s.state, s.state_register, s.projection, s.discontinuity, s.authored, s.carried, s.vertex_blended, s.pair_flips, phase_bins[0], phase_bins[1], phase_bins[2], phase_bins[3], phase_bins[4]);
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
          if (pacing_intervals.size() >= 16) {
            std::vector<float>& v = pacing_intervals;
            std::sort(v.begin(), v.end());
            const float median = v[v.size() / 2], p99 = v[v.size() * 99 / 100], worst = v.back();
            size_t long15 = 0, long2 = 0;
            for (float x : v) { if (x > 1.5f * median) ++long15; if (x > 2.0f * median) ++long2; }
            host::log("pacing: %zu frames, interval median %.2f ms, p99 %.2f, worst %.2f | %zu over 1.5x median, %zu over 2x | %s",
                      v.size(), median, p99, worst, long15, long2, renderer->display_timing_line().c_str());
          }
          pacing_intervals.clear();
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
    host::log("renderer: %llu in-between presents dropped for a newer frame", (unsigned long long)stale_discards);
    host::log("renderer: %llu simulation frames, %llu presented frames on its own thread, %llu drained without presenting, %llu with stage held across a rollback",
              submitted, presented, (unsigned long long)drained, (unsigned long long)discontinuities);
  }

 public:
  // Started through Steam (a non-Steam shortcut), its overlay is inside this process and draws into
  // the picture. With exclusive fullscreen on top of that a report had the graphics driver hang until
  // a restart, while the same PC was fine in Big Picture, which runs games borderless. Exclusive
  // fullscreen is left for runs without the overlay.
  static RenderOptions without_exclusive_under_overlay(RenderOptions options) {
    if (options.fullscreen && options.exclusive_fullscreen && GetModuleHandleW(L"GameOverlayRenderer64.dll")) {
      options.exclusive_fullscreen = false;
      host::log("display: the Steam overlay is loaded; using borderless fullscreen instead of exclusive");
    }
    return options;
  }
  ThreadedBackend(RenderOptions requested, bool visible) : options_(without_exclusive_under_overlay(requested)) {
    std::promise<void> initialized;
    auto ready = initialized.get_future();
    worker = std::thread([this, options = options_, visible, init = std::move(initialized)]() mutable {
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
