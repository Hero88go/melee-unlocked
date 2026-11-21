// Arena rooms for Slippi Direct: friends beyond the two players watch the match live, in a viewer
// window of their own, and rotate in by the room's rules (arena_rules.h).
//
// Every member keeps one room link to the room's host (the member who made the room). A link is a
// Slippi netplay connection of its own, on its own socket, made the way a Direct match connects; it
// carries only room messages (NP_MSG_MU_ARENA). The two fighters still play over their own Direct
// connection exactly as in a plain Direct game: nobody watching is ever on it.
//
// Between games the room starts the Direct search between the two friends it put in the ring, from
// the online character select; the players pick their characters and press Start as usual. During a
// game the first fighter (ring slot 0) sends the live feed (arena_feed.h) to the host, which passes it to
// everyone watching; each watcher's game writes it as a recording still being written and opens a
// viewer window on it.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "arena_rules.h"
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace slippi::arena {

// ---- setup (before the game starts) ----
// Test rooms with no Slippi servers: --arena-test <index>/<count>/<base port>[/<character>]. Member 0
// hosts; each member i links to it on loopback (host side base+100+i, member side base+200+i) and
// plays its matches on base+300+20*(games%10)+i. Every member is locked in with <character> (default Fox) as soon
// as it is connected, since nobody presses Start in a test.
bool set_test(const std::string& spec);
bool test_mode();
bool locks_in_itself();   // tests: the room locks pairs in with a random stage (test room or MELEE_TEST_ARENA_AUTOLOCK)
// The online recording's code list (a watcher's live recording needs it; the feed does not carry it).
void set_recording_codes(std::vector<uint8_t> codes);
// Opens a viewer window on a live recording; returns its process handle, or null. Set by the host.
void set_viewer_launcher(std::function<void*(const std::string& path)> launch);

// ---- simulation thread ----
void tick();                                                   // once a frame, any scene
void recording_event(const uint8_t* data, uint32_t size);     // the local recording, command byte first
void shutdown();

// ---- the panel (UI thread) ----
enum class Act : uint8_t {
  ToggleLine = 1, Ask = 2, Answer = 3, GiveUp = 4, SetMode = 5, ProposeSwitch = 6, VoteSwitch = 7, Leave = 8,
  SetCharacter = 9,              // arg the character, text[0] the costume (sent by the room itself)
  OpenRoom = 20, AddCode = 21,   // the host's own (AddCode carries a connect code)
  LeaveRoom = 22,                // this player leaves the room (a host's room closes with it)
  JoinYouGame = 23,              // under YouGame: join the arena whose room code is text (an invite)
};
void act(Act a, uint8_t arg = 0, const std::string& text = {});

struct View {
  bool active = false;      // in a room
  bool hosting = false;
  bool linked = false;      // a member: the link to the host is up
  uint8_t self = kNone;
  Room room;
  std::array<int, kMaxMembers> ping_ms{};   // each member's ping to the host, -1 unknown, 0 the host
  bool watching_live = false;               // a viewer window is open on this game
  std::string status;                       // one plain line: what this member's game is doing
  std::string notice;                       // a short-lived line beside the room tab (a full room), else empty
  uint16_t warming = 0;                     // who is warming up in Training (the warm-up room), a bit per member id
};
View view();
// Tests only (MELEE_TEST_ARENA_FIXTURE=<state>[:long]): view() answers a made-up room so the panel can
// be seen in every state on any character select, offline included. States: alone, n2, n3, n5, n8,
// n12, n16, asking (this member asked to play), prompt (this fighter is asked), watching, doubles
// (a vote to switch), linklost. ":long" gives 8-wide names, 3-digit records and pings.
bool test_fixture_active();
// MELEE_TEST_ARENA_FIXTURE=cycle: the look to show now (each state in turn under each look), else -1.
int test_fixture_look();
int test_fixture_index();                 // -1 unless cycling
const char* test_fixture_state(int index);
// True while this game is in a room (any thread).
bool in_room();
// True for a few seconds after this game left a room or its room closed (any thread): the Direct pair
// that ends with it is not a finished online session (no "back to Training").
bool room_just_ended();
// True while a Slippi search or a connected pair is up (any thread): then Z belongs to the game
// ("Z to cancel") unless this game is in a room.
bool search_busy();
// The online character select's pick (the game shim, cmd 0xEE): Slippi's external character id, or
// kNone while no character is picked, and the costume. Shown as this member's portrait in the room.
void set_css_pick(uint8_t character, uint8_t costume);
// That pick (Source Port: the select reports it), false when none is known.
bool css_pick(uint8_t* character, uint8_t* costume);
// The online character select's hand (the game shim, cmd 0xEF), in tenths of the screen's units
// (x -350 at the left edge, 260 or 290 at the right), and when it last moved.
void set_css_hand(int16_t x10, int16_t y10);
bool css_hand(float* x, float* y);   // false before the first report
// The online select's mode with the hand (Matchmaking::OnlinePlayMode; -1 a game library that doesn't say).
void set_css_mode(int mode);
// Ranked or Unranked: no rooms there (they are a Direct feature; under YouGame, a YouGame Friends one), so no
// ARENA tab or box (pc_settings.cpp).
bool css_ranked_or_unranked();
// Rooms for players (the tab, the panels, the room logic, the settings rows). MELEE_TEST_ARENA_ROOMS=0
// (tests only) turns them off, for runs that compare a game with and without them.
bool rooms_enabled();

// ---- warm-up room (slippi_warmup.h), simulation thread ----
// A member who is not about to fight may go to Training while the room goes on: the room's link is
// its own connection and stays up. `why` says why not ("Your match is next").
bool warmup_ok(std::string* why = nullptr);
// The member went to warm up (true: Training, then anywhere offline) or is back on the character
// select (false). The room's match window stays; where and how big follows the player (pip_tick).
void set_warming(bool on);
// The warm-up room's small window moved and sized with the controller (Practice menu, Move and resize):
// on picks it (where it is now becomes RenderOptions::live_warmup_pip_cx/cy/cw, and it shows it is
// picked), off puts it down. The arena's own thread applies it; any thread may call.
void pip_adjust(bool on);
bool pip_active();   // the two windows are laid out now (a small window is up)
// The room's match while the player is not fighting: as the settings say (room_watch on the select,
// warmup_watch while away); on the select PICTURE IN PICTURE on it makes it small for that wait. The
// side bar's button toggles it (any thread); watch_layout says how it shows now: 0 no match window,
// 1 small, 2 big with this game small, 3 in its own window (this game as it is).
void toggle_watch_layout();
int watch_layout();
// The Arena panel's right side bar card on the select: its bottom, a share of the window's height
// (said each frame it is drawn). The small window sits under it there.
void note_side_card_bottom(float share);
// The room put this member in the ring and the ring is full: their game is next.
bool turn_called();
// The game the room is playing, for the warm-up room's strip. Score: the fighters' stocks and
// percent, sent by the first fighter about twice a second (arena_feed kScore).
struct LiveScore {
  bool live = false;          // a score arrived for the game running now
  uint32_t frame = 0;         // the game's frame (60 a second)
  struct Fighter {
    bool present = false; uint8_t character = 0xFF, costume = 0, stocks = 0; float percent = 0;
    std::string name;         // the player's name as the game shows it (older senders: empty)
  } fighter[4];
};
struct WarmupInfo {
  std::string place;    // "Next up", "2 ahead of you", "Watching", "Your match is next"
  std::string versus;   // "HERO vs WISE" while a game runs, else ""
  LiveScore score;
};
WarmupInfo warmup_info();

// ---- tests ----
// The member's last few log-worthy events, for hidden runs (also in the log as "arena: ...").
std::vector<std::string> recent_events();

}  // namespace slippi::arena
