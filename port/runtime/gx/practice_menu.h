// The Practice menu as players see it in Training: one menu (pages of rows bound to the practice CPU
// settings and the search while practicing) drawn in one of four looks, all driven by the
// controller. The full Practice window (pc_settings.cpp) stays for the mouse.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <climits>
#include <string>
#include <vector>

struct ImDrawList;

namespace practice_menu {

// Saved as "practicestyle". kWindow is the full Practice window, for the mouse and keyboard.
enum Style : int { kBars = 0, kRails = 1, kSlots = 2, kStrip = 3, kWindow = 4, kStyleCount = 5 };
const char* style_name(int style);

struct Search {
  bool available = false;   // the search can be used here (Training, event matches)
  bool can_start = false;
  bool searching = false;
  std::string mode;         // "Unranked", "Direct"
  std::string elapsed;      // "0:42"
  std::string state;        // what the matchmaking server is doing
  std::string opponent;
  std::string code;         // the Direct code searched for
};

struct Frame {
  float display_w = 0, display_h = 0;
  float game_x0 = 0, game_y0 = 0, game_x1 = 0, game_y1 = 0;   // the game picture in the window
  bool native_source = false;   // Source Port: the Training tools run there
  bool open = false;            // the menu is open (closed: only what a look keeps on screen)
  bool armed = false;           // the controller was let go since the menu opened
  uint16_t buttons = 0;         // GameCube buttons of the menu's controller
  int8_t stick_x = 0, stick_y = 0;
  bool keyboard = true;         // arrow keys, Enter, Esc and typing also drive it
  bool hint = false;            // closed: show how to open it
  Search search;
  double time = 0;              // seconds, for animation and key repeat
};

struct Result {
  bool close = false;            // leave the menu
  bool changed = false;          // a saved setting changed (practice CPU, blocked codes)
  bool start_unranked = false;
  bool start_direct = false;
  bool start_teams = false;       // Teams with direct_code (everyone enters the same code)
  bool cancel_search = false;
  bool open_window = false;      // "All settings": the full Practice window
  std::string direct_code;
};

// Draws the menu (open) or what the look keeps on screen while closed (the search time, the
// strip), and handles the controller and keyboard. Call once per overlay frame inside ImGui.
Result frame(int style, const Frame& f);
void opened();                          // the menu was just opened

// The replay viewer's menu (practice from a replay): shown while the viewer is paused, in the same
// look. Take over a player, retry the attempt, or go back to watching.
struct Replay {
  uint32_t ports = 0;          // bit per player in the replay
  std::string names[4];        // "P1 Falco"
  int live_port = -1;          // the port being played live, -1 while watching
  // The newest frame data readout (host/replay_coach.h), shown top left; alpha 0 hides it.
  struct Note {
    std::string head, value, detail, advice;
    int sign = 0;              // the value's color: -1 minus, 0 even, 1 plus
    float alpha = 0.0f;
  } note;
  // The coach's moments (host/replay_coach.h), per port, once the replay was analyzed.
  struct Moment {
    int32_t frame = 0;
    std::string when, what, value, advice;
  };
  std::vector<Moment> moments[4];
  bool analyzed = false, analyzing = false;
  float progress = 0.0f;
};
struct ReplayResult {
  int take_over = -1;          // the port to take over
  bool retry = false, stop = false, resume = false;
  bool analyze = false;          // find the moments (the viewer's Analyze pass)
  int32_t jump = INT32_MIN;     // jump to this replay frame and stay paused
};
ReplayResult replay_frame(int style, const Frame& f, const Replay& r);
void show_error(const std::string& s);  // shown in the description box (a bad Direct code)

// Melee's own text: the game's font, decoded from the player's disc at first use (falls back to
// the PC font when the disc has none).
float text_width(float px, const char* s);
void text(ImDrawList* dl, float x, float y, float px, uint32_t color, const char* s, float skew = 0.0f,
          bool shadow = true);

}  // namespace practice_menu
