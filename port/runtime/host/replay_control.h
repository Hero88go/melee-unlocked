// Replay viewer controls, shared by both viewers (the Source Port's --replay and the Static Recomp's
// playback build): pause, frame step, slow motion, fast forward, jumps, and showing a session "as it
// was played" from the trace file beside the replay.
//
// Everything here changes only WHEN a replay frame is simulated and whether it is shown. The frames
// themselves come from the replay file as before, so paused, stepped, slowed or jumped playback
// reaches the same game states as plain playback. With no control used and no test script the gate
// returns at once.
//
// The engine calls gate() on the simulation thread before it simulates each replay frame. The bar
// (gx/replay_bar.h) reads status() and sends its clicks from the render thread.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <string>

namespace replay_control {

constexpr int32_t kNoFrame = INT32_MIN;
// The most states an engine is asked to keep at once (Engine::keep): size the pool for this many.
constexpr int kMaxKeptStates = 60;

// What the engine playing the replay can do about a jump back. Simulation thread.
struct Engine {
  // Keep the game's state as it is now, before replay frame `frame` is simulated. Null when the
  // engine keeps no states.
  void (*keep)(int32_t frame) = nullptr;
  // Bring back the newest kept state at or before `target` and return its frame; kNoFrame when
  // there is none (nothing changed).
  int32_t (*restore)(int32_t target) = nullptr;
  // No kept states: start the match again from its first frame. The engine calls begin() again
  // when the match restarts. False when it cannot.
  bool (*restart)() = nullptr;
};

enum class Gate {
  Run,       // simulate the frame
  GoBack,    // a jump back is wanted: call go_back() where the engine can restore a state
  Restored,  // go_back(): an older state is in place, the game goes on from it
  Restart,   // go_back(): the engine was asked to start the match again
};

// Command line, before the replay loads: --trace <file> names the session trace; --as-experienced
// looks for "<replay>.trace" beside the replay. Either one turns "as it was played" on.
void set_trace_file(const std::string& path);
void set_as_experienced(bool on);
void set_replay_path(const std::string& path);

// The match starts (or starts again after a restart): replay frames first_frame..last_frame.
void begin(int32_t first_frame, int32_t last_frame, const Engine& engine);
void end();
bool active();

// Simulation thread, before replay frame `frame` is simulated. Blocks while paused (the window and
// the settings panel stay alive on the render thread), paces slow motion, fast forward and the
// session's own timing, and keeps the states a jump back needs.
Gate gate(int32_t frame);
Gate go_back();

// Simulation thread (gx_core): the frame being simulated is on the way to a jump's target and is
// not shown.
bool skip_render();
// Render thread: the picture stands still (paused, or held as the session held it) but the bar,
// the overlay and the settings panel still have to be drawn.
bool repaint_wanted();

struct Status {
  bool active = false, paused = false, seeking = false, fast = false, as_played = false;
  int32_t frame = 0, first_frame = 0, last_frame = 0, seek_target = 0;
  double speed = 1.0;       // 1, 0.5 or 0.25
  double last_input = 0.0;  // host::now_seconds() of the last control used
};
Status status();

// Render thread: the bar's own controls, and whether a menu has the keyboard and the pads (the
// viewer's keys do nothing then).
void toggle_pause();
void seek_to(int32_t frame);
void set_ui_busy(bool busy);

}  // namespace replay_control
