// Slippi Online command handling for the EXI device: matchmaking, netplay input exchange,
// rollback savestates, chat, match state. Port of the online half of Dolphin's CEXISlippi.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <array>
#include <string>
#include <vector>
#include <memory>
#include "native_online_policy.h"
#if MU_SQUAD
#include "squad_costume_rule.h"
#endif

namespace yougame::lobby { enum class Profile; }   // yougame_lobby_wire.h: Friends4, Doubles

namespace slippi { class Matchmaking; }

namespace slippi::online {

struct Config {
  std::string lobby_code;        // One explicit launcher-approved Direct match; never a Discord invite.
  int lobby_character = 2;       // external character id (a mod disc has more than 26)
  std::string lobby_status_file;
  std::string user_dir = "runtime/slippi/User/Slippi";   // user.json, direct-codes.json (Slippi Launcher layout)
  int delay = 2;                 // Slippi Online input delay (frames)
  bool auto_delay = false;       // pick the delay from the measured ping each match (delay is the fallback)
  int chat = 0;                  // 0 enabled, 1 direct only, 2 disabled
  std::string ranked_rules_path; // optional package rules; parsed once after the startup build is set
  // Up to three connect codes never to play in Unranked: a match with one is turned down and the
  // search goes on. Not in Ranked, Direct, Teams or Party.
  std::string blocked_codes[3];
};
Config& config();

void init();
void load_ranked_rules();
// The app asked for the Online menu first (--resume online after an update). Without it a game the
// YouGame app started boots on the main menu; standalone keeps Slippi's boot to Online.
void request_boot_online();
std::string ranked_rules_digest(int mode); // only a verified YouGame remix Ranked session
void shutdown();
// Simulation tick: scene/mode changes and match starts cancel a linking sheet.
void slippi_link_tick(uint8_t major, uint8_t minor, uint8_t mode);
// Each engine sets this before its game starts; Static mod discs use OtherMod.
void set_native_gameplay_profile(NativeGameplayProfile profile);

// What this client plays online, sent to the opponent after connecting (a netplay message stock
// Slippi Dolphin ignores). mod_view false = the retail game; Source can also select its retail
// content view. In Direct a mod build plays only against the same
// build; allow_unverified lets it play an opponent who sends no build (Slippi Dolphin with the same
// mod), at the player's word.
struct LocalBuild {
  bool mod_view = false;
  std::string fingerprint;   // the mod content's identity (hex), empty for the retail game
  std::string name;          // shown in messages, e.g. "Akaneia 1.0.1"
  bool allow_unverified = false;
  bool extended_content = false;   // only Static mod boot runs the disc's added fighter/stage code
  bool static_engine = false;      // engine identity is independent of the selected content view
};
void set_local_build(const LocalBuild& build);
// Called with true when a Direct opponent sent no build while this side shows a mod (the opponent is
// on Slippi Dolphin, whose game is the retail one unless the player said otherwise), and with false
// when the next search starts. The host switches that match to the retail game and back.
void set_plain_opponent_handler(void (*handler)(bool plain, const char* why));
const LocalBuild& local_build();
// Handles one online command (cmd byte, payload after it); responses go to `read_queue`.
// Returns false for commands this module does not own.
bool handle(uint8_t cmd, const uint8_t* payload, uint32_t payload_len, std::vector<uint8_t>& read_queue);
// Validates the fixed EXI payload size before native or legacy callers enter a command handler.
bool valid_command_payload(uint8_t cmd, const uint8_t* payload, uint32_t payload_len);
// Early inputs (host/early_input.h). enabled: the player's setting. accept: this engine also uses
// the other player's early inputs for its prediction, which only the Source Port can (the Static
// Recomp's prediction is Slippi's own game code); it is what the build message announces.
bool early_inputs_enabled();
bool early_inputs_accept();
// Counts rollback loads so the renderer can treat them as discontinuities.
uint64_t rollback_count();
// A native savestate load (the source port serves Slippi's savestate commands itself). `to_frame`
// is the frame loaded, for the session trace's rollback depth.
constexpr int32_t kRollbackFrameUnknown = INT32_MIN;
void note_rollback(int32_t to_frame = kRollbackFrameUnknown);
bool is_online_match();
#if MU_SQUAD
// Squad Strike (squad_costume_rule.h). While an online squad match is set up (op 5 handed the game
// its setup, until the next op 0 or 5 or the connection ends), a costume or Kirby hat path (lower
// case, leading '/') has its fixed size and the standard copy checked in that match's final view;
// false otherwise. The lookup opens squad_costume::pick of them. first_fallback is true once per
// path per match (its log line). refuse cancels the match: a lookup found nothing within its size.
// The copy finder gives a path's standard copy in the current view; a copy over its size refuses
// the search (op 4) and the match (op 5).
struct SquadCostume { squad_costume::Budget budget; squad_costume::Standard standard; };
// console: a Friends console session runs; then Friends' synced select's copies apply while its
// squads are in play (MU_SQUAD_OP_CONSOLE, armed by the game) and no online match is set up.
bool squad_costume_rule(const std::string& path, SquadCostume* out, bool console);
bool squad_costume_first_fallback(const std::string& path);
void squad_costume_refuse(const std::string& path, bool mod);
void set_squad_standard_copy(squad_costume::Standard (*copy)(const std::string& path));
// Forgets Friends' armed costume rule and its logs: called when a console session begins or ends
// (EngineHooks::session_edge) and on an in-process host reset. Any thread; the game thread applies
// it before it next reads or changes the rule.
void squad_console_reset();
#endif
// Gecko codes agreed for the match (gecko_session.h): call every character select frame.
void gecko_pump();
// The in-game slot the local player occupies in the running online match (0-3).
int local_player_slot();
// The shield colour the opponent on in-game `slot` sent after connecting (0x1000000 | RGB), 0 none.
uint32_t remote_shield_color(int slot);
// Simulation-thread snapshot for local display; names never enter the synchronized game state.
std::array<std::string, 4> player_names_for_overlay();
// Most recent measured round trip to the opponent, in milliseconds; 0 when not connected.
int ping_ms();
// Every other player's ping in a Slippi match (Teams: up to three), empty when there is none.
std::vector<int> remote_pings_ms();
// The online mode of the session that is running or being set up, as a Matchmaking::OnlinePlayMode
// value (RANKED 0, YouGame mode only; UNRANKED 1, DIRECT 2, TEAMS 3, PARTY 4). -1 when there is no online session at
// all, which is what "offline" means to the rest of the port. Covers matchmaking, the online
// character select screen and the match itself, so a feature can be gated on the mode before the
// match starts rather than only once it is running.
int session_mode();
// In-game player slot of the local player in an online match (0..3).
int local_player_index();
// True while the game is polling the online menus (mode select, the online character select screen,
// waiting for an opponent) and not running a match.
bool in_online_menus();
// Online submenu and CSS, including when the Friends row is hidden.
bool online_status_visible();
// Direct only: a connected opponent announced automatic L-cancel in its build message. Slippi
// Dolphin and builds before 0.9.200 never send it, so it is false against them.
bool opponent_auto_lcancel();

// Warm-up room (slippi_warmup.h). Between matches with someone the player is connected to (Direct,
// Teams), the connection can be parked while the player goes to Training: the link, its selections
// and chat stay exactly as they are on the network thread, and the game sees no session at all
// (session_mode() -1, is_online_match() false), so Training plays as offline Training does. Back on
// the online character select the pair plays on. Simulation thread.
struct WarmupPeer {
  std::string name;
  bool ready = false;      // locked in on their character select
  int warmup = -1;         // 1 warming up too, 0 on the select, -1 their build does not say
  int character = -1;      // their pick once locked in (Slippi's external id), -1 not known
  int costume = 0;
  int picked = -1;         // their pick on the select before locking in (our builds tell it), -1 not known
};
struct WarmupLink {
  bool parked = false;
  bool link_up = false;    // connected, with every opponent still there
  int mode = -1;           // Matchmaking::OnlinePlayMode
  int peers = 0;
  WarmupPeer peer[3];
};
// Whether the player can warm up now; `why` says why not (shown under the button).
bool warmup_possible(std::string* why = nullptr);
bool warmup_park();        // true when the connection is now parked
void warmup_unpark();      // the player is on the way back to the select
bool warmup_parked();
// The character this player has on the online character select (Slippi's external id), sent to an
// opponent on our build when it changes, so their warm-up says what was picked before any lock-in.
void warmup_note_pick(int character, int costume);
WarmupLink warmup_link();  // also while not parked: the connected pair as it stands
// session_mode(), or the parked session's mode while warming up: for settings that must not change
// under the opponent's feet in the middle of a session (automatic L-cancel).
int session_mode_or_parked();

// Play while searching (native_practice.cpp): a search the player chose to keep while they leave its
// character select. kept_search() reads what it is doing without polling the menus' match state (the
// select's own poll takes over when the player is back on it). search_away(true): the game sees no
// session meanwhile (session_mode() -1); every cleanup clears it. Simulation thread.
struct KeptSearch {
  bool away = false;
  int state = 0;             // Matchmaking::ProcessState: 0 idle, 1 initializing, 2 matchmaking, 3 connecting, 4 connected, 5 error
  int mode = -1;             // Matchmaking::OnlinePlayMode
  bool squad = false, friend_room = false, lobby = false;
  bool locked = false;       // this player is locked in (every search from the select is)
  uint64_t started_ms = 0, now_ms = 0;   // when the search went out, and now, on one clock
  std::string error;
};
KeptSearch kept_search();
void search_away(bool away);

// Native practice uses the same Slippi matchmaking implementation as the game's online menus.
// These calls are simulation-thread only. native_poll_match is deliberately side-effecting and
// must be advanced no more than once per intended 60 Hz tick, just like CMD_GET_MATCH_STATE.
struct NativeMatchPoll {
  int process_state = 0;
  bool connection_success = false;
  bool local_ready = false;
  bool remote_ready = false;
  std::string error;
  std::string opponent;
};
bool native_start_match(int mode, const std::string& connect_code, uint8_t character,
                        uint8_t color, std::string* error, const std::string& friend_room = {});
NativeMatchPoll native_poll_match();
void native_cleanup_match();
// The 4-player Friends lobby (yougame_lobby.h, mode_policy::friends_lobby4): arms the 4-door select's lobby
// and starts its search. room empty: the Friends row, which holds a new room (this player hosts); else an
// accepted invite's room (a guest). The select then drives it through MU_LOBBY_COMMAND until B leaves it.
void arm_friends_lobby(const std::string& room);
// The same lobby on a profile: Doubles (G2) is the 2-seat home (me and a partner); its Start searches for
// opponents through Rooms, and the 4-player match runs in its own room while the home stays parked.
void arm_friends_lobby(const std::string& room, yougame::lobby::Profile profile);
// The Doubles row asked for a new Doubles home (CMD_YOUGAME_FRIENDS_MENU 16): true once per press.
bool take_doubles_lobby_request();
bool friends_lobby_armed();
// Another match takes over (a 1v1 friend invite accepted on the lobby's select): the lobby is let go
// without its LEAVE, so the select's unload can't close the new connection (view.arm changed).
void disarm_friends_lobby();
// The YouGame app's Leave for the armed lobby (its overlay, {"push":{"cmd":"leave"}}): the select backs out
// as its B does, to Online. Any thread; nothing when no lobby is armed or a match is running.
void ask_friends_lobby_leave();
// The Squad Strike row's "Play a friend" is waiting on the friend picker (it keeps the 1v1 friend room).
bool squad_friend_intent();
// Squad Strike in a friend room ("Play a friend" on the Squad Strike row). The room the friend
// picker returned (picked) carries it; any other room joined clears it (picked = false).
void squad_friend_room(const std::string& room, bool picked);
// The friend picker closed without a room (cancelled, failed, refused, or left): Squad Strike off.
void squad_friend_picker_closed();
// The --online-test ranked harness: locks in the same character and colour again (a random legal
// stage) for the set's next game, when a ranked pair is connected and not locked in. True if it did.
bool native_ranked_next_game(uint8_t character, uint8_t color);

// The running (or last) online game as negotiated: stage id, the two players' characters, its rng
// offset and the last online frame. For a YouGame ranked game report's id and detail; -1 unknown.
struct GameFacts {
  int stage = -1, frames = -1;
  int characters[2] = {-1, -1};
  int teams[4] = {-1, -1, -1, -1}; // actual match block teams, -1 outside Teams
  int64_t rng = -1;   // the match's rng offset: the same on both players' games
};
GameFacts last_game_facts();
// Ports (bit per 0-based port) the netcode marked disconnected in the current or last match.
unsigned disconnected_ports();
// The character (and costume) the local player has picked on the online character select, false
// when none is picked yet.
bool local_css_selection(uint8_t* character, uint8_t* color);
// The Direct code to keep searching for while practicing: the running Direct search's, else the
// most recent one used. Empty when there is none.
std::string direct_code_for_practice();

// Arena rooms (slippi_arena.h). The online session as the room sees it: whether a Direct search or
// connection is up, the connect code it is for (UTF-8), Matchmaking's ProcessState, and whether a
// match is running. Simulation thread.
struct ArenaMatchStatus {
  bool active = false;     // searching, connected, or in the match
  bool direct = false;     // and it is a Direct session
  int mode = -1;           // the last search's Matchmaking::OnlinePlayMode
  std::string code;
  int state = 0;
  bool in_match = false;
  bool local_ready = false;
};
ArenaMatchStatus arena_match_status();
// Starts a Direct search for `code` from the online character select. locked: lock in this
// character with a random legal stage (the test harness); otherwise the player picks and presses
// Start as usual once connected.
// mode: Matchmaking::DIRECT for a singles pair, TEAMS for a doubles four (everyone searches the
// same code; team 0 red, 1 blue).
bool arena_start_search(int mode, const std::string& code, bool locked, uint8_t character, uint8_t color,
                        uint8_t team, std::string* error);
// The same search for an arena under YouGame: the arena's room carries the game, between `fighters`
// (YouGame player ids in ring order, 2 or 4). `code` is the arena's word for the match (the opponent's
// id, or the host's for doubles), which arena_match_status() reports back.
bool arena_start_yg_search(int mode, const std::vector<std::string>& fighters, const std::string& code, bool locked,
                           uint8_t character, uint8_t color, uint8_t team, std::string* error);
// The Slippi account's connect code and display name; false when not logged in.
bool local_identity(std::string* code, std::string* name);
// A matchmaking client of its own for an arena room link (the game's own search is untouched).
std::unique_ptr<Matchmaking> arena_matchmaking();
// A room message that reached the game's own Direct connection (its search met a room's link
// instead of a plain Direct player): the oldest one, false when there is none.
bool arena_pop_from_match(std::vector<uint8_t>& out);
// Locks in this character and colour with a random legal stage when a Direct pair is connected and
// not locked in (the arena test harness only, where nobody presses Start). True if it did.
// random_stage false: the character only, the stage left to the stage select (a rematch's loser
// counterpicks there; the warm-up tests play it so).
bool arena_lock_in(uint8_t character, uint8_t color, bool random_stage = true);
// Counts arena pairings: the room bumps it each time it starts a search for a new opponent, and the
// online character select gives the first game's stage pick to player 1, as a peer-to-peer session
// does (a fresh pair has no previous winner). 0 outside a room.
void arena_note_new_pairing();
uint32_t arena_pairing();

}  // namespace slippi::online
