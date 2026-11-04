// YouGame online: what replaces Slippi's servers when the YouGame desktop app launches the game.
//
// Slippi's matchmaking server, user record and game reporter are swapped for YouGame's: the
// player's identity and match tickets come from the desktop app's bridge, matchmaking and results
// go through YouGame's rooms Worker. With hello.netBroker.direct the app owns all networking:
// brokered rooms and sealed datagrams through a shared ring, with virtual ENet addresses. Internet
// builds keep STUN and punching before the Slippi netplay client takes over. The game's
// menus, the rollback engine and the netplay wire format do not change.
//
// YouGame mode is on only when the app started the game: YOUGAME_STDIO=1 (the app's sandboxed
// launch, the bridge on stdin/stdout) or YOUGAME_BRIDGE + YOUGAME_TOKEN (its loopback bridge). See
// docs/MELEE_UNLOCKED.md in the YouGame repository. Off, nothing here runs and nothing changes,
// and in YouGame mode nothing reaches a Slippi host.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
struct _XINPUT_STATE;
#ifndef _WIN32
#include "yougame_pad_mirror.h"
#endif

namespace yougame {
namespace relay_pads {
// After the host selects --log-file; releases controller messages buffered during startup.
void logging_ready();
bool active();
bool read(unsigned slot, _XINPUT_STATE* state);
// Serialized with activation, so even the probe/rumble workers cannot open native pads
// after the app takes over. poll serves the relay or invokes the native fallback.
bool poll(unsigned slot, _XINPUT_STATE* state, const std::function<bool()>& native);
void rumble(const std::function<void()>& native);
}
namespace mode_policy { enum class ModRefusal; }

// Call once at startup, before the window exists: takes the bridge's address and token out of the
// environment (so nothing the game starts inherits them) and, in stdio mode, starts reading the
// app's lines, so the player box's commands are heard from the first frame.
void start();

// Launched by the YouGame desktop app: online play goes through YouGame, never Slippi.
bool active();
// Melee Unlocked's own launcher holds the YouGame account and serves the same loopback bridge
// (YOUGAME_BRIDGE + YOUGAME_TOKEN + MELEE_YOUGAME_LAUNCHER=1). The game keeps all its normal
// GitHub behaviour (Slippi, mods, spectator, settings); only the YouGame rows of the online menu
// use the bridge, one search at a time.
bool launcher();
// YouGame is not open yet (MELEE_YOUGAME_COMING_SOON): its online rows show as Coming soon and cannot
// be picked. Hidden test runs reopen them with MELEE_TEST_YOUGAME_OPEN=1 (the end-to-end runs).
bool coming_soon();
// YouGame's online rows can be offered: the app started the game, or the launcher's bridge is up.
bool available();
// The current search or match is a YouGame one: always in the app, and under the launcher from the
// moment a YouGame row is picked until a Slippi row is picked.
bool session();
// The online menu's choice, before its character select: true for a YouGame row. Ignored in the app.
void set_launcher_session(bool yougame);
// Launcher only: YouGame is on another (older) release than this build. Asks the launcher again
// (a local answer, under 2 s). Empty when this build is allowed or nothing is known.
std::string behind_version_now();
// Asks the launcher to start that release once this game closes, in `mode` ("ranked" or "direct").
bool restart_older(const std::string& mode, std::string* error);
// --online-mode ranked|direct: the launcher started this game for that YouGame mode. 1 ranked,
// 2 direct, 0 none; answered once.
void set_online_mode_arg(int mode);
int take_online_mode_arg();
// This build's version as YouGame and the rooms see it.
std::string build_version();
// The bridge is this process's stdin/stdout (the app's sandboxed launch): stdout carries only
// bridge lines (the log goes to stderr), and the GameCube adapter comes from the app's pad mirror.
bool stdio_bridge();

// Best-effort compatibility code for an explicit listing layer. At most one request per process;
// any reply (including an old app's unknown-request reply) suffices. Returns within 500 ms.
void notify_mod_refused(bool explicit_layer, mode_policy::ModRefusal refusal,
                        const std::string& mod = {}, const std::string& version = {});

// The game draws inside the YouGame player box (YOUGAME_EMBED=1): a child window the app places.
bool embedded();
// The player box's commands ("rect x y w h", "hide", "show", "focus", "blur", "volume v", "quit"),
// delivered on the bridge's reader thread. Lines that arrive before a handler is set are kept.
void set_box_handler(std::function<void(const std::string&)> handler);
#ifdef MELEE_LINUX_X11_EMBED
// Linux full host: ordered, bounded commands buffered from startup, drained by the video thread.
std::vector<std::string> take_box_commands();
#endif
// The pad mirror the app shares (a section handle in this process, 0 until it arrives): the
// GameCube adapter as the YouGame app's controller helper reads it (gc_adapter.cpp).
uintptr_t pad_mirror_handle();
#ifndef _WIN32
// Inherited, borrowed O_RDONLY descriptor and advertised mapping size.
pad_mirror::Attachment pad_mirror_attachment();
#endif

struct Identity {
  bool ok = false;
  std::string player_id;   // this game's player id for the signed-in account (the room's player id)
  std::string name;        // display name, as the room shows it to opponents
  std::string code;        // connect code, "TAG#123"; empty when YouGame has not assigned one
  std::string error;       // why there is no identity (signed out, the app is not answering)
};
// The signed-in YouGame player, from a cache: never blocks (the game thread reads it every frame
// in the online menus). The first call starts asking the app in the background and answers "not
// signed in yet"; a failed answer is asked again after a few seconds. refresh_identity() asks
// again now, in the background (the online menu's "log in", which re-read user.json for Slippi).
Identity identity();
void refresh_identity();
// The first answer, waiting for it until `ms` after the first call of the process, never longer in
// total (the online menu's status query at boot: answered before the identity arrived, the menus
// would draw Slippi's "log in" screen; a slow app must not stall the game thread again and again).
Identity wait_identity(int ms);

// A key the YouGame page owns, pressed while the game has the keyboard ("escape": the page leaves
// full screen). Sent over the stdio bridge as `yg {"key":"escape"}`.
void relay_key(const char* key);

// The page pause's state for the page (yougame_ux.h PagePause::Result), sent over the stdio bridge as
// `yg {"event":{"type":"pause","pausable":B,"paused":P,"reason":R}}` (reason left out when null).
void pause_event(bool pausable, bool paused, const char* reason);

// The host picker runs off the simulation thread while code entry remains visible.
// Throws on cancellation, a page error, or an invalid answer; opens no room.
std::string pick_friend_room(const std::function<bool()>& cancelled);

// One matchmaking search: the game's online menu choice.
struct Search {
  int mode = 1;               // Slippi's ids: 0 ranked, 1 unranked, 2 direct, 3 teams
  std::string direct_code;    // the opponent's connect code (Direct)
  std::string friend_room;    // internal invite; never an opponent connect code
  uint16_t udp_port = 0;      // the netplay port; STUN and the punch run from it so the NAT mapping is the one ENet uses
};

struct Player {
  int port = 0;               // 1-based, the in-game port (room seat + 1)
  bool local = false;
  std::string id, name, code; // id: the room's player id
  std::string address;        // virtual "ip:port" in direct mode, otherwise the punched address; empty locally
};

struct Match {
  bool ok = false;
  std::string error;          // shown in the game's error box
  bool retry = false;         // the connection failed after a match was found: search again
  std::string match_id;       // a fresh id per match (never the room's code, which stays private)
  bool host = false;          // seat 0: the decider
  int local_index = 0;        // 0-based local port
  std::vector<Player> players;
};

// Runs a whole search on the matchmaking thread: ticket, queue or direct room, compatibility
// exchange and app-owned links (or legacy punching). Returns when the players can reach each other, on an error, or when `cancelled` turns true.
// The room stays open afterwards for results (game_started / game_ended) until end_session().
Match find_match(const Search& search, const std::function<bool()>& cancelled);

// Room slot == Slippi player index (controller port - 1). Unknown or no room: -1.
int seat_team(int player_idx);

// The online game the room is recording started (the host asks the room to start a match).
void game_started();
// One game's end, from the game's Game End record (the Source Port) or Slippi's report (the Static
// Recomp). A game is reported once, however many places say it ended.
struct GameEnd {
  int winner = -1;              // 0-based port of the single winner as the caller read it, -1 none
  int end_method = 0;           // Slippi's: 1 TIME!, 2 GAME!, 7 No Contest (LRA-Start, disconnect, desync)
  int lras = -1;                // the port a No Contest names, -1 none
  bool has_placements = false;  // placements[]: the record's four placement bytes (0 first, 0xFF none)
  int placements[4] = {-1, -1, -1, -1};
  int teams[4] = {-1, -1, -1, -1}; // actual negotiated match teams, by Slippi player index
  int stage = -1, frames = -1;  // the game's stage id and length, for the ranked report's detail
  int characters[2] = {-1, -1};
  int64_t rng = -1;             // the match's rng offset (both games agree on it): the report's id
};
void game_ended(const GameEnd& end);
// Ranked: one best-of-3 set per room (yougame_ranked.h), decided from the games the room settles.
void ranked_set_complete(uint8_t end_mode);   // C2 (a future GameSetup port)
void ranked_match_status(uint8_t status);     // C4: 40 is Slippi's "abandoned"
void ranked_game_setup();                     // the game sent a GameSetup step: its C2 decides
// Game thread, every online menu poll: a game's result is still on its way (no next game may start
// yet), or the set is decided or its room is gone (break the pair, back to the search).
bool ranked_hold();
bool ranked_set_over();
// No ranked room is open or still finishing, and one was (the --online-test ranked harness).
bool ranked_room_retired();
// This client's games with unique room receipts (the --online-test quit-after harness).
int ranked_games_received();
// The Ranked character select's header ("Ranked Mode", "Ranked - Placements 2/5",
// "Ranked - Gold II - 1534"). The first ask without a known rank reads it from the app (mpRank).
std::string ranked_header();
// The last ranked standing is cached per process (E3/E5 and the match-state rank bytes).
std::vector<uint8_t> rank_response();
uint8_t rank_visibility();
uint8_t local_rank();

// Why the game leaves the room. Quit: the local player left (leaving the online menus, holding Z,
// a new search); Lost: the connection to the opponent broke; Over: the ranked set is decided.
enum class Leave { Quit, Lost, Over };
// Leaves the room: the session is over (the game's online cleanup). Only Ranked reads `why`.
void end_session(Leave why);

// Status for logs and the YouGame page: "searching", "connecting", "playing", the opponent.
std::string status_line();

}  // namespace yougame
