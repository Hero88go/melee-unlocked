// SPDX-License-Identifier: GPL-2.0-or-later
#include "replay_control.h"
#include "audio.h"
#include "host.h"
#ifdef MELEE_NO_SLIPPI
#include "netplay_state.h"   // no host music player to pause in this build
#else
#include "jukebox.h"
#endif
#include "net_overlay.h"
#include "net_trace.h"
#include "net_trace_file.h"
#include "net_trace_read.h"
#include "window.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace gx { void mark_discontinuity(); }   // gx_core.h: the next frame is not the neighbour of the last one shown

namespace replay_control {
namespace {

constexpr double kTick = net_trace::kTickSeconds;
constexpr double kSpeeds[3] = {0.25, 0.5, 1.0};
constexpr int kNormalSpeed = 2;               // index of 1.0 in kSpeeds
constexpr double kFastFactor = 4.0;           // fast forward: four times the game's rate
constexpr int32_t kJumpFrames = 5 * 60, kLongJumpFrames = 30 * 60;
constexpr int32_t kKeepMinFrames = 2 * 60;    // kept states are never closer than two seconds
constexpr uint16_t kPadStart = 0x1000, kPadZ = 0x0010, kPadR = 0x0020;

struct Keys {
  bool space = false, left = false, right = false, up = false, down = false, home = false, tab = false,
       period = false, slower = false, faster = false, shift = false;
};

// One action of MELEE_TEST_REPLAY_CONTROLS (see parse_script).
struct ScriptItem {
  enum Kind { Pause, Step, Back, Forward, Seek, Restart, Speed } kind = Pause;
  int32_t frame = 0, arg = 0;
  bool has_arg = false, fired = false;
  double speed = 1.0;
};

struct State {
  // Shared with the render thread.
  std::atomic<bool> active{false}, paused{false}, seeking{false}, fast{false}, skip_render{false},
      holding{false}, ui_busy{false}, as_played{false};
  std::atomic<int32_t> frame{0}, first_frame{0}, last_frame{0}, seek_target{0}, req_seek{kNoFrame};
  std::atomic<int> speed_index{kNormalSpeed}, req_toggle{0};
  std::atomic<double> last_input{0.0};

  // Simulation thread only.
  Engine engine;
  bool configured = false;
  bool base_fast = false;          // the run itself is unpaced (--fast): no sleeps of ours either
  bool keep_on = false;
  int32_t keep_interval = kKeepMinFrames, next_keep = kNoFrame, first_kept = kNoFrame;
  int32_t want_seek = kNoFrame, back_target = kNoFrame, seek_from = 0;
  double seek_started = 0.0, last_release = 0.0, last_toggle = 0.0, last_pad_poll = 0.0;
  int step_pending = 0;
  bool resume_after_steps = false, fast_script = false;
  double script_resume_at = 0.0;
  std::vector<ScriptItem> script;
  // Keys and pads as last read, for edges.
  Keys prev_keys;
  bool prev_start = false, prev_valid = false;
  double step_hold_since = 0.0, step_repeat_at = 0.0;

  // "As it was played".
  std::string trace_file, replay_path;
  bool as_experienced = false, trace_checked = false, trace_on = false;
  net_trace::Schedule schedule;
  net_trace::Pacer pacer;
  std::vector<net_trace::Pacer::Step> steps;
  int32_t trace_last = kNoFrame;   // the last frame whose ticks went to the overlay
};
State g;

const char* base_name(const std::string& path) {
  const size_t slash = path.find_last_of("\\/");
  return path.c_str() + (slash == std::string::npos ? 0 : slash + 1);
}

void note_input() { g.last_input.store(host::now_seconds(), std::memory_order_relaxed); }

// The game's sound is not queued while the picture is not moving at the game's own rate, and the
// music holds its place while the picture stands still or jumps.
void apply_audio() {
  const bool paused = g.paused.load(), seeking = g.seeking.load();
  host::audio_set_muted(paused || seeking || g.fast.load() || g.speed_index.load() != kNormalSpeed);
  slippi::jukebox::set_paused(paused || seeking);
}

// The host's own 60 Hz pacing off (fast forward and jumps) or back as the run had it.
void set_unpaced(bool on) { host::options.fast = g.base_fast || on; }

void pump_own_window() {
  if (host::g_has_window) host::window_pump();   // the simulation thread owns the window only without the render thread
}

void sleep_until(double deadline) {
  while (!host::exit_requested()) {
    const double left = deadline - host::now_seconds();
    if (left <= 0.0) break;
    if (left > 0.002) Sleep(1);
    else SwitchToThread();
  }
}

// The picture is held as the session held it; the render thread keeps drawing the overlay.
void hold_for(double seconds) {
  g.holding.store(true, std::memory_order_relaxed);
  const double until = host::now_seconds() + seconds;
  while (!host::exit_requested()) {
    const double left = until - host::now_seconds();
    if (left <= 0.0) break;
    pump_own_window();
    sleep_until(host::now_seconds() + std::min(left, 0.004));
  }
  g.holding.store(false, std::memory_order_relaxed);
}

void set_paused(bool paused, int32_t frame) {
  if (g.paused.load() == paused) return;
  g.paused.store(paused);
  g.script_resume_at = 0.0;
  g.resume_after_steps = false;
  g.step_pending = 0;
  host::log(paused ? "replay: paused at frame %d" : "replay: resumed at frame %d", frame);
  apply_audio();
  if (!paused) g.last_release = host::now_seconds();
}

void set_speed(int index, int32_t frame) {
  index = std::clamp(index, 0, kNormalSpeed);
  if (index == g.speed_index.load()) return;
  g.speed_index.store(index);
  host::log("replay: speed %gx at frame %d", kSpeeds[index], frame);
  apply_audio();
}

void set_fast(bool fast, int32_t frame) {
  if (g.fast.load() == fast) return;
  g.fast.store(fast);
  host::log(fast ? "replay: fast forward from frame %d" : "replay: fast forward ends at frame %d", frame);
  if (!g.seeking.load()) set_unpaced(fast);
  apply_audio();
  g.last_release = host::now_seconds();
}

// MELEE_TEST_REPLAY_CONTROLS (tests): a comma separated list of <action>@<frame>[:<argument>]. The
// frame is a replay frame number as the log prints it (the match's first frame is -123); the action
// fires once, before that frame is simulated.
//   pause@F:N    pause, resume by itself after N sixtieths of a second (default 60)
//   step@F:N     pause, step N frames (default 1), resume
//   back@F:N     jump back N frames (default 300)        fwd@F:N   jump forward N frames (default 300)
//   seek@F:T     jump to frame T                         restart@F jump to the first frame
//   speed@F:S    S is 4x (fast forward until another speed action), 1x, 0.5x or 0.25x
// Example: "pause@600:120,back@1500:300,speed@2000:4x,speed@2600:1x".
void parse_script() {
  const char* text = std::getenv("MELEE_TEST_REPLAY_CONTROLS");
  if (!text || !*text) return;
  const std::string all(text);
  for (size_t pos = 0; pos < all.size();) {
    size_t comma = all.find(',', pos);
    if (comma == std::string::npos) comma = all.size();
    std::string item = all.substr(pos, comma - pos);
    pos = comma + 1;
    while (!item.empty() && item.front() == ' ') item.erase(item.begin());
    while (!item.empty() && item.back() == ' ') item.pop_back();
    if (item.empty()) continue;
    const size_t at = item.find('@');
    std::string name = item.substr(0, at), rest = at == std::string::npos ? std::string() : item.substr(at + 1), arg;
    const size_t colon = rest.find(':');
    if (colon != std::string::npos) {
      arg = rest.substr(colon + 1);
      rest.resize(colon);
    }
    ScriptItem it;
    char* stop = nullptr;
    it.frame = (int32_t)std::strtol(rest.c_str(), &stop, 10);
    const bool frame_ok = !rest.empty() && stop && *stop == 0;
    it.has_arg = !arg.empty();
    it.arg = (int32_t)std::strtol(arg.c_str(), nullptr, 10);
    bool known = true;
    if (name == "pause") it.kind = ScriptItem::Pause;
    else if (name == "step") it.kind = ScriptItem::Step;
    else if (name == "back") it.kind = ScriptItem::Back;
    else if (name == "fwd") it.kind = ScriptItem::Forward;
    else if (name == "seek") it.kind = ScriptItem::Seek;
    else if (name == "restart") it.kind = ScriptItem::Restart;
    else if (name == "speed") { it.kind = ScriptItem::Speed; it.speed = std::strtod(arg.c_str(), nullptr); }
    else known = false;
    if (!known || !frame_ok || (it.kind == ScriptItem::Seek && !it.has_arg) || (it.kind == ScriptItem::Speed && it.speed <= 0.0)) {
      host::log("replay: test controls: \"%s\" is not an action (see replay_control.cpp)", item.c_str());
      continue;
    }
    g.script.push_back(it);
  }
  host::log("replay: test controls: %zu actions (%s)", g.script.size(), text);
}

void run_script(int32_t frame) {
  for (ScriptItem& it : g.script) {
    if (it.fired || frame < it.frame) continue;
    it.fired = true;
    note_input();
    switch (it.kind) {
      case ScriptItem::Pause:
        set_paused(true, frame);
        g.script_resume_at = host::now_seconds() + (it.has_arg ? std::max(it.arg, 1) : 60) / 60.0;
        break;
      case ScriptItem::Step:
        set_paused(true, frame);
        g.step_pending = it.has_arg ? std::max(it.arg, 1) : 1;
        g.resume_after_steps = true;
        break;
      case ScriptItem::Back: g.want_seek = frame - (it.has_arg ? it.arg : kJumpFrames); break;
      case ScriptItem::Forward: g.want_seek = frame + (it.has_arg ? it.arg : kJumpFrames); break;
      case ScriptItem::Seek: g.want_seek = it.arg; break;
      case ScriptItem::Restart: g.want_seek = g.first_frame.load(); break;
      case ScriptItem::Speed:
        g.fast_script = it.speed >= 2.0;
        set_fast(g.fast_script, frame);
        set_speed(it.speed >= 0.75 ? kNormalSpeed : it.speed >= 0.375 ? 1 : 0, frame);
        break;
    }
  }
}

// The keyboard, only while the game window has the focus and no menu has the keys.
Keys read_keys() {
  Keys k;
  const HWND front = GetForegroundWindow();
  DWORD pid = 0;
  if (!front || !GetWindowThreadProcessId(front, &pid) || pid != GetCurrentProcessId()) return k;
  const auto down = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
  k.space = down(VK_SPACE); k.left = down(VK_LEFT); k.right = down(VK_RIGHT); k.up = down(VK_UP);
  k.down = down(VK_DOWN); k.home = down(VK_HOME); k.tab = down(VK_TAB); k.period = down(VK_OEM_PERIOD);
  k.slower = down(VK_OEM_4); k.faster = down(VK_OEM_6); k.shift = down(VK_SHIFT);
  return k;
}

void toggle_pause_now(int32_t frame) {
  const double now = host::now_seconds();
  // A keyboard key bound as the controller's Start arrives as both a key and a button.
  if (now - g.last_toggle < 0.2) return;
  g.last_toggle = now;
  set_paused(!g.paused.load(), frame);
  note_input();
}

// Keys, pads and the bar's clicks. Nothing read here reaches the game: playback takes its inputs
// from the replay file.
void poll_controls(int32_t frame, bool waiting) {
  const double now = host::now_seconds();
  if (g.req_toggle.exchange(0) & 1) toggle_pause_now(frame);
  const int32_t wanted = g.req_seek.exchange(kNoFrame);
  if (wanted != kNoFrame) { g.want_seek = wanted; note_input(); }

  // While the game stands still nothing reads the controllers: read them here, so Start resumes.
  if (waiting && now - g.last_pad_poll >= 0.016) {
    g.last_pad_poll = now;
    host::PadState unused[4];
    host::input_poll(unused);
  }
  if (g.ui_busy.load(std::memory_order_relaxed)) {
    // A menu has the keys and the pads. Whatever is held when it closes is not a new press.
    Keys held;
    held.space = held.left = held.right = held.up = held.down = held.home = held.period = held.slower = held.faster = true;
    g.prev_keys = held;
    g.prev_start = true;
    g.prev_valid = true;
    if (!g.fast_script) set_fast(false, frame);
    return;
  }
  const Keys k = read_keys();
  bool start = false, trigger = false;
  host::PadState pads[4];
  if (host::window_ui_pads(pads)) {
    for (const host::PadState& p : pads) {
      if (p.err != 0) continue;
      if ((p.button & kPadStart) && !(p.button & kPadZ)) start = true;   // Z + Start opens the settings
      if ((p.button & kPadR) || p.trig_r > 150) trigger = true;
    }
  }
  if (!g.prev_valid) {   // the first read: nothing held now is a press
    g.prev_keys = k;
    g.prev_start = start;
    g.prev_valid = true;
  }
  const Keys was = g.prev_keys;
  const bool was_start = g.prev_start;
  g.prev_keys = k;
  g.prev_start = start;

  if ((k.space && !was.space) || (start && !was_start)) toggle_pause_now(frame);
  const bool paused = g.paused.load();
  const int32_t jump = k.shift ? kLongJumpFrames : kJumpFrames;
  const bool step_key = k.right || k.period, step_was = was.right || was.period;
  if (paused) {
    // Right or period: one frame; held, it repeats.
    if (step_key && !step_was) {
      g.step_pending = 1;
      g.step_hold_since = now;
      g.step_repeat_at = now + 0.35;
      note_input();
    } else if (step_key && now >= g.step_repeat_at && g.step_pending == 0) {
      g.step_pending = 1;
      g.step_repeat_at = now + 0.05;
      note_input();
    }
  } else if (k.right && !was.right) {
    g.want_seek = frame + jump;
    note_input();
  }
  if (k.left && !was.left) { g.want_seek = frame - jump; note_input(); }
  if (k.home && !was.home) { g.want_seek = g.first_frame.load(); note_input(); }
  if ((k.slower && !was.slower) || (k.down && !was.down)) { set_speed(g.speed_index.load() - 1, frame); note_input(); }
  if ((k.faster && !was.faster) || (k.up && !was.up)) { set_speed(g.speed_index.load() + 1, frame); note_input(); }
  const bool fast = !paused && (k.tab || trigger || g.fast_script);
  if (fast && !g.fast.load()) note_input();
  set_fast(fast, frame);
}

void keep_if_due(int32_t frame) {
  if (!g.keep_on || (g.next_keep != kNoFrame && frame < g.next_keep)) return;
  g.engine.keep(frame);
  if (g.first_kept == kNoFrame || frame < g.first_kept) g.first_kept = frame;
  g.next_keep = frame + g.keep_interval;
}

// Frames from `frame` up to the target are simulated unpaced, unseen and unheard.
void enter_seek(int32_t frame, int32_t target) {
  g.seek_target.store(target);
  g.seeking.store(true);
  g.skip_render.store(frame < target - 1);
  set_unpaced(true);
  apply_audio();
}

void finish_seek(int32_t frame) {
  g.seeking.store(false);
  g.skip_render.store(false);
  set_unpaced(g.fast.load());
  apply_audio();
  gx::mark_discontinuity();
  g.trace_last = kNoFrame;
  const double now = host::now_seconds();
  g.last_release = now;
  host::log("replay: jumped to frame %d (from %d, %.0f ms)", frame, g.seek_from, (now - g.seek_started) * 1000.0);
}

Gate begin_seek(int32_t frame, int32_t target) {
  const int32_t first = g.first_frame.load(), last = g.last_frame.load();
  // Not into the last second: reaching the end closes the viewer.
  target = std::clamp(target, first, std::max(first, last - 60));
  if (target == frame || (target > frame && frame >= last - 60)) return Gate::Run;
  g.seek_from = frame;
  g.seek_started = host::now_seconds();
  if (target > frame) {
    enter_seek(frame, target);
    return Gate::Run;
  }
  if (!g.engine.restore && !g.engine.restart) {
    host::log("replay: cannot jump back from frame %d (this viewer keeps no earlier state)", frame);
    return Gate::Run;
  }
  g.back_target = target;
  return Gate::GoBack;
}

// The session trace beside the replay: loaded once, when the replay's frame range is known.
void load_trace() {
  if (g.trace_checked) return;
  g.trace_checked = true;
  if (!g.as_experienced && g.trace_file.empty()) return;
  const std::string path = !g.trace_file.empty() ? g.trace_file : net_trace::trace_path(g.replay_path);
  net_trace::Trace trace;
  std::string error;
  if (!net_trace::read_trace(path, &trace, &error) || trace.records.empty()) {
    host::log("replay: no session trace to show (%s: %s); playing the replay normally", base_name(path),
              error.empty() ? "it has no rows" : error.c_str());
    return;
  }
  // MELEE_TRACE_FRAME_OFFSET (tests): the online frame number minus the replay frame number.
  int32_t offset = net_trace::kOnlineToReplay;
  if (const char* v = std::getenv("MELEE_TRACE_FRAME_OFFSET")) offset = (int32_t)std::strtol(v, nullptr, 10);
  g.schedule.build(trace, offset);
  // The trace has to be this match's: its frames must cover the replay's to within five seconds.
  const int32_t slack = 5 * 60;
  const int32_t first = g.first_frame.load(), last = g.last_frame.load();
  if (g.schedule.empty() || std::abs(g.schedule.first_frame() - first) > slack || std::abs(g.schedule.last_frame() - last) > slack) {
    host::log("replay: the session trace %s does not match this replay (trace frames %d to %d, replay %d to %d); playing the replay normally",
              base_name(path), g.schedule.empty() ? 0 : g.schedule.first_frame(), g.schedule.empty() ? 0 : g.schedule.last_frame(), first, last);
    return;
  }
  g.pacer.set(&g.schedule);
  g.trace_on = true;
  g.as_played.store(true);
  net_overlay::set_replay(true);
  net_trace::begin_match();
  host::log("replay: showing the session as it was played (%zu trace rows)", g.schedule.size());
}

// The ticks the session spent on this frame go to the overlay's ring, and the picture is held for
// as long as the session held it.
//
// What is reproduced is the timing and the overlay. The rollback frames the player saw (frames
// simulated on a guess, displayed, then replaced) are NOT reconstructed in this version: the replay
// file holds only the final frames. A rollback is marked on the overlay, with its depth.
void show_trace(int32_t frame, bool paced) {
  if (g.trace_last == kNoFrame || frame != g.trace_last + 1) {
    // The start, or playback jumped: the overlay's window is refilled with the ticks before here.
    net_trace::begin_match();
    g.pacer.reset();
    size_t first = g.schedule.size(), count = 0;
    for (int32_t probe = std::max(frame, g.schedule.first_frame()); probe <= g.schedule.last_frame(); ++probe)
      if (g.schedule.frame_records(probe, &first, &count)) break;
    for (size_t i = first > net_trace::kCapacity ? first - net_trace::kCapacity : 0; i < first; ++i)
      net_trace::push(g.schedule.record(i));
  }
  g.trace_last = frame;
  double real = 0.0;
  g.pacer.next(frame, &g.steps, &real);
  double held = 0.0;
  for (const auto& step : g.steps) {
    if (paced && step.wait > 0.0) {
      hold_for(step.wait);
      held += step.wait;
    }
    net_trace::push(g.schedule.record(step.record));
  }
  if (paced && real >= 0.25) {
    char note[120];
    if (real > held + 0.05) std::snprintf(note, sizeof note, "The game stood still for %.1f s here (shown as %.1f s)", real, held);
    else std::snprintf(note, sizeof note, "The game stood still for %.2f s here", real);
    net_overlay::set_replay_note(note, 4.0);
  }
}

// When the frame runs. At normal speed the host's own retrace pacing does it; slow motion and fast
// forward are paced here, against the time the frame before was let through.
void pace(int32_t frame) {
  const bool fast = g.fast.load();
  const int speed = g.speed_index.load();
  if (g.trace_on) show_trace(frame, !g.base_fast && !fast && speed == kNormalSpeed);
  if (!g.base_fast) {
    if (fast) sleep_until(g.last_release + kTick / kFastFactor);
    else if (speed != kNormalSpeed) sleep_until(g.last_release + kTick / kSpeeds[speed]);
  }
  g.last_release = host::now_seconds();
}

}  // namespace

void set_trace_file(const std::string& path) { g.trace_file = path; }
void set_as_experienced(bool on) { g.as_experienced = on; }
void set_replay_path(const std::string& path) { g.replay_path = path; }

void begin(int32_t first_frame, int32_t last_frame, const Engine& engine) {
  if (!g.configured) {
    g.configured = true;
    g.base_fast = host::options.fast;
    parse_script();
  }
  g.engine = engine;
  g.first_frame.store(first_frame);
  g.last_frame.store(last_frame);
  g.frame.store(first_frame);
  // States for jumping back cover the whole replay: one every two seconds, further apart for a
  // long match so their number stays under kMaxKeptStates. An unpaced run with no test script
  // (the replay compare gates) keeps none.
  const int32_t total = std::max(1, last_frame - first_frame + 1);
  g.keep_interval = std::max(kKeepMinFrames, (total + (kMaxKeptStates - 10) - 1) / (kMaxKeptStates - 10));
  g.keep_on = engine.keep != nullptr && (!g.script.empty() || !g.base_fast);
  g.next_keep = kNoFrame;
  g.first_kept = kNoFrame;
  g.prev_valid = false;
  g.trace_last = kNoFrame;
  g.last_release = host::now_seconds();
  load_trace();
  if (!g.active.exchange(true) && g.keep_on)
    host::log("replay: controls on, a state kept every %d frames for jumping back", g.keep_interval);
}

void end() {
  if (!g.active.exchange(false)) return;
  g.paused.store(false);
  g.seeking.store(false);
  g.skip_render.store(false);
  g.fast.store(false);
  host::options.fast = g.base_fast;
  host::audio_set_muted(false);
  slippi::jukebox::set_paused(false);
  if (g.trace_on) net_overlay::set_replay(false);
}

bool active() { return g.active.load(std::memory_order_relaxed); }
bool skip_render() { return g.skip_render.load(std::memory_order_relaxed); }
bool repaint_wanted() {
  return g.active.load(std::memory_order_relaxed) &&
         (g.paused.load(std::memory_order_relaxed) || g.holding.load(std::memory_order_relaxed));
}

Gate gate(int32_t frame) {
  if (!g.active.load(std::memory_order_relaxed)) return Gate::Run;
  g.frame.store(frame, std::memory_order_relaxed);
  if (g.seeking.load()) {
    const int32_t target = g.seek_target.load();
    if (frame >= target - 1) g.skip_render.store(false);   // the frame before the target is shown again
    if (frame < target) {
      keep_if_due(frame);
      return Gate::Run;
    }
    finish_seek(frame);
  }
  run_script(frame);
  bool waiting = false;
  for (;;) {
    poll_controls(frame, waiting);
    if (g.want_seek != kNoFrame) {
      const int32_t target = g.want_seek;
      g.want_seek = kNoFrame;
      if (begin_seek(frame, target) == Gate::GoBack) return Gate::GoBack;
      if (g.seeking.load()) {
        keep_if_due(frame);
        return Gate::Run;
      }
    }
    if (!g.paused.load()) break;
    if (g.step_pending > 0) {   // one frame is let through, the pause holds at the next
      --g.step_pending;
      break;
    }
    if (g.resume_after_steps) { set_paused(false, frame); break; }
    if (g.script_resume_at > 0.0 && host::now_seconds() >= g.script_resume_at) { set_paused(false, frame); break; }
    if (host::exit_requested()) break;
    waiting = true;
    pump_own_window();
    Sleep(4);
  }
  keep_if_due(frame);
  pace(frame);
  return Gate::Run;
}

Gate go_back() {
  const int32_t target = g.back_target;
  g.back_target = kNoFrame;
  if (!g.active.load() || target == kNoFrame) return Gate::Run;
  if (g.engine.restore) {
    const int32_t goal = g.first_kept != kNoFrame ? std::max(target, g.first_kept) : target;
    const int32_t landed = g.engine.restore(goal);
    if (landed != kNoFrame) {
      // The game goes on from the kept state: `landed` is simulated next, with no gate call for it.
      g.next_keep = landed + g.keep_interval;
      g.frame.store(landed);
      if (landed < goal) enter_seek(landed, goal);
      else finish_seek(landed);
      return Gate::Restored;
    }
  }
  if (g.engine.restart && g.engine.restart()) {
    enter_seek(g.first_frame.load(), target);
    return Gate::Restart;
  }
  host::log("replay: cannot jump back from frame %d (no earlier state is kept)", g.seek_from);
  return Gate::Run;
}

Status status() {
  Status s;
  s.active = g.active.load(std::memory_order_relaxed);
  s.paused = g.paused.load(std::memory_order_relaxed);
  s.seeking = g.seeking.load(std::memory_order_relaxed);
  s.fast = g.fast.load(std::memory_order_relaxed);
  s.as_played = g.as_played.load(std::memory_order_relaxed);
  s.frame = g.frame.load(std::memory_order_relaxed);
  s.first_frame = g.first_frame.load(std::memory_order_relaxed);
  s.last_frame = g.last_frame.load(std::memory_order_relaxed);
  s.seek_target = g.seek_target.load(std::memory_order_relaxed);
  s.speed = kSpeeds[std::clamp(g.speed_index.load(std::memory_order_relaxed), 0, kNormalSpeed)];
  s.last_input = g.last_input.load(std::memory_order_relaxed);
  return s;
}

void toggle_pause() { g.req_toggle.fetch_add(1); }
void seek_to(int32_t frame) { g.req_seek.store(frame); }
void set_ui_busy(bool busy) { g.ui_busy.store(busy, std::memory_order_relaxed); }

}  // namespace replay_control
