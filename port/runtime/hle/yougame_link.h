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
// Unranked with a local Slippi login uses Slippi's matchmaking and native UDP instead.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
struct _XINPUT_STATE;
#ifndef _WIN32
#include "yougame_pad_mirror.h"
#endif

namespace yougame {
namespace lobby { struct Plan; class Control; }   // yougame_lobby_setup.h, yougame_lobby.h
namespace signin { struct View; enum class Button; }   // yougame_signin.h
namespace relay_pads {
// After the host selects --log-file; releases controller messages buffered during startup.
void logging_ready();
bool active();
bool read(unsigned slot, _XINPUT_STATE* state);
// Serialized with activation, so even the probe/rumble workers cannot open native pads
// after the app takes over. poll serves the relay or invokes the native fallback.
bool poll(unsigned slot, _XINPUT_STATE* state, const std::function<bool()>& native);
void rumble(const std::function<void()>& native);
// The app has sent a raw HID pad (kind "hid") this run, so HID pads are its own from then on.
bool owns_hid();
#ifdef YOUGAME_AGENT_TEST_HOOK
void arm_rumble_test(double boot_ms);
#endif
}
namespace mode_policy { enum class ModRefusal; }

// Call once at startup, before the window exists: takes the bridge's address and token out of the
// environment (so nothing the game starts inherits them) and, in stdio mode, starts reading the
// app's lines, so the player box's commands are heard from the first frame.
void start();
// Host-only, lock-free handoff to the best-effort match hint worker. Never affects gameplay.
void online_match_state(bool open);
// Host-only, lock-free: the game's state for the YouGame button (yougame_game_state.h kPlaying,
// kPaused, kMenu). A worker sends gameState on a change and again after each reconnect.
void report_game_state(int state);

// Launched by the YouGame desktop app (including Slippi-routed Unranked).
bool active();
// Melee Unlocked's own launcher holds the YouGame account and serves the same loopback bridge
// (YOUGAME_BRIDGE + YOUGAME_TOKEN + MELEE_YOUGAME_LAUNCHER=1). The game keeps all its normal
// GitHub behaviour (Slippi, mods, spectator, settings); only the YouGame rows of the online menu
// use the bridge, one search at a time.
bool launcher();
// YouGame is not open yet (MELEE_YOUGAME_COMING_SOON): the GitHub build's YouGame rows show greyed out
// as Coming soon and cannot be picked. Never in a game the YouGame app started (active()). Hidden test
// runs reopen them with MELEE_TEST_YOUGAME_OPEN=1 (the end-to-end runs).
bool coming_soon();
// coming_soon() without the local channel: true for the whole run when the GitHub build's rows wait
// and the app did not start the game. The menu file is built from this (greyed and open copies).
bool coming_soon_build();
// No YouGame connected yet on the app's local channel, in a game that may ask for it (started on its own, or by
// Melee Unlocked's launcher with no YouGame sign-in of its own): the YouGame rows read as not ready (the menu's
// bit 6), and picking one runs YouGame's start hook (first_row_picked). False under the launcher's bridge.
bool ask_on_use();
// YouGame's online rows can be offered: the app started the game, or the launcher's bridge is up.
bool available();
// The local channel (yougame_channel.h): a game nothing started finds a running YouGame app on this
// computer. start_channel probes once on its own thread, after the log is open (main, after
// logging_ready); `automated` (--hidden/--headless) runs never probe unless MELEE_TEST_YOUGAME_CHANNEL=1,
// and MELEE_NO_YOUGAME_CHANNEL=1 turns it off. While a verified channel is up, launcher() and
// available() are true and coming_soon() is false, so the YouGame rows open as under the launcher.
void start_channel(bool automated);
// The Online menu opened: probe again (at most once every 5 s). Never waits; returns at once.
void online_menu_opened();
// The online menu's first row while YouGame is not found (coming_soon()). When the YouGame app is
// installed (its yougame link type, yougame-staging when the launcher chose staging: the registry on
// Windows, xdg-mime on Linux) it reads "Open YouGame": picking it, or entering the Online menu once per
// run, opens the app on the game's page and looks for it every second for 25 s ("Opening YouGame...").
// Otherwise "Get YouGame" opens https://yougame.co/desktop (staging.yougame.co/desktop when the
// launcher chose staging) in the player's browser. Hidden or headless runs only log, unless
// MELEE_TEST_YOUGAME_GET_PAGE=1. Never waits on the game's frame.
void open_get_page();
void first_row_picked();
// The place that asked for YouGame was left (the Online menu backed out of or left for a character
// select, CMD_YOUGAME_MENU 0x21; a YouGame search cancelled while it waited): YouGame's small window
// (Windows), still up from that ask, closes as Play offline (never remembered for an ask on use), and
// one still being looked for closes as soon as it shows. Get YouGame already downloading in it carries
// on. Never waits.
void withdraw_ask(const char* why);
// Game start (main, after start_channel): YouGame's start hook (yougame_start.h) for a game nothing
// started. YouGame running: nothing. Not on this computer: YouGame's own small window, before the game
// window. Installed but not running: that window too, never the client window. Quiet in agent, hidden and test runs.
void start_hook_at_boot(bool automated);
const char* first_row_text();
bool channel_connected();
// Sign in by code (yougame_signin.h, contract section 8): the local channel is up and its app is signed
// out, so the YouGame rows offer sign-in instead of staying locked (the menu's Coming soon bit: picking a
// row opens the panel and the menu carries on into that row once the player is in). sign_in_open on
// the row's pick; the overlay (port/runtime/gx/signin_overlay.cpp) reads sign_in_view each frame while
// sign_in_showing and sends the player's presses. Never blocks: asks run on their own threads.
bool sign_in_needed();
void sign_in_open();
bool sign_in_showing();
void sign_in_view(signin::View& out);
void sign_in_press(signin::Button button);
// The box as drawn this frame (pc_settings.cpp): under the settings or the Esc menu it takes no keys.
void sign_in_drawn(bool drawn);
// Esc with the box on screen: Back, as Shift+Tab is (channel_menu_key), from the window procedure. True
// when it took the press; the window then keeps that Esc from the menus, ImGui and the game.
bool sign_in_escape();
// The current search or match is a YouGame one: always in the app, and under the launcher from the
// moment a YouGame row is picked until a Slippi row is picked.
bool session();
// The online menu's choice, before its character select: true for a YouGame row. Ignored in the app.
void set_launcher_session(bool yougame);
// YouGame's friend flow can run in this game: the Friends row holds a room and opens YouGame's friend
// picker and the Friends lobby, and an invite's join push takes the player into the friend's room. Always
// in the app (active()); for a game started on its own, while the local channel to a running YouGame app
// is up (launcher() with channel_connected()). The old launcher bridge keeps the code search.
bool friends_flow();
// The online menu's page for the last pick (game: CMD_YOUGAME_MENU's 1-byte pick): 0 Slippi, 1 YouGame,
// 2 Unranked on both (YouGame and Slippi); -1 before any pick. Read by the app's routing.
void set_menu_service(int service);
int menu_service();
// Launcher only: YouGame is on another (older) release than this build. Asks the launcher again
// (a local answer, under 2 s). Empty when this build is allowed or nothing is known.
std::string behind_version_now(bool* can_restart = nullptr);   // can_restart: the launcher can start that release
// Asks the launcher to start that release once this game closes, in `mode` ("ranked" or "direct").
bool restart_older(const std::string& mode, std::string* error);
// A YouGame row pressed while the launcher is not signed in to YouGame: asks the launcher's bridge to open its
// own Connect (sign-in) window, or bring it to the front (muOpenConnect, off this thread). False when this game
// has no launcher bridge to ask. The rows open once the launcher is signed in (identity() asks again).
bool open_launcher_connect();
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

// The game draws in the YouGame player box (YOUGAME_EMBED=1); the app places its window.
bool embedded();
// Windows launch contract, consumed before window creation; false/zero elsewhere.
bool embed_toplevel();
uint32_t app_pid();
void embed_window_ready(uintptr_t hwnd = 0);
void console_boot_ready();
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

// Private native-game CPU bracket bridge (mu_tourney.h). No-op outside a YouGame launch.
bool tournament_active();
bool tournament_pending();
bool tournament_command(const uint8_t* payload, uint32_t size, std::vector<uint8_t>& reply);
void tournament_drain();

// A key relayed while the game has the keyboard: "shift-escape" leaves app full screen.
// Plain "escape" is also sent for older apps. Sent as `yg {"key":"shift-escape"}`.
void relay_key(const char* key);

// Shift+Tab (or the pad's Start with D-pad Down) in a game started on its own (local channel):
// {"type":"menuButton","kind":"keys"|"pad"}, as the
// SDK's yg_menu_button. The full app answers opened:true with its menu over the game; otherwise the
// game shows its own YouGame box (sign in), and with no app the Get / Open YouGame ask. False only
// under the stdio bridge (the shortcut keys carry it there) or with YouGame turned off, so the key
// stays the practice toggle.
bool channel_menu_key(const char* kind = "keys");
// A game started on its own with the YouGame app's channel up, in a menu or paused: the app's YouGame
// button sits over the window's top-left corner (the app hides it while playing, gameState). Any thread.
bool app_button_shown();
// The device the player last used, for the app's button pictures: {"type":"inputKind","kind":"keys"|"pad"}
// over the local channel, once per change and once per connection. Any thread; never waits.
void input_kind(const char* kind);
// The signed-in YouGame box (yougame_signin.h Home) over the game; Shift+Tab with no app menu.
void home_open();

// The page pause's state for the page (yougame_ux.h PagePause::Result), sent over the stdio bridge as
// `yg {"event":{"type":"pause","pausable":B,"paused":P,"reason":R}}` (reason left out when null).
void pause_event(bool pausable, bool paused, const char* reason);

// The host picker runs off the simulation thread while code entry remains visible.
// Throws on cancellation, a page error, or an invalid answer; opens no room.
std::string pick_friend_room(const std::function<bool()>& cancelled, const std::string& held = {}
#ifdef MELEE_DOUBLES
                             , bool doubles = false, std::string* invited = nullptr // who the list invited, when it names them
#endif
                             );
// The Friends lobby's invite (the empty door's A): the app's multi-invite picker for the lobby's room,
// up to three friends, who take seats in the order they accept. Off the simulation thread; throws on
// cancellation, a page error (RoomFull: the site's own words) or an invalid room.
void lobby_invite(const std::string& room, const std::function<bool()>& cancelled);
// The Doubles home's invite (the partner door's A): the app's single-friend Teams picker for the home.
void doubles_invite(const std::string& room, const std::function<bool()>& cancelled);
std::string slippi_link_status(const std::string& status);
void blend_slippi_stopped(uint64_t epoch, const std::string& reason);
// Blended Unranked ended with no match on either side (not left by the player): an out-of-date refusal its
// YouGame side kept (yougame_signin.h Update) is offered now, never while Slippi's side may still match.
void blend_over(uint64_t epoch);

// Retire any previous YouGame room before selecting plain UDP for a Slippi search.
bool prepare_slippi_match(const std::function<bool()>& cancelled, uint64_t epoch = 0);
// Reserve a generation on the caller before launching a helper. Retires the previous room, except a
// Friends lobby room parked between matches when keep_lobby (the lobby's own next search adopts it).
uint64_t begin_search(bool keep_lobby = false);
// Serialize a blend commit with the helper's guarded global effects.
bool guard_search(uint64_t epoch, const std::function<void()>& commit);

// Runs `create` with ENet sockets on native UDP, then puts back the selection a YouGame search made.
// ENet picks a socket's transport when it is created, so a host made here stays native: blended
// Unranked makes Slippi's matchmaking client while YouGame's search (app-owned links) runs.
void with_native_udp(const std::function<void()>& create);

// Whether the base listing's Unranked can use a Slippi login, for the app: `yg {"slippi":...}` over the
// stdio bridge the first time and whenever it changes (yougame_blend.h slippi_link_line). `tag` is the
// player's own connect code; nothing else of the login ever goes out. Remixes never call it.
void slippi_link(bool linked, const std::string& tag);

// One matchmaking search: the game's online menu choice.
struct Search {
  uint64_t epoch = 0;        // reserved before launching a blend helper; 0: reserve on entry
#if MU_SQUAD
  // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
  bool squad = false;
  std::string squad_budget;
#endif
  int mode = 1;               // Slippi's ids: 0 ranked, 1 unranked, 2 direct, 3 teams
  std::string direct_code;    // the opponent's connect code (Direct)
  std::string friend_room;    // internal invite; never an opponent connect code
  uint16_t udp_port = 0;      // the netplay port; STUN and the punch run from it so the NAT mapping is the one ENet uses
  bool udp_port_fixed = false;   // udp_port was asked for (a fixed Slippi port): bound explicitly, else the system picks
  // Blended Unranked only (yougame_blend.h), called on the search's thread. on_searching: the queue's
  // "searching" count (players searching there, this one included). claim: the queue named a room;
  // false declines it (Slippi's side was first): briefly join and leave the assigned room.
  std::function<void(int waiting)> on_searching;
  std::function<bool()> claim;
  std::string status;         // the status line while it searches (yougame_blend.h kSearch*); empty: "searching"
  // The 4-player Friends lobby (yougame_lobby.h): hold a room (lobby_room empty) or join the code a
  // friend's invite carries. The search waits in the room's lobby until its host starts a match, and
  // the screen drives the lobby through lobby_control (picks, CPUs, rules, Start).
  bool friends_lobby = false;
  std::string lobby_room;
  std::shared_ptr<lobby::Control> lobby_control;
  // The Doubles home's match (G2, set only by find_match itself): matchmaking placed the home in this room,
  // joined with the queue's own ticket, while the home stays parked. Its setup is the match's; its renewals
  // ask for a match-admission ticket (friends::doubles_match_ticket), never the queue's.
  struct DoublesMatch {
    std::string url, ticket, room, home;
  } doubles_match;
  // An arena game (slippi_arena.cpp under YouGame): the room players of this game, in ring order (ring slot n
  // plays port n+1), found in the arena's own room (arena_room below) instead of a room of their own. 2 or 4.
  std::vector<std::string> arena_fighters;
};

struct Player {
  int port = 0;               // 1-based, the in-game port (room seat + 1)
  bool local = false;
  std::string id, name, code; // id: the room's player id
  std::string address;        // virtual "ip:port" in direct mode, otherwise the punched address; empty locally
};

struct Match {
  bool ok = false;
  uint16_t udp_port = 0;      // actual own-UDP bound port, handed to the gameplay socket
  std::string error;          // shown in the game's error box
  bool retry = false;         // the connection failed after a match was found: search again
  std::string match_id;       // a fresh id per match (never the room's code, which stays private)
  bool host = false;          // seat 0: the decider
  int local_index = 0;        // 0-based local port
  std::vector<Player> players;
  bool update_required = false; // Only an explicit update_required server code, never error text.
};

// Runs a whole search on the matchmaking thread: ticket, queue or direct room, compatibility
// exchange and app-owned links (or legacy punching). Returns when the players can reach each other, on an error, or when `cancelled` turns true.
// The room stays open afterwards for results (game_started / game_ended) until end_session().
Match find_match(const Search& search, const std::function<bool()>& cancelled);

// Room slot == Slippi player index (controller port - 1). Unknown or no room: -1.
int seat_team(int player_idx);
// The Doubles match room (a 4-seat Teams room; yougame_doubles_teams.h): each slot's team and the
// stage seed, from the room's start (its console session) when Rooms sent one, else from the room's
// nativeTeams seats (a room made before seeds takes one derived from its code). open: in such a room.
// refusal: in one, but neither named the teams (the match is refused on screen, never slot order).
// consistent: every roster participant the seats name sits in that seat's slot (matched by connection).
struct DoublesRoom {
  std::array<int, 4> team{{-1, -1, -1, -1}};
  uint64_t seed = 0;
  bool open = false, consistent = true;
  const char* refusal = nullptr;
};
DoublesRoom doubles_room();
// The 4-player Friends lobby's match (yougame_lobby_setup.h): the plan built from the setup the room froze
// at Start. open: the current room plays one; the match is then built from the plan alone (ports, CPUs,
// rules, stage, seed), never from the netplay selections.
struct FriendsLobby {
  bool open = false;
  std::shared_ptr<const lobby::Plan> plan;
};
FriendsLobby friends_lobby();
// The game refused the Friends lobby's frozen setup before frame 1: a guest ends that match in the room
// (lobby::Session::refuse_setup); the host's own call-off follows the park (next_match).
void lobby_refuse_setup();
// The Friends lobby's key: friends-lobby-1 ahead of this build's own compatibility key (64 at most).
std::string friends_lobby_compatibility();

// The online game the room is recording started (the host asks the room to start a match).
void game_started(int64_t rng, int stage);
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
  unsigned gone = 0;            // ports whose game the netcode dropped (disconnected), by Slippi player index
};
void game_ended(const GameEnd& end);
// Ranked: one best-of-3 set per room (yougame_ranked.h), decided from the games the room settles.
void ranked_set_complete(uint8_t end_mode);   // C2 (a future GameSetup port)
void ranked_match_status(uint8_t status);     // C4: 40 is Slippi's "abandoned"
void ranked_game_setup();                     // the game sent a GameSetup step: retain pair for C2 + result
// Game thread, every online menu poll: a game's result is still on its way (no next game may start
// yet), or the set is decided or its room is gone (break the pair, back to the search).
bool ranked_hold();
bool ranked_set_over();
// No ranked room is open or still finishing, and one was (the --online-test ranked harness).
bool ranked_room_retired();
// Game thread: this session's own room socket died (no close frame from the room, and not our own close).
// A match link that dropped with it was most likely this side's connection, not the opponent leaving.
bool own_room_dropped();
// This client's games with unique room receipts (the --online-test quit-after harness).
int ranked_games_received();
// The Ranked character select's header ("Ranked Mode", "Ranked - Placements 2/5",
// "Ranked - Gold II - 1534"). Each CSS visit reads mpRank at most once, with failure back-off.
std::string ranked_header();
void ranked_screen_enter();
std::array<std::string, 6> ranked_lines();
uint8_t opponent_rank();
// The last ranked standing is cached per process (E3/E5 and the match-state rank bytes).
std::vector<uint8_t> rank_response();
std::vector<uint8_t> rank_record_response(); // FE: self/opponent and the authoritative set result
uint8_t rank_visibility();
uint8_t local_rank();

// Why the game leaves the room. Quit: the local player left (leaving the online menus, holding Z,
// a new search); Lost: the connection to the opponent broke; Over: the ranked set is decided.
// Next: a Friends lobby match is over and everyone is back on the lobby's select: its room, links and
// lobby stay up (parked) for the next lobby search; anything but that search closes them. A Friends
// lobby room parks on Lost too (a friend's game dropped: back to the select); only Quit closes it.
enum class Leave { Quit, Lost, Over, Next };
// A Friends lobby room is parked (close_room Next or Lost) for the lobby's next search to adopt.
bool lobby_parked();

// The arena's room under YouGame (slippi_arena.cpp): one private room of friends::kArenaSeats for a whole arena,
// made by its host and joined from an invite. It carries the arena's own messages; each game between two (or
// four) of its players is found in the same room (Search::arena_fighters). Any thread.
namespace arena_room {
// Opens it in the background: `code` empty makes a new room that this player hosts, else joins that room.
void open(const std::string& code);
// 0 closed, 1 opening, 2 open, -1 failed or lost (`error` says why).
int state(std::string* error = nullptr);
void close();
std::string code();
std::string me();     // this player's connection in the room
std::string host();   // the connection of the player who made it, as first seen (it never moves)
struct Seat { std::string id, name, conn; };
std::vector<Seat> roster();
// Queues one arena message for one connection (empty: everyone else). Sent in batches within the room's limits.
void send(const std::string& conn, const std::vector<uint8_t>& bytes);
bool pop(std::string* from, std::vector<uint8_t>* bytes);
// Opens the app's invite picker for the room in the background. invite_state: 0 idle, 1 open, 2 sent,
// -1 refused (`error` says why).
void invite();
int invite_state(std::string* error = nullptr);
}  // namespace arena_room
// Leaves the room: the session is over (the game's online cleanup). Only Ranked reads `why`.
void end_session(Leave why);

// Simulation thread: starts only from a positively known idle game scene and set state.
// Once started the caller holds the scene loop until the renderer requests the normal clean exit.
// online_screen: an Online menu or online CSS is showing, so the accept line ends in " online".
bool update_safe_point(bool idle_scene, bool online_busy, bool ranked_set, bool online_screen);
bool update_handoff_visible();
void update_notice_presented(); // renderer, after drawing Updating...

// Status for logs and the YouGame page: "searching", "connecting", "playing", the opponent.
std::string status_line();
// The room's line for a search with nobody to meet yet (rooms "searching".paused), cleaned for one
// overlay row; empty when the room sent none. The overlay shows it in place of a YouGame-only search line.
std::string search_paused_line();
std::string paused_line(const std::string& raw);
void set_search_paused(const std::string& raw);

}  // namespace yougame
