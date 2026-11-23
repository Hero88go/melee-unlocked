// Slippi Online command handling (see slippi_online.h): port of the online half of Dolphin's
// CEXISlippi plus SlippiSavestate. The game-side Slippi codes drive everything: they ask for
// the match state each frame in the lobby, send local inputs and fetch remote ones each frame
// in a match, and capture/load savestates around rollbacks.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "slippi_agent_lock.h"
#include "test_env.h"
#include "slippi_online.h"
#include "slippi_stage_pool.h"
#include "gecko_session.h"
#include "yougame_net_test.h"
#include "yougame_link.h"
#include "yougame_lobby_setup.h"
#include "yougame_lobby_screen.h"
#include "yougame_unranked.h"
#include "yougame_mode_policy.h"
#include "online_rows.h"
#include "yougame_ranked_rules.h"
#include "yougame_ranked.h"
#include "yougame_friends.h"
#include "yougame_doubles.h"
#include "yougame_slippi_link.h"
#include <atomic>
#include <future>
#include "netplay_summary.h"
#include "enet/yg_enet_socket.h"
#include "native_slippi_bridge.h"
#include "slippi_net.h"
#include "slippi_gone_port.h"
#include "slippi_time_sync.h"
#include "slippi_input_suggestions.h"
#include "slippi_report.h"
#include "native_practice.h"
#ifdef YOUGAME_AGENT_TEST_HOOK
#include "yougame_agent_static.h"
#include "slippi_playback.h"
#include "native_practice.h"
#endif
#include "exi_slippi.h"
#include "host.h"
#include <chrono>
#include <thread>
#include "window.h"
#include "discord_presence.h"
#include "gx_core.h"
#include "coming_soon_overlay.h"
#include "cosmetic_mods.h"
#if MU_SQUAD
// Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
#include "yougame_squad.h"
#include "squad_costume_rule.h"
#include "mod_profile.h"
#endif
#include "net_trace.h"
#include "netplay_state.h"
#include "early_input.h"
#include "render_options.h"
#include <algorithm>
#include <cmath>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace slippi::online {
namespace {

Config g_config;

// The online menu's remembered choices (its two pages, owner's pick 11-18): the page last shown and
// which service Unranked searches (Both searches YouGame and Slippi). user_dir/online-menu.json,
// beside direct-codes.json.
// Bit 0: the Slippi page; bit 1: Unranked on both; bits 2-3: Unranked's slider value (0 the page's
// own, 1 YouGame, 2 Both, 3 Slippi; ops 0x45-0x48 set it).
static std::mutex g_menu_prefs_mutex;
static int g_menu_prefs = -1;
static int online_menu_prefs_locked() {
  if (g_menu_prefs < 0) {
    g_menu_prefs = 0;
    std::ifstream in(g_config.user_dir + "/online-menu.json", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.find("\"page\": \"slippi\"") != std::string::npos) g_menu_prefs |= 1;
    if (text.find("\"also_search\": true") != std::string::npos) g_menu_prefs |= 2;
    if (text.find("\"unranked\": \"yougame\"") != std::string::npos) g_menu_prefs |= 1 << 2;
    else if (text.find("\"unranked\": \"both\"") != std::string::npos) g_menu_prefs |= 2 << 2;
    else if (text.find("\"unranked\": \"slippi\"") != std::string::npos) g_menu_prefs |= 3 << 2;
  }
  return g_menu_prefs;
}
static int online_menu_prefs() {
  std::lock_guard<std::mutex> lock(g_menu_prefs_mutex);
  return online_menu_prefs_locked();
}
static void online_menu_prefs_set(uint8_t op) {
  std::lock_guard<std::mutex> lock(g_menu_prefs_mutex);
  int prefs = online_menu_prefs_locked();
  switch (op) {
    case 0x40: prefs &= ~1; break;
    case 0x41: prefs |= 1; break;
    case 0x42: prefs &= ~2; break;
    case 0x43: prefs |= 2; break;
    case 0x45: case 0x46: case 0x47: case 0x48:
      prefs = (prefs & ~0xE) | ((op - 0x45) << 2) | (op == 0x47 ? 2 : 0);
      break;
    default: return;
  }
  if (prefs == g_menu_prefs) return;
  g_menu_prefs = prefs;
  const std::string path = g_config.user_dir + "/online-menu.json", tmp = path + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out << "{\n  \"page\": \"" << ((prefs & 1) ? "slippi" : "yougame") << "\",\n  \"also_search\": "
        << ((prefs & 2) ? "true" : "false") << ",\n  \"unranked\": \""
        << ((prefs >> 2 & 3) == 1 ? "yougame" : (prefs >> 2 & 3) == 2 ? "both" : (prefs >> 2 & 3) == 3 ? "slippi" : "page")
        << "\"\n}\n";
    if (!out) return;
  }
  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  const int unranked = prefs >> 2 & 3;
  host::log("online menu: remembered page %s, Unranked %s", (prefs & 1) ? "Slippi" : "YouGame",
            unranked == 2 || (unranked == 0 && (prefs & 2)) ? "searches YouGame and Slippi"
            : unranked == 1 ? "searches YouGame" : unranked == 3 ? "searches Slippi" : "searches the page's own service");
}
#if MU_SQUAD
// Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
bool g_squad_row = false;
// "Play a friend" (MU_SQUAD_OP_FRIEND INVITE): Squad Strike goes with the room the friend picker
// it opened returns, and only that room. Never the row, never a leftover CSS choice: a room joined
// any other way (an invite, the results screen) is a plain friend room until the friend picks it.
bool g_squad_friend_intent = false;
std::string g_squad_friend_room;
MuSquadSide g_squad_side{};
std::string g_squad_budget;
std::unique_ptr<source_port::mods::Sha256> g_squad_budget_hash;
squad_costume::Budgets g_squad_costumes;   // from the identity facts; kept for the session
squad_costume::Standard (*g_squad_standard)(const std::string&) = nullptr;   // the host's copy finder
// Armed by op 5 when it hands the game a squad setup whose standard copies all hold, checked in
// that match's final view; the file lookup then uses the copies found there. Cleared by op 0, a
// normal or refused op 5, and every connection cleanup, so a new search and normal play never see it.
// It stays on through the results and a rematch's CSS until that rematch's op 5; CSS preloads then
// load the disc copies, which only lowers memory use.
bool g_squad_costume_match = false;
std::unordered_map<std::string, squad_costume::Standard> g_squad_match_standards;
std::unordered_set<std::string> g_squad_fallback_logged;   // one line per path per match
// What the player reads when a costume's standard copy is over its fighter's size.
const char* squad_costume_refusal(bool mod) {
  return mod ? "This mod's costumes are too big for Squad Strike online. Match not started."
             : "This disc's own costumes are too big for Squad Strike online. Match not started.";
}
// Every costume and hat path's standard copy in the current view, into `out` when given. False,
// with the first such path in name order and whether its copy is a mod's, when one does not hold.
// A twin the disc lacks holds: the game opens the .dat instead. Without a copy finder (an engine
// with no file lookup of its own) there is nothing to check and nothing to fall back on.
bool squad_standards(std::unordered_map<std::string, squad_costume::Standard>* out, std::string* over, bool* mod) {
  over->clear();
  *mod = false;
  if (!g_squad_standard) return true;
  return squad_costume::check_standards(g_squad_costumes, g_squad_standard, out, over, mod);
}
// Friends' synced select (MU_SQUAD_OP_CONSOLE): its own facts and copies, apart from an online
// search's, asked by the lookup only while the console session runs. The path last logged as over.
squad_costume::Console g_squad_console;
std::string g_squad_console_over;
// A console session began or ended, or the host reset in process (squad_console_reset): the
// game thread forgets the rule, its over-size line and its fallback lines before it next reads or
// changes them, so a rule armed before never carries into the next session or boot. The next
// select arms it again from its own facts (an arm with none sends them first).
std::atomic<bool> g_squad_console_reset{false};
static void squad_console_forget() {
  if (!g_squad_console_reset.exchange(false, std::memory_order_acq_rel)) return;
  g_squad_console.off();
  g_squad_console_over.clear();
  g_squad_fallback_logged.clear();
}
#endif
NativeGameplayProfile g_native_gameplay_profile = NativeGameplayProfile::Vanilla;
LocalBuild g_local_build;   // retail Source by default; each engine sets its build before playing
yougame::ranked_rules::Rules g_ranked_rules;
bool g_ranked_rules_loaded = false, g_ranked_rules_valid = false;
bool remix_ranked(int mode) {
  return yougame::ranked_rules::applies(yougame::session(), mode, g_local_build.mod_view, g_local_build.fingerprint);
}
bool use_ranked_rules(int mode) { return g_ranked_rules_valid && remix_ranked(mode); }
// The profile the online rules see: the mod's only while its files are what the game shows.
NativeGameplayProfile effective_profile() {
  return g_local_build.mod_view ? g_native_gameplay_profile : NativeGameplayProfile::Vanilla;
}
const char* local_matchmaking_error(int mode) {
#ifdef YOUGAME_AGENT_TEST_HOOK
  if (!yougame::net_test::mode_allowed(yougame::net_test::config(), mode)) {
    host::log("AGENT TEST: net delay refuses %s; Direct only", yougame::net_test::mode_name(mode));
    return "AGENT TEST net delay requires Direct";
  }
#endif
  // A Static Recomp mod session runs the disc's code, which Unranked and Party cannot play against
  // the standard game. Instead of refusing, the game starts again as the standard game with the
  // disc's looks (what the Source Port switches to on the spot); the player picks the mode again.
  // Not under a launcher or the YouGame channel either: the parent keeps the first process's handle
  // and would see the game end, and the new copy would start without the parent's bridge.
  if (g_local_build.static_engine && g_local_build.mod_view && !yougame::session() && !yougame::launcher() &&
      (mode == 1 || mode == 4) &&
      native_profile_mode_error(effective_profile(), mode, false) && host::restart_with_mod_looks())
    return "Starting the standard game with this mod's looks for Unranked and Party...";
  if (const char* reason = native_profile_mode_error(effective_profile(), mode, yougame::session())) return reason;
  if (yougame::session() && mode == 0 && !g_config.ranked_rules_path.empty() && !g_ranked_rules_valid)
    return yougame::ranked_rules::kInvalid;
  if (remix_ranked(mode) && !g_ranked_rules_valid) return yougame::ranked_rules::kInvalid;
  if (use_ranked_rules(mode))
    if (const char* reason = yougame::ranked_rules::selection_error(g_ranked_rules)) {
      host::log("yougame: Ranked refused: %s", reason);
      return reason;
    }
  if (g_local_build.mod_view && !native_mod_fingerprint_valid(g_local_build.fingerprint)) {
    if (yougame::session() && (mode == 0 || mode == 1))
      return "Could not verify this mod's content. Restart the game before playing Ranked or Unranked.";
    return "Could not verify this mod's content. Restart the game before playing Direct.";
  }
  return nullptr;
}
bool yougame_online_mod_content(int mode) {
  return native_yougame_online_mod_content(yougame::session(), mode, g_local_build.mod_view,
                                           g_local_build.extended_content, g_local_build.fingerprint);
}
// Vanilla character select kinds are 0-25 and the stage kinds end with the heal stage (0x55); an id
// past those comes from a build with more content (m-ex adds its fighters and stages after them).
constexpr uint64_t kBuildWaitMs = 3000;   // how long a mod build waits for the opponent's build message
static void (*g_plain_opponent_handler)(bool, const char*) = nullptr;   // see set_plain_opponent_handler

// Command ids live in native_slippi_bridge.h, shared with the native game's call path.

inline uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
inline uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
inline void append_u32(std::vector<uint8_t>& q, uint32_t v) { for (int i = 3; i >= 0; --i) q.push_back((uint8_t)(v >> (8 * i))); }

// ---------------------------------------------------------------- savestates
// Exactly Dolphin's SlippiSavestate: full RAM regions minus the sound/VI exclusions, with the
// main heap bounds read from the game (0x804d76b8/0x804d76bc).
struct PreserveBlock { uint32_t address, length; bool operator==(const PreserveBlock& o) const { return address == o.address && length == o.length; } };
struct PreserveHash { size_t operator()(const PreserveBlock& b) const { return b.address ^ b.length; } };

class Savestate {
 public:
  struct Loc { uint32_t start, end; std::vector<uint8_t> data; };
  static bool force_init;
  Savestate() { initBackupLocs(); for (auto& l : locs_) l.data.resize(l.end - l.start); }
  void Capture() { for (auto& l : locs_) std::memcpy(l.data.data(), host::ptr(l.start, l.end - l.start), l.end - l.start); }
  void Load(const std::vector<PreserveBlock>& blocks) {
    for (auto& b : blocks) {
      auto& keep = preservation_[b];
      keep.resize(b.length);
      std::memcpy(keep.data(), host::ptr(b.address, b.length), b.length);
    }
    for (auto& l : locs_) { std::memcpy(host::ptr(l.start, l.end - l.start), l.data.data(), l.end - l.start); host::mark_ram_write(l.start, l.end - l.start); }
    for (auto& b : blocks) { std::memcpy(host::ptr(b.address, b.length), preservation_[b].data(), b.length); host::mark_ram_write(b.address, b.length); }
  }
 private:
  static std::vector<Loc> processed_;
  std::vector<Loc> locs_;
  std::unordered_map<PreserveBlock, std::vector<uint8_t>, PreserveHash> preservation_;

  void initBackupLocs() {
    struct Region { uint32_t start, end; };
    std::vector<Region> full = {
        {0x80005520, 0x80005940}, {0x803b7240, 0x804DEC00}, {0x8065c000, 0x8071b000}, {0x80bd5c40, 0x811AD5A0},
    };
    std::vector<PreserveBlock> exclude = {
        {0x804031A0, 0x24}, {0x80407FB4, 0x34C}, {0x80433C64, 0x1EE80}, {0x804A8D78, 0x17A68}, {0x804C28E0, 0x399C}, {0x804D7474, 0x8},
        {0x804D74F0, 0x50}, {0x804D7548, 0x4}, {0x804D7558, 0x24}, {0x804D7580, 0xC}, {0x804D759C, 0x4}, {0x804D7720, 0x4},
        {0x804D7744, 0x4}, {0x804D774C, 0x8}, {0x804D7758, 0x8}, {0x804D7788, 0x10}, {0x804D77C8, 0x4}, {0x804D77D0, 0x4},
        {0x804D77E0, 0x4}, {0x804DE358, 0x80}, {0x804DE800, 0x70},
        {0x804d6030, 0x4}, {0x804d603c, 0x4}, {0x804d7218, 0x4}, {0x804d7228, 0x8}, {0x804d7740, 0x4}, {0x804d7754, 0x4},
        {0x804d77bc, 0x4}, {0x804de7f0, 0x10},
        {0x804c0980, 0x15F8},
    };
    if (!processed_.empty() && !force_init) { locs_ = processed_; for (auto& l : locs_) l.data.clear(); return; }
    force_init = false;
    full[3].start = host::rd32(0x804d76b8);
    full[3].end = host::rd32(0x804d76bc);
    host::log("slippi: savestate heap region %08X-%08X", full[3].start, full[3].end);
    std::sort(exclude.begin(), exclude.end(), [](const PreserveBlock& a, const PreserveBlock& b) { return a.address < b.address; });
    std::vector<Loc> locs;
    for (auto& r : full) locs.push_back({r.start, r.end, {}});
    size_t idx = 0;
    for (PreserveBlock ipb : exclude) {
      while (ipb.length > 0) {
        while (idx < locs.size() && ipb.address >= locs[idx].end) ++idx;
        if (idx >= locs.size()) break;
        if (ipb.address < locs[idx].start) {
          int new_size = (int32_t)ipb.length - ((int32_t)locs[idx].start - (int32_t)ipb.address);
          ipb.length = new_size > 0 ? new_size : 0;
          ipb.address = locs[idx].start;
          continue;
        }
        int new_size = (int32_t)ipb.length - ((int32_t)locs[idx].end - (int32_t)ipb.address);
        if (locs[idx].end > ipb.address + ipb.length) locs.insert(locs.begin() + idx + 1, {ipb.address + ipb.length, locs[idx].end, {}});
        locs[idx].end = ipb.address;
        if (locs[idx].end <= locs[idx].start) locs.erase(locs.begin() + idx);
        new_size = new_size > 0 ? new_size : 0;
        ipb.address = ipb.address + (ipb.length - new_size);
        ipb.length = (uint32_t)new_size;
      }
    }
    processed_ = locs;
    locs_ = locs;
  }
};
bool Savestate::force_init = true;
std::vector<Savestate::Loc> Savestate::processed_;

// ---------------------------------------------------------------- state
std::unique_ptr<User> g_user;
std::unique_ptr<Matchmaking> g_matchmaking;
std::unique_ptr<NetplayClient> g_netplay;
std::unique_ptr<DirectCodes> g_direct_codes, g_teams_codes;
std::string g_discord_join_code;
uint64_t g_discord_join_expires_ms = 0;
std::map<int32_t, std::unique_ptr<Savestate>> g_active_savestates;
std::deque<std::unique_ptr<Savestate>> g_available_savestates;
PlayerSelections g_local_selections;
bool g_local_random_stage = false;
Matchmaking::MatchSearchSettings g_last_search;
// The next search is an arena game under YouGame (arena_start_yg_search): its fighters and the arena's
// whole word for the match (a player id, longer than the 18 bytes a typed code gets).
struct ArenaNext { std::vector<std::string> fighters; std::string code; } g_arena_next;
// The 4-player Friends lobby (yougame_lobby.h) on the 4-door select: armed by the Friends row (the host
// holds a room) or an accepted lobby invite (a guest joins its room), until the player leaves it (LEAVE).
// One Control lasts the whole lobby, across its matches. closed: its search failed or its room closed
// (a cleanup that wasn't the lobby's own); the select then offers only B. dropped: a connection broke
// before the match began (cleanup Lost before the splash); the next READ takes the parked room back,
// with why it fell through as the select's note. note_seq bumps once per drop recorded (view.note_seq), so
// two drops in a row with the same reason both show.
struct FriendsLobby {
  bool armed = false, guest = false, closed = false, dropped = false;
  yougame::lobby::Profile profile = yougame::lobby::Profile::Friends4;   // Doubles: the 2-seat home (G2)
  std::string room, note;
  uint8_t note_seq = 0;
  std::shared_ptr<yougame::lobby::Control> control;
  std::shared_ptr<std::atomic<bool>> inviting = std::make_shared<std::atomic<bool>>(false);
  std::shared_ptr<std::atomic<bool>> invite_stop;
} g_friends_lobby;
// Bumps each time a lobby is armed or disarmed (READ's view.arm): a select that opened on another
// arming never sends LEAVE, so it can't close the lobby an accepted invite just armed.
uint8_t g_lobby_arm = 0;
// Each fighter's costume count in this build, from the select (MU_LOBBY_OP_COSTUMES); Melee's own
// until it says. The match clamps every port's colour to it before the setup hash (lobby::apply).
std::array<uint8_t, MU_LOBBY_CHARS> g_lobby_costumes = [] {
  std::array<uint8_t, MU_LOBBY_CHARS> n{};
  for (unsigned i = 0; i < MU_LOBBY_CHARS; ++i) n[i] = (uint8_t)mu_lobby_costumes_melee(i);
  return n;
}();
#ifdef MELEE_DOUBLES
// YouGame Doubles on the 4-peer Teams client: the Doubles home's match room is this Teams search's room
// (yougame_doubles_teams.h). Games started in it, so later games mix their index into the stage.
uint32_t g_doubles_games = 0;
bool doubles_route() {
  return yougame::doubles_teams::match_search((int)g_last_search.mode, !g_last_search.friend_room.empty());
}
#endif
yougame::slippi_linking::Flow g_slippi_link(yougame::slippi_linking::presentation);
uint32_t g_link_scene = 0;
bool g_link_menu = false;
Matchmaking::MatchmakeResult g_recent_mm_result;
std::vector<uint16_t> g_allowed_stages = {0x2, 0x3, 0x8, 0x1C, 0x1F, 0x20};
std::vector<uint16_t> g_stage_pool;
bool g_stage_pool_uses_rules = false;
const std::vector<uint16_t>& allowed_stages(int mode) {
  return yougame::ranked_rules::stage_pool(g_ranked_rules_valid ? &g_ranked_rules : nullptr,
                                         remix_ranked(mode), g_allowed_stages);
}
std::vector<PlayerSelections> g_overwrite_selections;
std::string g_forced_error;
// The match setup hash (mu_setup_hash.h) of the setup the game is offered now. Game thread only.
struct SetupHashState {
  bool have = false, agreed = false;
  MuSetupHash local{};
  uint64_t since_ms = 0;   // when this setup was first built: the wait for every peer's hash
  int logged[REMOTE_PLAYER_MAX] = {-1, -1, -1};   // the last decision logged per peer
  uint64_t logged_setup[REMOTE_PLAYER_MAX] = {};
  MuSetupHashPending pending[REMOTE_PLAYER_MAX] = {};   // a mismatch that has not stood long enough yet
  MuSetupHashWait wait[REMOTE_PLAYER_MAX] = {};         // each peer's latest hash: the wait counts from it too
};
SetupHashState g_setup_hash;
bool g_play_session_active = false;
// The Unranked character select says "Opponent left" (MU_YG_SUB_UNRANKED_NOTE) until the next search or
// any other cleanup. Set only where the opponent's link dropped (prepare_online_match_state). Game thread.
bool g_unranked_opponent_left = false;
// Set where a match's inputs first find the opponent gone mid-game (handle_online_inputs). The game then
// shows DISCONNECTED and its own cleanup follows, which turns this into the note instead of clearing it.
bool g_unranked_left_in_game = false;
uint8_t g_local_player_index = 0, g_remote_player_index = 1;
uint32_t g_stall_frame_counts[REMOTE_PLAYER_MAX] = {};
int g_frames_to_skip = 0, g_frames_to_advance = 0, g_fall_behind = 0, g_fall_far_behind = 0;
bool g_currently_skipping = false, g_currently_advancing = false;
static bool g_poor_performance_logged = false;
std::mt19937 g_rng((uint32_t)time_ms());
uint64_t g_rollbacks = 0;
static diagnostics::Summary g_summary;
static void finish_summary(const char* result);
// For the one-line summary at the end of an online game (delay, ping, rollbacks, stalls).
static uint64_t g_match_rollbacks_start = 0, g_match_stalls = 0, g_match_input_waits = 0, g_match_advances = 0;
static int g_input_wait_frames = 0;   // length of the wait for remote inputs that is running now
static int g_input_wait_latest = 0;   // the other player's newest frame when that wait began
// Early inputs: the sampler thread's way to the netplay client. It only queues a packet; the
// mutex keeps the client alive across the call, and the sampler is stopped before the client goes.
static std::mutex g_early_send_mutex;
static void send_early_pad(int32_t target, const uint8_t pad[host::early_input::kPadBytes]) {
  std::lock_guard<std::mutex> lk(g_early_send_mutex);
  if (g_netplay) g_netplay->SendEarlyPad(target, pad);
}
static void send_suggestion(int32_t target, uint32_t seq, const uint8_t pad[host::early_input::kPadBytes]) {
  std::lock_guard<std::mutex> lk(g_early_send_mutex);
  if (g_netplay) g_netplay->SendSlippiPadSuggestion(target, seq, pad);
}
static void stop_early_inputs() {
  host::early_input::stop();
  suggestions::end_game();
}
static int g_match_delay = 0;
bool g_in_online_match = false;
// Warm-up room (warmup_park): the pair stays connected while the player is in Training.
bool g_warmup_parked = false;
// A kept search (native_practice.cpp, play while searching): the player chose to leave the search screen
// and keep the search; it runs on its thread as it would on the select, and the game sees no session
// (session_mode() -1), so whatever the player plays meanwhile is offline as it always is.
bool g_search_away = false;
uint64_t g_search_started_ms = 0;   // when the last search went out (the kept search's timer)
uint16_t g_warmup_pick = 0xFF00;           // this player's pick on the select: character << 8 | costume
uint16_t g_warmup_pick_sent = 0xFFFF;      // what the opponents last got
const void* g_warmup_pick_link = nullptr;  // on which connection
GameFacts g_game_facts;   // the negotiated stage and characters, and the last online frame (YouGame's ranked report)
// Session trace (net_trace.h): one record per online tick for the "Network and timing" overlay.
// A record stays open until the next tick asks for inputs, because its work time, its rollbacks
// and the frames presented during it are only known by then. Observation only.
static net_trace::Record g_trace_record;
static bool g_trace_desync_pending = false;   // a checksum disagreed: marked on the next traced tick
static bool g_trace_open = false;
static int32_t g_trace_offset_us = 0;   // the time sync offset as last measured (every 30 frames)
void trace_close_tick() {
  if (!g_trace_open) return;
  g_trace_open = false;
  g_trace_record.sim_ms = (float)host::last_sim_frame_ms();
  g_trace_record.presents = (uint16_t)std::min<uint32_t>(net_trace::take_presents(), 0xFFFFu);
  net_trace::push(g_trace_record);
}
// The match is over (game report) or the connection is gone: the last record goes out and the
// session trace file is closed. The file work is the trace's own thread's.
void trace_end_match() {
  trace_close_tick();
  net_trace::file_end();
}
// A savestate load in the open tick. `to_frame` is the frame loaded, or kRollbackFrameUnknown. The
// game asks for frame N's inputs first, finds the misprediction in that answer, and loads at the
// start of frame N: the depth is N minus the loaded frame, the game's own count of it.
void trace_rollback(int32_t to_frame) {
  if (!g_trace_open) return;
  if (g_trace_record.rollbacks < 0xFF) ++g_trace_record.rollbacks;
  if (to_frame == kRollbackFrameUnknown) return;
  const int depth = std::clamp(g_trace_record.frame - to_frame, 0, 0xFF);
  g_summary.max_rollback = std::max(g_summary.max_rollback, (uint32_t)depth);
  if (depth > g_trace_record.rollback_depth) g_trace_record.rollback_depth = (uint8_t)depth;
}
// Determinism oracle: the game hands us a checksum of its finalized state each frame and the
// opponent's client sends theirs; a mismatch is a desync between the two simulations.
std::map<int32_t, uint32_t> g_local_checksums;
uint32_t g_checksums_compared = 0, g_checksums_mismatched = 0;
int32_t g_last_checksum_frame[REMOTE_PLAYER_MAX] = {};   // per remote: every peer's checksums are compared, not only the first to report a frame
// Ports (bit per 0-based port) the netcode marked disconnected this match: the Friends lobby's result
// ranks a human among them last when the game decided an order, else voids or gives the last side the win
// (yougame_lobby_setup.h finish_report).
unsigned g_gone_ports = 0;
int g_gone_own_frames = -1;   // -1 until the match's first leave, then whether every peer reads a player who left by their own frames
static_assert(REMOTE_PLAYER_MAX == 3, "one entry each below");
int32_t g_gone_last_frame[REMOTE_PLAYER_MAX] = {slippi::kNoFrame, slippi::kNoFrame, slippi::kNoFrame};   // per remote: its last frame, the first time it is read after leaving
static_assert(yougame::lobby::kSearchMode == Matchmaking::DIRECT, "the Friends lobby searches in Direct");

// Real remote machines, bounded before indexing connection-scoped build and setup-hash arrays.
uint8_t bounded_remote_machine_count() {
  const int n = g_netplay ? g_netplay->RemoteMachineCount() : g_matchmaking ? (int)g_matchmaking->RemotePlayerCount() : 0;
  return (uint8_t)std::min(n, REMOTE_PLAYER_MAX);
}

// Rollback has one entry per human port other than the main local port, including our partner.
uint8_t bounded_remote_port_count() {
  return uint8_t(g_netplay ? std::min(g_netplay->RemotePortCount(), REMOTE_PLAYER_MAX) : bounded_remote_machine_count());
}

bool is_disconnected() { return !g_netplay || g_netplay->GetSlippiConnectStatus() != NetplayClient::ConnectStatus::CONNECTED; }
bool chat_enabled() { return g_last_search.mode == Matchmaking::DIRECT ? (g_config.chat == 0 || g_config.chat == 1) : g_config.chat == 0; }

uint16_t random_stage(int mode) {
  const bool rules = use_ranked_rules(mode);
  if (rules != g_stage_pool_uses_rules) g_stage_pool.clear();
  g_stage_pool_uses_rules = rules;
  const auto& stages = allowed_stages(mode);
  // Doubles: no Fountain of Dreams, also not one a singles game left in the bag (an arena room
  // goes from singles to doubles with the bag half drawn).
  stage_pool::prepare(g_stage_pool, stages, mode == Matchmaking::TEAMS && !rules);
  int i = (int)(g_rng() % g_stage_pool.size());
  uint16_t s = g_stage_pool[i];
  g_stage_pool.erase(g_stage_pool.begin() + i);
  return s;
}

// ---------------------------------------------------------------- Discord presence
// Both of these return on a single atomic load when the player has not enabled Discord presence,
// and neither ever touches a socket, a pipe or the filesystem on this thread: the presence module
// owns one thread of its own and these only hand it a small struct.
const char* mode_name(Matchmaking::OnlinePlayMode mode) {
  switch (mode) {
    case Matchmaking::RANKED: return yougame::session() ? "Ranked" : "Online";
    case Matchmaking::UNRANKED: return "Unranked";
    case Matchmaking::DIRECT: return "Direct match";
    case Matchmaking::TEAMS: return "Teams";
    case Matchmaking::PARTY: return "Party";
  }
  return "Online";
}

// What the player is doing when they are not online. Melee routes every mode through one state
// machine (gm_1A3F.c, `static struct stateMachine state_machine` at 0x80479D30) whose first byte is
// routingInfo::curr_mode, a GameModeKind. Reading that one byte is how the presence can say
// "Training" instead of calling everything outside a netplay match "In the menus", which is what a
// player in training mode saw. Read only, on the simulation thread, and nothing is written back.
constexpr uint32_t kStateMachine = 0x80479D30;
const char* offline_mode_name() {
  // The native game keeps its state machine in its own image, not at the console address.
  if (host::game_image) return nullptr;
  switch (host::rd8(kStateMachine)) {
    case 0x02: return "VS Mode";
    case 0x03: return "Classic";
    case 0x04: return "Adventure";
    case 0x05: return "All-Star";
    case 0x0F: return "Target Test";
    case 0x10: return "Super Sudden Death";
    case 0x11: return "Invisible Melee";
    case 0x12: return "Slo-Mo Melee";
    case 0x13: return "Lightning Melee";
    case 0x1B: return "Tournament";
    case 0x1C: return "Training";
    case 0x1D: return "Tiny Melee";
    case 0x1E: return "Giant Melee";
    case 0x1F: return "Stamina Mode";
    case 0x20: return "Home-Run Contest";
    case 0x21: return "10-Man Melee";
    case 0x22: return "100-Man Melee";
    case 0x23: return "3-Minute Melee";
    case 0x24: return "15-Minute Melee";
    case 0x25: return "Endless Melee";
    case 0x26: return "Cruel Melee";
    case 0x2B: return "Event Match";
    // Title, menus, trophy gallery, boot and the rest are all honestly "the menus".
    default: return nullptr;
  }
}

void update_discord_presence(bool force = false) {
  if (!host::discord::enabled() || yougame::session()) return;   // YouGame shows what its players are doing
  // prepare_online_match_state runs this every frame the online menus are up. Rebuilding twice a
  // second is already far more often than Discord accepts an update. Every caller is on the
  // simulation thread (all of them are reached through handle()), so the static needs no guard.
  static uint64_t last_ms = 0;
  const uint64_t now = time_ms();
  if (!force && now - last_ms < 2000) return;
  last_ms = now;

  host::discord::Presence p;
  // The join secret is this player's own Slippi connect code and never anything else. A presence is
  // public, and a connect code carries no network location; an IP address must never end up here.
  const std::string my_code = g_user && g_user->IsLoggedIn() ? g_user->GetUserInfo().connect_code : std::string();
  const bool searching = g_matchmaking && g_matchmaking->IsSearching();
  const bool in_match = g_in_online_match && !is_disconnected();

  if (in_match) {
    p.details = mode_name(g_last_search.mode);
    std::string opponent;
    if (g_matchmaking)
      for (int i = 0; i < 4 && opponent.empty(); ++i)
        if (i != (int)g_local_player_index) opponent = g_matchmaking->GetPlayerName((uint8_t)i);
    p.state = opponent.empty() ? "In a match" : "vs " + opponent;
    p.party_size = 2; p.party_max = 2;
    // Keeps the Discord party id stable across the whole session. The party is full, so this only
    // names the party: no join secret is published while a match is running.
    p.join_code = my_code;
  } else if (searching) {
    const bool by_code = g_last_search.mode == Matchmaking::DIRECT || g_last_search.mode == Matchmaking::TEAMS;
    p.details = mode_name(g_last_search.mode);
    p.state = by_code ? "Waiting for a friend" : "Searching";
    p.party_size = 1; p.party_max = 2;
    if (by_code) p.join_code = my_code;   // a friend can join a code search, not a matchmaking queue
  } else if (const char* offline = offline_mode_name()) {
    p.details = offline;
    p.state = my_code;
    // Still joinable: someone in training is exactly the person a friend wants to pull into a game.
    if (!my_code.empty()) { p.party_size = 1; p.party_max = 2; p.join_code = my_code; }
  } else {
    p.details = "In the menus";
    p.state = my_code;
    if (!my_code.empty()) { p.party_size = 1; p.party_max = 2; p.join_code = my_code; }
  }
  host::discord::publish(p);
}

void consume_discord_join() {
  if (!host::discord::enabled() || yougame::session()) return;   // YouGame invites come through YouGame
  const std::string code = host::discord::take_join_code();
  if (code.empty() || !g_direct_codes) return;
  // Queue it for the Direct screen and remember it for the next Direct matchmaking command. The
  // latter makes the invite usable even if the player accepts the suggestion without typing it.
  g_direct_codes->AddOrUpdateCode(code);
  g_discord_join_code = code;
  g_discord_join_expires_ms = time_ms() + 5 * 60 * 1000;
  host::log("slippi: Discord invite from %s; it is now the first suggestion under Online > Direct", code.c_str());
}

bool discord_join_pending() {
  if (!g_discord_join_code.empty() && time_ms() >= g_discord_join_expires_ms) {
    g_discord_join_code.clear();
  }
  return !g_discord_join_code.empty();
}

// `why` matters only to a YouGame ranked set (yougame_link.h Leave): the game's own cleanup command
// is the player leaving, the checks below drop a connection that broke or a match they refuse.
void cleanup_connection(yougame::Leave why = yougame::Leave::Lost) {
  g_slippi_link.cancel([](const char* line) { host::log("%s", line); });
  // The match-state poll sets it after this cleanup; a game that ended because the opponent went keeps it
  // through the game's own cleanup that follows (g_unranked_left_in_game). Anything else clears it.
  g_unranked_opponent_left = g_unranked_left_in_game;   // kept until the next search (CMD_FIND_OPPONENT)
  finish_summary(why == yougame::Leave::Quit ? "quit" : why == yougame::Leave::Next ? "completed" : "disconnected");
  // The Friends lobby: Next and Lost park its room for the next search (yougame_link.h Leave); any other
  // cleanup leaves the room, and the select can only go back.
  if (g_friends_lobby.armed && why != yougame::Leave::Next && why != yougame::Leave::Lost) g_friends_lobby.closed = true;
  if (g_friends_lobby.armed && why == yougame::Leave::Lost && !g_in_online_match) {
    g_friends_lobby.dropped = true;
    ++g_friends_lobby.note_seq;
    g_friends_lobby.note = g_forced_error;   // why, when it was said before this cleanup (else below)
  }
  host::log("slippi: connection cleanup%s", g_warmup_parked ? " (the player left the warm-up)"
                                          : g_search_away ? " (the kept search ends)" : "");
  g_warmup_parked = false;
  g_search_away = false;
  g_warmup_pick_sent = 0xFFFF;   // a new opponent gets the pick afresh
  if (g_user) g_user->ResetOnlineCompatibility();
  host::cosmetics::thaw_after_online_session();
  stop_early_inputs();
  if (g_matchmaking || g_netplay) {
    std::lock_guard<std::mutex> lk(g_early_send_mutex);
    std::thread([mm = std::move(g_matchmaking), nc = std::move(g_netplay)]() mutable { mm.reset(); nc.reset(); }).detach();
  }
  g_matchmaking = std::make_unique<Matchmaking>(g_user.get());
  g_netplay = nullptr;
  if (yougame::session()) yougame::end_session(why);
  g_local_selections.Reset();
  g_stage_pool.clear();
  // A drop's reason said after its cleanup (Lost) is kept for the select's note before the select's own
  // cleanup (lobby_css_prep's Next) clears it.
  if (g_friends_lobby.armed && g_friends_lobby.dropped && !g_forced_error.empty()) g_friends_lobby.note = g_forced_error;
  g_forced_error.clear();
  g_overwrite_selections.clear();
  g_setup_hash = {};
  g_play_session_active = false;
  g_in_online_match = false;
#if MU_SQUAD
  g_squad_costume_match = false;   // the connection is gone: no squad match is set up
  g_squad_match_standards.clear();
#endif
  gecko_session::disconnected();
#ifdef YOUGAME_AGENT_TEST_HOOK
  yougame::agent_test::static_match.event(yougame::agent_test::StaticEvent::Cancel, true, [] {});
#endif
  trace_end_match();
  host::set_emulation_speed(1.0);
  update_discord_presence(true);   // back to "In the menus" straight away, not two seconds later
}

// ---------------------------------------------------------------- per-frame online flow
// The remote player the current wait is for, and how far along they were when it began.
static int g_input_wait_player = 0;
bool should_skip_online_frame(int32_t frame, int32_t finalized_frame) {
  auto st = g_netplay->GetSlippiConnectStatus();
  if (st == NetplayClient::ConnectStatus::FAILED || st == NetplayClient::ConnectStatus::DISCONNECTED) return false;
  bool any_needs_inputs = false;
  int behind = -1;
  uint8_t remote_count = bounded_remote_port_count();
  for (uint8_t i = 0; i < remote_count; ++i) {
    if (g_netplay->IsLocalVirtualPort(i)) continue; // filled locally before this frame is read; never stalls
    auto pad = g_netplay->GetSlippiRemotePad(i, ROLLBACK_MAX_FRAMES);
    if (pad->is_disconnected && pad->despawn_frame != 0) { g_stall_frame_counts[i] = 0; continue; }
    int32_t latest = pad->latest_frame;
    bool enough = latest - finalized_frame >= (frame - finalized_frame - ROLLBACK_MAX_FRAMES);
    // Start in step. The Source Port loads a match with no emulated disc time, so it reached frame 1
    // well before a Slippi Dolphin opponent, ran into the countdown and froze at frame 8 (the rollback
    // limit) until they caught up, then time sync halted it again at frame 30: in one player's log 47
    // of 49 matches froze there, up to 1.25 s (GitHub #44). Holding frame 2 until the other side has
    // started puts that wait before anything moves, and the two games then start together. Frame 2,
    // not 1: both games have sent frame 1's inputs by then (frame 1 re-run would also redo the
    // match setup), so neither waits on the other forever.
    if (frame == 2 && latest < 1) enough = false;
    if (enough) { g_stall_frame_counts[i] = 0; continue; }
    g_stall_frame_counts[i]++;
    any_needs_inputs = true;
    if (behind < 0) behind = i;
    // 7 s without the other side's inputs means they are gone, except before their first frame (the frame-2
    // hold above): they may still be loading the stage, which took 13.3 s on a slow GPU in one match, and the
    // player waiting dropped them as "opponent left" before the match began. Until then they get 30 s.
    const int stall_limit = latest < 1 ? 60 * 30 : 60 * 7;
    if (g_stall_frame_counts[i] > stall_limit) {
      host::log("slippi: force-disconnecting player %u after %d s stall (frame %d, latest %d)", pad->player_idx, stall_limit / 60, frame, latest);
      g_netplay->ForceDisconnectPlayer(pad->player_idx);
      g_stall_frame_counts[i] = 0;
      continue;
    }
  }
  // A report of "it froze, then felt delayed" cannot be judged without knowing which side stopped.
  // The start and the end of every wait for the other player's inputs are logged for that.
  if (any_needs_inputs) {
    if (g_input_wait_frames++ == 0) {
      ++g_match_input_waits;
      // The player who is behind, not always the first one: with three others in a doubles match
      // this line used to name the first player's frame whoever the wait was for.
      g_input_wait_player = behind < 0 ? 0 : behind;
      auto pad = g_netplay->GetSlippiRemotePad(g_input_wait_player, ROLLBACK_MAX_FRAMES);
      g_input_wait_latest = pad->latest_frame;
      host::log("slippi: waiting for the other player's inputs on frame %d (player %d of %u others, their latest frame %d, ping %u ms)",
                frame, g_input_wait_player + 1, (unsigned)remote_count, pad->latest_frame, (unsigned)g_netplay->LastPingMs());
    }
    return true;
  }
  if (g_input_wait_frames > 0) {
    // Which side stopped: while this side waited N frames, a game that kept running sent N or more
    // new frames, and they arrive together once the connection lets them through. A game that had
    // itself stopped comes back with few.
    auto pad = g_netplay->GetSlippiRemotePad(g_input_wait_player < (int)remote_count ? g_input_wait_player : 0, ROLLBACK_MAX_FRAMES);
    const int gained = pad->latest_frame - g_input_wait_latest;
    if (gained >= g_input_wait_frames) ++g_summary.held_up;
    else ++g_summary.fell_behind;
    host::log("slippi: inputs arrived on frame %d after waiting %d frames (%.2f s); their latest frame went %d to %d: %s", frame,
              g_input_wait_frames, g_input_wait_frames / 60.0, g_input_wait_latest, pad->latest_frame,
              gained >= g_input_wait_frames ? "their game kept running, the inputs were held up on the way"
                                            : "their game fell behind too");
    // A wait the player can feel (6 frames, 0.1 s, or more) marks the trace by itself, so the match
    // is kept and Send recent game logs carries it even when nobody pressed a mark button.
    if (g_input_wait_frames >= 6) g_trace_record.flags |= net_trace::kMark;
    g_input_wait_frames = 0;
  }
  // Every 30 frames for the whole match: a strict threshold while the match starts, then only for a
  // gap the other side's own catch-up cannot cover (slippi_time_sync.h). Checking only the first 120
  // frames left an instance that drifted ahead there for the rest of the match, predicting and
  // rolling back.
  if (frame % ONLINE_LOCKSTEP_INTERVAL == 0 && !g_currently_skipping) {
    int32_t offset = g_netplay->CalcTimeOffsetUs();
    g_trace_offset_us = offset;
    if (const int halt = slippi::time_sync::HaltFrames(offset, frame)) {
      g_currently_skipping = true;
      g_frames_to_skip = halt;
      host::log("slippi: halting on frame %d for time sync (offset %d us, %d frames)", frame, offset, g_frames_to_skip);
    } else if (offset > slippi::time_sync::kSlackUs || offset < -slippi::time_sync::kSlackUs) {
      host::log("slippi: time offset %d us on frame %d (no frame shed)", offset, frame);
    }
  }
  if (g_frames_to_skip > 0) { --g_frames_to_skip; return true; }
  g_currently_skipping = false;
  return false;
}

bool opponent_runahead() {
  return g_matchmaking->AllRemotesAreBots();
}

bool should_advance_online_frame(int32_t frame) {
  if (opponent_runahead()) return false;
  if (frame % ONLINE_LOCKSTEP_INTERVAL == 0) {
    int32_t offset = g_netplay->CalcTimeOffsetUs();
    g_trace_offset_us = offset;
    float deviation = 0;
    // MELEE_SYNC_SMOOTH=1 (measured against the default before it becomes a setting): start the
    // speed correction at 2 ms instead of 8 ms ahead, up to the same 1% Slippi allows, so the two
    // games meet again before a frame has to be halted or skipped. Only this side's own speed
    // changes; the other player's game, Slippi Dolphin included, runs its own sync unchanged.
    static const bool smooth = [] { const char* v = std::getenv("MELEE_SYNC_SMOOTH"); return v && *v == '1'; }();
    if (smooth) {
      if (offset > -2000 && offset < 2000) deviation = 0;
      else if (offset < 0) deviation = std::min(-offset / (2 * 16683.0f), 1.0f) * 0.01f;
      else deviation = std::min(offset / (2 * 16683.0f), 1.0f) * -0.01f;
    } else if (offset > -250 && offset < 8000) deviation = 0;
    else if (offset < 0) deviation = std::min(-offset / (3 * 16683.0f), 1.0f) * 0.01f;
    else deviation = std::min(offset / (3 * 16683.0f), 1.0f) * -0.005f;
    // Slippi Dolphin runs the console's 59.94 Hz (16683 us a frame) and its time sync is built on
    // that; the host ticks 16667 us, which left this side drifting ahead until the opponent sped up.
    host::set_emulation_speed(16667.0 / 16683.0 * (1.0 + deviation));
    const int32_t frame_time = 16683, t1 = 10000, t2 = frame_time + t1;
    g_fall_behind += offset < -t1 ? 1 : 0;
    g_fall_far_behind += offset < -t2 ? 1 : 0;
    bool slow = (offset < -t1 && g_fall_behind > 50) || (offset < -t2 && g_fall_far_behind > 15);
    // The counts add up over the whole match, as in Slippi Dolphin (where this is a 10 s notice), so
    // once past the mark every later check repeated the line: once a match is enough.
    if (slow && !g_poor_performance_logged && g_matchmaking->RemotePlayerCount() == 1) {
      g_poor_performance_logged = true;
      host::log("slippi: possible poor match performance detected on frame %d (offset %d us)", frame, offset);
    }
    if (offset < -t2 && !g_currently_advancing) {
      g_currently_advancing = true;
      g_frames_to_advance = slippi::time_sync::AdvanceFrames(offset, frame);
      host::log("slippi: advancing on frame %d for time sync (offset %d us, %d frames)", frame, offset, g_frames_to_advance);
    }
  }
  if (g_frames_to_advance > 0) {
    if (frame % 5 != 0) return false;
    --g_frames_to_advance; ++g_match_advances;
    return true;
  }
  g_currently_advancing = false;
  return false;
}

// The input delay this match uses. Auto: enough frames to cover half the round trip plus 4 ms of
// jitter, never under Slippi's default 2 nor over 6; with no ping measured yet, the set value. The
// game asks for it twice while a match is set up (CMD_GET_DELAY, then the match start block), so the
// value is latched on the first ask and held until the match ends: both asks must agree.
static int g_latched_delay = 0;
static int effective_delay() {
  // The Friends lobby's Start froze one input delay for every machine (yougame_lobby_setup.h).
  if (const auto lobby = yougame::friends_lobby(); lobby.open && lobby.plan && lobby.plan->ok) return lobby.plan->input_delay;
  // MELEE_TEST_AUTO_DELAY=1 (hidden runs, which read no settings file) turns auto on.
  static const bool test_auto = [] { const char* v = host::test_env::getenv("MELEE_TEST_AUTO_DELAY"); return v && *v == '1'; }();
  if (!g_config.auto_delay && !test_auto) return g_config.delay;
  if (g_latched_delay > 0) return g_latched_delay;
  // The game polls the match state block (which carries this value) every frame of the search, long
  // before an opponent is connected, and takes the delay from it once, when the match starts
  // (mu_online.c). So nothing is latched before there is a ping, or before a connected opponent is
  // known never to send one (Slippi Dolphin and older builds send no pre-match ping): until then the
  // set delay is answered without being held. In-match pings come from pad acks; at match setup
  // only the pre-match ping (0xE2) exists.
  const int match_ping = g_netplay ? (int)g_netplay->LastPingMs() : 0;
  const int ping = match_ping > 0 ? match_ping : g_netplay ? g_netplay->PreMatchPingMs() : 0;
  if (ping <= 0) {
    if (!g_netplay || g_netplay->GetSlippiConnectStatus() != NetplayClient::ConnectStatus::CONNECTED) return g_config.delay;
    const auto build = g_netplay->GetRemoteBuild(0);
    const uint64_t now_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const uint64_t connected_at = g_netplay->ConnectedAtMs();
    const bool waited = connected_at > 0 && now_ms > connected_at + 2000;
    if (!(build.received && !build.pings) && !waited) return g_config.delay;   // the ping is on its way
    g_latched_delay = g_config.delay;
    host::log("slippi: auto delay: no ping from this opponent, using the set delay %d", g_latched_delay);
    return g_latched_delay;
  }
  g_latched_delay = std::clamp((int)std::ceil((ping / 2.0 + 4.0) / 16.683), 2, 6);
  host::log("slippi: auto delay: %s ping %d ms, %d frames", match_ping > 0 ? "match" : "pre-match", ping, g_latched_delay);
  return g_latched_delay;
}

void prepare_opponent_inputs(int32_t frame, bool should_skip, std::vector<uint8_t>& q) {
  q.clear();
  uint8_t frame_result = 1;
  auto st = g_netplay->GetSlippiConnectStatus();
  if (should_skip) frame_result = 2;
  else if (st != NetplayClient::ConnectStatus::CONNECTED) frame_result = 3;
  else if (should_advance_online_frame(frame)) frame_result = 4;
  q.push_back(frame_result);
  uint8_t remote_count = bounded_remote_port_count();
  q.push_back(remote_count);
  g_netplay->FillDroppedPlayerInputs(frame);
  std::unique_ptr<RemotePadOutput> results[REMOTE_PLAYER_MAX];
  int32_t latest_from_opps = -123 - 1;
  uint32_t last_checksum_frame = 0, last_checksum = 0;
  for (int i = 0; i < remote_count; ++i) {
    results[i] = g_netplay->GetSlippiRemotePad(i, ROLLBACK_MAX_FRAMES, frame);
    if (results[i]->is_disconnected) continue;
    int32_t cf = results[i]->checksum_frame;
    if (results[i]->checksum_primary && cf > g_last_checksum_frame[i] && results[i]->checksum) {
      auto it = g_local_checksums.find(cf);
      if (it != g_local_checksums.end()) {
        g_last_checksum_frame[i] = cf; ++g_checksums_compared;
        if (it->second != results[i]->checksum) { ++g_checksums_mismatched; g_trace_desync_pending = true; host::log("slippi: DESYNC: checksum mismatch at frame %d (ours %08X, player %u %08X)", cf, it->second, results[i]->player_idx, results[i]->checksum); }
        else if (g_checksums_compared % 20 == 0) host::log("slippi: checksums agree through frame %d (%u compared, %u mismatched)", cf, g_checksums_compared, g_checksums_mismatched);
      }
    }
    if (results[i]->latest_frame > latest_from_opps) {
      last_checksum_frame = (uint32_t)results[i]->checksum_frame;
      last_checksum = results[i]->checksum;
      latest_from_opps = results[i]->latest_frame;
    }
  }
  // Slippi 3.7.0's player drop protocol (despawn_frame, FillDroppedPlayerInputs) replaces our gone-port
  // remap; the YouGame result still needs which ports left.
  for (int i = 0; i < remote_count; ++i)
    if (results[i]->is_disconnected && results[i]->player_idx < PLAYER_COUNT_MAX) g_gone_ports |= 1u << results[i]->player_idx;
  for (int i = 0; i < remote_count; ++i) {
    if (!results[i]->is_disconnected) { append_u32(q, (uint32_t)results[i]->checksum_frame); append_u32(q, results[i]->checksum); continue; }
    // A dropped player's checksums stop: the game gets someone else's current one to compare. Their
    // frames need no such trick, FillDroppedPlayerInputs keeps them current.
    append_u32(q, last_checksum_frame); append_u32(q, last_checksum);
  }
  for (int i = remote_count; i < REMOTE_PLAYER_MAX; ++i) { append_u32(q, 0); append_u32(q, 0); }
  int offset[REMOTE_PLAYER_MAX] = {};
  int32_t latest_read[REMOTE_PLAYER_MAX] = {};
  for (int i = 0; i < remote_count; ++i) {
    offset[i] = std::max(0, (results[i]->latest_frame - frame) * PAD_FULL_SIZE);
    int32_t latest = std::min(results[i]->latest_frame, frame);
    latest_read[i] = latest;
    append_u32(q, (uint32_t)latest);
  }
  for (int i = remote_count; i < REMOTE_PLAYER_MAX; ++i) { latest_read[i] = frame; append_u32(q, (uint32_t)frame); }
  append_u32(q, (uint32_t)*std::min_element(std::begin(latest_read), std::end(latest_read)));
  uint8_t latest_inputs[REMOTE_PLAYER_MAX][PAD_FULL_SIZE] = {};
  for (int i = 0; i < REMOTE_PLAYER_MAX; ++i) {
    std::vector<uint8_t> tx;
    if (i < remote_count && offset[i] < (int)results[i]->data.size()) tx.assign(results[i]->data.begin() + offset[i], results[i]->data.end());
    tx.resize(PAD_FULL_SIZE * ROLLBACK_MAX_FRAMES, 0);
    std::copy(tx.begin(), tx.begin() + PAD_FULL_SIZE, latest_inputs[i]);   // what the game loads when it has no input for the frame
    q.insert(q.end(), tx.begin(), tx.end());
  }
  // Slippi 3.7.0. The frame each remote player's fighter despawns on, 0 if it does not: the game
  // compares it with the frame it is processing, so it lands on the same frame for everyone, also in
  // a rollback.
  for (int i = 0; i < REMOTE_PLAYER_MAX; ++i) append_u32(q, i < remote_count ? (uint32_t)results[i]->despawn_frame : 0);
  // The input to predict with for each of the frames after each player's latest: the latest input,
  // or a newer input suggestion from that player. The game checks every prediction against the real
  // input and rolls back if they differ.
  for (int i = 0; i < REMOTE_PLAYER_MAX; ++i) {
    uint8_t predictions[suggestions::kPredictedFrames][suggestions::kReportBytes];
    for (auto& p : predictions) std::memcpy(p, latest_inputs[i], PAD_FULL_SIZE);
    if (i < remote_count) suggestions::fill_predictions((size_t)i, latest_read[i], results[i]->is_disconnected, latest_inputs[i], predictions);
    q.insert(q.end(), &predictions[0][0], &predictions[0][0] + sizeof predictions);
  }
  // Random bits the game sends with its next pad and mixes into the RNG seed. Not from the match's
  // own generator: the opponent sees them and could work out that generator's state.
  { static std::random_device pad_rng_bits; q.push_back((uint8_t)(pad_rng_bits() & 0xFF)); }
  // Early inputs, Source Port only (its online code is native and reads past Slippi's reply; the
  // Static Recomp's game code reads the fixed reply and must get exactly that): per remote, a count
  // and then entries of target pad frame + 12-byte pad, for frames after that remote's latest
  // confirmed one. The game predicts a not-yet-confirmed frame from the newest entry up to it.
  if (host::game_image && early_inputs_accept()) {
    std::vector<host::early_input::Entry> entries;
    for (int i = 0; i < REMOTE_PLAYER_MAX; ++i) {
      entries.clear();
      if (i < remote_count && !results[i]->is_disconnected) g_netplay->EarlyPads(i, latest_read[i], entries);
      if (entries.size() > (size_t)host::early_input::kMaxEntries) entries.resize(host::early_input::kMaxEntries);
      q.push_back((uint8_t)entries.size());
      for (const auto& e : entries) {
        append_u32(q, (uint32_t)e.target);
        q.insert(q.end(), e.pad, e.pad + host::early_input::kPadBytes);
        for (int k = host::early_input::kPadBytes; k < PAD_FULL_SIZE; ++k) q.push_back(0);
      }
    }
  }
}

// Diagnostic, off unless MELEE_TRACE_ONLINE_FRAMES=1, Legacy only (it reads console addresses):
// each online command's frame beside the engine's frame counters (gm_80479D58: +0 bodies run,
// +8 unpaused bodies) and the pad queue (HSD_PadLibData: qread, qwrite, qcount). This is how the
// native engine's online frame numbering is matched to the console game's.
bool trace_online_frames() {
  static const bool on = [] {
    const char* v = std::getenv("MELEE_TRACE_ONLINE_FRAMES");
    return v && v[0] == '1';
  }();
  return on && !host::game_image;
}
void trace_online_frame(const char* what, int32_t frame) {
  if (!trace_online_frames()) return;
  const uint32_t pad = 0x804C1F78u;
  host::log("online trace: %s frame %d engine unk0 %d unk8 %d pad qread %u qwrite %u qcount %u retrace %u",
            what, frame, (int32_t)host::rd32(0x80479D58u), (int32_t)host::rd32(0x80479D60u),
            host::rd8(pad + 1), host::rd8(pad + 2), host::rd8(pad + 3), host::retrace_count());
}

void handle_online_inputs(const uint8_t* payload, uint32_t size, std::vector<uint8_t>& q) {
  // 27: Slippi 3.7.0's TXB; 36 when the native game also sends its local partner's pad (PAD_WIRE_SIZE).
  if (size < 27 || (g_netplay && g_netplay->SecondLocalPort() >= 0 && size != 36)) { q.assign(1, 3); return; }
  q.clear();
  int32_t frame = (int32_t)be32(payload), finalized = (int32_t)be32(payload + 4);
  trace_online_frame("inputs", frame);
  uint32_t finalized_checksum = be32(payload + 8);
  uint8_t delay = payload[12];
  const uint8_t* inputs = payload + 13;
  const bool is_leaving = payload[26] != 0;   // Slippi 3.7.0; payload[25] is the port the local inputs come from
  if (frame == 1) {
    // A same-ticket rematch reopens presence even if its previous summary/report is still pending.
    // Repeated frame 1 polls are deduplicated by the host worker's atomic edge counter.
    if (!is_disconnected()) yougame::online_match_state(true);
    if (!g_summary.active && !is_disconnected()) {
#ifdef MELEE_DOUBLES
      if (doubles_route()) {
        ++g_doubles_games;   // all four start each game together
        yougame::doubles::ui.teams_game();   // the home's match has begun for this seat
      }
#endif
      g_summary.begin((int)g_last_search.mode, yougame::active() &&
          !g_last_search.friend_room.empty());
      yg_enet_mirror_active(1);
    }
    g_available_savestates.clear();
    g_active_savestates.clear();
    // Console-RAM savestates are the static recomp's; the native game keeps its own snapshots, and
    // its memory holds no console heap bounds to size these from.
    if (!host::game_image)
      for (int i = 0; i < ROLLBACK_MAX_FRAMES; ++i) g_available_savestates.push_back(std::make_unique<Savestate>());
    for (auto& c : g_stall_frame_counts) c = 0;
    g_frames_to_skip = 0; g_currently_skipping = false;
    g_frames_to_advance = 0; g_currently_advancing = false; g_fall_behind = 0; g_fall_far_behind = 0;
    g_poor_performance_logged = false;
    g_local_selections.Reset();
    g_match_rollbacks_start = g_rollbacks; g_match_stalls = 0; g_match_delay = delay;
    g_match_input_waits = 0; g_match_advances = 0; g_input_wait_frames = 0;
    // The game resends frame 1 while it waits for the opponent: the trace starts on the first one.
    if (!g_trace_open || g_trace_record.frame != 1) {
      g_trace_open = false; g_trace_offset_us = 0;
      net_trace::take_presents();
      net_trace::begin_match();
      // The session trace file sits beside the match's replay, under its name. The Static Recomp's
      // replay is open and named by now; the Source Port names its replay when it writes it and
      // reports it then (net_trace::replay_saved). No replay, no trace.
      host::wait_file_writes();   // the last match's replay names its trace: it is on disk first
      if (host::game_image) net_trace::file_begin(host::options.replay_dir.c_str(), nullptr);
      else if (slippi::recording()) net_trace::file_begin(slippi::replay_directory().c_str(), slippi::last_replay_path().c_str());
    }
    if (g_netplay) { g_netplay->GetAndResetAvgPingMs(); g_netplay->StartSlippiGame(); }
#if MU_SQUAD
    // A squad link that dropped during the VS load: the game was never played, so the room records
    // nothing and the player is told it was cancelled (the disconnect below still ends it).
    const bool squad_never_started = g_last_search.squad && is_disconnected();
    if (squad_never_started) {
      host::log("squad: connection lost during the VS load, match cancelled");
      // A squad match this game refused (a costume over its size) says why, not "connection lost".
      g_forced_error = g_netplay && g_netplay->SquadCancelled() ? g_netplay->SquadCancelText()
                                                                : "Match cancelled: connection lost.";
    }
    if (yougame::session() && !squad_never_started) yougame::game_started(g_game_facts.rng, g_game_facts.stage);   // the room records this game's result
#else
    if (yougame::session()) yougame::game_started(g_game_facts.rng, g_game_facts.stage);   // the room records this game's result
#endif
    host::log("slippi: online game starts, delay %d frames, direct peer to peer", (int)delay);
    g_latched_delay = (int)delay;   // the delay the game took: every later answer this match agrees with it
    g_in_online_match = true;
    g_friends_lobby.note.clear();
    gecko_session::match_started();   // the agreed Gecko codes, decided from both locks
    if (g_user) g_user->ResetOnlineCompatibility();  // the next game needs its own fresh check
    host::input_mark_match_start();
    // Early inputs: watch the controller for the match. Sent only to peers whose build takes them.
    host::early_input::reset_stats();
    // Slippi input suggestions: on with the same setting, and off whenever a bot is in the game, on
    // either side (a bot is expected to make the connection feel good on its own).
    bool bot_in_game = false;
    if (g_matchmaking) for (const auto& player : g_matchmaking->GetPlayerInfo()) bot_in_game |= player.is_bot;
    const bool suggest = early_inputs_enabled() && !bot_in_game;
    suggestions::start_game(suggest, bounded_remote_port_count());
    if (early_inputs_enabled()) host::early_input::start(send_early_pad, suggest ? send_suggestion : nullptr);
    g_local_checksums.clear(); g_checksums_compared = 0; g_checksums_mismatched = 0; g_trace_desync_pending = false; std::fill(std::begin(g_last_checksum_frame), std::end(g_last_checksum_frame), 0);
    g_gone_ports = 0;
    g_gone_own_frames = -1;
    std::fill(std::begin(g_gone_last_frame), std::end(g_gone_last_frame), slippi::kNoFrame);
  }
  if (frame > 0 && (uint32_t)frame > g_summary.frames) {
    g_summary.frames = (uint32_t)frame;
    if (g_netplay) g_summary.ping.add((uint64_t)std::max(0, g_netplay->LastPingMs()));
  }
  // The local player chose to leave (party mode): that ends the game for this side as losing the
  // connection does, and the other players take it for a disconnect too.
  if (is_leaving && !is_disconnected() && g_netplay) g_netplay->ForceDisconnect();
  if (is_disconnected()) {
    // YouGame Unranked: the opponent left mid-game (closed the game, or their connection went), not this
    // player (is_leaving). Read now, while the session and its room are still here; the game's cleanup
    // after DISCONNECTED makes it the character select's "Opponent left".
    if (!g_unranked_left_in_game && g_summary.active && g_last_search.mode == Matchmaking::UNRANKED)
      host::log("yougame: unranked: disconnect mid-game (session %d, leaving %d, friend room %d, connected %d, own drop %d)",
                yougame::session() ? 1 : 0, is_leaving ? 1 : 0, g_last_search.friend_room.empty() ? 0 : 1, g_play_session_active ? 1 : 0,
                yougame::own_room_dropped() ? 1 : 0);
    if (!is_leaving && !g_unranked_left_in_game && g_summary.active &&
        yougame::unranked::opponent_left(yougame::session(), (int)g_last_search.mode,
            !g_last_search.friend_room.empty(), g_play_session_active,
            yougame::session() && yougame::own_room_dropped())) {
      g_unranked_left_in_game = g_unranked_opponent_left = true;
      host::log("yougame: unranked: the opponent left mid-game; the character select says so until the next search");
    }
    finish_summary("disconnected");
    q.push_back(3);
    return;
  }
  if (frame % 30 == 0) host::log("slippi: online frame %d wall %.3f s retrace %u rollbacks %llu", frame, host::now_seconds(), host::retrace_count(), (unsigned long long)g_rollbacks);
  g_game_facts.frames = frame;
  if (finalized > 0 && finalized_checksum) { g_local_checksums[finalized] = finalized_checksum; while (g_local_checksums.size() > 600) g_local_checksums.erase(g_local_checksums.begin()); }
  g_netplay->DropOldRemoteInputs(finalized);
  bool skip = should_skip_online_frame(frame, finalized);
  if (skip) { ++g_match_stalls; g_netplay->SendSlippiPad(nullptr); host::early_input::note_skipped(); }
  else {
    if (frame == 1) for (int i = 1; i <= delay; ++i) g_netplay->SendSlippiPad(std::make_unique<Pad>(i), g_netplay->SecondLocalPort() >= 0 ? std::make_unique<Pad>(i) : nullptr);
    g_netplay->SendSlippiPad(std::make_unique<Pad>(frame + delay, finalized, finalized_checksum, inputs),
        g_netplay->SecondLocalPort() >= 0 ? std::make_unique<Pad>(frame + delay, finalized, finalized_checksum, payload + 27) : nullptr);
    host::early_input::note_sent_pad(frame + delay, inputs);
    host::input_note_pad_frame(frame);
  }
  prepare_opponent_inputs(frame, skip, q);
  host::early_input::note_frame_done(skip, !q.empty() && q[0] == 4, inputs);
  // Session trace: the tick before is complete now; open this one.
  trace_close_tick();
  g_trace_record = {};
  g_trace_record.wall = host::now_seconds();
  g_trace_record.frame = frame;
  g_trace_record.offset_us = g_trace_offset_us;
  g_trace_record.wait_frames = (uint16_t)std::min(g_input_wait_frames, 0xFFFF);
  g_trace_record.ping_ms = (uint16_t)std::clamp(g_netplay->LastPingMs(), 0, 0xFFFF);
  if (skip) g_trace_record.flags |= g_input_wait_frames > 0 ? net_trace::kWait : net_trace::kShed;
  else if (!q.empty() && q[0] == 4) g_trace_record.flags |= net_trace::kAdvance;
  if (g_trace_desync_pending) { g_trace_record.flags |= net_trace::kDesync; g_trace_desync_pending = false; }
  // F8: the player marks "that just felt wrong", so a report can point at the moment.
  static const char* const kMarkNames[] = {"", "", "looked wrong", "input wrong", "sounded wrong"};
  static const uint8_t kMarkFlags[] = {0, net_trace::kMark, net_trace::kMarkVisual, net_trace::kMarkInput, net_trace::kMarkAudio};
  auto mark = [&](int kind) {
    g_trace_record.flags |= kMarkFlags[kind];
    if (kind == 1) host::log("slippi: marked frame %d in the session trace", frame);
    else host::log("slippi: marked frame %d in the session trace (%s)", frame, kMarkNames[kind]);
  };
  if (const int kind = host::window_take_trace_mark()) mark(kind);
  // The same from the controller, and saying which kind: D-pad Left "that looked wrong", D-pad
  // Right "my input came out wrong or late", D-pad Down "that sounded wrong". None of the three does anything in a match, and the
  // press is read from the pad the game is given, so it works on every controller and binding.
  {
    static uint8_t held = 0;
    const uint8_t dpad = inputs[1] & 0x07;   // PAD_BUTTON_LEFT 1, PAD_BUTTON_RIGHT 2, PAD_BUTTON_DOWN 4
    const uint8_t pressed = (uint8_t)(dpad & ~held);
    held = dpad;
    if (pressed & 1) mark(2);
    if (pressed & 2) mark(3);
    if (pressed & 4) mark(4);
  }
  std::memcpy(g_trace_record.pad, inputs, PAD_DATA_SIZE);
  g_trace_open = true;
}

void handle_capture_savestate(const uint8_t* payload) {
  if (is_disconnected()) return;
  int32_t frame = (int32_t)be32(payload);
  trace_online_frame("capture", frame);
  std::unique_ptr<Savestate> ss;
  if (!g_available_savestates.empty()) { ss = std::move(g_available_savestates.back()); g_available_savestates.pop_back(); }
  else if (!g_active_savestates.empty()) { auto it = g_active_savestates.begin(); ss = std::move(it->second); g_active_savestates.erase(it); }
  else {
    // Both pools are empty: no online game has started them, or this is the native game, which keeps
    // its own snapshots and creates none here. There is nothing to capture into.
    static bool logged = false;
    if (!logged) { logged = true; host::log("slippi: savestate capture for frame %d ignored: no savestate pool", frame); }
    return;
  }
  if (g_active_savestates.count(frame)) { g_available_savestates.push_back(std::move(g_active_savestates[frame])); g_active_savestates.erase(frame); }
  ss->Capture();
  g_active_savestates[frame] = std::move(ss);
}

void handle_load_savestate(const uint8_t* payload) {
  int32_t frame = (int32_t)be32(payload);
  trace_online_frame("load", frame);
  if (!g_active_savestates.count(frame)) { host::log("slippi: savestate for frame %d does not exist", frame); return; }
  std::vector<PreserveBlock> blocks;
  for (int i = 4; i + 8 <= 32 && be32(payload + i) != 0; i += 8)
    blocks.push_back({be32(payload + i), be32(payload + i + 4)});
  g_active_savestates[frame]->Load(blocks);
  ++g_rollbacks;
  trace_rollback(frame);
  // The next frame the game finishes continues from this older state, not from the frame on screen.
  // Blending the two (sub-frame animation) would draw positions that never existed on either
  // timeline, which on continuously animated stages showed as a glitch that only ever happened
  // online. The renderer holds exact frames across it instead. Training Mode's savestates use this
  // same load outside an online match; those are hard cuts.
  gx::mark_discontinuity(is_online_match());
  for (auto& kv : g_active_savestates) g_available_savestates.push_back(std::move(kv.second));
  g_active_savestates.clear();
}

// ---------------------------------------------------------------- lobby
int g_older_offer_mode = -1;   // YouGame is on an older release: the mode Start restarts into, -1 none
// A Friends room opened the online character select. Friends runs in Direct mode, so the menu asks
// once on its next entry (CMD_YOUGAME_FRIENDS_MENU 14) to put the cursor on Friends, not Slippi.
std::atomic<bool> g_friends_css{false};

void start_find_match(const uint8_t* payload, const std::string& friend_room = {}) {
  if (!friend_room.empty()) g_friends_css = true;
  Matchmaking::MatchSearchSettings search;
  search.mode = (Matchmaking::OnlinePlayMode)payload[0];
  g_older_offer_mode = -1;
  // Under the launcher, YouGame may still be on an older release: say so, and Start restarts in it.
  if (yougame::session() && yougame::launcher()) {
    bool can_restart = false;
    const std::string older = yougame::behind_version_now(&can_restart);
    // YouGame ahead of this build: the player updates Melee Unlocked, nothing to restart into.
    auto newer_than_ours = [](const std::string& a, const std::string& b) {
      int x[4] = {}, y[4] = {};
      std::sscanf(a.c_str(), "%d.%d.%d.%d", &x[0], &x[1], &x[2], &x[3]);
      std::sscanf(b.c_str(), "%d.%d.%d.%d", &y[0], &y[1], &y[2], &y[3]);
      for (int i = 0; i < 4; ++i) if (x[i] != y[i]) return x[i] > y[i];
      return false;
    };
    if (!older.empty() && newer_than_ours(older, yougame::build_version())) {
      g_forced_error = "YouGame is on Melee Unlocked " + older + " and you have " + yougame::build_version() +
                       ". Update Melee Unlocked from the launcher to play YouGame modes. Press Z to go back.";
      return;
    }
    if (!older.empty()) {
      // YouGame has not moved to this version yet: say so. When the launcher can start the version
      // YouGame is on, Start restarts in it.
      g_forced_error = "YouGame is on Melee Unlocked " + older + " and you have " + yougame::build_version() +
                       (can_restart ? ". Press Start to restart in " + older + " for YouGame, or Z to go back."
                                    : ". YouGame modes open here once YouGame updates. Press Z to go back.");
      if (can_restart) g_older_offer_mode = (int)search.mode;
      return;
    }
  }
  if (const char* reason = local_matchmaking_error((int)search.mode)) {
    g_forced_error = reason;
    return;
  }
  // Both Unranked under the launcher adds Slippi's Unranked only for what a Slippi Unranked pick plays:
  // the retail game, no mod view (native_online_policy.h). Otherwise it searches YouGame alone.
  search.slippi_gameplay = !g_local_build.mod_view &&
                           !native_profile_mode_error(effective_profile(), (int)Matchmaking::UNRANKED, false);
  std::string sj((const char*)payload + 1, 18);
  sj.erase(std::find(sj.begin(), sj.end(), '\0'), sj.end());
  const ArenaNext arena = std::move(g_arena_next);
  g_arena_next = ArenaNext{};
  if (!arena.fighters.empty()) {
    // An arena game under YouGame: the arena's room carries it. Not a code the player typed, so it
    // stays out of the recent codes, and a pending Discord join is left for the player's own Direct.
    search.arena_fighters = arena.fighters;
    sj = arena.code;
  } else if (search.mode == Matchmaking::DIRECT && discord_join_pending()) {
    // Choosing Direct after accepting a Discord Join means join that sender. Use the queued code
    // even if the game's text field still contains its old value; the user need not retype/paste it.
    const std::string invite_sj = utf8_to_shiftjis(g_discord_join_code);
    sj.assign(invite_sj.begin(), invite_sj.begin() + std::min<size_t>(invite_sj.size(), 18));
    g_discord_join_code.clear();
  }
  if (arena.fighters.empty()) {
    if (search.mode == Matchmaking::DIRECT) g_direct_codes->AddOrUpdateCode(shiftjis_to_utf8(sj));
    else if (search.mode == Matchmaking::TEAMS && friend_room.empty()) g_teams_codes->AddOrUpdateCode(shiftjis_to_utf8(sj));
  }
  search.connect_code = sj;
  search.friend_room = friend_room;
#if MU_SQUAD
  // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
  if (friend_room.empty()) {
    search.squad = yougame::squad::selected(search.mode, g_squad_row, false);
  } else {
    // A friend room plays Squad Strike only when its own "Play a friend" picker returned it.
    search.squad = search.mode == Matchmaking::DIRECT && friend_room == g_squad_friend_room;
    g_squad_friend_room.clear();
    g_squad_friend_intent = false;
  }
  // A friend room meets before anyone picks: each lock-in carries its own lineup, and no queue key
  // uses the budget there, so only Unranked needs both before it searches.
  if (search.squad && friend_room.empty()) {
    // session(): the app's bridge, or the Squad Strike row picked under Melee Unlocked's launcher.
    // active() alone is the app only, and refused every Squad Strike search started from the launcher.
    if ((!yougame::session() && !Matchmaking::local_peer.enabled) ||
        !mu_squad_side_valid(&g_squad_side) || g_squad_budget.size() != 64) {
      host::log("squad: search refused (session %d, lineup %d, budget %zu)", yougame::session() ? 1 : 0,
                mu_squad_side_valid(&g_squad_side) ? 1 : 0, g_squad_budget.size());
      g_forced_error = "Squad Strike could not start. Pick your 4 fighters again.";
      return;
    }
    search.squad_side = g_squad_side;
    search.squad_budget = g_squad_budget;
  }
#endif
  g_last_search = search;
  // A new search: whoever answers may be on this build with the same mod, so the mod shows again.
  if (g_plain_opponent_handler) g_plain_opponent_handler(false, nullptr);
  // Restoring the mod view may change the pool since CSS requested its random stage.
  if (use_ranked_rules(search.mode) && g_local_selections.is_stage_selected && g_local_random_stage)
    g_local_selections.stage_id = random_stage(search.mode);
  if (Matchmaking::IsFixedRulesMode(search.mode)) {
    const bool extended = yougame_online_mod_content((int)search.mode);
    if ((!extended && g_local_selections.character_id >= 26) ||
        (use_ranked_rules(search.mode) && !g_ranked_rules.character_allowed(g_local_selections.character_id))) { g_forced_error = "The character you selected is not allowed in this mode"; return; }
    const auto& stages = allowed_stages(search.mode);
    if ((!extended || search.mode == Matchmaking::RANKED) && g_local_selections.is_stage_selected && std::find(stages.begin(), stages.end(), g_local_selections.stage_id) == stages.end()) {
      g_forced_error = "The stage being requested is not allowed in this mode"; return;
    }
  } else if (search.mode == Matchmaking::TEAMS && g_local_selections.character_id >= 26 &&
             !(g_local_build.mod_view && g_local_build.extended_content)) {
    g_forced_error = "The character you selected is not allowed in this mode"; return;
  }
  if (!enet_ready()) { g_forced_error = "Networking unavailable"; return; }
  host::cosmetics::freeze_for_online_session();
  g_search_started_ms = time_ms();
  // A client that already searched (it found a match, failed or ended) still holds its search
  // thread: starting another on it ends the process (std::thread assigned while joinable). The
  // character select cleans up before every search, but other paths can reach here without that
  // (an arena room's stage select locked in after its pair was dropped), so replace it here too.
  if (g_matchmaking && g_matchmaking->GetMatchmakeState() != Matchmaking::IDLE) {
    host::log("slippi: a new search on a used client (state %d): replaced first", (int)g_matchmaking->GetMatchmakeState());
    std::lock_guard<std::mutex> lk(g_early_send_mutex);
    std::thread([mm = std::move(g_matchmaking)]() mutable { mm.reset(); }).detach();
    g_matchmaking = std::make_unique<Matchmaking>(g_user.get());
  }
  g_matchmaking->FindMatch(search);
  update_discord_presence(true);
}

// The armed Friends lobby's search (the select is up): Direct with no code, holding a room (the host) or
// joining the invite's (a guest); after a match, the room the last one parked (yougame_link.cpp). A search
// already running or connected stays. False when it can't run (the reason is in g_forced_error).
bool start_lobby_search() {
  auto& lobby = g_friends_lobby;
  if (!lobby.armed || lobby.closed) return false;
  if (!g_user) init();
  if (g_matchmaking && (g_matchmaking->IsSearching() ||
                        g_matchmaking->GetMatchmakeState() == Matchmaking::CONNECTION_SUCCESS)) return true;
  g_forced_error.clear();
  if (yougame::session() && yougame::launcher() && !yougame::behind_version_now().empty()) {
    g_forced_error = "YouGame is on another Melee Unlocked version. Go back and choose Friends again.";
    return false;
  }
  if (const char* reason = local_matchmaking_error((int)Matchmaking::DIRECT)) { g_forced_error = reason; return false; }
  if (!enet_ready()) { g_forced_error = "Networking unavailable"; return false; }
  Matchmaking::MatchSearchSettings search;
  search.mode = Matchmaking::DIRECT;
  search.friends_lobby = true;
  search.lobby_room = lobby.room;
  search.lobby_control = lobby.control;
  lobby.dropped = false;   // this search takes the parked room back (ENTER, or READ after a drop)
  g_last_search = search;
  if (g_plain_opponent_handler) g_plain_opponent_handler(false, nullptr);
  host::cosmetics::freeze_for_online_session();
  // A client whose last search ended (a refused ticket leaves it in the error state) still holds its
  // finished search thread: starting another on it ended the process (std::terminate), which is how a
  // refused Doubles home closed the game (player report 11-22). Replace it, as start_search does.
  if (g_matchmaking && g_matchmaking->GetMatchmakeState() != Matchmaking::IDLE) {
    host::log("friends lobby: a new search on a used client (state %d): replaced first", (int)g_matchmaking->GetMatchmakeState());
    std::lock_guard<std::mutex> lk(g_early_send_mutex);
    std::thread([mm = std::move(g_matchmaking)]() mutable { mm.reset(); }).detach();
  }
  if (!g_matchmaking) g_matchmaking = std::make_unique<Matchmaking>(g_user.get());
  g_matchmaking->FindMatch(search);
  update_discord_presence(true);
  return true;
}

bool tag_matches_input(const uint8_t* input, uint8_t len, const std::string& tag) {
  std::string jis = utf8_to_shiftjis(tag);
  for (int i = 0; i < len; ++i) {
    uint8_t a = i * 2 < (int)jis.size() ? (uint8_t)jis[i * 2] : 0, b = i * 2 + 1 < (int)jis.size() ? (uint8_t)jis[i * 2 + 1] : 0;
    if (input[i * 3] != a || input[i * 3 + 1] != b) return false;
  }
  return true;
}

void handle_name_entry_load(const uint8_t* payload, std::vector<uint8_t>& q) {
  // The field holds 8 characters (24 bytes of payload); a longer length from the game is cut to that.
  uint8_t len = std::min<uint8_t>(payload[24], 8);
  uint32_t initial = be32(payload + 25);
  uint8_t scroll = payload[29], mode = payload[30];
  DirectCodes* history = mode == Matchmaking::TEAMS ? g_teams_codes.get() : g_direct_codes.get();
  uint32_t cur = initial;
  if (scroll == 1) ++cur;
  else if (scroll == 2) cur = cur > 0 ? cur - 1 : cur;
  else if (scroll == 3) cur = 0;
  std::string tag = "1";
  if (mode == Matchmaking::DIRECT && discord_join_pending()) {
    // Return the invite as the active completion even with an empty field, so the code appears
    // immediately when Online > Direct opens. start_find_match also consumes it if the field was
    // not explicitly accepted by the user.
    tag = history->get(0);
    cur = 0;
  } else {
    while (cur < (uint32_t)history->length()) {
      tag = history->get((int)cur);
      if (tag_matches_input(payload, len, tag)) break;
      cur = scroll == 2 ? cur - 1 : cur + 1;
    }
    tag = history->get((int)cur);
  }
  if (tag == "1") {
    std::string init_tag = history->get((int)initial);
    if (tag_matches_input(payload, len, init_tag)) { tag = init_tag; cur = initial; }
  }
  q.clear();
  if (tag == "1") {
    q.push_back(0);
    q.insert(q.end(), payload, payload + 3 * len);
    q.insert(q.end(), 3 * (8 - len), 0);
    q.push_back(len);
    append_u32(q, initial);
    return;
  }
  q.push_back(1);
  std::string jis = utf8_to_shiftjis(tag);
  for (int i = 0; i < 8; ++i) {
    for (int j = i * 2; j < i * 2 + 2; ++j) q.push_back(j < (int)jis.size() ? (uint8_t)jis[j] : 0);
    q.push_back(0);
  }
  q.push_back((uint8_t)(jis.size() / 2));
  append_u32(q, cur);
}

// The CSS toggle is left visible; room teams own both the outgoing selection and match.
// Also used on first connection, since a character may have been locked before matchmaking.
// Doubles keeps the toggle's value: there the room owns the pairs and team_id is the colour the player
// chose (doubles_teams::team_colors), which the Teams select starts on the room team's.
bool lock_room_team_selection() {
  if (!yougame::session() || g_last_search.mode != Matchmaking::TEAMS || !g_matchmaking) return false;
#ifdef MELEE_DOUBLES
  if (doubles_route()) return false;
#endif
  const int team = yougame::seat_team(g_matchmaking->LocalPlayerIndex());
  if (team < 0) return false;
  const bool changed = g_local_selections.team_id != team;
  g_local_selections.team_id = (uint8_t)team;
  return changed;
}

void set_match_selections(const uint8_t* payload) {
  PlayerSelections s;
  s.team_id = payload[0];
  s.character_id = payload[1];
  s.character_color = payload[2];
  s.is_character_selected = payload[3] != 0;
  s.stage_id = be16(payload + 4);
  uint8_t stage_option = payload[6];
  s.alt_stage_mode = payload[8];
  s.is_stage_selected = stage_option == 1 || stage_option == 3;
  if (stage_option == 3) s.stage_id = random_stage(payload[7]);
  s.rng_offset = g_rng() % 0xFFFF;
  const auto& local_peer = Matchmaking::local_peer;
  if (local_peer.enabled && local_peer.test_stage >= 0) {
    s.stage_id = static_cast<uint16_t>(local_peer.test_stage);
    s.is_stage_selected = true;
    host::log("slippi: local regression stage forced to %u", s.stage_id);
  }
#if MU_SQUAD
  // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
  // A friend room follows its own search (or the friend's pick), never the menu row.
  const bool friend_room = !g_last_search.friend_room.empty();
  if (friend_room && g_netplay && s.is_character_selected && g_netplay->SquadFriendWaiting()) {
    // Held before it counts as a lock-in, never sent plain: the friend's build may still turn this
    // room into Squad Strike. The character select says why (MU_SQUAD_ROOM_WAITING).
    host::log("squad: lock-in held, waiting for the friend's game");
    return;
  }
#endif
  g_local_selections.Merge(s);
  if (s.is_stage_selected) g_local_random_stage = stage_option == 3 && !(local_peer.enabled && local_peer.test_stage >= 0);
  lock_room_team_selection();
#if MU_SQUAD
  const bool squad = friend_room ? (g_last_search.squad || (g_netplay && g_netplay->SquadAdopted()))
                                 : yougame::squad::selected(payload[7], g_squad_row, false);
  if (g_netplay && squad) g_netplay->SetSquadSelection(g_squad_side);
#endif
  if (g_netplay) g_netplay->SetMatchSelections(g_local_selections);
}

void prepare_online_match_state(std::vector<uint8_t>& q);

// The same-build rule. 1: play, 0: wait for the opponent's build, -1: refuse (message in *why).
// Direct with a mod: every opponent must be on the same mod build (or, at the player's word, send
// no build: Slippi Dolphin with the same mod). Retail game: an opponent who says it is on a mod is
// refused, since their fighters and stages would not exist here. Other modes play the retail game on
// both sides; an opponent claiming a mod there is refused too. A YouGame Ranked/Unranked mod session instead
// requires the same mod with a full verified fingerprint, with no unverified-peer fallback.
int build_verdict(uint8_t remote_count, std::string* why) {
  if (!g_netplay) return 1;
  // Teams is played by code like Direct, so a mod build is checked on every remote player the same way.
  const bool direct = g_last_search.mode == Matchmaking::DIRECT || g_last_search.mode == Matchmaking::TEAMS;
  // A local mod session only: vanilla YouGame Ranked/Unranked keeps the checks below unchanged.
  // The room key and the mod side's own verdict keep a mod build out of a vanilla match.
  const bool yougame_mod = yougame::session() &&
      (g_last_search.mode == Matchmaking::RANKED || g_last_search.mode == Matchmaking::UNRANKED) && g_local_build.mod_view;
  bool waiting = false;
  // A Friends lobby match plays the room's frozen setup (CPUs, rules). A peer that can't build it
  // would start another match without knowing it: refused (mu_friends_lobby.h), never played.
  const auto lobby = yougame::friends_lobby();
  const bool lobby_match = lobby.open && lobby.plan && lobby.plan->ok;
  for (int i = 0; i < remote_count; ++i) {
    const auto rb = g_netplay->GetRemoteBuild(i);
    if (lobby_match) {
      if (lobby.plan->humans != lobby.plan->machines && rb.received && !mu_local_partner_capable(rb.caps2)) {
        *why = "A player's game can't play local partners. Everyone needs the latest Melee Unlocked.";
        return -1;
      }
      const uint64_t since = g_netplay->ConnectedAtMs();
      const uint64_t now = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count();
      const int verdict = mu_friends_lobby_verdict(rb.received, rb.setup_hash, rb.caps2,
                                                   since != 0 && now - since >= kBuildWaitMs);
      if (verdict < 0) { *why = MU_FRIENDS_LOBBY_OLD_BUILD_TEXT; return -1; }
      if (verdict == 0) { waiting = true; continue; }
    }
    if (yougame::session() && g_last_search.mode == Matchmaking::RANKED) {
      const uint64_t since = g_netplay->ConnectedAtMs();
      const uint64_t now = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count();
      const int verdict = yougame::ranked_rules::peer_verdict(ranked_rules_digest(0), rb.received,
          rb.ranked_rules_digest, since != 0 && now - since >= kBuildWaitMs);
      if (verdict < 0) { *why = "YouGame Ranked requires the same remix ranked rules"; return -1; }
      if (verdict == 0) waiting = true;
    }
    if (yougame_mod) {
      const uint64_t since = g_netplay->ConnectedAtMs();
      const uint64_t now = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count();
      const int verdict = native_yougame_online_build_verdict(g_local_build.mod_view, g_local_build.fingerprint,
          rb.received, rb.mod_view, rb.fingerprint, since != 0 && now - since >= kBuildWaitMs);
      if (verdict < 0) {
        *why = rb.received ? "YouGame Ranked/Unranked requires the same build: both vanilla or the same verified mod"
                           : "Could not verify your opponent's build. YouGame Ranked/Unranked requires matching verified builds";
        return -1;
      }
      if (verdict == 0) waiting = true;
      continue;
    }
    if (!rb.received) {
      if (g_local_build.mod_view && direct && !g_local_build.allow_unverified) waiting = true;
      continue;
    }
    if (!direct) {
      if (rb.mod_view) { *why = "Your opponent is on a mod build; this mode plays the retail game"; return -1; }
      continue;
    }
    if (g_local_build.mod_view) {
      if (!native_direct_builds_match(true, g_local_build.fingerprint, rb.mod_view, rb.fingerprint)) {
        const int port = i < g_netplay->LocalPlayerPort() ? i + 1 : i + 2;
        // An opponent on the standard game: say it is this player's own mod disc that differs, and
        // where it is switched off. A copy of Melee with one changed file left in Mods\Discs read as
        // "a different mod build" to a player whose ISOs were the same (0.9.203 report).
        // Short enough for the character select's four-line error box (the longer wording was cut off).
        if (!rb.mod_view)
          *why = "Your mods are on" + (g_local_build.name.empty() ? std::string("") : ": " + g_local_build.name) +
                 ". Player " + std::to_string(port) + " has none. Turn yours off in Mods, or use the same";
        else
          *why = "Player " + std::to_string(port) + " has different mods. Everyone needs the same ones";
        return -1;
      }
    } else if (rb.mod_view) {
      *why = "Opponent has mods on" + (rb.name.empty() ? std::string("") : ": " + rb.name) +
             ". Turn them off in Mods, or use the same";
      return -1;
    }
  }
  if (!waiting) return 1;
  if (yougame_mod) return 0;   // the verified-build policy above owns its timeout; never fall back
  if (lobby_match) return 0;   // mu_friends_lobby_verdict owns its timeout: a missing build is refused
  const uint64_t since = g_netplay->ConnectedAtMs();
  const uint64_t now = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  if (since == 0 || now - since < kBuildWaitMs) return 0;
  // No build arrived in the wait: the opponent is on Slippi Dolphin (or a version from before the
  // build message), which cannot say what it runs. This used to be refused unless the player had
  // found and ticked a switch for it, every session; players read the switch as the opponent's and
  // could not play at all. Then it started with the mod's files on this side only, and a report
  // desynced on the first thing the mod's files do differently (a Stadium transformation, six games
  // in). Slippi Dolphin plays the retail game unless the player says it has the same mod, so the
  // match starts on the retail game: the host switches this side's files before any are loaded for it.
  static uint64_t logged_for = 0;
  if (logged_for != since) {
    logged_for = since;
    host::log("slippi: the opponent sent no build (Slippi Dolphin); this Direct match plays the retail game without %s",
              g_local_build.name.c_str());
    if (g_plain_opponent_handler) g_plain_opponent_handler(true, "Opponent is on Slippi Dolphin: this match plays the standard game");
  }
  return 1;
}

// mu_setup_hash.h, inputs: what the setup is built from, in player order. Only what every machine
// holds the same once every message has arrived: the selections as sent (before the game's own
// overwrites; character fields only once picked, stage fields only once picked, as Merge keeps
// them), those overwrites, the room's teams and the Squad Strike lineups.
uint64_t setup_inputs_hash(const std::vector<PlayerSelections>& sent, const int* room_teams, bool room_teams_used,
                           const std::vector<uint8_t>& squad) {
  uint64_t h = mu_fnv64_bytes(MU_FNV64_OFFSET, "mu-setup-inputs-1", 17);
  h = mu_fnv64_u8(h, (unsigned)sent.size());
  for (const auto& s : sent) {
    h = mu_fnv64_u8(h, s.player_idx);
    h = mu_fnv64_u8(h, s.is_character_selected);
    if (s.is_character_selected) {
      h = mu_fnv64_u8(h, s.character_id);
      h = mu_fnv64_u8(h, s.character_color);
      h = mu_fnv64_u8(h, s.team_id);
    }
    h = mu_fnv64_u8(h, s.is_stage_selected);
    if (s.is_stage_selected) {
      h = mu_fnv64_u16(h, s.stage_id);
      h = mu_fnv64_u8(h, s.alt_stage_mode);
    }
    h = mu_fnv64_u32(h, s.rng_offset);
  }
  h = mu_fnv64_u8(h, (unsigned)g_overwrite_selections.size());
  for (const auto& o : g_overwrite_selections) {
    h = mu_fnv64_u8(h, o.player_idx);
    h = mu_fnv64_u8(h, o.character_id);
    h = mu_fnv64_u8(h, o.character_color);
    h = mu_fnv64_u16(h, o.stage_id);
  }
  h = mu_fnv64_u8(h, room_teams_used);
  if (room_teams_used)
    for (int i = 0; i < PLAYER_COUNT_MAX; ++i) h = mu_fnv64_u8(h, (unsigned)(room_teams[i] + 1));
  return mu_fnv64_bytes(h, squad.data(), squad.size());
}

// mu_setup_hash.h, setup: every byte that defines the match as this side will start it. The mode,
// the player count, each port's slot type, character, colour, costume index and team, the stage,
// the RNG offset, the stage variant, the whole game-info block (stocks, timer, rules and items
// included) and the Squad Strike setup bytes when Squad Strike is live (empty otherwise). With them,
// while Squad Strike is live, this side's budget identity (the 64-hex disc budget hash) and the
// online heap floors: each side admits and trims lineups against its own, so two sides that
// disagree would load different squads. They are setup, never inputs: the peer's identity never
// arrives in a message to wait for, so a difference is a mismatch that refuses the match. The block
// is kept between matches, so an empty port's record still holds whoever last played there: only
// its slot type defines this match, and the rest of that record is left out.
// A CPU's record (slot type 1: the Friends lobby's CPUs) is kept whole like a human's: its character,
// colour, team and level are this match's (mu_setup_record_hashed).
// A Friends lobby match folds in the frozen setup's own digest (`lobby`, 0 otherwise): two machines that
// hold different setups for one match refuse it.
uint64_t setup_block_hash(std::vector<uint8_t> block, unsigned players, uint32_t rng, uint16_t alt_stage,
                          const std::vector<uint8_t>& squad, const std::string& squad_budget, uint64_t lobby = 0) {
  for (int i = 0; i < PLAYER_COUNT_MAX; ++i) {
    const size_t record = 0x60 + (size_t)i * 0x24;
    if (record + 0x24 > block.size() || mu_setup_record_hashed(block[record + 1])) continue;   // 0x61: 0 human, 1 CPU
    const uint8_t type = block[record + 1];
    std::fill(block.begin() + record, block.begin() + record + 0x24, 0);
    block[record + 1] = type;
  }
  uint64_t h = mu_fnv64_bytes(MU_FNV64_OFFSET, "mu-setup-1", 10);
  h = mu_fnv64_u8(h, (unsigned)g_last_search.mode);
  h = mu_fnv64_u8(h, players);
  for (int i = 0; i < PLAYER_COUNT_MAX; ++i) {
    h = mu_fnv64_u8(h, (unsigned)i);
    for (int field : {0x61, 0x60, 0x63, 0x67, 0x69}) h = mu_fnv64_u8(h, block[field + i * 0x24]);
  }
  h = mu_fnv64_u16(h, (unsigned)((block[0xE] << 8) | block[0xF]));
  h = mu_fnv64_u32(h, rng);
  h = mu_fnv64_u16(h, alt_stage);
  h = mu_fnv64_bytes(h, block.data(), block.size());
  h = mu_fnv64_bytes(h, squad.data(), squad.size());
#if MU_SQUAD
  if (!squad.empty()) {
    h = mu_fnv64_bytes(h, squad_budget.data(), squad_budget.size());
    h = mu_fnv64_u32(h, MU_SQUAD_ONLINE_ALLM);
    h = mu_fnv64_u32(h, MU_SQUAD_ONLINE_ALLA);
  }
#else
  (void)squad_budget;
#endif
  if (lobby) { h = mu_fnv64_u32(h, (uint32_t)(lobby >> 32)); h = mu_fnv64_u32(h, (uint32_t)lobby); }
  return h;
}

// Waits for every peer's setup hash before the game is told the opponent is ready. 1: start,
// 0: wait, -1: refused (*why holds the player's text, empty for a plain disconnect). The only clock
// is the connect timeout, after which a peer whose hash never came counts as disconnected.
int setup_hash_gate(uint8_t remote_count, uint64_t inputs, uint64_t setup, std::string* why) {
  if (!g_netplay) return 1;
  // Tests only: this side builds a different setup, so a pair run shows the refusal end to end.
  if (const char* skew = host::test_env::getenv("MELEE_TEST_SETUP_HASH_SKEW"); skew && skew[0] == '1') setup ^= 1;
  if (!g_setup_hash.have || g_setup_hash.local.inputs != inputs || g_setup_hash.local.setup != setup) {
    g_setup_hash = {};
    g_setup_hash.have = true;
    g_setup_hash.local = {g_netplay->LocalPlayerPort(), inputs, setup};
    g_setup_hash.since_ms = time_ms();
    g_netplay->SendSetupHash(inputs, setup);
    host::log("setup hash %016llx local (inputs %016llx), sent", (unsigned long long)setup,
              (unsigned long long)inputs);
  }
  bool waiting = false, wait_over = false;
  const uint64_t now = time_ms();
  for (int i = 0; i < remote_count && i < REMOTE_PLAYER_MAX; ++i) {
    const auto build = g_netplay->GetRemoteBuild(i);
    const auto peer = g_netplay->GetSetupHash(i);
    const auto decision = mu_setup_hash_decide(build.setup_hash, peer.received, &g_setup_hash.local, &peer.value);
    // Noted on every call, agreed or not: a peer's newer hash restarts its wait from when it arrived
    // (mu_setup_hash_wait_over). A ranked seat one strike step ahead is not a lost peer.
    const bool over = mu_setup_hash_wait_over(&g_setup_hash.wait[i], g_setup_hash.since_ms, peer.received,
                                              &peer.value, now, kConnectTimeoutMs);
    if (g_setup_hash.logged[i] != (int)decision || g_setup_hash.logged_setup[i] != peer.value.setup) {
      g_setup_hash.logged[i] = (int)decision;
      g_setup_hash.logged_setup[i] = peer.value.setup;
      if (decision == MU_SETUP_HASH_SKIP)
        host::log("setup hash: remote[%d] has no setup hash; not checked (another pool)", i);
      else if (peer.received)
        host::log("setup hash %016llx local, %016llx remote[%d] (inputs %016llx local, %016llx remote): %s",
                  (unsigned long long)g_setup_hash.local.setup, (unsigned long long)peer.value.setup, i,
                  (unsigned long long)g_setup_hash.local.inputs, (unsigned long long)peer.value.inputs,
                  decision == MU_SETUP_HASH_AGREE ? "agree"
                  : decision == MU_SETUP_HASH_MISMATCH ? "MISMATCH, waiting for a newer hash before refusing"
                                                       : "other inputs, waiting");
      else host::log("setup hash %016llx local, waiting for remote[%d]", (unsigned long long)g_setup_hash.local.setup, i);
    }
    if (decision != MU_SETUP_HASH_MISMATCH) g_setup_hash.pending[i].open = 0;
    else if (mu_setup_hash_mismatch_final(&g_setup_hash.pending[i], &g_setup_hash.local, &peer.value, time_ms())) {
      host::log("setup hash: setup differs from remote[%d] for %d ms; match not started", i, MU_SETUP_HASH_SETTLE_MS);
      *why = MU_SETUP_HASH_TEXT;
      return -1;
    }
    if (decision == MU_SETUP_HASH_WAIT || decision == MU_SETUP_HASH_MISMATCH) {
      waiting = true;
      wait_over = wait_over || over;
    }
  }
  if (!waiting) {
    if (!g_setup_hash.agreed) host::log("setup hash %016llx: every peer that checks agrees", (unsigned long long)g_setup_hash.local.setup);
    g_setup_hash.agreed = true;
    return 1;
  }
  if (wait_over) {
    bool differs = false;
    for (int i = 0; i < remote_count && i < REMOTE_PLAYER_MAX; ++i) differs = differs || g_setup_hash.pending[i].open;
    if (differs) {
      host::log("setup hash: setup still differs after %llu ms; match not started", (unsigned long long)kConnectTimeoutMs);
      *why = MU_SETUP_HASH_TEXT;
      return -1;
    }
    host::log("setup hash: no matching hash after %llu ms; treated as a disconnect", (unsigned long long)kConnectTimeoutMs);
    why->clear();
    return -1;
  }
  return 0;
}

// Retrace of the last CMD_GET_MATCH_STATE. The game polls it every frame the online menus are up
// (mode select, the online character select, waiting for the opponent) and never during a match,
// so it is the signal for "the player is in the online menus right now".
uint32_t g_last_match_state_retrace = 0;
std::atomic<bool> g_online_menu_visible{false};

void prepare_online_match_state(std::vector<uint8_t>& q) {
  // The guest polls again on returning to CSS. End the match flag, retaining the peer and room.
  if (g_in_online_match && yougame::unranked::keep_css(yougame::active(), (int)g_last_search.mode)) {
    yougame::online_match_state(false);
    g_in_online_match = false;
    host::log("yougame: match over, back to the character screen");
  }
  g_last_match_state_retrace = host::retrace_count();
  host::set_emulation_speed(1.0);
  update_discord_presence();   // rate limited internally; the game polls this every frame
  static std::vector<uint8_t> block = {
      0x32, 0x01, 0x86, 0x4C, 0xC3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x6E, 0x00, 0x1F, 0x00, 0x00,
      0x01, 0xE0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
      0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x09, 0x00, 0x78, 0x00,
      0xC0, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80,
      0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x05, 0x00, 0x04, 0x01, 0x00, 0x01, 0x00, 0x00, 0x09, 0x00, 0x78, 0x00,
      0xC0, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80,
      0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x15, 0x03, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x09, 0x00, 0x78, 0x00,
      0xC0, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80,
      0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x15, 0x03, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x09, 0x00, 0x78, 0x00,
      0xC0, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80,
      0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x21, 0x03, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x09, 0x00, 0x78, 0x00,
      0x40, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80,
      0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x21, 0x03, 0x04, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x09, 0x00, 0x78, 0x00,
      0x40, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x3F, 0x80,
      0x00, 0x00, 0x3F, 0x80, 0x00, 0x00,
  };
  static const std::vector<uint8_t> shipped = block;   // as shipped, before any match wrote it
  q.clear();
  Matchmaking::ProcessState mm_state = !g_forced_error.empty() ? Matchmaking::ERROR_ENCOUNTERED : g_matchmaking->GetMatchmakeState();
  {
    // What the game reads as the match state, once per change: a set abandoned at the results screen
    // because this was not CONNECTION_SUCCESS (11-01, game 1 of a Ranked set) must say so.
    static int reported = -1;
    if ((int)mm_state != reported) {
      static const char* const names[] = {"idle", "initializing", "matchmaking", "opponent connecting", "connection success", "error"};
      const int n = (int)mm_state;
      std::string why;
      if (!g_forced_error.empty()) why = " (forced error: " + g_forced_error + ")";
      else if (mm_state == Matchmaking::ERROR_ENCOUNTERED) why = " (" + g_matchmaking->GetErrorMessage() + ")";
      host::log("slippi: match state %s -> %s%s", reported < 0 || reported > 5 ? "start" : names[reported],
                n >= 0 && n <= 5 ? names[n] : "unknown", why.c_str());
      reported = n;
    }
  }
#if MU_SQUAD
  // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
  if (g_netplay && g_netplay->SquadCancelled()) {
    // Read the reason before cleanup_connection() drops g_netplay.
    std::string reason = g_netplay->SquadCancelText();
    cleanup_connection();
    g_forced_error = std::move(reason);
    prepare_online_match_state(q);
    return;
  }
#endif
  q.push_back((uint8_t)mm_state);
  uint8_t local_ready = g_local_selections.is_character_selected ? 1 : 0;
  uint8_t remote_ready = 0;
  UserInfo me = g_user->GetUserInfo();
  uint16_t alt_stage_mode = 0;
  if (mm_state == Matchmaking::CONNECTION_SUCCESS) {
    g_local_player_index = (uint8_t)std::clamp(g_matchmaking->LocalPlayerIndex(), 0, PLAYER_COUNT_MAX - 1);
    if (lock_room_team_selection() && g_netplay) g_netplay->SetMatchSelections(g_local_selections);
    if (!g_netplay) {
      g_netplay = g_matchmaking->GetNetplayClient();
      g_recent_mm_result = g_matchmaking->GetMatchmakeResult();
      g_allowed_stages = g_recent_mm_result.stages;
      if (g_allowed_stages.empty()) g_allowed_stages = {0x2, 0x3, 0x8, 0x1C, 0x1F, 0x20};
      g_stage_pool.clear();
      g_local_selections.stage_id = random_stage(g_last_search.mode);
#if MU_SQUAD
      // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
      // A rejected lineup after search but before connection must invalidate first lock-in too.
      if (g_last_search.squad) g_netplay->SetSquadSelection(g_squad_side);
#endif
      g_netplay->SetMatchSelections(g_local_selections);
    }
    bool connected = g_netplay->GetSlippiConnectStatus() == NetplayClient::ConnectStatus::CONNECTED;
    if (g_netplay->GetActivePlayerIndices().size() != bounded_remote_port_count()) connected = false;
    if (!connected) {
      // The opponent may have refused first and left: a build that was already received still
      // explains why, instead of a plain disconnect.
      std::string refusal;
      if (build_verdict(bounded_remote_machine_count(), &refusal) < 0) {
        host::log("slippi: build check: %s", refusal.c_str());
        cleanup_connection();
        g_forced_error = refusal;
        prepare_online_match_state(q);
        return;
      }
    }
    if (connected) {
      const MatchInfo mi = g_netplay->GetMatchInfo();
      g_remote_player_index = mi.remote[0].player_idx;
      remote_ready = 1;
      uint8_t remote_count = bounded_remote_machine_count();
      for (int i = 0; i < remote_count; ++i) if (!mi.remote[i].is_character_selected) remote_ready = 0;
      std::string refusal;
      const int verdict = build_verdict(remote_count, &refusal);
      if (verdict < 0) {
        host::log("slippi: build check: %s", refusal.c_str());
        cleanup_connection();
        g_forced_error = refusal;
        prepare_online_match_state(q);
        return;
      }
      if (verdict == 0) remote_ready = 0;   // the opponent's build is not known yet
      if (yougame::session() && g_last_search.mode == Matchmaking::RANKED) {
        // GameSetup retains the pair until C2 AND the authoritative room result.
        // The legacy random-stage flow still releases at decision. In either flow
        // a pending game receipt or decided set prevents another gameplay start.
        if (yougame::ranked_set_over()) {
          host::log("slippi: the ranked set is over; back to the search");
          cleanup_connection(yougame::Leave::Over);
          prepare_online_match_state(q);
          return;
        }
        if (yougame::ranked_hold()) remote_ready = 0;
      }
      if (local_ready && remote_ready && !yougame::session() && !Matchmaking::local_peer.enabled) {
        std::string version_error;
        const int version = g_user->PollOnlineCompatibility(&version_error);
        if (version < 0) {
          cleanup_connection();
          g_forced_error = version_error;
          prepare_online_match_state(q);
          return;
        }
        if (version == 0) remote_ready = 0;  // fresh verification, including an existing connection's rematch
      }
      if (remote_count == 1 && !yougame::friends_lobby().open) {
        bool decider = g_netplay->IsDecider();
        g_local_player_index = decider ? 0 : 1;
        g_remote_player_index = decider ? 1 : 0;
      }
    } else {
#if MU_SQUAD
      // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
      // Locked in and not yet playing: the link dropped before the first frame. Say so instead of
      // a silent return to the search, and play nothing (no match starts, so no result is sent).
      const bool squad_cancelled = g_last_search.squad && g_local_selections.is_character_selected && !g_in_online_match;
#endif
      // YouGame Unranked: the opponent left (or their game or connection went) mid-game or on the
      // character select between games. Read before the cleanup forgets the session and its room.
      if (yougame::session() || g_last_search.mode == Matchmaking::UNRANKED)
        host::log("yougame: unranked: link gone (session %d, mode %d, friend room %d, connected %d, own drop %d)",
                  yougame::session() ? 1 : 0, (int)g_last_search.mode, g_last_search.friend_room.empty() ? 0 : 1,
                  g_play_session_active ? 1 : 0, yougame::session() && yougame::own_room_dropped() ? 1 : 0);
      const bool opponent_left = yougame::unranked::opponent_left(
          yougame::session(), (int)g_last_search.mode, !g_last_search.friend_room.empty(),
          g_play_session_active, yougame::session() && yougame::own_room_dropped());
      cleanup_connection();
      if (opponent_left) {
        g_unranked_opponent_left = true;
        host::log("yougame: unranked: the opponent left; the character select says so until the next search");
      }
#if MU_SQUAD
      if (squad_cancelled) {
        host::log("squad: connection lost before the first frame, match cancelled");
        g_forced_error = "Match cancelled: connection lost.";
      }
#endif
      prepare_online_match_state(q);
      return;
    }
    if (!g_play_session_active) g_play_session_active = true;
  } else {
    stop_early_inputs();
    std::lock_guard<std::mutex> lk(g_early_send_mutex);
    g_netplay = nullptr;
#if MU_SQUAD
    g_squad_costume_match = false;
    g_squad_match_standards.clear();
#endif
  }
  uint32_t rng_offset = 0;
  std::string local_name, opp_name;
  uint8_t chat_message_id = 0, chat_message_player_idx = 0, sent_chat_message_id = 0;
  q.push_back(local_ready);
  q.push_back(remote_ready);
  q.push_back(g_local_player_index);
  q.push_back(g_remote_player_index);
  if (g_netplay) {
    bool single = g_matchmaking && g_matchmaking->RemotePlayerCount() == 1;
    bool chat = chat_enabled();
    sent_chat_message_id = g_netplay->GetSlippiRemoteSentChatMessage(chat);
    if (sent_chat_message_id <= 0) {
      PlayerSelections rm = g_netplay->GetSlippiRemoteChatMessage(chat);
      chat_message_id = (uint8_t)rm.message_id;
      chat_message_player_idx = rm.player_idx;
      if (chat_message_id == CHAT_MSG_CHAT_DISABLED && !single) chat_message_id = chat_message_player_idx = 0;
    } else {
      chat_message_player_idx = g_local_player_index;
    }
    if (single || !g_matchmaking) chat_message_player_idx = sent_chat_message_id > 0 ? g_local_player_index : g_remote_player_index;
    local_name = yougame::session() && yougame::launcher() ? yougame::identity().name : me.display_name;
  }
  const bool setup_built = local_ready && remote_ready;
  // The Friends lobby's frozen setup (yougame_lobby_setup.h): when there is one, it defines the match.
  const yougame::FriendsLobby lobby = yougame::friends_lobby();
  const yougame::lobby::Plan* lobby_plan = lobby.open && lobby.plan && lobby.plan->ok ? lobby.plan.get() : nullptr;
  uint64_t setup_lobby = 0;
  uint64_t setup_inputs = 0;
  std::vector<uint8_t> setup_squad;   // the Squad Strike setup bytes, empty when it is not live
  std::string setup_budget;           // with them, this side's squad budget identity (setup hash only)
  std::array<int, PLAYER_COUNT_MAX> lobby_colors;   // a Doubles lobby match: each machine's colour, by port
  lobby_colors.fill(-1);
  if (local_ready && remote_ready) {
    bool decider = g_netplay->IsDecider();
    uint8_t remote_count = bounded_remote_machine_count();
    const MatchInfo mi = g_netplay->GetMatchInfo();
    PlayerSelections lps = mi.local;
    PlayerSelections rps[REMOTE_PLAYER_MAX];
    for (int i = 0; i < REMOTE_PLAYER_MAX; ++i) rps[i] = mi.remote[i];
    bool local_char_ok = lps.character_id < 26, remote_char_ok = true;
    for (int i = 0; i < remote_count; ++i) if (rps[i].character_id >= 26) remote_char_ok = false;
    std::vector<PlayerSelections> frozen_selections;
    // A Doubles lobby match: each machine's team colour, by its own port (lobby::doubles_colors), from the
    // selections as they arrived (the frozen picks below replace them in `ordered`).
    if (lobby_plan) {
      if (lps.player_idx < PLAYER_COUNT_MAX) lobby_colors[lps.player_idx] = lps.team_id;
      for (int i = 0; i < remote_count; ++i) if (rps[i].player_idx < PLAYER_COUNT_MAX) lobby_colors[rps[i].player_idx] = rps[i].team_id;
    }
    std::vector<PlayerSelections*> ordered(lobby_plan ? lobby_plan->humans : remote_count + 1, nullptr);
    if (lps.player_idx < ordered.size()) ordered[lps.player_idx] = &lps;
    for (int i = 0; i < remote_count; ++i) if (rps[i].player_idx < ordered.size()) ordered[rps[i].player_idx] = &rps[i];
    if (lobby_plan) {
      // Selection messages remain one per machine. The frozen setup supplies every human port,
      // including local partners; readiness above still waits for each real connection's lock-in.
      frozen_selections.resize(lobby_plan->humans);
      for (int port = 0; port < lobby_plan->humans; ++port) {
        auto& pick = frozen_selections[port];
        pick.player_idx = uint8_t(port); pick.character_id = uint8_t(lobby_plan->port[port].pick.ch);
        pick.character_color = uint8_t(lobby_plan->port[port].pick.color);
        pick.team_id = uint8_t(lobby_plan->port[port].pick.team);
        pick.is_character_selected = pick.is_stage_selected = true;
        pick.stage_id = uint16_t(lobby_plan->stage); pick.rng_offset = lobby_plan->seed;
        ordered[port] = &pick;
      }
    }
    // A port with no selections is no match: refused, never filled with this player's own pick (that
    // played a fighter nobody picked, and in the Friends lobby a seat that is not this player's).
    if (std::find(ordered.begin(), ordered.end(), nullptr) != ordered.end()) {
      host::log("slippi: a port's selections are missing (%u remote); match not started", (unsigned)remote_count);
      cleanup_connection();
      g_forced_error = "A player's picks didn't arrive. Match not started.";
      prepare_online_match_state(q);
      return;
    }
    std::vector<PlayerSelections> sent_selections;   // as sent, before the game's overwrites (setup hash)
    for (auto* o : ordered) sent_selections.push_back(*o);
    for (size_t i = 0; i < g_overwrite_selections.size() && i < ordered.size(); ++i) {
      ordered[i]->character_id = g_overwrite_selections[i].character_id;
      ordered[i]->character_color = g_overwrite_selections[i].character_color;
      ordered[i]->stage_id = g_overwrite_selections[i].stage_id;
    }
    uint16_t stage_id = 0x1F;
    for (auto* s : ordered) { if (!s->is_stage_selected) continue; stage_id = s->stage_id; alt_stage_mode = s->alt_stage_mode; break; }
    if (Matchmaking::local_peer.enabled && Matchmaking::local_peer.test_stage >= 0) {
      // Force the final host-built setup packet too: each test peer may have sent its menu choice
      // before the other peer finished connecting, so the earlier local selection override alone
      // is not sufficient to make a deterministic local regression match.
      stage_id = static_cast<uint16_t>(Matchmaking::local_peer.test_stage);
      host::log("slippi: local regression match stage %u", stage_id);
    }
#ifdef MELEE_DOUBLES
    // Doubles v1 has no stage select: every seat locks in random, and the match's seed (its start's, else
    // the room's) picks a legal doubles stage (no Fountain of Dreams), the same on all four.
    const bool doubles = doubles_route();
    yougame::DoublesRoom doubles_room;
    if (doubles) {
      doubles_room = yougame::doubles_room();
      stage_id = yougame::doubles_teams::stage(doubles_room.seed, g_doubles_games);
      alt_stage_mode = 0;
    }
#endif
    // A selection can arrive marked "stage picked" with stage 0, which is no stage (B-528: game 3 of a set; the stage
    // load found no music for it and stopped at ground.c). Both games then take the same legal stage, chosen by the
    // rng offset both sides already agree on, instead of starting a match on nothing.
    if (stage_id == 0) {
      const uint32_t agreed = decider ? lps.rng_offset : rps[0].rng_offset;
      stage_id = stage_pool::agreed_stage(agreed, remote_count > 1);
      host::log("slippi: the selections named no stage (0); both games take stage %u", (unsigned)stage_id);
    }
    if (Matchmaking::IsFixedRulesMode(g_last_search.mode)) {
      const bool extended = yougame_online_mod_content((int)g_last_search.mode);
      if (use_ranked_rules(g_last_search.mode)) {
        const bool rules_local_ok = g_ranked_rules.character_allowed(lps.character_id);
        bool rules_remote_ok = true;
        for (int i = 0; i < remote_count; ++i)
          if (!g_ranked_rules.character_allowed(rps[i].character_id)) rules_remote_ok = false;
        if (!rules_local_ok || !rules_remote_ok) {
          cleanup_connection(rules_local_ok ? yougame::Leave::Lost : yougame::Leave::Quit);
          g_forced_error = "A selected character is not allowed by this remix's ranked rules";
          prepare_online_match_state(q); return;
        }
      }
      // This player's own pick is refused: in a ranked set that is leaving it (Quit), not a claim.
      if (!extended && !local_char_ok) { cleanup_connection(yougame::Leave::Quit); g_forced_error = "The character you selected is not allowed in this mode"; prepare_online_match_state(q); return; }
      if (!extended && !remote_char_ok) { cleanup_connection(); prepare_online_match_state(q); return; }
      const auto& stages = allowed_stages(g_last_search.mode);
      if ((!extended || g_last_search.mode == Matchmaking::RANKED) &&
          std::find(stages.begin(), stages.end(), stage_id) == stages.end()) { cleanup_connection(); prepare_online_match_state(q); return; }
    } else if (g_last_search.mode == Matchmaking::TEAMS && !(g_local_build.mod_view && g_local_build.extended_content)) {
      if (!local_char_ok) { cleanup_connection(); g_forced_error = "The character you selected is not allowed in this mode"; prepare_online_match_state(q); return; }
      if (!remote_char_ok) { cleanup_connection(); prepare_online_match_state(q); return; }
    } else if (g_last_search.mode == Matchmaking::DIRECT || g_last_search.mode == Matchmaking::TEAMS) {
      // build_verdict already confirmed the peer's content. Only Static mod boot advertises that
      // it runs the disc's extended fighter/stage code; Source and retail keep vanilla limits.
      bool foreign = false;
      if (lobby_plan) {
        // The Friends lobby (always DIRECT, yougame_lobby_setup.h kSearchMode): its picks and stage are
        // the frozen setup's, applied below; the selections only said everyone is in.
        for (int p = 0; p < lobby_plan->ports && p < (int)lobby_plan->port.size(); ++p)
          if (!native_direct_selection_supported(g_local_build.mod_view, g_local_build.extended_content,
                                                 (uint8_t)lobby_plan->port[p].pick.ch, (uint16_t)lobby_plan->stage)) foreign = true;
      } else {
        foreign = !native_direct_selection_supported(g_local_build.mod_view, g_local_build.extended_content,
                                                     lps.character_id, stage_id);
        for (int i = 0; i < remote_count; ++i)
          if (!native_direct_selection_supported(g_local_build.mod_view, g_local_build.extended_content,
                                                 rps[i].character_id, stage_id)) foreign = true;
      }
      if (foreign) {
        cleanup_connection();
        g_forced_error = g_local_build.mod_view
            ? "Your opponent picked a character or stage whose code this build does not have"
            : "Your opponent is on a different build (their character or stage is not in this game)";
        host::log("slippi: build check: %s", g_forced_error.c_str());
        prepare_online_match_state(q);
        return;
      }
    }
    rng_offset = decider ? lps.rng_offset : rps[0].rng_offset;
    uint8_t first_team = ordered[0]->team_id;
    bool all_same_team = true;
    for (auto* s : ordered) if (s->team_id != first_team) all_same_team = false;
    static const uint8_t perms[6][4] = {{0, 0, 1, 1}, {1, 1, 0, 0}, {0, 1, 1, 0}, {1, 0, 0, 1}, {0, 1, 0, 1}, {1, 0, 1, 0}};
    const uint8_t* team_assign = perms[rng_offset % 6];
    bool teams = g_last_search.mode == Matchmaking::TEAMS;
    std::array<int, PLAYER_COUNT_MAX> room_teams;
    room_teams.fill(-1);
    std::array<int, 2> team_color{{0, 1}};   // Doubles: the colour each room team wears (team_colors)
    bool room_teams_known = teams && yougame::session();
#ifdef MELEE_DOUBLES
    if (doubles) {
      // The room owns the teams (its start's, else its seats'). A room that named none, a peer whose 0x82
      // says otherwise, or a roster whose seats sit in other slots is not this room's match: refuse to
      // start, on screen.
      std::array<int, 4> room_team{{-1, -1, -1, -1}}, sent{{-1, -1, -1, -1}};
      unsigned present = 0;
      for (auto* s : ordered) {
        if (!s->is_character_selected || s->player_idx >= 4) continue;
        present |= 1u << s->player_idx;
        room_team[s->player_idx] = doubles_room.team[s->player_idx];
        sent[s->player_idx] = s->team_id;
      }
      const char* refusal = !doubles_room.open ? yougame::doubles_teams::kTeamMismatch
          : doubles_room.refusal ? doubles_room.refusal
          : !doubles_room.consistent ? yougame::doubles_teams::kTeamMismatch
          : yougame::doubles_teams::team_conflict(room_team, sent, present);
      if (refusal) {
        if (doubles_room.refusal) host::log("doubles: the room named no teams; not starting");
        else host::log("doubles: teams disagree with the room (sent %d %d %d %d); not starting", sent[0], sent[1], sent[2], sent[3]);
        cleanup_connection();
        g_forced_error = refusal;
        prepare_online_match_state(q); return;
      }
      for (int i = 0; i < 4; ++i) room_teams[i] = room_team[i];
      room_teams_known = true;
      team_color = yougame::doubles_teams::team_colors(room_team, sent, present);
    } else
#endif
    if (room_teams_known) for (auto* s : ordered) {
      if (!s->is_character_selected) continue;
      const int team = yougame::seat_team(s->player_idx);
      room_teams[s->player_idx] = team;
      if (team < 0) room_teams_known = false;
    }
#if MU_SQUAD
    // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
    {
      MuSquadSetup squad{};
      unsigned char bytes[MU_SQUAD_WIRE_MAX];
      if ((lobby_plan && yougame::lobby::squad_setup(*lobby_plan, &squad)) || (!lobby_plan && g_netplay->SquadSetup(&squad))) {
        const size_t size = mu_squad_encode(&squad, bytes, sizeof bytes);
        setup_squad.assign(bytes, bytes + size);
        setup_budget = lobby_plan ? lobby_plan->squad_budget : g_squad_budget;
      }
    }
#endif
    setup_inputs = setup_inputs_hash(sent_selections, room_teams.data(), room_teams_known, setup_squad);
    // The block is kept between matches, and a Friends lobby setup writes bytes no other match does
    // (P1/P2 slot types, CPU kind and level, the rules bit): back to the shipped block's first. They
    // are hashed, so two machines with different pasts would otherwise refuse the match.
    for (int i = 0; i < 4; ++i) {
      if (i < 2) block[0x61 + i * 0x24] = shipped[0x61 + i * 0x24];
      block[0x6E + i * 0x24] = shipped[0x6E + i * 0x24];
      block[0x6F + i * 0x24] = shipped[0x6F + i * 0x24];
    }
    block[1] = (uint8_t)((block[1] & ~0x01) | (shipped[1] & 0x01));
    for (auto* s : ordered) {
      if (!s->is_character_selected) continue;
      uint8_t team = teams ? s->team_id : 0;
      if (room_teams_known) team = (uint8_t)room_teams[s->player_idx];
      else if (teams && all_same_team) team = team_assign[s->player_idx];
#ifdef MELEE_DOUBLES
      if (doubles) team = (uint8_t)team_color[room_teams[s->player_idx]];   // checked 0 or 1 above
#endif
      block[0x60 + s->player_idx * 0x24] = s->character_id;
      block[0x63 + s->player_idx * 0x24] = s->character_color;
      block[0x67 + s->player_idx * 0x24] = 0;
      block[0x69 + s->player_idx * 0x24] = team;
    }
    // P3/P4's slot types, then the costume indices over this match's human ports (mu_setup_hash.h).
    mu_setup_ports(block.data(), block.size(), uint8_t(ordered.size() - 1), teams ? 1 : 0);
    block[0x8] = teams ? 1 : 0;
    block[0xE] = (uint8_t)(stage_id >> 8); block[0xF] = (uint8_t)stage_id;
    // Direct pauses (Slippi's rule) unless a player turned it off and every build reads that (PauseOff).
    const bool pause_off = g_last_search.mode == Matchmaking::DIRECT && g_netplay && g_netplay->PauseOff();
    {
      static int logged = -1;
      if (g_last_search.mode == Matchmaking::DIRECT && logged != (pause_off ? 1 : 0)) {
        logged = pause_off ? 1 : 0;
        host::log("slippi: Direct pause %s", pause_off ? "off (a player turned it off)" : "on");
      }
    }
    bool pause_allowed = g_last_search.mode == Matchmaking::DIRECT && !pause_off;
    block[2] = pause_allowed ? (block[2] & 0xF7) : (block[2] | 0x8);
    bool stage_selection_mode = g_last_search.mode == Matchmaking::DIRECT || g_last_search.mode == Matchmaking::TEAMS;
    if (!stage_selection_mode) alt_stage_mode = 0;
    // Every game starts at 4 stocks and 0%; the timer is written below.
    for (int i = 0; i < 4; ++i) {
      block[0x62 + i * 0x24] = 4;
      block[0x70 + i * 0x24] = 0; block[0x71 + i * 0x24] = 0;
    }
    // One line per distinct match setup (this runs every frame both sides are ready), so multi-
    // instance test logs show the teams, characters and stage every peer agreed on.
    {
      char setup[160];
      int len = std::snprintf(setup, sizeof setup, "teams %d stage %u rng %08X:", teams ? 1 : 0, (unsigned)stage_id, rng_offset);
      for (int i = 0; i < PLAYER_COUNT_MAX && len > 0 && len < (int)sizeof setup; ++i)
        if (block[0x61 + i * 0x24] == 0)
          len += std::snprintf(setup + len, sizeof setup - len, " P%d char %u color %u team %u", i + 1, block[0x60 + i * 0x24],
                               block[0x63 + i * 0x24], block[0x69 + i * 0x24]);
      static std::string last_setup;
      if (last_setup != setup) { last_setup = setup; host::log("slippi: match setup %s", setup); }
      for (int i = 0; i < 4; ++i) g_game_facts.teams[i] = teams ? block[0x69 + i * 0x24] : -1;
#ifdef MELEE_DOUBLES
      // The result names the room's teams, not the colours they wore (room_team_outcome compares them).
      if (doubles) for (int i = 0; i < 4; ++i) g_game_facts.teams[i] = room_teams[i];
#endif
      g_game_facts.stage = stage_id;
      g_game_facts.rng = rng_offset;
      g_game_facts.characters[0] = block[0x60];
      g_game_facts.characters[1] = block[0x60 + 0x24];
    }
  }
  {
    block[0x0] = 0x32; block[0x3] = 0x4C;
    uint32_t t = 8 * 60;
    // Tests only: MELEE_ONLINE_TEST_TIMER=<seconds> ends a match quickly so the
    // result reporting can be exercised.
    if (const char* test_timer = std::getenv("MELEE_ONLINE_TEST_TIMER")) t = (uint32_t)std::atoi(test_timer);
    block[0x10] = (uint8_t)(t >> 24); block[0x11] = (uint8_t)(t >> 16); block[0x12] = (uint8_t)(t >> 8); block[0x13] = (uint8_t)t;
  }
  if (setup_built && lobby_plan) {
    // The Friends lobby: every port's record, the CPUs, stocks or time, teams, the stage and the seed
    // come from the room's frozen setup, the same on every machine. The selections above only said
    // that every player is in.
    const int remote_count = bounded_remote_machine_count();
    if (lobby_plan->machines != remote_count + 1 || lobby_plan->local_port != g_local_player_index) {
      host::log("slippi: Friends lobby setup has %d machines (local port %d), session %d players (local %u); match not started",
                lobby_plan->machines, lobby_plan->local_port, remote_count + 1, (unsigned)g_local_player_index);
      cleanup_connection();
      g_forced_error = yougame::lobby::kSetupUnusable;
      prepare_online_match_state(q);
      return;
    }
    uint32_t timer = 0;
    if (const char* test_timer = std::getenv("MELEE_ONLINE_TEST_TIMER")) timer = (uint32_t)std::atoi(test_timer);
    // Doubles: the room's pairs wear the colours their players chose (lobby::doubles_colors). Hashed with
    // the block: a machine that mapped other colours refuses the match, never plays a different one.
    const auto team_colors = yougame::lobby::doubles_colors(*lobby_plan, lobby_colors);
    yougame::lobby::apply(*lobby_plan, block.data(), block.size(), timer, g_lobby_costumes.data(), &team_colors);
    rng_offset = lobby_plan->seed;
    alt_stage_mode = 0;
    setup_inputs = yougame::lobby::inputs_hash(*lobby_plan);
    setup_lobby = yougame::lobby::setup_digest(*lobby_plan);
    {
      static std::string last;
      char line[192];
      int len = std::snprintf(line, sizeof line, "match %s stage %d seed %08X friendly fire %s:", lobby_plan->match_id.c_str(),
                              lobby_plan->stage, lobby_plan->seed, lobby_plan->rules.friendly_fire ? "on" : "off");
      for (int i = 0; i < lobby_plan->ports && len > 0 && len < (int)sizeof line; ++i)
        len += std::snprintf(line + len, sizeof line - len, " P%d %s char %u team %u", i + 1,
                             block[0x61 + i * 0x24] == 1 ? "cpu" : "human", block[0x60 + i * 0x24], block[0x69 + i * 0x24]);
      if (last != line) { last = line; host::log("slippi: Friends lobby setup %s", line); }
    }
    for (int i = 0; i < 4; ++i) g_game_facts.teams[i] = lobby_plan->rules.teams ? block[0x69 + i * 0x24] : -1;
    // Doubles: the room's teams, not the colours they wore (as the 4-peer route keeps them).
    if (lobby_plan->doubles)
      for (int i = 0; i < 4; ++i) g_game_facts.teams[i] = i < lobby_plan->ports ? lobby_plan->port[i].pick.team : -1;
    g_game_facts.stage = (uint16_t)lobby_plan->stage;
    g_game_facts.rng = rng_offset;
    g_game_facts.characters[0] = block[0x60];
    g_game_facts.characters[1] = block[0x60 + 0x24];
  }
  block[0xB] = 0xFF;
  uint64_t items = 0xF80000000F000000ull;
  if (g_recent_mm_result.items != 0) { items |= (uint64_t)g_recent_mm_result.items << 28; block[0xB] = 3; }
  for (int i = 0; i < 8; ++i) block[0x23 + i] = (uint8_t)(items >> (56 - 8 * i));
  // The block is final here. Both sides ready: hold "opponent ready" until every peer that sends a
  // setup hash has sent the same one; a different one refuses the match, before any frame runs.
  if (setup_built) {
    std::string why;
    const int gate = setup_hash_gate(bounded_remote_machine_count(), setup_inputs,
                                     setup_block_hash(block, lobby_plan ? lobby_plan->humans : bounded_remote_port_count() + 1u, rng_offset, alt_stage_mode,
                                                      setup_squad, setup_budget, setup_lobby), &why);
    if (gate < 0) {
      g_netplay->ForceDisconnect();
      cleanup_connection();
      g_forced_error = why;
      prepare_online_match_state(q);
      return;
    }
    if (gate == 0) q[2] = 0;   // MSRB_REMOTE_READY: the game keeps waiting for the opponent
  } else if (g_setup_hash.have) {
    g_setup_hash = {};   // a new setup (a rematch, a changed pick) starts a new wait
  }
  append_u32(q, rng_offset);
  q.push_back((uint8_t)effective_delay());
  q.push_back(sent_chat_message_id);
  q.push_back(chat_message_id);
  q.push_back(chat_message_player_idx);
  std::array<uint8_t, 2> ranks{};
  if (yougame::session() && g_last_search.mode == Matchmaking::RANKED) {
    ranks.fill(0xFF); // Slippi's hidden-rank sentinel when the roster has no standing
    if (g_local_player_index < 2) ranks[1 - g_local_player_index] = yougame::opponent_rank();
    if (g_local_player_index < 2 && yougame::rank_visibility()) ranks[g_local_player_index] = yougame::local_rank();
  }
  q.push_back(ranks[0]);
  q.push_back(ranks[1]);
  std::string ln = convert_string_for_game(local_name, 15);
  q.insert(q.end(), ln.begin(), ln.end());
  for (int i = 0; i < 4; ++i) { std::string n = convert_string_for_game(g_matchmaking->GetPlayerName((uint8_t)i), 15); q.insert(q.end(), n.begin(), n.end()); }
  std::vector<std::string> opponent_names;
  auto players = g_matchmaking->GetPlayerInfo();
  int team_idx = block[0x69 + g_local_player_index * 0x24];
  for (int i = 0; i < 4; ++i) {
    bool teams = g_last_search.mode == Matchmaking::TEAMS;
    bool same_team = block[0x69 + i * 0x24] == team_idx;
    bool human = block[0x61 + i * 0x24] == 0;
    if (g_local_player_index == i || !human || (same_team && teams)) continue;
    std::string n = g_matchmaking->GetPlayerName((uint8_t)i);
    if (!n.empty()) opponent_names.push_back(n);
  }
  if (mm_state == Matchmaking::OPPONENT_CONNECTING) {
    // Before selections arrive, the ticket owns the local port; the game block and
    // g_local_player_index may still describe the previous match.
    opponent_names.clear();
    const int local = g_matchmaking->LocalPlayerIndex();
    for (const auto& p : players)
      if (p.port - 1 != local && !p.display_name.empty()) opponent_names.push_back(p.display_name);
  }
  size_t num_opp = opponent_names.empty() ? 1 : opponent_names.size();
  int chars_per_name = (int)((15 - (num_opp - 1)) / num_opp);
  std::string opp_text;
  for (auto& n : opponent_names) { if (!opp_text.empty()) opp_text += "/"; opp_text += truncate_length_char(n, chars_per_name); }
  // MSRB_OPP_NAME is only the CSS summary. Keep the four player names/codes above/below
  // raw: the native HUD and replay metadata consume those identity fields.
  // Unranked's Both under the launcher searched two pools: the summary names the one that answered.
  const bool both_pools = yougame::menu_service() == 2 && !yougame::active() && g_last_search.mode == Matchmaking::UNRANKED;
  opp_name = convert_string_for_game(display::opponent_label(g_matchmaking->DisplayProvider(), opp_text, 15, both_pools), 15);
  if (both_pools && !opp_text.empty()) {   // once per label: which pool a Both search's opponent came from
    static std::string logged;
    const std::string shown = display::opponent_label(g_matchmaking->DisplayProvider(), opp_text, 15, both_pools);
    if (logged != shown) { logged = shown; host::log("online: Both search, opponent shown as \"%s\"", shown.c_str()); }
  }
  q.insert(q.end(), opp_name.begin(), opp_name.end());
  std::array<UserInfo, PLAYER_COUNT_MAX> identities;
  for (const auto& player : players) if (player.port >= 1 && player.port <= PLAYER_COUNT_MAX) identities[player.port - 1] = player;
  if (lobby_plan) for (int port = 0; port < lobby_plan->ports; ++port) identities[port].uid = lobby_plan->ids[port];
  for (const auto& player : identities) { std::string c = convert_connect_code_for_game(player.connect_code); q.insert(q.end(), c.begin(), c.end()); }
  for (const auto& player : identities) { std::string uid = player.uid; uid.resize(29); q.insert(q.end(), uid.begin(), uid.end()); }
  std::string err = convert_string_for_game(!g_forced_error.empty() ? g_forced_error : g_matchmaking->GetErrorMessage(), 120);
  q.insert(q.end(), err.begin(), err.end());
  q.insert(q.end(), block.begin(), block.end());
  std::string match_id = g_recent_mm_result.id;
  match_id.resize(51);
  q.insert(q.end(), match_id.begin(), match_id.end());
  q.push_back((uint8_t)alt_stage_mode);
  // Slippi 3.7.0: what decides each player's rank, and their ranked MMR, by port, then whether each
  // is a bot account. The game records them in the replay.
  for (int i = 0; i < 4; ++i) {
    const UserInfo info = i < (int)players.size() ? players[i] : UserInfo();
    uint32_t rating, mmr;
    std::memcpy(&rating, &info.ranked_rating, 4); std::memcpy(&mmr, &info.ranked_mmr, 4);
    append_u32(q, rating);
    append_u32(q, (uint32_t)info.ranked_update_count);
    q.push_back((uint8_t)((uint16_t)info.ranked_global_placement >> 8)); q.push_back((uint8_t)info.ranked_global_placement);
    q.push_back((uint8_t)((uint16_t)info.ranked_regional_placement >> 8)); q.push_back((uint8_t)info.ranked_regional_placement);
    append_u32(q, mmr);
  }
  for (int i = 0; i < 4; ++i) q.push_back((uint8_t)(i < (int)players.size() && players[i].is_bot));
}

void prepare_online_status(std::vector<uint8_t>& q) {
  q.clear();
  // At boot the menus ask before the app has answered who is signed in: wait for that first answer
  // (a moment), so the name and connect code are there the first time the online menu draws.
  if (yougame::active()) yougame::wait_identity(4000);
  if (yougame::active()) announce_slippi_link(g_user->dir());   // once, off this thread; remixes send nothing
  g_user->AttemptLogin();
  UserInfo me = g_user->GetUserInfo();
  uint8_t app_state = 0;
  if (g_user->IsLoggedIn()) app_state = 1;   // version check: this port speaks SLIPPI_SEMVER; the server enforces the rest
  q.push_back(app_state);
  // A YouGame copy shows the YouGame name, as the match does (local_name above): it showed the Slippi
  // account's name on the character select (player report, 0.9.992).
  const std::string yg_name = yougame::session() && yougame::launcher() ? yougame::identity().name : std::string();
  std::string name = convert_string_for_game(yg_name.empty() ? me.display_name : yg_name, 15);
  q.insert(q.end(), name.begin(), name.end());
  std::string code = convert_connect_code_for_game(me.connect_code);
  q.insert(q.end(), code.begin(), code.end());
}

static void finish_summary(const char* result) {
  if (!g_summary.active) return;
  yougame::online_match_state(false);
  g_summary.active = false;
  host::log("%s", g_summary.line(g_rollbacks - g_match_rollbacks_start, g_match_stalls,
                               g_checksums_mismatched, result).c_str());
  yg_enet_mirror_active(0);
}

void handle_report_game(const uint8_t* p) {
  // ReportGameQuery (packed, big-endian fields): mode, frameLength, gameIndex, tiebreakIndex, winnerIdx, gameEndMethod, lrasInitiator, syncedTimer, players[4], gameInfoBlock[312]
  uint8_t mode = p[0];
  uint32_t frames = be32(p + 1), game_index = be32(p + 5), tiebreak = be32(p + 9);
  int8_t winner = (int8_t)p[13]; uint8_t end_method = p[14]; int8_t lras = (int8_t)p[15];
  g_summary.frames = frames;
  finish_summary(end_method == 7 ? "no-contest" : end_method == 1 ? "timeout" : lras >= 0 ? "lras" : "completed");
  const uint8_t* players = p + 20;
  const uint8_t* info_block = players + 4 * 9;
  int stage = be16(info_block + 0xE);
  host::log("slippi: game report: mode %u, %u frames, game %u, tiebreak %u, winner %d, end %u, lras %d, stage %d",
            mode, frames, game_index, tiebreak, winner, end_method, lras, stage);
  host::log("slippi: online game summary: delay %d frames, average ping %.1f ms, %llu rollbacks, %llu stalled frames, %llu waits for the other player, %llu frames advanced, %u frames",
            g_match_delay, g_netplay ? g_netplay->GetAndResetAvgPingMs() : 0.0,
            (unsigned long long)(g_rollbacks - g_match_rollbacks_start), (unsigned long long)g_match_stalls,
            (unsigned long long)g_match_input_waits, (unsigned long long)g_match_advances, frames);
  {
    const host::early_input::Stats e = host::early_input::stats();
    host::log("slippi: early inputs %s: %llu changes, %llu packets, %llu guarded, %llu resent after a skip, %llu follow-ups, %llu received, %llu controller reads, local controller port %d",
              early_inputs_enabled() ? (early_inputs_accept() ? "on" : "sending only") : "off",
              (unsigned long long)e.changes, (unsigned long long)e.packets, (unsigned long long)e.guarded,
              (unsigned long long)e.resent_after_skip, (unsigned long long)e.follow_ups, (unsigned long long)e.received, (unsigned long long)e.samples, e.local_port + 1);
  }
  {
    const suggestions::Stats s = suggestions::stats();
    host::log("slippi: input suggestions %s: %llu sent, %llu received, %llu replies predicted from one, %d players ignored",
              s.enabled ? "on" : "off", (unsigned long long)host::early_input::stats().suggestions,
              (unsigned long long)s.received, (unsigned long long)s.predicted_frames, s.ignored_players);
  }
  stop_early_inputs();
  g_latched_delay = 0;   // the next match picks its delay from its own ping
  trace_end_match();
  // Source publishes its Game End record in h_replay_event, except a duel's TIME!, which this
  // report decides (yougame_ranked.h time_out_waits_for_report). A disconnect or desync in the
  // frames between that TIME! and this report turns it into a No Contest (end 7), so Source
  // publishes 7 here too. Ranked also defers a No Contest record until this report distinguishes
  // a connected desync (winner -2, replay) from a disconnect (winner -3, forfeit).
  // Static YouGame Teams uses that record in exi_slippi too: ReportGameQuery has no placements and
  // must not void it first.
  if ((!host::game_image || end_method == 1 || end_method == 7) &&
      !(yougame::session() && g_last_search.mode == Matchmaking::TEAMS))
    host::publish_lobby_result(winner, end_method, lras);
  {
    // Exactly CEXISlippi::handleReportGame: one report per game with every slot's result.
    UserInfo me = g_user->GetUserInfo();
    report::GameReport r;
    r.uid = me.uid; r.play_key = me.play_key; r.match_id = g_recent_mm_result.id; r.replay_path = slippi::last_replay_path();
    r.online_mode = mode; r.duration_frames = frames; r.game_index = game_index; r.tiebreak_index = tiebreak;
    r.winner_index = winner; r.game_end_method = end_method; r.lras_initiator = lras; r.stage_id = stage;
    for (int i = 0; i < 4; ++i) {
      report::PlayerReport pr;
      for (const auto& player : g_recent_mm_result.players) if (player.port == i + 1) pr.uid = player.uid;
      const auto lobby = yougame::friends_lobby();
      if (lobby.open && lobby.plan && lobby.plan->ok && i < lobby.plan->ports) pr.uid = lobby.plan->ids[i];
      pr.slot_type = players[i * 9]; pr.stocks_remaining = players[i * 9 + 1];
      uint32_t dmg = be32(players + i * 9 + 2); float dmg_f; std::memcpy(&dmg_f, &dmg, 4); pr.damage_done = dmg_f;
      pr.character_id = info_block[0x60 + 0x24 * i]; pr.color_id = info_block[0x63 + 0x24 * i];
      pr.starting_stocks = info_block[0x62 + 0x24 * i]; pr.starting_percent = be16(info_block + 0x70 + 0x24 * i);
      r.players.push_back(pr);
    }
    // A local two-instance test peering never went through Slippi's matchmaking, so there is no
    // server-side match: reporting it would file a fake game under the signed-in account.
    if (Matchmaking::local_peer.enabled) host::log("slippi: local test peering, game report not sent");
    else if (!yougame::session()) report::log_game(r);   // YouGame records results through its room
  }
#ifdef YOUGAME_AGENT_TEST_HOOK
  yougame::agent_test::static_match.event(yougame::agent_test::StaticEvent::Report,
      !host::game_image && yougame::session() && !slippi::playback::enabled(), [] {
        host::log("yougame: test quit after game %d", yougame::agent_test::static_match.finished());
        yougame::agent_test::static_quit_requested.store(true);
        native_cleanup_match();
      });
#endif
}

void handle_get_player_settings(std::vector<uint8_t>& q) {
  q.clear();
  std::vector<std::vector<std::string>> by_player(4);
  auto mine = g_user->GetUserChatMessages();
  if (mine.size() == 16) by_player[0] = mine;
  for (auto& p : g_matchmaking->GetPlayerInfo()) if (p.port >= 1 && p.port <= 4) by_player[p.port - 1] = p.chat_messages;
  for (int i = 0; i < 4; ++i) {
    if (by_player[i].size() != 16) by_player[i] = User::GetDefaultChatMessages();
    for (int j = 0; j < 16; ++j) {
      std::string s = convert_string_for_game(by_player[i][j], 25);
      s.resize(51);
      q.insert(q.end(), s.begin(), s.end());
    }
  }
}

}  // namespace

Config& config() { return g_config; }
void set_plain_opponent_handler(void (*handler)(bool plain, const char* why)) { g_plain_opponent_handler = handler; }

void set_local_build(const LocalBuild& build) {
  if (build.mod_view != g_local_build.mod_view || build.fingerprint != g_local_build.fingerprint ||
      build.allow_unverified != g_local_build.allow_unverified || build.extended_content != g_local_build.extended_content ||
      build.static_engine != g_local_build.static_engine)
    host::log("slippi: local build: %s%s%s", build.mod_view ? "mod " : "retail game",
              build.mod_view ? build.name.c_str() : "", build.allow_unverified ? " (Slippi Dolphin opponents allowed)" : "");
  g_local_build = build;
}
const LocalBuild& local_build() { return g_local_build; }
static std::atomic<bool> g_boot_online{false};
void request_boot_online() { g_boot_online = true; }
void load_ranked_rules() {
  if (g_ranked_rules_loaded) return;
  g_ranked_rules_loaded = true;
  if (g_config.ranked_rules_path.empty()) return;
  try {
    std::ifstream file(std::filesystem::u8path(g_config.ranked_rules_path), std::ios::binary);
    std::string text(yougame::ranked_rules::kMaxBytes + 1, '\0');
    if (file) {
      file.read(text.data(), (std::streamsize)text.size());
      text.resize((size_t)file.gcount());
      g_ranked_rules_valid = !file.bad() && yougame::ranked_rules::parse(text,
          g_local_build.mod_view && g_local_build.extended_content, g_ranked_rules);
    }
  } catch (const std::exception&) { g_ranked_rules_valid = false; }
  if (!g_ranked_rules_valid) { host::log("ranked rules: invalid; ranked unavailable"); return; }
  host::log("ranked rules: %zu stages, characters %s, digest %.8s", g_ranked_rules.stages.size(),
      g_ranked_rules.all_characters ? "all" : std::to_string(g_ranked_rules.characters.size()).c_str(),
      g_ranked_rules.digest.c_str());
}
std::string ranked_rules_digest(int mode) {
  // session(): a launcher YouGame session sends the same digest as an app one (upstream: session()).
  if (!yougame::session() || mode != 0) return {};
  static const auto base = yougame::ranked_rules::vanilla();
  return yougame::ranked_rules::branch_digest(use_ranked_rules(mode) ? g_ranked_rules.digest : base.digest);
}
void set_native_gameplay_profile(NativeGameplayProfile profile) {
  if (session_mode() >= 0) {
    host::log("slippi: cannot change gameplay profile during an online session");
    return;
  }
  g_native_gameplay_profile = profile;
}
uint64_t rollback_count() { return g_rollbacks; }
void note_rollback(int32_t to_frame) { ++g_rollbacks; trace_rollback(to_frame); }
bool early_inputs_enabled() { return host::early_input::enabled(); }
// MELEE_TEST_NO_EARLY_PADS=1 (tests only): this side does not take this build's early pads, as a Slippi
// Dolphin player does not, so the other side sends it Slippi input suggestions instead.
bool early_inputs_accept() {
  static const bool no_early_pads = [] { const char* v = host::test_env::getenv("MELEE_TEST_NO_EARLY_PADS"); return v && *v == '1'; }();
  return host::early_input::enabled() && host::game_image != nullptr && !no_early_pads;
}
// The project's own peer-to-peer session (app/source_p2p.cpp) publishes its state in
// host::netplay::session(). Everything in the host that asks "is a network match running" asks the
// functions below, so when Slippi has no session of its own they answer from that state: offline-only
// features (the player's Gecko codes, 20XX options, skin changes, automatic L-cancel) then stay off
// in a peer-to-peer match exactly as in a Slippi one. With a Slippi session, Slippi's answer stands.
static bool slippi_session_up() {
  return g_in_online_match || g_play_session_active || (g_matchmaking && g_matchmaking->IsSearching());
}
static bool p2p_session_up() { return !slippi_session_up() && host::netplay::session_mode() >= 0; }
#if MU_SQUAD
// Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
bool squad_costume_rule(const std::string& path, SquadCostume* out, bool console) {
  // An online squad match's copies; otherwise Friends' synced select's, while its session runs.
  if (!g_squad_costume_match) {
    if (!console) return false;
    squad_console_forget();
    return g_squad_console.rule(path, &out->budget, &out->standard);
  }
  const auto standard = g_squad_match_standards.find(path);
  if (standard == g_squad_match_standards.end()) return false;
  out->budget = g_squad_costumes.budget(path);
  out->standard = standard->second;
  return out->budget.bytes != 0;
}
void squad_console_reset() { g_squad_console_reset.store(true, std::memory_order_release); }
bool squad_costume_first_fallback(const std::string& path) { return g_squad_fallback_logged.insert(path).second; }
void squad_costume_refuse(const std::string& path, bool mod) {
  host::log("squad: %s %s is over its budget at load, match cancelled", mod ? "the mod's" : "the disc's own", path.c_str());
  g_squad_costume_match = false;
  g_squad_console.disarm();
  if (g_netplay) g_netplay->RefuseSquad(squad_costume_refusal(mod), "squad: costume over its budget, match cancelled");
}
void set_squad_standard_copy(squad_costume::Standard (*copy)(const std::string& path)) { g_squad_standard = copy; }
#endif
// Gecko codes for the match (gecko_session.h), every character select frame: the exchange starts when
// a connection to one opponent is up and its build message came (or did not, within the wait), and
// its messages move both ways; when the codes are agreed, this side's selection goes again with the
// new lock.
void gecko_pump() {
  static const NetplayClient* started = nullptr;
  const bool up = g_netplay && g_netplay->GetSlippiConnectStatus() == NetplayClient::ConnectStatus::CONNECTED;
  if (!up) {
    if (started) { gecko_session::disconnected(); started = nullptr; }
    return;
  }
  if (started != g_netplay.get()) {
    const auto rb = g_netplay->GetRemoteBuild(0);
    const uint64_t since = g_netplay->ConnectedAtMs();
    const uint64_t now = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (!rb.received && (since == 0 || now - since < kBuildWaitMs)) return;   // its build message may still come
    const bool direct = g_last_search.mode == Matchmaking::DIRECT;
    gecko_session::connected(direct && rb.received && rb.gecko && g_netplay->RemoteCount() == 1);
    started = g_netplay.get();
  }
  std::vector<uint8_t> message;
  while (g_netplay->PopGecko(message)) gecko_session::receive(message.data(), message.size());
  for (const auto& out : gecko_session::take_outgoing()) g_netplay->SendGecko(out);
  if (gecko_session::take_resend()) g_netplay->ResendSelections();
}
bool is_online_match() { return g_in_online_match || (p2p_session_up() && host::netplay::is_online_match()); }
int local_player_slot() { return p2p_session_up() ? host::netplay::local_player_slot() : (int)g_local_player_index; }
std::array<std::string, 4> player_names_for_overlay() {
  if (p2p_session_up()) return host::netplay::player_names();
  std::array<std::string, 4> names{};
  if (!g_in_online_match || !g_matchmaking) return names;
  for (int i = 0; i < 4; ++i) names[i] = g_matchmaking->GetPlayerName((uint8_t)i);
  // HUD nicknames stay plain; provider labels belong to the CSS summary only.
  if (g_user && g_local_player_index < 4 && names[g_local_player_index].empty())
    names[g_local_player_index] = g_user->GetUserInfo().display_name;
  return names;
}
std::vector<int> remote_pings_ms() {
  std::vector<int> out;
  if (p2p_session_up() || !g_netplay) return out;
  for (int i = 0; i < g_netplay->RemoteCount(); ++i) out.push_back(g_netplay->RemotePingMs(i));
  return out;
}
int ping_ms() { return p2p_session_up() ? host::netplay::ping_ms() : (g_netplay ? g_netplay->LastPingMs() : 0); }

// g_last_search keeps the mode of the last search for the whole session, so it only means anything
// while an online session is actually up: searching, set up (the online character select screen),
// or in the match. Everywhere else this is offline and the answer is -1.
// Port colours (Game > Online, owner 11-22 from a player's request): in an online 1v1 each player shows in
// their first choice; a colour both picked goes to one of them by the match's agreed seed and the other gets
// their second choice; a player with no choice keeps their own port's colour. Both games know both choices
// (the build message), so both draw the same. Display only (mu_port_colour in the game).
uint32_t port_colour_bits() {
  // Whole online session, not only once the match runs: the tags and damage displays take their colour as
  // the match scene builds them, before is_online_match() turns true.
  if (!g_netplay || session_mode() < 0 || bounded_remote_machine_count() != 1) return 0;
  int8_t mine[2], theirs[2];
  if (!g_netplay->PortColours(mine, theirs) || (mine[0] < 0 && theirs[0] < 0)) return 0;
  const int me = local_player_index();
  if (me < 0 || me > 1) return 0;
  const int8_t* pref[2] = {me == 0 ? mine : theirs, me == 0 ? theirs : mine};
  int colour[2] = {-1, -1};
  bool taken[4] = {};
  for (int p = 0; p < 2; ++p)
    if (pref[p][0] < 0) { colour[p] = p; taken[p] = true; }
  const int first = (int)((g_game_facts.rng >> 3) & 1u);
  for (int k = 0; k < 2; ++k) {
    const int p = k == 0 ? first : 1 - first;
    if (colour[p] >= 0) continue;
    for (const int c : {(int)pref[p][0], (int)pref[p][1], p})
      if (c >= 0 && c < 4 && !taken[c]) { colour[p] = c; break; }
    for (int c = 0; colour[p] < 0 && c < 4; ++c) if (!taken[c]) colour[p] = c;
    taken[colour[p]] = true;
  }
  static uint32_t logged = 0xFFFFFFFFu;
  const uint32_t bits = ((uint32_t)colour[0] | ((uint32_t)colour[1] << 2)) << 28;
  if (bits != logged) { logged = bits; host::log("slippi: port colours P1 %d, P2 %d (0 red, 1 blue, 2 yellow, 3 green)", colour[0], colour[1]); }
  return bits;
}

bool opponent_auto_lcancel() {
  if (!g_netplay || g_last_search.mode != Matchmaking::DIRECT) return false;
  if (g_netplay->GetSlippiConnectStatus() != NetplayClient::ConnectStatus::CONNECTED) return false;
  const int n = std::min<int>(bounded_remote_machine_count(), REMOTE_PLAYER_MAX);
  for (int i = 0; i < n; ++i) if (g_netplay->GetRemoteBuild(i).auto_lcancel) return true;
  return false;
}

int session_mode() {
  if (g_warmup_parked) return -1;   // warming up in Training: offline, as far as the game is concerned
  if (g_search_away) return -1;     // a kept search: the player plays offline meanwhile
  const bool session = g_in_online_match || g_play_session_active || (g_matchmaking && g_matchmaking->IsSearching());
  if (!session) return host::netplay::session_mode();   // a peer-to-peer session, or -1
  return (int)g_last_search.mode;
}
int session_mode_or_parked() { return g_warmup_parked || g_search_away ? (int)g_last_search.mode : session_mode(); }

// ---------------------------------------------------------------- kept search
KeptSearch kept_search() {
  KeptSearch out;
  out.away = g_search_away;
  if (!g_matchmaking) return out;
  out.state = !g_forced_error.empty() ? (int)Matchmaking::ERROR_ENCOUNTERED : (int)g_matchmaking->GetMatchmakeState();
  out.mode = (int)g_last_search.mode;
#if MU_SQUAD
  out.squad = g_last_search.squad;
#endif
  out.friend_room = !g_last_search.friend_room.empty();
  out.lobby = g_friends_lobby.armed || g_last_search.friends_lobby;
  out.locked = g_local_selections.is_character_selected;
  out.started_ms = g_search_started_ms;
  out.now_ms = time_ms();
  if (out.state == (int)Matchmaking::ERROR_ENCOUNTERED)
    out.error = !g_forced_error.empty() ? g_forced_error : g_matchmaking->GetErrorMessage();
  return out;
}

void search_away(bool away) {
  if (g_search_away == away) return;
  g_search_away = away;
  host::log("slippi: kept search: %s", away ? "the player left the select; the search goes on" : "back toward the select");
}

// ---------------------------------------------------------------- warm-up room
static bool warmup_link_complete() {
  if (!g_netplay || !g_matchmaking) return false;
  if (g_matchmaking->GetMatchmakeState() != Matchmaking::CONNECTION_SUCCESS) return false;
  if (g_netplay->GetSlippiConnectStatus() != NetplayClient::ConnectStatus::CONNECTED) return false;
  return g_netplay->GetActivePlayerIndices().size() == bounded_remote_port_count();
}

bool warmup_possible(std::string* why) {
  const auto no = [&](const char* reason) { if (why) *why = reason; return false; };
  if (g_warmup_parked) return no("Already warming up");
  if (!g_user || !g_matchmaking || !g_netplay) return no("Not connected");
  if (g_last_search.mode != Matchmaking::DIRECT && g_last_search.mode != Matchmaking::TEAMS)
    return no("Direct and Teams only");
  if (yougame::session()) return no("Not between games of a set");
  if (!warmup_link_complete()) return no("Not connected");
  if (!in_online_menus()) return no("From the character select");
  // A lock-in is final on the wire (Slippi's selections only ever add one), so a player who has
  // locked in stays: the opponent would otherwise start a match against a player in Training.
  if (g_local_selections.is_character_selected) return no("Locked in");
  return true;
}

bool warmup_park() {
  std::string why;
  if (!warmup_possible(&why)) {
    host::log("slippi: warm-up refused: %s", why.c_str());
    return false;
  }
  stop_early_inputs();
  // The flag stays set on the character select after a game until the pair is cleaned up; Training
  // must not look like a match running.
  g_in_online_match = false;
  g_warmup_parked = true;
  g_netplay->SendWarmup(true, (uint8_t)(g_warmup_pick >> 8), (uint8_t)g_warmup_pick);
  host::log("slippi: warm-up: the %s connection stays up while the player is in Training",
            g_last_search.mode == Matchmaking::TEAMS ? "Teams" : "Direct");
  return true;
}

void warmup_unpark() {
  if (!g_warmup_parked) return;
  g_warmup_parked = false;
  if (g_netplay) g_netplay->SendWarmup(false, (uint8_t)(g_warmup_pick >> 8), (uint8_t)g_warmup_pick);
  host::log("slippi: warm-up over: back to the character select%s",
            warmup_link_complete() ? "" : " (the connection was lost meanwhile)");
}

bool warmup_parked() { return g_warmup_parked; }

void warmup_note_pick(int character, int costume) {
  g_warmup_pick = character >= 0 && character < 26 ? (uint16_t)(character << 8 | (costume & 0xFF)) : (uint16_t)0xFF00;
  if (!g_netplay || g_warmup_parked || !warmup_link_complete()) return;
  if (g_warmup_pick_link != g_netplay.get()) { g_warmup_pick_link = g_netplay.get(); g_warmup_pick_sent = 0xFFFF; }
  if (g_warmup_pick == g_warmup_pick_sent) return;
  g_warmup_pick_sent = g_warmup_pick;
  g_netplay->SendWarmup(false, (uint8_t)(g_warmup_pick >> 8), (uint8_t)g_warmup_pick);
}

WarmupLink warmup_link() {
  WarmupLink out;
  out.parked = g_warmup_parked;
  if (!g_netplay || !g_matchmaking) return out;
  out.mode = (int)g_last_search.mode;
  out.link_up = warmup_link_complete();
  const int n = std::min<int>(bounded_remote_port_count(), REMOTE_PLAYER_MAX);
  out.peers = n;
  const MatchInfo mi = g_netplay->GetMatchInfo();
  // Remote i is the i-th port other than this player's, in port order (PlayerIdxFromPort).
  int remote = 0;
  for (int port = 0; port < 4 && remote < n; ++port) {
    if (port == (int)g_local_player_index) continue;
    WarmupPeer& p = out.peer[remote];
    p.name = g_matchmaking->GetPlayerName((uint8_t)port);
    p.ready = mi.remote[remote].is_character_selected;
    if (p.ready && mi.remote[remote].character_id < 26) {
      p.character = mi.remote[remote].character_id;
      p.costume = mi.remote[remote].character_color;
    }
    p.warmup = g_netplay->RemoteWarmup(remote);
    const int pick = g_netplay->RemotePick(remote);
    if (pick >= 0) p.picked = pick >> 8;
    ++remote;
  }
  return out;
}
uint32_t remote_shield_color(int slot) { return g_netplay ? g_netplay->RemoteShieldColor(slot) : 0u; }
int local_player_index() { return p2p_session_up() ? host::netplay::local_player_slot() : (int)g_local_player_index; }
bool online_status_visible() {
  return !is_online_match() && (g_online_menu_visible.load() || in_online_menus());
}
bool in_online_menus() {
  if (p2p_session_up()) return host::netplay::in_online_menus();
  const uint32_t now = host::retrace_count();
  return g_last_match_state_retrace && now - g_last_match_state_retrace < 10;
}

bool native_start_match(int mode, const std::string& connect_code, uint8_t character,
                        uint8_t color, std::string* error, const std::string& friend_room) {
  if (!g_user) init();
  if (session_mode() >= 0 || g_warmup_parked || g_search_away) {
    if (error) *error = g_warmup_parked ? "Warming up: go back to your match first"
                      : g_search_away ? "Already searching: hold L and R to go back to it" : "An online session is already active";
    return false;
  }
  if (const char* reason = local_matchmaking_error(mode)) {
    if (error) *error = reason;
    return false;
  }

  // Same payloads the guest sends from the stock Slippi online menus. Native practice chooses a
  // random legal stage and carries the practice character/costume into the normal online flow.
  uint8_t selections[9] = {0, character, color, 1, 0, 0, 3, (uint8_t)mode, 0};
  // Friends joins the room immediately, but the player still picks and locks in a fighter on CSS.
  // session(): a friend room joined over the local channel waits on CSS as in the app (active() there).
  selections[3] = yougame::friends::native_ready(yougame::session(), mode, connect_code, friend_room) ? 1 : 0;
  set_match_selections(selections);
  uint8_t find[19] = {};
  find[0] = (uint8_t)mode;
  const std::string sjis = utf8_to_shiftjis(connect_code);
  std::memcpy(find + 1, sjis.data(), std::min<size_t>(18, sjis.size()));
  start_find_match(find, friend_room);
  if (!g_forced_error.empty()) {
    if (error) *error = g_forced_error;
    return false;
  }
  if (error) error->clear();
  return g_matchmaking && g_matchmaking->IsSearching();
}

// The Doubles row's press (CMD_YOUGAME_FRIENDS_MENU 16), taken once by native_practice's tick.
std::atomic<bool> g_doubles_lobby_request{false};
bool take_doubles_lobby_request() { return g_doubles_lobby_request.exchange(false); }

void arm_friends_lobby(const std::string& room) { arm_friends_lobby(room, yougame::lobby::Profile::Friends4); }
void arm_friends_lobby(const std::string& room, yougame::lobby::Profile profile) {
  auto& lobby = g_friends_lobby;
  if (lobby.invite_stop) lobby.invite_stop->store(true);
  lobby = FriendsLobby{};
  ++g_lobby_arm;
  lobby.armed = true;
  lobby.guest = !room.empty();
  lobby.room = room;
  lobby.profile = profile;
  lobby.control = std::make_shared<yougame::lobby::Control>(profile);
  const bool doubles = profile == yougame::lobby::Profile::Doubles;
  host::log("%s: %s", doubles ? "doubles" : "friends lobby",
            lobby.guest ? (doubles ? "joining a friend's Doubles home" : "joining a friend's lobby")
                        : (doubles ? "opening a Doubles home" : "opening a lobby"));
  if (!start_lobby_search() && !g_forced_error.empty()) {
    host::log("friends lobby: the search can't start (%s)", g_forced_error.c_str());
    lobby.closed = true;
  }
}

bool friends_lobby_armed() { return g_friends_lobby.armed; }
void disarm_friends_lobby() {
  auto& lobby = g_friends_lobby;
  if (lobby.invite_stop) lobby.invite_stop->store(true);
  if (lobby.armed) host::log("friends lobby: disarmed (another match starts)");
  lobby = FriendsLobby{};
  ++g_lobby_arm;
}
bool squad_friend_intent() {
#if MU_SQUAD
  return g_squad_friend_intent;
#else
  return false;
#endif
}

void squad_friend_room(const std::string& room, bool picked) {
#if MU_SQUAD
  // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
  if (picked && g_squad_friend_intent) {
    g_squad_friend_room = room;
    host::log("squad: play a friend, the picker returned a room");
  } else {
    if (g_squad_friend_intent) host::log("squad: another friend room was joined, Squad Strike off");
    g_squad_friend_room.clear();
  }
  g_squad_friend_intent = false;
#else
  (void)room; (void)picked;
#endif
}

void squad_friend_picker_closed() {
#if MU_SQUAD
  // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
  if (g_squad_friend_intent) host::log("squad: friend picker closed without a room, Squad Strike off");
  g_squad_friend_intent = false;
  g_squad_friend_room.clear();
#endif
}

NativeMatchPoll native_poll_match() {
  NativeMatchPoll out;
  if (!g_user) init();
  std::vector<uint8_t> response;
  prepare_online_match_state(response);  // owns lifecycle work; exactly one coordinator call/tick
  if (!response.empty()) out.process_state = response[0];
  out.connection_success = out.process_state == (int)Matchmaking::CONNECTION_SUCCESS;
  if (response.size() >= 3) {
    out.local_ready = response[1] != 0;
    out.remote_ready = response[2] != 0;
  }
  if (!g_forced_error.empty()) out.error = g_forced_error;
  else if (out.process_state == (int)Matchmaking::ERROR_ENCOUNTERED && g_matchmaking)
    out.error = g_matchmaking->GetErrorMessage();
  if (g_matchmaking) {
    for (int i = 0; i < 4 && out.opponent.empty(); ++i)
      if (i != (int)g_local_player_index) out.opponent = g_matchmaking->GetPlayerName((uint8_t)i);
  }
  return out;
}

void native_cleanup_match() {
  if (!g_user) return;
  cleanup_connection(yougame::Leave::Quit);
}

GameFacts last_game_facts() { return g_game_facts; }
unsigned disconnected_ports() { return g_gone_ports; }

bool native_ranked_next_game(uint8_t character, uint8_t color) {
  if (!g_user || !g_matchmaking || !g_netplay || g_last_search.mode != Matchmaking::RANKED) return false;
  if (g_matchmaking->GetMatchmakeState() != Matchmaking::CONNECTION_SUCCESS || g_local_selections.is_character_selected) return false;
  uint8_t selections[9] = {0, character, color, 1, 0, 0, 3, (uint8_t)Matchmaking::RANKED, 0};   // a random legal stage
  set_match_selections(selections);
  return true;
}

ArenaMatchStatus arena_match_status() {
  ArenaMatchStatus out;
  out.active = slippi_session_up();
  // A search that failed is no longer "up", but its client still has to be cleaned up before the
  // next one (FindMatch on it again would end the program).
  out.state = g_matchmaking ? (int)g_matchmaking->GetMatchmakeState() : 0;
  out.direct = g_last_search.mode == Matchmaking::DIRECT;
  out.mode = (int)g_last_search.mode;
  out.code = shiftjis_to_utf8(g_last_search.connect_code);
  out.state = g_matchmaking ? (int)g_matchmaking->GetMatchmakeState() : 0;
  // The flag stays set on the character select after a game until the pair is cleaned up: a match is
  // running only while the game is not polling the online menus.
  out.in_match = g_in_online_match && !in_online_menus();
  out.local_ready = g_local_selections.is_character_selected;
  return out;
}

bool arena_start_search(int mode, const std::string& code, bool locked, uint8_t character, uint8_t color,
                        uint8_t team, std::string* error) {
  if (mode != Matchmaking::DIRECT && mode != Matchmaking::TEAMS) return false;
  if (!g_user) init();
  if (session_mode() >= 0 || g_warmup_parked) {
    if (error) *error = g_warmup_parked ? "Warming up: go back to your match first" : "An online session is already active";
    return false;
  }
  if (const char* reason = local_matchmaking_error(mode)) {
    if (error) *error = reason;
    return false;
  }
  // A client that has searched before (and failed or ended) is replaced first, as the online
  // character select does before every search.
  if (!g_matchmaking || g_matchmaking->GetMatchmakeState() != Matchmaking::IDLE) cleanup_connection(yougame::Leave::Quit);
  g_forced_error.clear();
  uint8_t selections[9] = {team, character, color, (uint8_t)(locked ? 1 : 0), 0, 0, 3, (uint8_t)mode, 0};
  set_match_selections(selections);
  uint8_t find[19] = {};
  find[0] = (uint8_t)mode;
  const std::string sjis = utf8_to_shiftjis(code);
  std::memcpy(find + 1, sjis.data(), std::min<size_t>(18, sjis.size()));
  start_find_match(find);
  if (!g_forced_error.empty()) {
    if (error) *error = g_forced_error;
    return false;
  }
  return g_matchmaking && g_matchmaking->IsSearching();
}

bool arena_start_yg_search(int mode, const std::vector<std::string>& fighters, const std::string& code, bool locked,
                           uint8_t character, uint8_t color, uint8_t team, std::string* error) {
  if (fighters.size() != 2 && fighters.size() != 4) return false;
  if (!yougame::session()) {
    if (error) *error = "The arena needs a YouGame sign-in";
    return false;
  }
  g_arena_next = {fighters, utf8_to_shiftjis(code)};
  const bool ok = arena_start_search(mode, code, locked, character, color, team, error);
  g_arena_next = ArenaNext{};   // refused before the search read it: never carried to the player's next search
  return ok;
}

bool local_identity(std::string* code, std::string* name) {
  if (!g_user) init();
  if (!g_user || !g_user->IsLoggedIn()) return false;
  const UserInfo info = g_user->GetUserInfo();
  if (code) *code = info.connect_code;
  if (name) *name = info.display_name.empty() ? info.connect_code : info.display_name;
  return !info.connect_code.empty();
}

std::unique_ptr<Matchmaking> arena_matchmaking() {
  if (!g_user) init();
  // Room links search on their own (not through start_find_match): they need this game's own hold on
  // Windows networking (the spectator server lets go of its own when it cannot open its port).
  if (!enet_ready()) return nullptr;
  return g_user ? std::make_unique<Matchmaking>(g_user.get()) : nullptr;
}

bool arena_pop_from_match(std::vector<uint8_t>& out) { return g_netplay && g_netplay->PopArena(out); }

bool arena_lock_in(uint8_t character, uint8_t color, bool random_stage) {
  if (!g_user || !g_matchmaking || !g_netplay) return false;
  if (g_last_search.mode != Matchmaking::DIRECT && g_last_search.mode != Matchmaking::TEAMS) return false;
  if (g_matchmaking->GetMatchmakeState() != Matchmaking::CONNECTION_SUCCESS || g_local_selections.is_character_selected) return false;
  uint8_t selections[9] = {g_local_selections.team_id, character, color, 1, 0, 0, (uint8_t)(random_stage ? 3 : 0),
                           (uint8_t)g_last_search.mode, 0};   // a random legal stage, or none yet
  set_match_selections(selections);
  return true;
}

static std::atomic<uint32_t> g_arena_pairing{0};
void arena_note_new_pairing() { g_arena_pairing.fetch_add(1, std::memory_order_relaxed); }
uint32_t arena_pairing() { return g_arena_pairing.load(std::memory_order_relaxed); }

bool local_css_selection(uint8_t* character, uint8_t* color) {
  if (!g_local_selections.is_character_selected || g_local_selections.character_id >= 26) return false;
  if (character) *character = g_local_selections.character_id;
  if (color) *color = g_local_selections.character_color;
  return true;
}

std::string direct_code_for_practice() {
  if (g_last_search.mode == Matchmaking::DIRECT && !g_last_search.connect_code.empty())
    return shiftjis_to_utf8(g_last_search.connect_code);
  return g_direct_codes && g_direct_codes->length() > 0 ? g_direct_codes->get(0) : std::string();
}

void init() {
  // Authentication uses shared read-only discovery in User. Mutable code history and reports
  // always stay in the configured local profile, including after a Log In rediscovery. In YouGame
  // mode the identity comes from the app (User::AttemptLogin) and Slippi's reporter never starts:
  // nothing may reach a Slippi host.
  if (!yougame::active()) report::init(host::options.iso, g_config.user_dir);
  g_user = std::make_unique<User>(g_config.user_dir);
  g_matchmaking = std::make_unique<Matchmaking>(g_user.get());
  std::error_code ec;
  std::filesystem::create_directories(g_config.user_dir, ec);
  g_direct_codes = std::make_unique<DirectCodes>(g_config.user_dir + "/direct-codes.json");
  g_teams_codes = std::make_unique<DirectCodes>(g_config.user_dir + "/teams-codes.json");
  g_local_selections.Reset();
  Savestate::force_init = true;
  if (!yougame::active() && !g_user->IsLoggedIn()) host::log("slippi: not logged in; sign in with Slippi Launcher, then choose Log In in the game");
  update_discord_presence(true);   // the connect code is only known once user.json has been read
}

void flush_cmd_trace_at_exit();
void shutdown() {
  g_slippi_link.cancel([](const char* line) { host::log("%s", line); });
  flush_cmd_trace_at_exit();
  host::cosmetics::thaw_after_online_session();
  report::shutdown();
  g_netplay.reset();
#if MU_SQUAD
  g_squad_costume_match = false;
  g_squad_match_standards.clear();
  g_squad_console.off();
#endif
  g_matchmaking.reset();
  g_active_savestates.clear();
  g_available_savestates.clear();
  host::discord::clear();   // no stale "In a match" left on the profile
}

void slippi_link_tick(uint8_t major, uint8_t minor, uint8_t mode) {
  g_link_scene = (uint32_t(major) << 16) | (uint32_t(minor) << 8) | (major == 8 ? mode : 0);
  g_slippi_link.tick(g_link_scene, g_in_online_match || g_play_session_active,
      [] { if (g_user) g_user->AttemptLogin(true); },
      [] {
        // A main-menu refusal has no search yet. Its next confirm rechecks the grant.
        if (g_link_menu) return;
        uint8_t payload[19] = {};
        payload[0] = uint8_t(g_last_search.mode);
        std::memcpy(payload + 1, g_last_search.connect_code.data(), std::min<size_t>(18, g_last_search.connect_code.size()));
        // Retire the old worker through the normal cleanup path before FindMatch owns a new thread.
        const auto selections = g_local_selections;
        const bool random_stage = g_local_random_stage;
        cleanup_connection(yougame::Leave::Quit);
        g_local_selections = selections;
        g_local_random_stage = random_stage;
        host::log("yougame: new search (Slippi link)");
        start_find_match(payload);
      }, [](const char* line) { host::log("%s", line); });
}

#if MU_SQUAD
// Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
// Private native ABI only. No new YouGame JSON keys, login, network, or room lifecycle.
static bool squad_command(const uint8_t* p, uint32_t n, std::vector<uint8_t>& q) {
  // A rejected second selection must invalidate the first, even at ABI validation.
  if (p && n && p[0] == 1) {
    g_squad_side = {};
    if (g_netplay) g_netplay->SetSquadSelection(g_squad_side);
  }
  if (!command_payload_valid(MU_SQUAD_COMMAND, p, n)) return false;
  switch (p[0]) {
    case 0:
      g_squad_row = p[1] != 0;
      g_squad_side = {};
      g_squad_costume_match = false;   // a new row choice: no squad match is set up
      g_squad_match_standards.clear();
      // Disc identity is session immutable; changing rows only clears the lineup.
      g_squad_budget_hash.reset();
      q.assign(1, 1); // positive host support acknowledgement

      break;
    case 1: {
      MuSquadSide side{};
      if (p[1] > MU_SQUAD_LINEUP) return false; // never past members[]
      side.count = p[1];
      for (unsigned i = 0; i < side.count; ++i) side.members[i] = {p[2 + i*2], p[3 + i*2]};
      if (!g_squad_row || !mu_squad_side_valid(&side)) return false;
      g_squad_side = side;
      q.assign(1, 1);
      break;
    }
    case 2:
      if ((!g_squad_row && !g_friends_lobby.armed) ||
          ((uint32_t(p[1]) << 24) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 8) | p[4]) != MU_SQUAD_ONLINE_ALLM ||
          ((uint32_t(p[5]) << 24) | (uint32_t(p[6]) << 16) | (uint32_t(p[7]) << 8) | p[8]) != MU_SQUAD_ONLINE_ALLA) return false;
      g_squad_budget.clear();
      g_squad_costumes.clear();
      g_squad_budget_hash = std::make_unique<source_port::mods::Sha256>();
      // v4: four-fighter lineups (MU_SQUAD_VERSION 3). The identity names the lineup size, so the
      // pool tag (yougame_squad.h pool_tag) and the setup hash keep 3- and 4-fighter builds apart.
      // v5: the main-heap floor MU_SQUAD_ONLINE_ALLM is the doubled 0xC96800 (was 0x64B400).
      // v6: the animation floor MU_SQUAD_ONLINE_ALLA is 0x1100000 (was 0xCB0000) on 40 MiB ARAM.
      g_squad_budget_hash->update("squad-budget-v6");
      g_squad_budget_hash->update(p + 1, n - 1);
      break;
    case 3:
      if (!g_squad_budget_hash) return false;
      g_squad_budget_hash->update(p + 1, n - 1);
      g_squad_costumes.record(p, n);
      break;
    case 4: {
      if (g_squad_budget_hash) {
        g_squad_budget = g_squad_budget_hash->hex();
        g_squad_budget_hash.reset();
      } // cached session identity can be acknowledged again without a disc scan
      // A skin over its size loads the standard copy (squad_costume_rule), so only a standard
      // copy over its size can outgrow the trim: that alone refuses, 2 for the disc's own copy,
      // 3 for a mod's, checked in the view of this search (op 5 checks again in the match's).
      std::string over;
      bool mod = false;
      if (!squad_standards(nullptr, &over, &mod))
        host::log("squad: %s %s is over its budget, search refused", mod ? "the mod's" : "the disc's own", over.c_str());
      q.assign(1, g_squad_budget.size() != 64 ? 0 : over.empty() ? 1 : mod ? 3 : 2);
      break;
    }
    case MU_SQUAD_OP_LOBBY_MODE: {
      const auto match = yougame::friends_lobby();
      q.assign(1, match.open && match.plan && match.plan->rules.squad ? 1 : 0);
      break;
    }
    case 5: {
      MuSquadSetup setup{};
      unsigned char bytes[MU_SQUAD_WIRE_MAX];
      g_squad_costume_match = false;
      g_squad_match_standards.clear();
      g_squad_fallback_logged.clear();
      const auto lobby_match = yougame::friends_lobby();
      const bool lobby_squad = lobby_match.open && lobby_match.plan && lobby_match.plan->rules.squad;
      if ((lobby_squad && yougame::lobby::squad_setup(*lobby_match.plan, &setup)) ||
          (!lobby_match.open && g_netplay && g_netplay->SquadSetup(&setup))) {
        // The view can change after the search (a Direct opponent on Slippi Dolphin, the mode's
        // content view), so the copies are checked again here, in this match's final view, before
        // the game trims and preloads. The ones found here are the ones the match opens.
        std::unordered_map<std::string, squad_costume::Standard> standards;
        std::string over;
        bool mod = false;
        const bool fits = squad_standards(&standards, &over, &mod);
#ifdef YOUGAME_AGENT_TEST_HOOK
        const bool test_refuse = lobby_squad && yougame::net_test::squad_refuse.load(std::memory_order_acquire);
#else
        const bool test_refuse = false;
#endif
        if (!fits || test_refuse) {
          if (fits) host::log("squad: agent test refusal (squadRefuse), match cancelled");
          else host::log("squad: %s %s is over its budget, match cancelled", mod ? "the mod's" : "the disc's own", over.c_str());
          if (g_netplay && fits) g_netplay->RefuseSquad("Agent test: this copy refuses Squad Strike setups.", "squad: agent test refusal, match cancelled");
          else if (g_netplay) g_netplay->RefuseSquad(squad_costume_refusal(mod), "squad: costume over its budget, match cancelled");
          if (lobby_squad) {
            // The match never began: park the room like any unusable Friends setup. Adoption's
            // next_match() has the host call the setup off (end-session), so nothing is recorded.
            yougame::lobby_refuse_setup();   // a guest ends the match in the room at once
            g_forced_error = "Squad Strike couldn't load these fighters. Match cancelled.";
            cleanup_connection(); // the ordinary unusable Friends setup path (Lost parks the room)
          }
          q.clear();
          break;
        }
        g_squad_match_standards = std::move(standards);
        g_squad_costume_match = g_squad_standard != nullptr;
        const size_t size = mu_squad_encode(&setup, bytes, sizeof bytes);
        q.assign(bytes, bytes + size);
      } else q.clear(); // normal 1v1: never import local CSS as a squad
      break;
    }
    case MU_SQUAD_OP_CONSOLE: {
      // Friends' synced select (a console session) charges costumes and hats at the same fixed
      // sizes as online on a main heap with no room over them, so while squads are in play there
      // the game arms the same rule (squad_costume::Console). Host-local, never sent to a peer.
      squad_console_forget();
      if (p[1] == MU_SQUAD_CONSOLE_FACT) {
        g_squad_console.record(p + 1, n - 1);
        q.assign(1, 1);
        break;
      }
      if (p[1] == MU_SQUAD_CONSOLE_OFF) {
        g_squad_console.off();
        g_squad_console_over.clear();
        g_squad_fallback_logged.clear();
        q.assign(1, 1);
        break;
      }
      std::string over;
      const uint8_t answer = g_squad_console.arm(g_squad_standard, &over);
      if (!over.empty() && over != g_squad_console_over)
        host::log("squad: %s %s is over its budget, Friends squads refused", answer == 3 ? "the mod's" : "the disc's own", over.c_str());
      g_squad_console_over = over;
      q.assign(1, answer);
      break;
    }
    case MU_SQUAD_OP_FRIEND: {
      const bool room = !g_last_search.friend_room.empty() && g_last_search.mode == Matchmaking::DIRECT;
      if (p[1] == MU_SQUAD_FRIEND_QUERY) {
        // This room's own pick, or the friend's: our client joined it when their build arrived.
        // Until that build is here the answer is "waiting", and lock-in waits with it.
        // Waiting is read first: once the friend's build is marked, an adoption is already visible.
        const bool waiting = !g_netplay || g_netplay->SquadFriendWaiting();
        const bool adopted = room && g_netplay && g_netplay->SquadAdopted();
        if (adopted && !g_last_search.squad) {
          g_last_search.squad = true;
          host::log("squad: friend room squad mode (friend's pick), character select told");
        }
        const uint8_t answer = !room ? MU_SQUAD_ROOM_PLAIN
            : g_last_search.squad ? MU_SQUAD_ROOM_SQUAD
            : waiting ? MU_SQUAD_ROOM_WAITING
            : MU_SQUAD_ROOM_PLAIN;
        q.assign(1, answer);
      } else if (p[1] == MU_SQUAD_FRIEND_INVITE) {
        // "Play a friend" on the Squad Strike row: the menu already moved to Friends (and set the
        // row with op 0), so this is the Friends press that opens the friend picker, for Squad Strike.
        yougame::friends::events.request_host();
        g_squad_friend_intent = yougame::friends::events.friend_entry();
        g_squad_friend_room.clear();
        host::log(g_squad_friend_intent ? "squad: play a friend, opening the friend picker"
                                        : "squad: play a friend, Friends is not selected");
        q.assign(1, g_squad_friend_intent ? 1 : 0);
      } else if (p[1] == MU_SQUAD_FRIEND_REFUSE) {
        // This game can't load Squad Strike: the friend's game says so on screen, never waits.
        const bool told = room && g_netplay;
        if (told) g_netplay->SquadFriendRefuse();
        q.assign(1, told ? 1 : 0);
      } else {
        // INTENT: the picker is opening or open, or it returned the room the game is moving to, or
        // that room's Squad Strike search is live (it starts while the menu is still on screen, and
        // takes g_squad_friend_room with it). Never a finished room's search: that outlives the
        // room, and the menu would keep the invite line.
        const bool live = room && g_last_search.squad && slippi_session_up();
        q.assign(1, g_squad_friend_intent || !g_squad_friend_room.empty() || live ? 1 : 0);
      }
      break;
    }
    default: return false;
  }
  return true;
}
#endif
// Live pick previews (mu_pick_preview.h), private native ABI. Display only: nothing here reaches
// match setup. Without a connected netplay client a send is dropped and a read answers no slots.
static bool pick_preview_command(const uint8_t* p, uint32_t n, std::vector<uint8_t>& q) {
  if (!command_payload_valid(MU_PICK_PREVIEW_COMMAND, p, n)) return false;
  if (p[0] == MU_PICK_PREVIEW_OP_SEND) {
    MuPickPreview pick{};
    if (!mu_pick_preview_decode(p + 1, n - 1, &pick)) return false;
    if (g_netplay) g_netplay->SendPickPreview(pick, host::retrace_count());
    return true;
  }
  MuPickPreviewSlot slots[MU_PICK_PREVIEW_REMOTES] = {};
  for (int i = 0; g_netplay && i < MU_PICK_PREVIEW_REMOTES; ++i)
    slots[i].valid = g_netplay->RemotePickPreview(i, &slots[i].pick, &slots[i].port);
  q.resize(MU_PICK_PREVIEW_REPLY);
  return mu_pick_preview_reply_encode(slots, q.data(), q.size()) == MU_PICK_PREVIEW_REPLY;
}

// The 4-door Friends lobby select (mu_lobby_wire.h), private native ABI. The screen's intents go to the
// lobby's Control and run on the session's thread (the search, or the parked room's pump); READ and PEEK
// answer what to draw (READ also takes a dropped lobby's parked room back, PEEK never does). Every other
// request answers one byte: 1 queued, 0 not possible now.
static bool lobby_command(const uint8_t* p, uint32_t n, std::vector<uint8_t>& q) {
  if (!command_payload_valid(MU_LOBBY_COMMAND, p, n)) return false;
  auto& lobby = g_friends_lobby;
  namespace lb = yougame::lobby;
  const uint8_t op = p[0];
  if (op == MU_LOBBY_OP_READ || op == MU_LOBBY_OP_PEEK) {
    // PEEK (the select's close, a LEAVE right behind it) never takes the parked room back.
    if (op == MU_LOBBY_OP_READ && lobby.dropped && lobby.armed && !lobby.closed &&
        !(g_matchmaking && (g_matchmaking->IsSearching() ||
                            g_matchmaking->GetMatchmakeState() == Matchmaking::CONNECTION_SUCCESS))) {
      // A connection broke between the room's setup and the splash: the room parked (close_room Lost), so
      // take it back (the adopt runs next_match), and say why on the select. No parked room: only B.
      lobby.dropped = false;
      if (!g_forced_error.empty()) lobby.note = g_forced_error;   // else what cleanup_connection kept
      if (!yougame::lobby_parked() || !start_lobby_search()) lobby.closed = true;
      host::log("friends lobby: a connection dropped before the match%s%s", lobby.closed ? "; the lobby closed" : "; back to the select",
                lobby.note.empty() ? "" : (" (" + lobby.note + ")").c_str());
    }
    bool failed = lobby.armed && g_matchmaking && g_matchmaking->GetMatchmakeState() == Matchmaking::ERROR_ENCOUNTERED;
    if (op == MU_LOBBY_OP_READ && failed && lobby.profile == lb::Profile::Doubles && !lobby.closed && yougame::lobby_parked()) {
      // The Doubles match room failed before its setup (a player never came, the queue closed, the setup
      // timed out): the home stayed parked (close_room keeps it for a match room), so take it back and say
      // why; Start searches again.
      ++lobby.note_seq;
      lobby.note = g_forced_error;
      cleanup_connection(yougame::Leave::Next);
      failed = !start_lobby_search();
      if (failed) lobby.closed = true;
      host::log("doubles: the match fell through before it started%s", failed ? "; the home closed" : "; back to the home");
    }
    const lb::Control::View cv = lobby.armed && lobby.control ? lobby.control->view() : lb::Control::View{};
    // A stale started view may survive cleanup until the search thread adopts the parked room.
    // Keep its refusal note until actual gameplay begins.
    lb::ScreenFlags flags;
    flags.armed = lobby.armed; flags.guest = lobby.guest; flags.inviting = lobby.inviting->load();
    flags.closed = lobby.closed || failed;
    flags.arm = g_lobby_arm;
    flags.note = lobby.note;
    flags.note_seq = lobby.note_seq;
    const MuLobbyView v = lb::screen_view(cv, flags);
    q.resize(MU_LOBBY_VIEW_SIZE);
    return mu_lobby_view_encode(&v, q.data(), q.size()) == MU_LOBBY_VIEW_SIZE;
  }
  if (op == MU_LOBBY_OP_COSTUMES) {   // this build's costume counts (validated 1-6 each): kept for every match
    std::copy(p + 1, p + 1 + MU_LOBBY_CHARS, g_lobby_costumes.begin());
    q.assign(1, 1);
    return true;
  }
  bool ok = false;
  const auto post = [&](lb::Control::Intent f) { lobby.control->post(std::move(f)); ok = true; };
  if (op == MU_LOBBY_OP_LEAVE) {
    // B out of the select: the lobby's own leave goes out with the search's cancel (find_match's guard) or
    // the parked room's close; then the room and links close (cleanup, as phase 1's B).
    const bool was = lobby.armed;
    if (lobby.invite_stop) lobby.invite_stop->store(true);
    lobby = FriendsLobby{};
    ++g_lobby_arm;
    if (was) host::log("friends lobby: leaving");
    cleanup_connection(yougame::Leave::Quit);
    q.assign(1, was ? 1 : 0);
    return true;
  }
  if (lobby.armed && !lobby.closed && lobby.control) {
    // HOVER and PICK are exactly {op, ch, color, team} (mu_lobby_request_ok).
    const lb::Pick pick = n == 4 ? lb::Pick{p[1], p[2], p[3]} : lb::Pick{};
    switch (op) {
#if MU_SQUAD
      case MU_LOBBY_OP_SQUAD_ADMISSION: {
        if (lobby.profile != lb::Profile::Friends4 || g_squad_budget.size() != 64) break;
        std::string over; bool mod = false;
        if (!squad_standards(nullptr, &over, &mod)) break;
        post([budget = g_squad_budget](lb::Session& s) { s.squad_admission(budget); });
        break;
      }
      case MU_LOBBY_OP_CYCLE_MODE: post([](lb::Session& s) { s.cycle_mode(); }); break;
      case MU_LOBBY_OP_LINEUP: {
        std::vector<lb::Pick> line;
        for (unsigned i = 0; i < p[1]; ++i) line.push_back({p[2 + 2*i], p[3 + 2*i], 0});
        post([line](lb::Session& s) { s.lineup(line); }); break;
      }
#endif
      case MU_LOBBY_OP_ENTER: ok = start_lobby_search(); break;
      case MU_LOBBY_OP_LOCAL_SEAT: post([claim = p[1] != 0](lb::Session& s) { s.local_seat(claim); }); break;
      case MU_LOBBY_OP_LOCAL_HOVER: post([pick](lb::Session& s) { s.local_hover(pick); }); break;
      case MU_LOBBY_OP_LOCAL_PICK: post([pick](lb::Session& s) { s.local_pick(pick); }); break;
      case MU_LOBBY_OP_LOCAL_UNPICK: post([](lb::Session& s) { s.local_unpick(); }); break;
      case MU_LOBBY_OP_HOVER: post([pick](lb::Session& s) { s.hover(pick); }); break;
      case MU_LOBBY_OP_PICK: post([pick](lb::Session& s) { s.pick(pick); }); break;
      case MU_LOBBY_OP_UNPICK: post([](lb::Session& s) { s.unpick(); }); break;
      case MU_LOBBY_OP_RULES: {
        lb::Rules r;
        r.teams = p[1] != 0; r.friendly_fire = p[2] != 0; r.time = p[3] != 0;
        r.stocks = p[4]; r.minutes = p[5]; r.pause = p[6] != 0;
        post([r](lb::Session& s) mutable { r.squad = s.state().rules.squad; s.set_rules(r); });
        break;
      }
      case MU_LOBBY_OP_SEAT: {
        const int seat = p[1], level = p[2];
        post([seat, level](lb::Session& s) { if (level) s.set_cpu(seat, level); else s.open_seat(seat); });
        break;
      }
      case MU_LOBBY_OP_CPU_PICK: {
        const int seat = p[1];
        const lb::Pick cpu{p[2], p[3], p[4]};
        const bool up = p[2] == MU_LOBBY_NONE;
        post([seat, cpu, up](lb::Session& s) { if (up) s.cpu_unpick(seat); else s.cpu_pick(seat, cpu); });
        break;
      }
      case MU_LOBBY_OP_START: {
        const int stage = p[1];
        post([stage](lb::Session& s) { s.start(stage); });
        break;
      }
      case MU_LOBBY_OP_STAGE_SELECT: {
        const bool open = p[1] != 0;
        post([open](lb::Session& s) { s.stage_select(open); });
        break;
      }
      case MU_LOBBY_OP_CANCEL: {   // Doubles: stop the search (Rooms answers match_active once placed)
        if (lobby.profile != lb::Profile::Doubles) break;
        // ...or take back the partner invite: the bridge closes the friend list (yougame_link.cpp bridge()).
        if (lobby.inviting->load() && lobby.invite_stop) lobby.invite_stop->store(true);
        post([](lb::Session& s) { s.cancel(); });
        break;
      }
      case MU_LOBBY_OP_INVITE: {
        const std::string room = lobby.control->view().room;
        if (room.empty() || lobby.inviting->load() ||
            (lobby.profile == lb::Profile::Doubles && lobby.control->view().state.seats[1].kind != lb::Kind::Open)) break;
        lobby.inviting->store(true);
        lobby.invite_stop = std::make_shared<std::atomic<bool>>(false);
        const bool doubles = lobby.profile == lb::Profile::Doubles;   // the picker's invite names the Doubles home
        std::thread([room, doubles, inviting = lobby.inviting, stop = lobby.invite_stop] {
          try {
            if (doubles) yougame::doubles_invite(room, [stop] { return stop->load(); });
            else yougame::lobby_invite(room, [stop] { return stop->load(); });
          } catch (...) {}   // logged there
          inviting->store(false);
        }).detach();
        ok = true;
        break;
      }
      default: break;
    }
  }
  q.assign(1, ok ? 1 : 0);
  return true;
}

bool valid_command_payload(uint8_t cmd, const uint8_t* payload, uint32_t payload_len) {
  return command_payload_valid(cmd, payload, payload_len);
}

namespace {
// MELEE_TRACE_SLIPPI_CMDS=1: one line per menu/online command, shared by both engines, so a
// static-recomp run and a native run can be compared with tools/slippi_trace_diff.py. Runs of
// identical (command, payload, reply) lines, like B3 every frame, collapse into one with a count.
bool trace_slippi_cmds() {
  static const bool on = [] { const char* v = std::getenv("MELEE_TRACE_SLIPPI_CMDS"); return v && v[0] == '1'; }();
  return on;
}
struct CmdTrace { std::string key; uint32_t count = 0, first_retrace = 0; uint64_t seq = 0; };
CmdTrace g_cmd_trace;
uint64_t g_cmd_seq = 0;
void flush_cmd_trace() {
  if (g_cmd_trace.count > 1)
    host::log("slippi-cmd: seq %llu retrace %u %s (x%u)", (unsigned long long)g_cmd_trace.seq,
              g_cmd_trace.first_retrace, g_cmd_trace.key.c_str(), g_cmd_trace.count);
}
void trace_cmd(uint8_t cmd, const uint8_t* payload, uint32_t payload_len, bool ok, const std::vector<uint8_t>& q) {
  char buf[16];
  std::string key;
  std::snprintf(buf, sizeof buf, "cmd %02X len %u", cmd, payload_len); key += buf;
  key += " payload ";
  const uint32_t shown = payload ? std::min<uint32_t>(payload_len, 64) : 0;
  for (uint32_t i = 0; i < shown; ++i) { std::snprintf(buf, sizeof buf, "%02X", payload[i]); key += buf; }
  if (shown < payload_len) key += "..";
  uint32_t h = 2166136261u;
  for (uint8_t b : q) { h ^= b; h *= 16777619u; }
  char tail[64];
  std::snprintf(tail, sizeof tail, " reply %u fnv %08X%s", ok ? (uint32_t)q.size() : 0u, ok ? h : 0u, ok ? "" : " unhandled");
  key += tail;
  ++g_cmd_seq;
  if (g_cmd_trace.count && key == g_cmd_trace.key) { ++g_cmd_trace.count; return; }
  flush_cmd_trace();
  g_cmd_trace = {key, 1, host::retrace_count(), g_cmd_seq};
  host::log("slippi-cmd: seq %llu retrace %u %s", (unsigned long long)g_cmd_seq, g_cmd_trace.first_retrace, key.c_str());
}
}  // namespace

void flush_cmd_trace_at_exit() { flush_cmd_trace(); g_cmd_trace = {}; }
bool handle_command(uint8_t cmd, const uint8_t* payload, uint32_t payload_len, std::vector<uint8_t>& q);

bool handle(uint8_t cmd, const uint8_t* payload, uint32_t payload_len, std::vector<uint8_t>& q) {
  if (!trace_slippi_cmds()) return handle_command(cmd, payload, payload_len, q);
  // The replay and match streams (B0-B2) run every frame and are not menu traffic; nor are the
  // per-frame pick previews, which the static recomp never sends.
  if (cmd == CMD_ONLINE_INPUTS || cmd == CMD_CAPTURE_SAVESTATE || cmd == CMD_LOAD_SAVESTATE ||
      cmd == MU_PICK_PREVIEW_COMMAND)
    return handle_command(cmd, payload, payload_len, q);
  // The static recomp passes its EXI read queue, which still holds the previous reply for commands
  // that answer nothing; the native caller passes an empty vector. Trace what this command wrote.
  std::vector<uint8_t> reply;
  const bool ok = handle_command(cmd, payload, payload_len, reply);
  trace_cmd(cmd, payload, payload_len, ok, reply);
  if (!reply.empty()) q.swap(reply);
  return ok;
}

bool handle_command(uint8_t cmd, const uint8_t* payload, uint32_t payload_len, std::vector<uint8_t>& q) {
#if MU_SQUAD
  // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
  if (cmd == MU_SQUAD_COMMAND) return squad_command(payload, payload_len, q);
#endif
  if (cmd == MU_PICK_PREVIEW_COMMAND) return pick_preview_command(payload, payload_len, q);
  if (cmd == MU_LOBBY_COMMAND) return lobby_command(payload, payload_len, q);
  if (!valid_command_payload(cmd, payload, payload_len)) {
    host::log("slippi: invalid payload for command %02X (%u bytes)", cmd, payload_len);
    return false;
  }
  if (!g_user) init();
  consume_discord_join();   // one atomic load unless a friend's Discord invite is actually waiting
  switch (cmd) {
    case CMD_ONLINE_INPUTS: handle_online_inputs(payload, payload_len, q); return true;
    case CMD_CAPTURE_SAVESTATE: handle_capture_savestate(payload); return true;
    case CMD_LOAD_SAVESTATE: handle_load_savestate(payload); return true;
    case CMD_GET_MATCH_STATE: prepare_online_match_state(q); return true;
    case CMD_FIND_OPPONENT:
      g_unranked_opponent_left = g_unranked_left_in_game = false;   // a new search: the last leave is old news
#ifdef MELEE_DOUBLES
      // The Doubles CSS's search (mu_slippi_css.c): no code, it joins the home's match room.
      if (payload[0] == Matchmaking::TEAMS && yougame::active()) {
        // A room plays one match: after its result the select never searches it again (the home re-queues).
        if (yougame::doubles::ui.teams_over()) {
          host::log("doubles: the match is over; not searching its room again");
          return true;
        }
        const std::string room = yougame::doubles::ui.teams_search_room();
        if (!room.empty()) {
          host::log("doubles: joining the match room on the 4-peer Teams client");
          g_doubles_games = 0;
          uint8_t find[19] = {};   // no connect code: whatever the name entry last held is not this search's
          find[0] = payload[0];
          start_find_match(find, room); return true;
        }
      }
#endif
      if (yougame::unranked::keep_css(yougame::active(), payload[0]))
        host::log("yougame: new search (Start)");
      start_find_match(payload); return true;
    case CMD_SET_MATCH_SELECTIONS: set_match_selections(payload); return true;
    case CMD_OPEN_LOGIN:
      if (yougame::active()) { yougame::refresh_identity(); return true; }
      if (!g_user->AttemptLogin(true)) host::log("slippi: login requested: sign in with Slippi Launcher, then choose Log In again");
      return true;
    case CMD_LOGOUT: if (!yougame::active()) g_user->LogOut(); return true;   // the YouGame account is the page's
    case CMD_UPDATE: host::log("slippi: update requested by the game (ignored)"); return true;
    case CMD_GET_ONLINE_STATUS: prepare_online_status(q); return true;
    case CMD_CLEANUP_CONNECTION:
      // Optional reason byte from the native CSS's existing hold-Z path; legacy guests send none.
      if (payload_len == 1 && payload[0] == 1 &&
          yougame::unranked::keep_css(yougame::active(), (int)g_last_search.mode))
        host::log("yougame: connection ended (Z)");
      // 2: the Friends lobby's select is back after its match: the room, links and lobby stay (parked).
      if (payload_len == 1 && payload[0] == 2 && g_friends_lobby.armed) { cleanup_connection(yougame::Leave::Next); return true; }
      // 3: the main menu came up (the Source Port's menus say which cleanup this is). A search the
      // player chose to keep goes on (native_practice decides); otherwise the cleanup as always.
      if (payload_len == 1 && payload[0] == 3 && slippi::native_practice::keep_search_at_menu()) return true;
      // The Static Recomp's menus send no reason: while a kept search is away the only cleanup the
      // game sends is its main menu's, and the search goes on.
      if (payload_len == 0 && g_search_away) { host::log("slippi: kept search: the menu's cleanup is skipped"); return true; }
      cleanup_connection(yougame::Leave::Quit); return true;
    case CMD_SEND_CHAT_MESSAGE: if (chat_enabled() && g_netplay) g_netplay->SendChatMessage(payload[0]); return true;
    case CMD_GET_NEW_SEED: q.clear(); append_u32(q, g_rng() % 0xFFFFFFFFu); return true;
    case CMD_REPORT_GAME: handle_report_game(payload); return true;
    case CMD_FETCH_CODE_SUGGESTION: handle_name_entry_load(payload, q); return true;
    case CMD_OVERWRITE_SELECTIONS: {
      g_overwrite_selections.clear();
      uint16_t stage = be16(payload);
      for (int i = 0; i < 4; ++i) {
        const uint8_t* c = payload + 2 + i * 3;
        if (!c[0]) continue;
        PlayerSelections s;
        s.is_character_selected = true; s.character_id = c[1]; s.character_color = c[2];
        s.is_stage_selected = true; s.stage_id = stage; s.player_idx = (uint8_t)i;
        g_overwrite_selections.push_back(s);
      }
      return true;
    }
    case CMD_GP_COMPLETE_STEP:
      if (yougame::session()) yougame::ranked_game_setup();   // retain pair for C2 + room result; first-to-two stays authoritative
      if (yougame::session() && g_netplay) {
        GamePrepStep step{};
        std::copy_n(payload, step.size(), step.begin());
        g_netplay->SendGamePrepStep(step);
      }
      return true;
    case CMD_GP_FETCH_STEP: {
      q.assign(6, 0);
      if (yougame::session() && g_netplay) {
        const auto reply = g_netplay->FetchGamePrepStep(payload[0]);
        q.assign(reply.begin(), reply.end());
      }
      return true;
    }
    case CMD_REPORT_SET_COMPLETE:
      if (yougame::session()) yougame::ranked_set_complete(payload[0]);
      return true;
    case CMD_REPORT_MATCH_STATUS_UPDATE:
      if (yougame::session()) yougame::ranked_match_status(payload[0]);
      return true;
    case CMD_GET_PLAYER_SETTINGS: handle_get_player_settings(q); return true;
    // Offline input now passes through the same host queue on both engines.
    // Disable the console's second offline queue so inputs are delayed once.
    case CMD_GET_DELAY: q.clear(); q.push_back(session_mode() >= 0 ? 1 : 0);
      q.push_back(session_mode() >= 0 ? (uint8_t)effective_delay() : 0); return true;
    // Ranks: YouGame mode answers with its ladder rank; standalone never shows one (visibility 0
    // hides every rank display; the rest reads as unranked).
    case CMD_GET_RANK:
      if (yougame::session()) q = yougame::rank_response();
      else { q.assign(16, 0); q[1] = 1; }
      return true;
    case CMD_FETCH_RANK: return true; // room pump receives the authoritative result asynchronously
    case CMD_GET_RANK_VISIBILITY: q.assign(1, yougame::session() ? yougame::rank_visibility() : 0); return true;
    case CMD_YOUGAME_SLIPPI_LINK: {
      if (agent_lock::blocked(agent_lock::Operation::AccountLink)) { q.assign(128, 0); return true; }
      using namespace yougame::slippi_linking;
      auto screen = Screen::None;
      const auto action = payload[0];
      if (action == 4) {
        g_slippi_link.cancel([](const char* line) { host::log("%s", line); });
        return true;
      }
      if (action > 3) return false;
      if (yougame::active() && host::gameplay::vanilla_only() && !g_in_online_match) {
        if ((action == 2 || action == 3) && (g_link_scene >> 16) == 1) screen = Screen::Refusal;
        else if (action <= 1 && (g_link_scene >> 8) == 0x800 && g_matchmaking) {
          const auto state = g_matchmaking->GetMatchmakeState();
          const auto error = g_forced_error.empty() ? g_matchmaking->GetErrorMessage() : g_forced_error;
          if (g_last_search.mode == Matchmaking::UNRANKED && g_last_search.friend_room.empty() &&
              state == Matchmaking::MATCHMAKING && yougame::status_line() == kSearching) screen = Screen::Search;
          else if (state == Matchmaking::ERROR_ENCOUNTERED && error == kRefusal) screen = Screen::Refusal;
        }
      }
      if (action == 1 || action == 3) {
        if (g_slippi_link.press(screen, g_link_scene, yougame::request_slippi_link)) g_link_menu = action == 3;
      }
      auto line = presentation.line(screen);
      if (screen == Screen::None) line.clear();
      q.assign(line.begin(), line.end()); q.resize(128);
      return true;
    }
    case CMD_YOUGAME_FRIENDS_MENU:
      // Query only: the menu checks the local app-granted Slippi account before entry.
      // Do not publish Friends state, wait for identity, or contact either service.
#ifdef MELEE_DOUBLES
      if (payload[0] == 12) { if (yougame::active()) yougame::doubles::ui.open("new"); return true; }
      if (payload[0] == 14) { // the home's match: open the Teams character select once, with this seat's team
        const bool idle = yougame::session() && session_mode() < 0;   // the app, or a YouGame row under the launcher
        const auto entry = yougame::doubles::ui.take_teams_entry(idle);
        if (entry[0]) host::log("doubles: opening the Teams character select (team %d)", entry[1] == 0xFF ? -1 : entry[1]);
        q.assign(entry.begin(), entry.end());
        return true;
      }
      if (payload[0] == 13) { // the Doubles home is on screen: the menu puts its cursor on Doubles
        std::lock_guard<std::mutex> lock(yougame::doubles::ui.mutex);
        const auto& ui = yougame::doubles::ui;
        q.assign(1, yougame::session() && ui.active && ui.foreground && !ui.playing ? 1 : 0);
        return true;
      }
#endif
      if (payload[0] == 16) { // the Doubles row (G2): open the 2-seat Doubles home's select (native_practice arms it)
        if (yougame::session()) g_doubles_lobby_request = true;   // the app's layout, and the launcher's Doubles row
        return true;
      }
      if (payload[0] == 8) {
        q.assign(1, yougame::active() && g_user && User::GrantedSlippiUser(g_user->dir()) ? 1 : 0);
        return true;
      }
      if (payload[0] == 3) {
        if (yougame::session()) yougame::ranked_screen_enter();
        return true;
      }
      if (payload[0] == 14) { // menu entry: did a Friends room open the CSS it came back from? (once)
        q.assign(1, g_friends_css.exchange(false) ? 1 : 0);
        return true;
      }
      if (payload[0] == 17) {
        // The split layout's Friends row was confirmed in a game started on its own, with the local channel
        // up (the menu sends this only then; never in the app, whose pad press opens the flow). The same
        // request a Friends press makes in the app: the next safe menu tick holds a Friends room and opens
        // YouGame's friend picker and lobby (native_practice). A confirm the menu made by itself (a row picked
        // before YouGame was connected, or --online-mode) has no pad press, so it comes this way too.
        if (!yougame::active() && yougame::friends_flow()) {
          yougame::friends::events.online_menu(true, true);
          yougame::friends::events.request_host();
          host::log("friends: the Friends row was confirmed (local channel)");
        }
        return true;
      }
      // Bit 2 tracks the Online submenu independently of the hidden Friends row.
      g_online_menu_visible = (payload[0] & 4) != 0;
      // friends_flow(): the app, or a game started on its own with the local channel up (the split layout's
      // Friends row). Under the app it is active(), as before.
      yougame::friends::events.online_menu(yougame::friends_flow() && (payload[0] & 3) != 0, (payload[0] & 3) == 2);
      return true;
    case CMD_YOUGAME_DATA:
      switch (payload[0]) {
        case MU_YG_SUB_RANK:
          q = yougame::session() ? yougame::rank_record_response() : yougame::ranked::rank_record_response({}, {}, {});
          return true;
        case MU_YG_SUB_POLICY: {
          auto policy = use_ranked_rules(0) ? g_ranked_rules.policy : yougame::ranked_policy::slippi_v1();
          // session(), not active(): a game the app started over the local channel has no bridge
          // URL, so active() is false there and Ranked's setup would read "no rules" and quit.
          const char* refusal = yougame::session() ? local_matchmaking_error(0) : "not in YouGame mode";
          policy.available = refusal == nullptr;
          if (refusal) host::log("yougame: Ranked rules unavailable to the game: %s", refusal);
          q = yougame::ranked_policy::encode(policy);
          return true;
        }
        case MU_YG_SUB_UNRANKED_NOTE:
          q.assign(1, g_unranked_opponent_left ? 1 : 0);
          return true;
        default: return false; // optional subcommand unknown to this host
      }
    case CMD_YOUGAME_RANKED:
      // This fork's own query (native_slippi_bridge.h): byte 0 bit 0 = Ranked is offered (YouGame
      // mode), then six fixed-width presentation lines in the game's text encoding. Standalone
      // Melee Unlocked leaves the Ranked bit clear. YouGame mode offers it only while
      // yougame_mode_policy.h says so (off since 2026-10-29).
      q.assign(1, yougame::mode_policy::friends_offered(yougame::active()) ? 2 : 0);
      // Facts for the C menu, also in standalone. Read the real environment in agent_copy().
      if (host::test_env::agent_copy()) q[0] |= 8;
      if (Matchmaking::local_peer.enabled) q[0] |= 16;
      // Bit 5: the first menu after boot is the main menu. A game the YouGame app started opens there for
      // a local match; Online opens first only when the app asked for it (--resume online after an
      // update). Standalone (the launcher, a bare run) keeps Slippi's boot to Online (boot.asm).
      if (yougame::active() && !g_boot_online) q[0] |= 32;
      if (yougame::active()) {
        // Bit 2 is the presentation capability; bit 0 alone controls the hidden menu row.
        q[0] |= 4;
        if (yougame::mode_policy::yougame_ranked_offered) q[0] |= 1;
        q.resize(1 + 6 * 128, 0);
        const auto lines = yougame::ranked_lines();
        for (size_t i = 0; i < lines.size(); ++i) {
          const auto text = convert_string_for_game(lines[i], 63);
          std::copy_n(text.begin(), std::min<size_t>(text.size(), 127), q.begin() + 1 + i * 128);
        }
      }
      return true;
    case CMD_YOUGAME_MENU: {
      // Upstream's online-menu query (Melee Unlocked 1.0; upstream numbers it 0xE7, which this fork's
      // Slippi link uses). A 1-byte payload is the menu's pick before its character select: 1 a
      // YouGame row, 0 a Slippi row (launcher only).
      // Reply byte 0: bit 0 the app's layout (the YouGame app started the game: the menu keeps this
      // fork's rows, CMD_YOUGAME_RANKED); bit 1 the split layout (YouGame Ranked, YouGame Direct, then
      // Slippi's rows); bit 2 the YouGame rows are open (the launcher's bridge is up and its account
      // signed in); bit 3 the Ranked header follows, in the game's text encoding.
      // 3: Start on the older-release offer (start_find_match): the launcher restarts in that release.
      // 0x10 | row: the first page is on screen with YouGame Coming soon (the cursor on `row`): only
      // feeds the stickers over the menu.
      // The plain query is the Online menu opening: a game started on its own looks for YouGame again.
      if (payload_len == 0) yougame::online_menu_opened();
      // A YouGame row pressed before the app could say it is signed out (still starting, its page still
      // loading): the shim's wait polls this query once a second, so the card opens on the first poll
      // that knows, not on a second press (11-17: the first press after a cold start sat
      // ~50 s and then showed nothing). Dropped after the shim's own 10-minute wait or any other pick.
      static std::atomic<int64_t> s_yg_pick{0};
      const auto now_pick_ms = [] {
        return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
      };
      if (payload_len == 0 && s_yg_pick) {
        if (now_pick_ms() - s_yg_pick > 600000) s_yg_pick = 0;
        else if (yougame::sign_in_needed()) { s_yg_pick = 0; yougame::sign_in_open(); }
      }
      // 0x20: the first row picked while YouGame is Coming soon: open the installed app, else the download page.
      // 0x10: older menus' Coming soon stickers; none are drawn since build 98.2 (the rows look the same
      // connected or not).
      // 0x21: the menu was left (Back to 1-P Mode, or into a character select) after a 0x20 asked for YouGame:
      // YouGame's small window, still up from that ask, closes with it (yougame::withdraw_ask).
      // 0x4A: only asks whether a Squad Strike search the player chose to keep is running (the online
      // menu then keeps the select's lineup for the way back; native_practice.cpp).
      if (payload_len == 1 && payload[0] == 0x4A) { q.assign(1, slippi::native_practice::keep_search_squad() ? 1 : 0); return true; }
      // 0x22: a YouGame row pressed while the launcher is not signed in to YouGame: the launcher's own Connect
      // (sign-in) window opens, or comes to the front (yougame::open_launcher_connect). Answers 1 when the launcher
      // was asked, 0 when there is no launcher to ask; the menu then waits on the row and goes on into it once the
      // rows open, as after 0x20.
      if (payload_len == 1 && payload[0] == 0x22) { s_yg_pick = 0; q.assign(1, yougame::open_launcher_connect() ? 1 : 0); return true; }
      if (payload_len == 1 && (payload[0] & 0xF0) == 0x10) {
      } else if (payload_len == 1 && payload[0] == 0x21) {
        s_yg_pick = 0;
        yougame::withdraw_ask("Online menu left");
      } else if (payload_len == 1 && payload[0] == 0x20) {
        if (yougame::coming_soon() || yougame::ask_on_use()) yougame::first_row_picked();
        // YouGame is open but signed out (local channel): the sign-in panel, then the menu carries on.
        else if (yougame::sign_in_needed()) yougame::sign_in_open();
        if (!yougame::sign_in_needed()) s_yg_pick = now_pick_ms();   // not known yet: the poll opens it
      } else if (payload_len == 1 && payload[0] == 3) {
        if (g_older_offer_mode >= 0) {
          std::string why;
          if (yougame::restart_older(g_older_offer_mode == Matchmaking::DIRECT ? "direct" : "ranked", &why)) host::request_exit(0);
          else g_forced_error = why;
        }
      } else if (payload_len == 1 && payload[0] == 0x4B) {
        // The Friends row: 1 while the extension's YouGame app is coming up (its channel brings the friend flow).
        q.assign(1, yougame::extension_waiting() ? 1 : 0);
        return true;
      } else if (payload_len == 1 && payload[0] == 0x49) {
        // Which service the opponent came from (the character select's header after a Both search):
        // 0 not known, 1 Slippi, 2 YouGame. Only asks.
        const auto won = g_matchmaking ? g_matchmaking->DisplayProvider() : display::Provider::Unknown;
        q.assign(1, won == display::Provider::Slippi ? 1 : won == display::Provider::YouGame ? 2 : 0);
        return true;
      } else if (payload_len == 1 && payload[0] >= 0x40 && payload[0] <= 0x48) {
        // The menu's remembered choices: 0x40 / 0x41 the YouGame / Slippi page is shown, 0x42 / 0x43
        // Unranked searches YouGame and Slippi off / on; 0x44 only asks (one byte: bit 0 the Slippi
        // page, bit 1 both, bits 2-3 the Unranked slider); 0x45-0x48 the slider's value.
        if (payload[0] == 0x44) { q.assign(1, (uint8_t) online_menu_prefs()); return true; }
        online_menu_prefs_set(payload[0]);
      } else if (payload_len == 1) {
        s_yg_pick = 0;
        // The pick: 1 a YouGame row, 0 a Slippi row, 2 Both Unranked (YouGame and Slippi at once). Under
        // the launcher, 2 is the blended search with the game's own Slippi login (slippi_net.cpp
        // startMatchmaking); launched by the app, its own mixed Unranked.
        yougame::set_menu_service(payload[0] <= 2 ? payload[0] : -1);
        if (payload[0] == 2)
          host::log("online menu: Both Unranked chosen (pick 2): searches YouGame and Slippi%s",
                    yougame::active() ? " (the app's mixed Unranked)" : " (YouGame alone without a Slippi login)");
        yougame::set_launcher_session(payload[0] == 1 || payload[0] == 2);
      }
      q.assign(1, 0);
      if (yougame::active()) q[0] |= 1;
      else if (online_rows::applied()) q[0] |= 2;   // the labels were rewritten (Source Port)
      // The local channel never holds a frame: its identity is asked on the probe's thread, and the
      // rows open on the first visit after it is in.
      if (yougame::launcher() && (yougame::channel_connected() ? yougame::identity().ok : yougame::wait_identity(500).ok)) q[0] |= 4;
      // Bit 7: the YouGame rows are open over the local channel, so the split layout's Friends row opens
      // YouGame's friend flow (friends_flow(): a Friends room, its picker and the lobby) instead of Direct's
      // connect code. The old launcher bridge leaves it clear and keeps Direct.
      if ((q[0] & 4) && yougame::channel_connected() && yougame::friends_flow()) q[0] |= 128;
      // Bit 6: YouGame is not ready for the rows yet: not open (picking a row starts it), or open and signed
      // out (picking a row opens Sign in by code). Either way the menu then carries on into that row.
      // Also a game on the app's channel with no YouGame found yet (yougame::ask_on_use: the launcher handed it to
      // the YouGame app, or it started on its own): picking a row asks with YouGame's start hook.
      if ((yougame::coming_soon() || yougame::ask_on_use() || yougame::sign_in_needed()) && !yougame::active() && (q[0] & 2)) q[0] |= 64;
      // Bit 4/5: started for a YouGame mode (--online-mode): the menu opens Ranked (16) or Direct (32).
      if (payload_len == 0 && (q[0] & 4)) {
        if (const int arg = yougame::take_online_mode_arg()) q[0] |= arg == 2 ? 32 : 16;
      }
      if (yougame::session()) {
        q[0] |= 8;
        std::string text = yougame::ranked_header();
        if (yougame::launcher() && text == "Ranked Mode") text = "YouGame Ranked";   // beside Slippi's rows
        const std::string header = convert_string_for_game(text, 30);
        q.insert(q.end(), header.begin(), header.end());
      }
      return true;
    }
    default: return false;
  }
}

}  // namespace slippi::online
