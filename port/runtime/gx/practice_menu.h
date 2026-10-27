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
  std::string offer;        // after an online session: "... Search Unranked again?" (D-pad Up / Down)
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

// The replay viewer's settings (Start while watching a replay, gx/replay_hud.h), in the same look.
struct ReplaySettings {
  uint8_t length = 3;          // a takeover's length: 0 until the combo drops, 1 ten seconds, 2 twenty, 3 no limit (default)
  uint8_t defense = 0;         // the other players once you change what happened: 0 smart, 1 no DI or tech,
                               // 2 the Practice CPU's settings, 3 follow the recording
  uint8_t tips = 1;            // tip banners and timeline markers
};
ReplaySettings& replay_settings();
// Start while watching opens it: take over the player named (left and right change the player), the
// player's tips in order (A jumps a second before one), and the settings one page down.
struct ReplayTip {
  int32_t frame = 0;
  std::string when, what, value, advice;
};
struct ReplayPanelIn {
  std::string player;          // "P1 Falco"
  std::vector<ReplayTip> tips;
};
struct ReplayPanel {
  bool close = false;          // back to the replay
  bool take_over = false;      // take over `player`
  int player_step = 0;         // -1 or 1: the previous or next player
  int32_t jump = INT32_MIN;    // jump to this replay frame (the panel closes, the replay stays paused)
};
ReplayPanel replay_panel(int style, const Frame& f, const ReplayPanelIn& in);
void show_error(const std::string& s);  // shown in the description box (a bad Direct code)

// Melee's own text: the game's font, decoded from the player's disc at first use (falls back to
// the PC font when the disc has none).
float text_width(float px, const char* s);
// A row of GameCube button glyphs and words, "A:Take over|X:Tip|S:Menu" (A B X Y Z L R, S Start,
// D D-pad), centred on `cy`; returns the width (draw false only measures).
float legend(ImDrawList* dl, float x, float cy, float u, const char* spec, float a = 1.0f, bool draw = true);
// `s` broken into lines no wider than `width` in the game's font at `px`.
std::vector<std::string> wrap_text(const std::string& s, float px, float width);
void text(ImDrawList* dl, float x, float y, float px, uint32_t color, const char* s, float skew = 0.0f,
          bool shadow = true);

}  // namespace practice_menu
