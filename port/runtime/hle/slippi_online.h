// Slippi Online command handling for the EXI device: matchmaking, netplay input exchange,
// rollback savestates, chat, match state. Port of the online half of Dolphin's CEXISlippi.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <array>
#include <string>
#include <vector>
#include "native_online_policy.h"

namespace slippi::online {

struct Config {
  std::string lobby_code;        // One explicit launcher-approved Direct match; never a Discord invite.
  int lobby_character = 2;       // external character id (a mod disc has more than 26)
  std::string lobby_status_file;
  std::string user_dir = "runtime/slippi/User/Slippi";   // user.json, direct-codes.json (Slippi Launcher layout)
  int delay = 2;                 // Slippi Online input delay (frames)
  int chat = 0;                  // 0 enabled, 1 direct only, 2 disabled
};
Config& config();

void init();
void shutdown();
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
};
void set_local_build(const LocalBuild& build);
// Called with true when a Direct opponent sent no build while this side shows a mod (the opponent is
// on Slippi Dolphin, whose game is the retail one unless the player said otherwise), and with false
// when the next search starts. The host switches that match to the retail game and back.
void set_plain_opponent_handler(void (*handler)(bool plain));
const LocalBuild& local_build();
// Handles one online command (cmd byte, payload after it); responses go to `read_queue`.
// Returns false for commands this module does not own.
bool handle(uint8_t cmd, const uint8_t* payload, uint32_t payload_len, std::vector<uint8_t>& read_queue);
// Validates the fixed EXI payload size before native or legacy callers enter a command handler.
bool valid_command_payload(uint8_t cmd, const uint8_t* payload, uint32_t payload_len);
// Counts rollback loads so the renderer can treat them as discontinuities.
uint64_t rollback_count();
// A native savestate load (the source port serves Slippi's savestate commands itself). `to_frame`
// is the frame loaded, for the session trace's rollback depth.
constexpr int32_t kRollbackFrameUnknown = INT32_MIN;
void note_rollback(int32_t to_frame = kRollbackFrameUnknown);
bool is_online_match();
// The in-game slot the local player occupies in the running online match (0-3).
int local_player_slot();
// Simulation-thread snapshot for local display; names never enter the synchronized game state.
std::array<std::string, 4> player_names_for_overlay();
// Most recent measured round trip to the opponent, in milliseconds; 0 when not connected.
int ping_ms();
// The online mode of the session that is running or being set up, as a Matchmaking::OnlinePlayMode
// value (UNRANKED 1, DIRECT 2, TEAMS 3, PARTY 4; 0 is Slippi's Ranked, never used). -1 when there is no online session at
// all, which is what "offline" means to the rest of the port. Covers matchmaking, the online
// character select screen and the match itself, so a feature can be gated on the mode before the
// match starts rather than only once it is running.
int session_mode();
// In-game player slot of the local player in an online match (0..3).
int local_player_index();
// True while the game is polling the online menus (mode select, the online character select screen,
// waiting for an opponent) and not running a match.
bool in_online_menus();

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
                        uint8_t color, std::string* error);
NativeMatchPoll native_poll_match();
void native_cleanup_match();

}  // namespace slippi::online
