// Slippi Online command handling (see slippi_online.h): port of the online half of Dolphin's
// CEXISlippi plus SlippiSavestate. The game-side Slippi codes drive everything: they ask for
// the match state each frame in the lobby, send local inputs and fetch remote ones each frame
// in a match, and capture/load savestates around rollbacks.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "slippi_online.h"
#include "native_slippi_bridge.h"
#include "slippi_net.h"
#include "slippi_report.h"
#include "exi_slippi.h"
#include "host.h"
#include "window.h"
#include "discord_presence.h"
#include "gx_core.h"
#include "cosmetic_mods.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <random>
#include <thread>
#include <unordered_map>

namespace slippi::online {
namespace {

Config g_config;
NativeGameplayProfile g_native_gameplay_profile = NativeGameplayProfile::Vanilla;
LocalBuild g_local_build;   // retail unless the Source Port's content view says otherwise
// The profile the online rules see: the mod's only while its files are what the game shows.
NativeGameplayProfile effective_profile() {
  return g_local_build.mod_view ? g_native_gameplay_profile : NativeGameplayProfile::Vanilla;
}
// Vanilla character select kinds are 0-25 and the stage kinds end with the heal stage (0x55); an id
// past those comes from a build with more content (m-ex adds its fighters and stages after them).
constexpr uint8_t kRetailCharacterCount = 26;
constexpr uint16_t kRetailStageCount = 0x56;
constexpr uint64_t kBuildWaitMs = 3000;   // how long a mod build waits for the opponent's build message

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
Matchmaking::MatchSearchSettings g_last_search;
Matchmaking::MatchmakeResult g_recent_mm_result;
std::vector<uint16_t> g_allowed_stages = {0x2, 0x3, 0x8, 0x1C, 0x1F, 0x20};
std::vector<uint16_t> g_stage_pool;
std::vector<PlayerSelections> g_overwrite_selections;
std::string g_forced_error;
bool g_play_session_active = false;
uint8_t g_local_player_index = 0, g_remote_player_index = 1;
uint32_t g_stall_frame_counts[REMOTE_PLAYER_MAX] = {};
int g_frames_to_skip = 0, g_frames_to_advance = 0, g_fall_behind = 0, g_fall_far_behind = 0;
bool g_currently_skipping = false, g_currently_advancing = false;
std::mt19937 g_rng((uint32_t)time_ms());
uint64_t g_rollbacks = 0;
bool g_in_online_match = false;
// Determinism oracle: the game hands us a checksum of its finalized state each frame and the
// opponent's client sends theirs; a mismatch is a desync between the two simulations.
std::map<int32_t, uint32_t> g_local_checksums;
uint32_t g_checksums_compared = 0, g_checksums_mismatched = 0;
int32_t g_last_checksum_frame[REMOTE_PLAYER_MAX] = {};   // per remote: every peer's checksums are compared, not only the first to report a frame

bool is_disconnected() { return !g_netplay || g_netplay->GetSlippiConnectStatus() != NetplayClient::ConnectStatus::CONNECTED; }
bool chat_enabled() { return g_last_search.mode == Matchmaking::DIRECT ? (g_config.chat == 0 || g_config.chat == 1) : g_config.chat == 0; }

uint16_t random_stage() {
  if (g_stage_pool.empty()) g_stage_pool.insert(g_stage_pool.end(), g_allowed_stages.begin(), g_allowed_stages.end());
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
  if (!host::discord::enabled()) return;
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
  if (!host::discord::enabled()) return;
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

void cleanup_connection() {
  host::log("slippi: connection cleanup");
  host::cosmetics::thaw_after_online_session();
  if (g_matchmaking || g_netplay) {
    std::thread([mm = std::move(g_matchmaking), nc = std::move(g_netplay)]() mutable { mm.reset(); nc.reset(); }).detach();
  }
  g_matchmaking = std::make_unique<Matchmaking>(g_user.get());
  g_netplay = nullptr;
  g_local_selections.Reset();
  g_stage_pool.clear();
  g_forced_error.clear();
  g_overwrite_selections.clear();
  g_play_session_active = false;
  g_in_online_match = false;
  host::set_emulation_speed(1.0);
  update_discord_presence(true);   // back to "In the menus" straight away, not two seconds later
}

// ---------------------------------------------------------------- per-frame online flow
bool should_skip_online_frame(int32_t frame, int32_t finalized_frame) {
  auto st = g_netplay->GetSlippiConnectStatus();
  if (st == NetplayClient::ConnectStatus::FAILED || st == NetplayClient::ConnectStatus::DISCONNECTED) return false;
  bool any_needs_inputs = false;
  uint8_t remote_count = g_matchmaking->RemotePlayerCount();
  for (uint8_t i = 0; i < remote_count; ++i) {
    auto pad = g_netplay->GetSlippiRemotePad(i, ROLLBACK_MAX_FRAMES);
    if (pad->is_disconnected) { g_stall_frame_counts[i] = 0; continue; }
    int32_t latest = pad->latest_frame;
    bool enough = latest - finalized_frame >= (frame - finalized_frame - ROLLBACK_MAX_FRAMES);
    if (enough) { g_stall_frame_counts[i] = 0; continue; }
    g_stall_frame_counts[i]++;
    any_needs_inputs = true;
    if (g_stall_frame_counts[i] > 60 * 7) {
      host::log("slippi: force-disconnecting player %u after 7 s stall (frame %d, latest %d)", pad->player_idx, frame, latest);
      g_netplay->ForceDisconnectPlayer(pad->player_idx);
      g_stall_frame_counts[i] = 0;
      continue;
    }
  }
  if (any_needs_inputs) return true;
  const int32_t frame_time = 16683, t1 = 10000, t2 = 2 * frame_time + t1;
  // Every 30 frames for the whole match, as Slippi Dolphin does: a strict threshold while the match
  // starts, then only when over two frames ahead (t2). Checking only the first 120 frames left an
  // instance that drifted ahead there for the rest of the match, predicting and rolling back.
  if (frame % ONLINE_LOCKSTEP_INTERVAL == 0 && !g_currently_skipping) {
    int32_t offset = g_netplay->CalcTimeOffsetUs();
    if (offset > (frame <= 120 ? t1 : t2)) {
      g_currently_skipping = true;
      int max_skip = frame <= 120 ? 5 : 1;
      g_frames_to_skip = std::min(((offset - t1) / frame_time) + 1, max_skip);
      host::log("slippi: halting on frame %d for time sync (offset %d us, %d frames)", frame, offset, g_frames_to_skip);
    }
  }
  if (g_frames_to_skip > 0) { --g_frames_to_skip; return true; }
  g_currently_skipping = false;
  return false;
}

bool opponent_runahead() {
  auto info = g_matchmaking->GetPlayerInfo();
  for (size_t i = 0; i < info.size(); ++i) { if ((int)i == g_matchmaking->LocalPlayerIndex()) continue; if (!info[i].is_bot) return false; }
  return true;
}

bool should_advance_online_frame(int32_t frame) {
  if (opponent_runahead()) return false;
  if (frame % ONLINE_LOCKSTEP_INTERVAL == 0) {
    int32_t offset = g_netplay->CalcTimeOffsetUs();
    float deviation = 0;
    if (offset > -250 && offset < 8000) deviation = 0;
    else if (offset < 0) deviation = std::min(-offset / (3 * 16683.0f), 1.0f) * 0.01f;
    else deviation = std::min(offset / (3 * 16683.0f), 1.0f) * -0.005f;
    host::set_emulation_speed(1.0 + deviation);
    const int32_t frame_time = 16683, t1 = 10000, t2 = frame_time + t1;
    g_fall_behind += offset < -t1 ? 1 : 0;
    g_fall_far_behind += offset < -t2 ? 1 : 0;
    bool slow = (offset < -t1 && g_fall_behind > 50) || (offset < -t2 && g_fall_far_behind > 15);
    if (slow && g_matchmaking->RemotePlayerCount() == 1)
      host::log("slippi: possible poor match performance detected (offset %d us)", offset);
    if (offset < -t2 && !g_currently_advancing) {
      g_currently_advancing = true;
      int max_adv = frame > 120 ? 3 : 0;
      g_frames_to_advance = std::min(((-offset - t1) / frame_time) + 1, max_adv);
      host::log("slippi: advancing on frame %d for time sync (offset %d us, %d frames)", frame, offset, g_frames_to_advance);
    }
  }
  if (g_frames_to_advance > 0) {
    if (frame % 5 != 0) return false;
    --g_frames_to_advance;
    return true;
  }
  g_currently_advancing = false;
  return false;
}

void prepare_opponent_inputs(int32_t frame, bool should_skip, std::vector<uint8_t>& q) {
  q.clear();
  uint8_t frame_result = 1;
  auto st = g_netplay->GetSlippiConnectStatus();
  if (should_skip) frame_result = 2;
  else if (st != NetplayClient::ConnectStatus::CONNECTED) frame_result = 3;
  else if (should_advance_online_frame(frame)) frame_result = 4;
  q.push_back(frame_result);
  uint8_t remote_count = g_matchmaking->RemotePlayerCount();
  q.push_back(remote_count);
  std::unique_ptr<RemotePadOutput> results[REMOTE_PLAYER_MAX];
  int32_t latest_from_opps = -123 - 1;
  uint32_t last_checksum_frame = 0, last_checksum = 0;
  for (int i = 0; i < remote_count; ++i) {
    results[i] = g_netplay->GetSlippiRemotePad(i, ROLLBACK_MAX_FRAMES);
    if (results[i]->is_disconnected) continue;
    int32_t cf = results[i]->checksum_frame;
    if (cf > g_last_checksum_frame[i] && results[i]->checksum) {
      auto it = g_local_checksums.find(cf);
      if (it != g_local_checksums.end()) {
        g_last_checksum_frame[i] = cf; ++g_checksums_compared;
        if (it->second != results[i]->checksum) { ++g_checksums_mismatched; host::log("slippi: DESYNC: checksum mismatch at frame %d (ours %08X, player %u %08X)", cf, it->second, results[i]->player_idx, results[i]->checksum); }
        else if (g_checksums_compared % 20 == 0) host::log("slippi: checksums agree through frame %d (%u compared, %u mismatched)", cf, g_checksums_compared, g_checksums_mismatched);
      }
    }
    if (results[i]->latest_frame > latest_from_opps) {
      last_checksum_frame = (uint32_t)results[i]->checksum_frame;
      last_checksum = results[i]->checksum;
      latest_from_opps = results[i]->latest_frame;
    }
  }
  constexpr int32_t DESPAWN_INTERVAL = 30;
  uint8_t should_despawn[REMOTE_PLAYER_MAX] = {0, 0, 0};
  for (int i = 0; i < remote_count; ++i) {
    if (!results[i]->is_disconnected) continue;
    int32_t threshold = results[i]->latest_frame + 2 * ROLLBACK_MAX_FRAMES + 2;
    int32_t despawn = ((threshold + DESPAWN_INTERVAL - 1) / DESPAWN_INTERVAL) * DESPAWN_INTERVAL;
    if (frame >= despawn) should_despawn[i] = 1;
  }
  for (int i = 0; i < remote_count; ++i) {
    if (!results[i]->is_disconnected) { append_u32(q, (uint32_t)results[i]->checksum_frame); append_u32(q, results[i]->checksum); continue; }
    results[i]->latest_frame = latest_from_opps;
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
  for (int i = 0; i < REMOTE_PLAYER_MAX; ++i) {
    std::vector<uint8_t> tx;
    if (i < remote_count && offset[i] < (int)results[i]->data.size()) tx.assign(results[i]->data.begin() + offset[i], results[i]->data.end());
    tx.resize(PAD_FULL_SIZE * ROLLBACK_MAX_FRAMES, 0);
    q.insert(q.end(), tx.begin(), tx.end());
  }
  for (int i = 0; i < REMOTE_PLAYER_MAX; ++i) q.push_back(should_despawn[i]);
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

void handle_online_inputs(const uint8_t* payload, std::vector<uint8_t>& q) {
  q.clear();
  int32_t frame = (int32_t)be32(payload), finalized = (int32_t)be32(payload + 4);
  trace_online_frame("inputs", frame);
  uint32_t finalized_checksum = be32(payload + 8);
  uint8_t delay = payload[12];
  const uint8_t* inputs = payload + 13;
  if (frame == 1) {
    g_available_savestates.clear();
    g_active_savestates.clear();
    // Console-RAM savestates are the static recomp's; the native game keeps its own snapshots, and
    // its memory holds no console heap bounds to size these from.
    if (!host::game_image)
      for (int i = 0; i < ROLLBACK_MAX_FRAMES; ++i) g_available_savestates.push_back(std::make_unique<Savestate>());
    for (auto& c : g_stall_frame_counts) c = 0;
    g_frames_to_skip = 0; g_currently_skipping = false;
    g_frames_to_advance = 0; g_currently_advancing = false; g_fall_behind = 0; g_fall_far_behind = 0;
    g_local_selections.Reset();
    if (g_netplay) g_netplay->StartSlippiGame();
    g_in_online_match = true;
    host::input_mark_match_start();
    g_local_checksums.clear(); g_checksums_compared = 0; g_checksums_mismatched = 0; std::fill(std::begin(g_last_checksum_frame), std::end(g_last_checksum_frame), 0);
  }
  if (is_disconnected()) {
    q.push_back(3);
    return;
  }
  if (frame % 30 == 0) host::log("slippi: online frame %d wall %.3f s retrace %u rollbacks %llu", frame, host::now_seconds(), host::retrace_count(), (unsigned long long)g_rollbacks);
  if (finalized > 0 && finalized_checksum) { g_local_checksums[finalized] = finalized_checksum; while (g_local_checksums.size() > 600) g_local_checksums.erase(g_local_checksums.begin()); }
  g_netplay->DropOldRemoteInputs(finalized);
  bool skip = should_skip_online_frame(frame, finalized);
  if (skip) g_netplay->SendSlippiPad(nullptr);
  else {
    if (frame == 1) for (int i = 1; i <= delay; ++i) g_netplay->SendSlippiPad(std::make_unique<Pad>(i));
    g_netplay->SendSlippiPad(std::make_unique<Pad>(frame + delay, finalized, finalized_checksum, inputs));
  }
  prepare_opponent_inputs(frame, skip, q);
}

void handle_capture_savestate(const uint8_t* payload) {
  if (is_disconnected()) return;
  int32_t frame = (int32_t)be32(payload);
  trace_online_frame("capture", frame);
  std::unique_ptr<Savestate> ss;
  if (!g_available_savestates.empty()) { ss = std::move(g_available_savestates.back()); g_available_savestates.pop_back(); }
  else { auto it = g_active_savestates.begin(); ss = std::move(it->second); g_active_savestates.erase(it); }
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
  // The next frame the game finishes continues from this older state, not from the frame on screen.
  // Blending the two (sub-frame animation) would draw positions that never existed on either
  // timeline, which on continuously animated stages showed as a glitch that only ever happened
  // online. The renderer holds exact frames across it instead.
  gx::mark_discontinuity();
  for (auto& kv : g_active_savestates) g_available_savestates.push_back(std::move(kv.second));
  g_active_savestates.clear();
}

// ---------------------------------------------------------------- lobby
void start_find_match(const uint8_t* payload) {
  Matchmaking::MatchSearchSettings search;
  search.mode = (Matchmaking::OnlinePlayMode)payload[0];
  if (const char* reason = native_profile_mode_error(effective_profile(), (int)search.mode)) {
    g_forced_error = reason;
    return;
  }
  std::string sj((const char*)payload + 1, 18);
  sj.erase(std::find(sj.begin(), sj.end(), '\0'), sj.end());
  if (search.mode == Matchmaking::DIRECT && discord_join_pending()) {
    // Choosing Direct after accepting a Discord Join means join that sender. Use the queued code
    // even if the game's text field still contains its old value; the user need not retype/paste it.
    const std::string invite_sj = utf8_to_shiftjis(g_discord_join_code);
    sj.assign(invite_sj.begin(), invite_sj.begin() + std::min<size_t>(invite_sj.size(), 18));
    g_discord_join_code.clear();
  }
  if (search.mode == Matchmaking::DIRECT) g_direct_codes->AddOrUpdateCode(shiftjis_to_utf8(sj));
  else if (search.mode == Matchmaking::TEAMS) g_teams_codes->AddOrUpdateCode(shiftjis_to_utf8(sj));
  search.connect_code = sj;
  g_last_search = search;
  if (Matchmaking::IsFixedRulesMode(search.mode)) {
    if (g_local_selections.character_id >= 26) { g_forced_error = "The character you selected is not allowed in this mode"; return; }
    if (g_local_selections.is_stage_selected && std::find(g_allowed_stages.begin(), g_allowed_stages.end(), g_local_selections.stage_id) == g_allowed_stages.end()) {
      g_forced_error = "The stage being requested is not allowed in this mode"; return;
    }
  } else if (search.mode == Matchmaking::TEAMS && g_local_selections.character_id >= 26) {
    g_forced_error = "The character you selected is not allowed in this mode"; return;
  }
  if (!enet_ready()) { g_forced_error = "Networking unavailable"; return; }
  host::cosmetics::freeze_for_online_session();
  g_matchmaking->FindMatch(search);
  update_discord_presence(true);
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
  uint8_t len = payload[24];
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
  if (stage_option == 3) s.stage_id = random_stage();
  s.rng_offset = g_rng() % 0xFFFF;
  const auto& local_peer = Matchmaking::local_peer;
  if (local_peer.enabled && local_peer.test_stage >= 0) {
    s.stage_id = static_cast<uint16_t>(local_peer.test_stage);
    s.is_stage_selected = true;
    host::log("slippi: local regression stage forced to %u", s.stage_id);
  }
  g_local_selections.Merge(s);
  if (g_netplay) g_netplay->SetMatchSelections(g_local_selections);
}

void prepare_online_match_state(std::vector<uint8_t>& q);

// The same-build rule. 1: play, 0: wait for the opponent's build, -1: refuse (message in *why).
// Direct with a mod: every opponent must be on the same mod build (or, at the player's word, send
// no build: Slippi Dolphin with the same mod). Retail game: an opponent who says it is on a mod is
// refused, since their fighters and stages would not exist here. Other modes play the retail game on
// both sides; an opponent claiming a mod there is refused too.
int build_verdict(uint8_t remote_count, std::string* why) {
  if (!g_netplay) return 1;
  const bool direct = g_last_search.mode == Matchmaking::DIRECT;
  bool waiting = false;
  for (int i = 0; i < remote_count; ++i) {
    const auto rb = g_netplay->GetRemoteBuild(i);
    if (!rb.received) {
      if (g_local_build.mod_view && direct && !g_local_build.allow_unverified) waiting = true;
      continue;
    }
    if (!direct) {
      if (rb.mod_view) { *why = "Your opponent is on a mod build; this mode plays the retail game"; return -1; }
      continue;
    }
    if (g_local_build.mod_view) {
      if (!rb.mod_view || rb.fingerprint != g_local_build.fingerprint) {
        *why = "Your opponent is not on " + g_local_build.name + ". This Direct match needs the same build";
        return -1;
      }
    } else if (rb.mod_view) {
      *why = "Your opponent is using " + (rb.name.empty() ? std::string("a mod build") : rb.name) +
             ". Use the same build, or ask them to play Direct without it";
      return -1;
    }
  }
  if (!waiting) return 1;
  const uint64_t since = g_netplay->ConnectedAtMs();
  const uint64_t now = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  if (since == 0 || now - since < kBuildWaitMs) return 0;
  // The game shows at most 120 characters.
  *why = "Opponent did not confirm " + g_local_build.name + ". For Slippi Dolphin, tick Opponent uses it on Slippi Dolphin";
  return -1;
}

// Retrace of the last CMD_GET_MATCH_STATE. The game polls it every frame the online menus are up
// (mode select, the online character select, waiting for the opponent) and never during a match,
// so it is the signal for "the player is in the online menus right now".
uint32_t g_last_match_state_retrace = 0;

void prepare_online_match_state(std::vector<uint8_t>& q) {
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
  q.clear();
  Matchmaking::ProcessState mm_state = !g_forced_error.empty() ? Matchmaking::ERROR_ENCOUNTERED : g_matchmaking->GetMatchmakeState();
  q.push_back((uint8_t)mm_state);
  uint8_t local_ready = g_local_selections.is_character_selected ? 1 : 0;
  uint8_t remote_ready = 0;
  UserInfo me = g_user->GetUserInfo();
  uint16_t alt_stage_mode = 0;
  if (mm_state == Matchmaking::CONNECTION_SUCCESS) {
    g_local_player_index = (uint8_t)g_matchmaking->LocalPlayerIndex();
    if (!g_netplay) {
      g_netplay = g_matchmaking->GetNetplayClient();
      g_recent_mm_result = g_matchmaking->GetMatchmakeResult();
      g_allowed_stages = g_recent_mm_result.stages;
      if (g_allowed_stages.empty()) g_allowed_stages = {0x2, 0x3, 0x8, 0x1C, 0x1F, 0x20};
      g_stage_pool.clear();
      g_local_selections.stage_id = random_stage();
      g_netplay->SetMatchSelections(g_local_selections);
    }
    bool connected = g_netplay->GetSlippiConnectStatus() == NetplayClient::ConnectStatus::CONNECTED;
    if (g_netplay->GetActivePlayerIndices().size() != g_matchmaking->RemotePlayerCount()) connected = false;
    if (!connected) {
      // The opponent may have refused first and left: a build that was already received still
      // explains why, instead of a plain disconnect.
      std::string refusal;
      if (build_verdict(g_matchmaking->RemotePlayerCount(), &refusal) < 0) {
        host::log("slippi: build check: %s", refusal.c_str());
        cleanup_connection();
        g_forced_error = refusal;
        prepare_online_match_state(q);
        return;
      }
    }
    if (connected) {
      MatchInfo* mi = g_netplay->GetMatchInfo();
      remote_ready = 1;
      uint8_t remote_count = g_matchmaking->RemotePlayerCount();
      for (int i = 0; i < remote_count; ++i) if (!mi->remote[i].is_character_selected) remote_ready = 0;
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
      if (remote_count == 1) {
        bool decider = g_netplay->IsDecider();
        g_local_player_index = decider ? 0 : 1;
        g_remote_player_index = decider ? 1 : 0;
      }
    } else {
      cleanup_connection();
      prepare_online_match_state(q);
      return;
    }
    if (!g_play_session_active) g_play_session_active = true;
  } else {
    g_netplay = nullptr;
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
    local_name = me.display_name;
  }
  if (local_ready && remote_ready) {
    bool decider = g_netplay->IsDecider();
    uint8_t remote_count = g_matchmaking->RemotePlayerCount();
    MatchInfo* mi = g_netplay->GetMatchInfo();
    PlayerSelections lps = mi->local;
    PlayerSelections rps[REMOTE_PLAYER_MAX];
    for (int i = 0; i < REMOTE_PLAYER_MAX; ++i) rps[i] = mi->remote[i];
    bool local_char_ok = lps.character_id < 26, remote_char_ok = true;
    for (int i = 0; i < remote_count; ++i) if (rps[i].character_id >= 26) remote_char_ok = false;
    std::vector<PlayerSelections*> ordered(remote_count + 1, nullptr);
    if (lps.player_idx < ordered.size()) ordered[lps.player_idx] = &lps;
    for (int i = 0; i < remote_count; ++i) if (rps[i].player_idx < ordered.size()) ordered[rps[i].player_idx] = &rps[i];
    for (auto& o : ordered) if (!o) o = &lps;   // defensive: never dereference a hole
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
    if (Matchmaking::IsFixedRulesMode(g_last_search.mode)) {
      if (!local_char_ok) { cleanup_connection(); g_forced_error = "The character you selected is not allowed in this mode"; prepare_online_match_state(q); return; }
      if (!remote_char_ok) { cleanup_connection(); prepare_online_match_state(q); return; }
      if (std::find(g_allowed_stages.begin(), g_allowed_stages.end(), stage_id) == g_allowed_stages.end()) { cleanup_connection(); prepare_online_match_state(q); return; }
    } else if (g_last_search.mode == Matchmaking::TEAMS) {
      if (!local_char_ok) { cleanup_connection(); g_forced_error = "The character you selected is not allowed in this mode"; prepare_online_match_state(q); return; }
      if (!remote_char_ok) { cleanup_connection(); prepare_online_match_state(q); return; }
    } else if (g_last_search.mode == Matchmaking::DIRECT) {
      // Characters and stages past the retail ones exist only in a mod build, and the Source Port
      // runs a mod's added fighters and stages only once their code is source (none yet): such a
      // selection ends the match here instead of desyncing or crashing inside it.
      bool foreign = stage_id >= kRetailStageCount;
      for (int i = 0; i < remote_count; ++i) if (rps[i].character_id >= kRetailCharacterCount) foreign = true;
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
    for (auto* s : ordered) {
      if (!s->is_character_selected) continue;
      uint8_t team = teams ? s->team_id : 0;
      if (teams && all_same_team) team = team_assign[s->player_idx];
      block[0x60 + s->player_idx * 0x24] = s->character_id;
      block[0x63 + s->player_idx * 0x24] = s->character_color;
      block[0x67 + s->player_idx * 0x24] = 0;
      block[0x69 + s->player_idx * 0x24] = team;
    }
    std::unordered_map<uint16_t, uint8_t> color_counts;
    for (int i = 0; i < PLAYER_COUNT_MAX; ++i) {
      if (block[0x61 + i * 0x24] != 0) continue;
      uint8_t char_id = block[0x60 + i * 0x24], color = block[0x63 + i * 0x24], team = block[0x69 + i * 0x24];
      char_id = char_id == 0x13 ? 0x12 : char_id;
      uint16_t key = (uint16_t)((char_id << 8) | (teams ? team : color));
      uint8_t& count = color_counts[key];
      block[0x67 + 0x24 * i] = count;
      count += 1;
    }
    block[0x8] = teams ? 1 : 0;
    block[0x61 + 2 * 0x24] = remote_count >= 2 ? 0 : 3;
    block[0x61 + 3 * 0x24] = remote_count >= 3 ? 0 : 3;
    block[0xE] = (uint8_t)(stage_id >> 8); block[0xF] = (uint8_t)stage_id;
    bool pause_allowed = g_last_search.mode == Matchmaking::DIRECT;
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
    }
  }
  if (g_last_search.mode == Matchmaking::PARTY) {
    block[0x0] = 0x12; block[0x3] = 0xCC;
    uint32_t t = 5 * 60; block[0x10] = (uint8_t)(t >> 24); block[0x11] = (uint8_t)(t >> 16); block[0x12] = (uint8_t)(t >> 8); block[0x13] = (uint8_t)t;
  } else {
    block[0x0] = 0x32; block[0x3] = 0x4C;
    uint32_t t = 8 * 60; block[0x10] = (uint8_t)(t >> 24); block[0x11] = (uint8_t)(t >> 16); block[0x12] = (uint8_t)(t >> 8); block[0x13] = (uint8_t)t;
  }
  block[0xB] = 0xFF;
  uint64_t items = 0xF80000000F000000ull;
  if (g_recent_mm_result.items != 0) { items |= (uint64_t)g_recent_mm_result.items << 28; block[0xB] = 3; }
  for (int i = 0; i < 8; ++i) block[0x23 + i] = (uint8_t)(items >> (56 - 8 * i));
  append_u32(q, rng_offset);
  q.push_back((uint8_t)g_config.delay);
  q.push_back(sent_chat_message_id);
  q.push_back(chat_message_id);
  q.push_back(chat_message_player_idx);
  q.push_back(0);   // player 1 rank: no ranks in Melee Unlocked
  q.push_back(0);   // player 2 rank
  std::string ln = convert_string_for_game(local_name, 15);
  q.insert(q.end(), ln.begin(), ln.end());
  for (int i = 0; i < 4; ++i) { std::string n = convert_string_for_game(g_matchmaking->GetPlayerName((uint8_t)i), 15); q.insert(q.end(), n.begin(), n.end()); }
  std::vector<std::string> opponent_names;
  int team_idx = block[0x69 + g_local_player_index * 0x24];
  for (int i = 0; i < 4; ++i) {
    bool teams = g_last_search.mode == Matchmaking::TEAMS;
    bool same_team = block[0x69 + i * 0x24] == team_idx;
    bool human = block[0x61 + i * 0x24] == 0;
    if (g_local_player_index == i || !human || (same_team && teams)) continue;
    std::string n = g_matchmaking->GetPlayerName((uint8_t)i);
    if (!n.empty()) opponent_names.push_back(n);
  }
  size_t num_opp = opponent_names.empty() ? 1 : opponent_names.size();
  int chars_per_name = (int)((15 - (num_opp - 1)) / num_opp);
  std::string opp_text;
  for (auto& n : opponent_names) { if (!opp_text.empty()) opp_text += "/"; opp_text += truncate_length_char(n, chars_per_name); }
  opp_name = convert_string_for_game(opp_text, 15);
  q.insert(q.end(), opp_name.begin(), opp_name.end());
  auto players = g_matchmaking->GetPlayerInfo();
  for (int i = 0; i < 4; ++i) { std::string c = convert_connect_code_for_game(i < (int)players.size() ? players[i].connect_code : ""); q.insert(q.end(), c.begin(), c.end()); }
  for (int i = 0; i < 4; ++i) { std::string uid = i < (int)players.size() ? players[i].uid : ""; uid.resize(29); q.insert(q.end(), uid.begin(), uid.end()); }
  std::string err = convert_string_for_game(!g_forced_error.empty() ? g_forced_error : g_matchmaking->GetErrorMessage(), 120);
  q.insert(q.end(), err.begin(), err.end());
  q.insert(q.end(), block.begin(), block.end());
  std::string match_id = g_recent_mm_result.id;
  match_id.resize(51);
  q.insert(q.end(), match_id.begin(), match_id.end());
  q.push_back((uint8_t)alt_stage_mode);
}

void prepare_online_status(std::vector<uint8_t>& q) {
  q.clear();
  g_user->AttemptLogin();
  UserInfo me = g_user->GetUserInfo();
  uint8_t app_state = 0;
  if (g_user->IsLoggedIn()) app_state = 1;   // version check: this port speaks SLIPPI_SEMVER; the server enforces the rest
  q.push_back(app_state);
  std::string name = convert_string_for_game(me.display_name, 15);
  q.insert(q.end(), name.begin(), name.end());
  std::string code = convert_connect_code_for_game(me.connect_code);
  q.insert(q.end(), code.begin(), code.end());
}

void handle_report_game(const uint8_t* p) {
  // ReportGameQuery (packed, big-endian fields): mode, frameLength, gameIndex, tiebreakIndex, winnerIdx, gameEndMethod, lrasInitiator, syncedTimer, players[4], gameInfoBlock[312]
  uint8_t mode = p[0];
  uint32_t frames = be32(p + 1), game_index = be32(p + 5), tiebreak = be32(p + 9);
  int8_t winner = (int8_t)p[13]; uint8_t end_method = p[14]; int8_t lras = (int8_t)p[15];
  const uint8_t* players = p + 20;
  const uint8_t* info_block = players + 4 * 9;
  int stage = be16(info_block + 0xE);
  host::log("slippi: game report: mode %u, %u frames, game %u, tiebreak %u, winner %d, end %u, lras %d, stage %d",
            mode, frames, game_index, tiebreak, winner, end_method, lras, stage);
  host::publish_lobby_result(winner, end_method);
  {
    // Exactly CEXISlippi::handleReportGame: one report per game with every slot's result.
    UserInfo me = g_user->GetUserInfo();
    report::GameReport r;
    r.uid = me.uid; r.play_key = me.play_key; r.match_id = g_recent_mm_result.id; r.replay_path = slippi::last_replay_path();
    r.online_mode = mode; r.duration_frames = frames; r.game_index = game_index; r.tiebreak_index = tiebreak;
    r.winner_index = winner; r.game_end_method = end_method; r.lras_initiator = lras; r.stage_id = stage;
    for (int i = 0; i < 4; ++i) {
      report::PlayerReport pr;
      pr.uid = g_recent_mm_result.players.size() > (size_t)i ? g_recent_mm_result.players[i].uid : "";
      pr.slot_type = players[i * 9]; pr.stocks_remaining = players[i * 9 + 1];
      uint32_t dmg = be32(players + i * 9 + 2); float dmg_f; std::memcpy(&dmg_f, &dmg, 4); pr.damage_done = dmg_f;
      pr.character_id = info_block[0x60 + 0x24 * i]; pr.color_id = info_block[0x63 + 0x24 * i];
      pr.starting_stocks = info_block[0x62 + 0x24 * i]; pr.starting_percent = be16(info_block + 0x70 + 0x24 * i);
      r.players.push_back(pr);
    }
    // A local two-instance test peering never went through Slippi's matchmaking, so there is no
    // server-side match: reporting it would file a fake game under the signed-in account.
    if (Matchmaking::local_peer.enabled) host::log("slippi: local test peering, game report not sent");
    else report::log_game(r);
  }
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
void set_local_build(const LocalBuild& build) {
  if (build.mod_view != g_local_build.mod_view || build.fingerprint != g_local_build.fingerprint ||
      build.allow_unverified != g_local_build.allow_unverified)
    host::log("slippi: local build: %s%s%s", build.mod_view ? "mod " : "retail game",
              build.mod_view ? build.name.c_str() : "", build.allow_unverified ? " (Slippi Dolphin opponents allowed)" : "");
  g_local_build = build;
}
const LocalBuild& local_build() { return g_local_build; }
void set_native_gameplay_profile(NativeGameplayProfile profile) {
  if (session_mode() >= 0) {
    host::log("slippi: cannot change gameplay profile during an online session");
    return;
  }
  g_native_gameplay_profile = profile;
}
uint64_t rollback_count() { return g_rollbacks; }
void note_rollback() { ++g_rollbacks; }
bool is_online_match() { return g_in_online_match; }
int local_player_slot() { return g_local_player_index; }
std::array<std::string, 4> player_names_for_overlay() {
  std::array<std::string, 4> names{};
  if (!g_in_online_match || !g_matchmaking) return names;
  for (int i = 0; i < 4; ++i) names[i] = g_matchmaking->GetPlayerName((uint8_t)i);
  if (g_user && g_local_player_index < 4 && names[g_local_player_index].empty())
    names[g_local_player_index] = g_user->GetUserInfo().display_name;
  return names;
}
int ping_ms() { return g_netplay ? g_netplay->LastPingMs() : 0; }

// g_last_search keeps the mode of the last search for the whole session, so it only means anything
// while an online session is actually up: searching, set up (the online character select screen),
// or in the match. Everywhere else this is offline and the answer is -1.
int session_mode() {
  const bool session = g_in_online_match || g_play_session_active || (g_matchmaking && g_matchmaking->IsSearching());
  return session ? (int)g_last_search.mode : -1;
}
int local_player_index() { return (int)g_local_player_index; }
bool in_online_menus() {
  const uint32_t now = host::retrace_count();
  return g_last_match_state_retrace && now - g_last_match_state_retrace < 10;
}

bool native_start_match(int mode, const std::string& connect_code, uint8_t character,
                        uint8_t color, std::string* error) {
  if (!g_user) init();
  if (session_mode() >= 0) {
    if (error) *error = "An online session is already active";
    return false;
  }
  if (const char* reason = native_profile_mode_error(effective_profile(), mode)) {
    if (error) *error = reason;
    return false;
  }

  // Same payloads the guest sends from the stock Slippi online menus. Native practice chooses a
  // random legal stage and carries the practice character/costume into the normal online flow.
  uint8_t selections[9] = {0, character, color, 1, 0, 0, 3, (uint8_t)mode, 0};
  set_match_selections(selections);
  uint8_t find[19] = {};
  find[0] = (uint8_t)mode;
  const std::string sjis = utf8_to_shiftjis(connect_code);
  std::memcpy(find + 1, sjis.data(), std::min<size_t>(18, sjis.size()));
  start_find_match(find);
  if (!g_forced_error.empty()) {
    if (error) *error = g_forced_error;
    return false;
  }
  if (error) error->clear();
  return g_matchmaking && g_matchmaking->IsSearching();
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
  cleanup_connection();
}

static bool file_exists(const std::string& p) { FILE* f = std::fopen(p.c_str(), "rb"); if (!f) return false; std::fclose(f); return true; }

void init() {
  // The launcher profile is a read-only authentication fallback. Keep the port's mutable direct
  // and teams-code history in the configured local profile so launching this app cannot rewrite or
  // truncate Dolphin/Slippi Launcher's friend/code history.
  const std::string configured_user_dir = g_config.user_dir;
  bool using_shared_launcher_login = false;
  report::init(host::options.iso, g_config.user_dir);
  // Without a user.json in the configured folder, use the Slippi Launcher's own login so a fresh
  // install of the port shares the account the user already signed into.
  if (!file_exists(g_config.user_dir + "/user.json")) {
    const char* appdata = std::getenv("APPDATA");
    if (appdata) {
      std::string launcher = std::string(appdata) + "/Slippi Launcher/netplay/User/Slippi";
      if (file_exists(launcher + "/user.json")) {
        host::log("slippi: using the Slippi Launcher login at %s", launcher.c_str());
        g_config.user_dir = launcher;
        using_shared_launcher_login = true;
      }
    }
  }
  g_user = std::make_unique<User>(g_config.user_dir);
  g_matchmaking = std::make_unique<Matchmaking>(g_user.get());
  const std::string code_history_dir = using_shared_launcher_login ? configured_user_dir : g_config.user_dir;
  if (using_shared_launcher_login) {
    std::error_code ec;
    std::filesystem::create_directories(code_history_dir, ec);
    host::log("slippi: keeping direct/teams code history local at %s (launcher profile is read-only)", code_history_dir.c_str());
  }
  g_direct_codes = std::make_unique<DirectCodes>(code_history_dir + "/direct-codes.json");
  g_teams_codes = std::make_unique<DirectCodes>(code_history_dir + "/teams-codes.json");
  g_local_selections.Reset();
  Savestate::force_init = true;
  if (!g_user->IsLoggedIn()) host::log("slippi: not logged in (no %s/user.json); log in with the Slippi Launcher and point --user-dir at its Slippi folder", g_config.user_dir.c_str());
  update_discord_presence(true);   // the connect code is only known once user.json has been read
}

void flush_cmd_trace_at_exit();
void shutdown() {
  flush_cmd_trace_at_exit();
  host::cosmetics::thaw_after_online_session();
  report::shutdown();
  g_netplay.reset();
  g_matchmaking.reset();
  g_active_savestates.clear();
  g_available_savestates.clear();
  host::discord::clear();   // no stale "In a match" left on the profile
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
  // The replay and match streams (B0-B2) run every frame and are not menu traffic.
  if (cmd == CMD_ONLINE_INPUTS || cmd == CMD_CAPTURE_SAVESTATE || cmd == CMD_LOAD_SAVESTATE)
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
  if (!valid_command_payload(cmd, payload, payload_len)) {
    host::log("slippi: invalid payload for command %02X (%u bytes)", cmd, payload_len);
    return false;
  }
  if (!g_user) init();
  consume_discord_join();   // one atomic load unless a friend's Discord invite is actually waiting
  switch (cmd) {
    case CMD_ONLINE_INPUTS: handle_online_inputs(payload, q); return true;
    case CMD_CAPTURE_SAVESTATE: handle_capture_savestate(payload); return true;
    case CMD_LOAD_SAVESTATE: handle_load_savestate(payload); return true;
    case CMD_GET_MATCH_STATE: prepare_online_match_state(q); return true;
    case CMD_FIND_OPPONENT: start_find_match(payload); return true;
    case CMD_SET_MATCH_SELECTIONS: set_match_selections(payload); return true;
    case CMD_OPEN_LOGIN:
      if (!g_user->AttemptLogin()) host::log("slippi: login requested: sign in with the Slippi Launcher, then copy its user.json to %s", g_config.user_dir.c_str());
      return true;
    case CMD_LOGOUT: g_user->LogOut(); return true;
    case CMD_UPDATE: host::log("slippi: update requested by the game (ignored)"); return true;
    case CMD_GET_ONLINE_STATUS: prepare_online_status(q); return true;
    case CMD_CLEANUP_CONNECTION: cleanup_connection(); return true;
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
    // Ranked game preparation and set reporting: Melee Unlocked has no Ranked play. The command
    // numbers stay understood so any Slippi menu code that sends them gets a harmless answer.
    case CMD_GP_COMPLETE_STEP: case CMD_REPORT_SET_COMPLETE: case CMD_REPORT_MATCH_STATUS_UPDATE: return true;
    case CMD_GP_FETCH_STEP: q.assign(6, 0); return true;   // nothing ready
    case CMD_GET_PLAYER_SETTINGS: handle_get_player_settings(q); return true;
    case CMD_GET_DELAY: q.clear(); q.push_back(1); q.push_back((uint8_t)g_config.delay); return true;
    // Ranks: never shown. Visibility 0 hides every rank display; the rest reads as unranked.
    case CMD_GET_RANK: q.assign(16, 0); q[1] = 1; return true;
    case CMD_FETCH_RANK: return true;
    case CMD_GET_RANK_VISIBILITY: q.assign(1, 0); return true;
    default: return false;
  }
}

}  // namespace slippi::online
