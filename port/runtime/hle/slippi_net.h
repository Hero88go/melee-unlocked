// Slippi Online networking: SFML-compatible packets, the ENet netplay client, the matchmaking
// client and the user record. Ports of Dolphin's SlippiNetplay/SlippiMatchmaking/SlippiUser
// with the same wire formats, so the port can play against stock Slippi clients.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "slippi_game_prep.h"
#include "opponent_display.h"
#include "slippi_mm_service.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "early_input_rules.h"
#include "../abi/mu_setup_hash.h"
#include "../abi/mu_pick_preview.h"
#include "../abi/mu_friends_lobby.h"
#if MU_SQUAD
// Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
#include "yougame_squad.h"
#endif

struct _ENetHost;
struct _ENetPeer;
namespace yougame::udp { struct EnetIngress; }
namespace yougame { struct Search; struct Match; }
namespace yougame::lobby { class Control; struct Plan; }
namespace slippi { struct PortInputs; }

namespace slippi {

constexpr int ROLLBACK_MAX_FRAMES = 7;
// How long a peer connection may take (NetplayClient::ThreadFunc), and how long a match setup
// waits for every peer's setup hash before that peer counts as disconnected.
constexpr uint64_t kConnectTimeoutMs = 8000;
constexpr int ONLINE_LOCKSTEP_INTERVAL = 30;
// Five remote players: Triples (6 players, Melee Unlocked only). Slippi's own layouts (the game's
// reply, the match state) keep their room for 3 remotes and 4 players; Triples adds to them.
constexpr int REMOTE_PLAYER_MAX = 5;
constexpr int SLIPPI_REMOTE_MAX = 3;
constexpr int SLIPPI_PLAYER_MAX = SLIPPI_REMOTE_MAX + 1;
constexpr int PLAYER_COUNT_MAX = REMOTE_PLAYER_MAX + 1;
constexpr int PAD_FULL_SIZE = 0xC;
constexpr int PAD_DATA_SIZE = 0x8;   // buttons, sticks and triggers
// What goes over the network for each frame (Slippi 3.7.0): the inputs and one byte of RNG bits.
constexpr int PAD_WIRE_SIZE = PAD_DATA_SIZE + 1;
// Player drops (Slippi 3.7.0): how long the other players' reports about a dropped player are awaited,
// and how many frames after the dropped player's last input the fighter despawns (2 * 7 + 2).
constexpr uint64_t DROP_REPORT_WAIT_MS = 300;
constexpr int32_t DROP_DESPAWN_FRAMES = 16;
constexpr uint8_t CHAT_MSG_CHAT_DISABLED = 0x10;
extern const char* const SLIPPI_SEMVER;   // netplay version this port speaks

enum NetMsg : uint8_t {
  NP_MSG_SLIPPI_PAD = 0x80, NP_MSG_SLIPPI_PAD_ACK = 0x81, NP_MSG_SLIPPI_MATCH_SELECTIONS = 0x82, NP_MSG_SLIPPI_CONN_SELECTED = 0x83,
  NP_MSG_SLIPPI_CHAT_MESSAGE = 0x84, NP_MSG_SLIPPI_COMPLETE_STEP = 0x85, NP_MSG_SLIPPI_SYNCED_STATE = 0x86,
  // Slippi 3.7.0: a mid-frame input hint (unreliable), and what a client has of a dropped player's inputs.
  NP_MSG_SLIPPI_PAD_SUGGESTION = 0x87, NP_MSG_SLIPPI_PLAYER_DROP = 0x88,
  // Melee Unlocked only: the sender's build (retail or a mod's identity). Slippi Dolphin logs an
  // unknown id and ignores it, so it is safe to send to anyone.
  NP_MSG_MU_BUILD = 0xE0,
  // Melee Unlocked only: an early input (early_input.h), sent only to a peer whose build message
  // said it takes them. A peer without that word (Slippi Dolphin, an older build) never gets one.
  NP_MSG_MU_EARLY_PAD = 0xE1,
  // Melee Unlocked only: a ping before the match (request or reply), sent only to a peer whose build
  // message said it answers them, so auto delay has a ping when the match is set up.
  NP_MSG_MU_PING = 0xE2,
#if MU_SQUAD
  // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
  NP_MSG_MU_SQUAD = 0xE3,
#endif
  // Melee Unlocked only: the hash of the match setup this side built (mu_setup_hash.h). Sent to
  // every peer; only a peer whose build message carried MU_SETUP_HASH_CAP is waited for.
  NP_MSG_MU_SETUP_HASH = 0xE6,
  // Melee Unlocked only: the sender's live pick on the character screen (mu_pick_preview.h), sent
  // only to a peer whose build message carried MU_PICK_PREVIEW_CAP. Display only: the match is
  // still set up from 0x82 (and 0xE3).
  NP_MSG_MU_PICK_PREVIEW = MU_PICK_PREVIEW_MSG,
  // Melee Unlocked only: arena room traffic (slippi_arena.h) on a room link, sent only to a peer
  // whose build message said it takes it (MU_BUILD_CAPS2_ARENA). Never on a match connection's pads.
  // Upstream numbers it 0xE3 and flags it with capability bit 0x10; this build already uses both
  // (Squad Strike), so it rides 0xE7 and the second capability byte here.
  NP_MSG_MU_ARENA = 0xE7,
  // Melee Unlocked only: Gecko codes offered, answered and agreed for the match (gecko_online.h),
  // sent only to a peer whose build message said it takes them (MU_BUILD_CAPS2_GECKO). Upstream
  // gives it this build's pick preview id and capability bit 0x40 (the setup hash here), so it
  // rides 0xE8 and the second capability byte here.
  NP_MSG_MU_GECKO = 0xE8,
  // Melee Unlocked only: the sender went to warm up in Training while staying connected, or came back
  // to the character select (slippi_warmup.h). Sent only to a peer whose build message said it takes
  // it; anyone else just sees a player who has not picked yet. Melee Unlocked 0.9.215 and 0.9.216
  // number it 0xE5 and flag it with first capability bit 0x80; this build already uses 0xE5 (machine
  // pads, slippi_port_inputs.h), so between builds with the third capability byte it rides 0xE9 and
  // caps3 bit 0x10, and toward those two releases 0xE5 (NP_MSG_MU_WARMUP_UPSTREAM).
  NP_MSG_MU_WARMUP = 0xE9,
  // Melee Unlocked only: tap jump off and tilt stick offered and answered for the match
  // (port/runtime/host/controls_wire.h), sent only to a peer whose build message carried
  // MU_BUILD_CAPS4_CONTROLS. Reliable and ordered ahead of each selection (0x82).
  NP_MSG_MU_CONTROLS = 0xEA,
};
static_assert(NP_MSG_MU_SETUP_HASH == MU_SETUP_HASH_MSG, "one setup hash id");
// The warm-up message's id on the wire toward (and from) Melee Unlocked 0.9.215 and 0.9.216, which have
// no third capability byte. Only ever read as warm-up from such a peer and outside machine pads.
constexpr uint8_t NP_MSG_MU_WARMUP_UPSTREAM = 0xE5;
// MU_BUILD third capability byte (this build on): 0x01 Squad Strike, 0x02 its friend rooms, 0x04 pick
// previews, 0x08 setup hash, 0x10 warm-up messages (NP_MSG_MU_WARMUP, 0xE9).
constexpr uint8_t MU_BUILD_CAPS3_WARMUP = 0x10;
// 0x20: this side decides Direct pausing from every player's 0x40 (1.0.1 on); 0x40: this player turned
// pausing off. A pause rule only one side applies would split the game at the first Start press, so
// pausing goes off only when every peer sends 0x20 (and the setup hash covers the pause bit anyway).
constexpr uint8_t MU_BUILD_CAPS3_PAUSE_RULE = 0x20;
constexpr uint8_t MU_BUILD_CAPS3_NO_PAUSE = 0x40;
// 0x80: this build plays an arena free-for-all (a Teams match made with teams off, slippi_arena.h). A
// free-for-all is refused unless every peer sends it: a build without it would start a teams game.
constexpr uint8_t MU_BUILD_CAPS3_ARENA_FFA = 0x80;
// MU_BUILD fourth capability byte, after the port colours (builds before it stop reading at the colours):
// 0x01 this game plays tap jump off and tilt stick and takes NP_MSG_MU_CONTROLS (controls::advertise).
constexpr uint8_t MU_BUILD_CAPS4_CONTROLS = 0x01;
// 0x04 this game plays 20XX Melee 1.03's ruleset (MU_CONTROL_20XX103). A peer without it is never
// offered the ruleset: its game would play the match without it. (0x02 is kept for voice chat.)
constexpr uint8_t MU_BUILD_CAPS4_20XX103 = 0x04;
// 0x08 an arena room's frozen setup (Pro AI or 20XX bots) plays the stage the people picked, the first
// person's by port as in a plain Direct match. Only when every peer sends it: the stage is in the setup
// hash, so a build that keeps the room's own draw would refuse the match.
constexpr uint8_t MU_BUILD_CAPS4_ARENA_STAGE_PICK = 0x08;
// First capability byte bit Melee Unlocked 0.9.215 and 0.9.216 read as warm-up (their 0xE5 messages).
constexpr uint8_t MU_BUILD_CAPS_UPSTREAM_WARMUP = 0x80;

// sf::Packet wire format: integers big-endian, bool as one byte, strings as u32 length + bytes.
class Packet {
 public:
  Packet() = default;
  Packet(const uint8_t* data, size_t size) : buf_(data, data + size) {}
  void append(const void* data, size_t size);
  const uint8_t* data() const { return buf_.data(); }
  size_t size() const { return buf_.size(); }
  bool ok() const { return ok_; }
  explicit operator bool() const { return ok_; }
  Packet& operator<<(uint8_t v);
  Packet& operator<<(bool v) { return *this << (uint8_t)(v ? 1 : 0); }
  Packet& operator<<(uint16_t v);
  Packet& operator<<(uint32_t v);
  Packet& operator<<(int32_t v) { return *this << (uint32_t)v; }
  Packet& operator<<(const std::string& s);
  Packet& operator>>(uint8_t& v);
  Packet& operator>>(bool& v);
  Packet& operator>>(uint16_t& v);
  Packet& operator>>(uint32_t& v);
  Packet& operator>>(int32_t& v);
  Packet& operator>>(std::string& s);
 private:
  bool check(size_t n);
  std::vector<uint8_t> buf_;
  size_t pos_ = 0;
  bool ok_ = true;
};

struct Pad {
  int32_t frame = 0, checksum_frame = 0;
  uint32_t checksum = 0;
  uint8_t buf[PAD_FULL_SIZE] = {};
  explicit Pad(int32_t f) : frame(f) {}
  Pad(int32_t f, const uint8_t* data) : frame(f) { for (int i = 0; i < PAD_WIRE_SIZE; ++i) buf[i] = data[i]; }
  Pad(int32_t f, int32_t cf, uint32_t c, const uint8_t* data) : Pad(f, data) { checksum_frame = cf; checksum = c; }
};

struct RemotePadOutput {
  int32_t latest_frame = 0, checksum_frame = 0;
  uint32_t checksum = 0;
  uint8_t player_idx = 0;
  bool is_disconnected = false;
  bool checksum_primary = true; // compare once per machine, though every port carries the checksum
  int32_t despawn_frame = 0;   // the frame a dropped player's fighter despawns on, 0 until settled
  std::vector<uint8_t> data;
};


struct PlayerSelections {
  uint8_t player_idx = 0, character_id = 0, character_color = 0, team_id = 0;
  bool is_character_selected = false;
  uint16_t stage_id = 0;
  bool is_stage_selected = false;
  uint8_t alt_stage_mode = 0;
  uint32_t rng_offset = 0;
  int message_id = 0;
  bool error = false;
  void Merge(const PlayerSelections& s) {
    rng_offset = s.rng_offset;
    if (s.is_stage_selected) { stage_id = s.stage_id; is_stage_selected = true; alt_stage_mode = s.alt_stage_mode; }
    if (s.is_character_selected) { character_id = s.character_id; character_color = s.character_color; team_id = s.team_id; is_character_selected = true; }
  }
  void Reset() { character_id = character_color = team_id = 0; is_character_selected = false; stage_id = 0; is_stage_selected = false; rng_offset = 0; }
};

struct MatchInfo {
  PlayerSelections local;
  PlayerSelections remote[REMOTE_PLAYER_MAX];
  void Reset() { local.Reset(); for (auto& r : remote) r.Reset(); }
};

struct UserInfo {
  std::string uid, play_key, display_name, connect_code, latest_version;
  int port = 0;
  std::vector<std::string> chat_messages;
  bool is_bot = false;
  // What the matchmaking service says of the player's rank (Slippi 3.7.0 records it in the replay).
  float ranked_rating = 0, ranked_mmr = 0;
  int ranked_update_count = 0, ranked_global_placement = 0, ranked_regional_placement = 0;
};

// The user record written by the Slippi Launcher (user.json in the Slippi user folder).
class User {
 public:
  // slippi_only loads a Slippi login without replacing the menu's YouGame identity.
  // Under YouGame this reads only its explicit grant; standalone keeps upstream discovery.
  explicit User(std::string user_dir, bool slippi_only = false);
  // Every YouGame Slippi path uses only the account explicitly granted by its app.
  // No ambient launcher discovery or server request when the grant is absent.
  static std::unique_ptr<User> GrantedSlippiUser(const std::string& user_dir);
  // A search's own copy of the menu's signed-in Slippi login (the launcher's Both Unranked): the same
  // account with its own version check, read from no file and refreshed from no server. Null when
  // `from` is not signed in.
  static std::unique_ptr<User> SearchCopy(const User& from);
  bool AttemptLogin(bool rediscover = false);  // Log In rechecks shared launcher folders too.
  bool IsLoggedIn() const { return logged_in_; }
  UserInfo GetUserInfo() const { std::lock_guard<std::mutex> lock(mutex_); return info_; }
  void LogOut();
  void OverwriteLatestVersion(const std::string& v) { std::lock_guard<std::mutex> lock(mutex_); info_.latest_version = v; }
  std::vector<std::string> GetUserChatMessages() const;
  static std::vector<std::string> GetDefaultChatMessages();
  const std::string& dir() const { return dir_; }
  // Refreshes display name, connect code, latest version and chat messages from Slippi's user
  // API on a background thread. user.json is only rewritten by the Slippi Launcher at login, so a
  // name changed on the website since then is stale in it; the server's copy is what the opponent
  // and the in-game name tag already show.
  void RefreshFromServer();
  // Fetch the current server version, with the existing offline-account fallback.
  using VersionFetch = bool (*)(const std::string&, std::string*, int*);
  bool CheckOnlineCompatibility(std::string* error, VersionFetch fetch = nullptr);
  // One verification shared by a search, its peer connection and first game. A reset starts
  // fresh verification for the next search/rematch, including after a cancelled request.
  bool CheckSessionCompatibility(std::string* error, VersionFetch fetch = nullptr);
  // Game thread only: 0 checking, 1 approved, -1 refused. Also covers rematches which reuse
  // a peer connection. Checking never blocks the character-select/render thread.
  int PollOnlineCompatibility(std::string* error, VersionFetch fetch = nullptr);
  void ResetOnlineCompatibility() { ++compatibility_generation_; compatibility_done_ = false; }
 private:
  struct Copy {};
  User(Copy, const User& from);
  std::string dir_;
  bool slippi_only_ = false;
  std::string account_file_;
  bool account_discovered_ = false;
  UserInfo info_;
  bool logged_in_ = false;
  mutable std::mutex mutex_;
  std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
  bool compatibility_done_ = false, compatibility_ok_ = false;
  std::atomic<uint64_t> compatibility_generation_{0};
  std::string compatibility_error_;
  std::mutex compatibility_request_mutex_;
  uint64_t verified_generation_ = ~uint64_t{0};
  bool verified_ok_ = false;
  std::string verified_error_;
  bool CheckSessionCompatibilityAt(uint64_t generation, std::string* error, VersionFetch fetch);
  // Destroyed before the fields used by their workers: no detached access to a destroyed User.
  std::future<void> refresh_;
  struct VersionResult { bool ok; std::string error; uint64_t generation; };
  std::future<VersionResult> compatibility_;
};

// Connect-code history (direct-codes.json / teams-codes.json in the user folder).
class DirectCodes {
 public:
  DirectCodes(std::string path);
  std::string get(int index) const;
  int length() const { return (int)codes_.size(); }
  void AddOrUpdateCode(const std::string& code);
 private:
  void Load();
  void Save();
  std::string path_;
  std::vector<std::string> codes_;
};

class NetplayClient {
 public:
  enum class ConnectStatus { UNSET, INITIATED, CONNECTED, FAILED, DISCONNECTED };
  enum class DisconnectReason : uint32_t { UNSPECIFIED = 0, POOR_PERFORMANCE = 1
#if MU_SQUAD
    // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
    , SQUAD_PROTOCOL = 2
#endif
  };

  NetplayClient(std::vector<std::string> addrs, std::vector<uint16_t> ports, uint8_t remote_player_count, uint16_t local_port,
                bool is_decider, uint8_t player_idx, bool own_udp = false, std::vector<std::string> public_fallbacks = {}
#if MU_SQUAD
                // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
                , bool squad_search = false, MuSquadSide squad_side = {}, bool squad_friend_room = false
#endif
                , const yougame::lobby::Plan* lobby_plan = nullptr);
  ~NetplayClient();

  bool IsDecider() const { return is_decider_; }
  uint8_t LocalPlayerPort() const { return player_idx_; }
  ConnectStatus GetSlippiConnectStatus() const { return status_.load(std::memory_order_acquire); }
  // The opponent came from YouGame's rooms: SendBuild's first capability byte keeps YouGame's meanings.
  void set_yougame_peer(bool on) { yougame_peer_.store(on); }
  bool OwnUDPPortUnavailable() const { return own_udp_port_unavailable_; }
  std::vector<int> GetFailedConnections() const { return failed_connections_; }
  void StartSlippiGame();
  void SendSlippiPad(std::unique_ptr<Pad> pad, std::unique_ptr<Pad> partner = nullptr);
  int RemoteMachineCount() const { return remote_player_count_; }
  int RemotePortCount() const;
  int SecondLocalPort() const;
  bool IsLocalVirtualPort(int index) const;
  void SetMatchSelections(PlayerSelections& s);
#if MU_SQUAD
  // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
  void SetSquadSelection(const MuSquadSide& side);
  bool SquadSetup(MuSquadSetup* out);
  bool SquadCancelled() const { return squad_cancelled_.load(); }
  // A friend room this side joined in Squad Strike because the friend picked it (their MU_BUILD came
  // with MU_SQUAD_CAP); the game's character select asks for it with MU_SQUAD_OP_FRIEND.
  bool SquadAdopted() const { return squad_adopted_.load(); }
  // A friend room whose friend's build has not arrived yet: it may still turn the room into Squad
  // Strike, so nobody locks in until it does (the joiner's lock-in is held, never sent plain).
  // Reads the build mark before squad_search_: an adoption stores squad_adopted_, squad_search_, then
  // the mark, so "not waiting" here always comes with the adoption already visible.
  bool SquadFriendWaiting() const { return squad_friend_room_ && !squad_peer_build_.load() && !squad_search_.load(); }
  // This game can't load Squad Strike (memory, disc identity): a build without either squad bit
  // tells the friend, whose game cancels the room on screen.
  void SquadFriendRefuse();
  // What the player is told when a squad match is refused (set before CancelSquad).
  const char* SquadCancelText() const { return squad_cancel_text_.load(); }
  void CancelSquad(const char* reason);
  // Cancel with this text for the player (a static string) instead of the generic one.
  void RefuseSquad(const char* text, const char* reason);
  bool SquadPeerDisconnect(uint32_t reason);
#endif
  void ResendSelections();   // the same selection again, with a fresh Gecko lock
  std::unique_ptr<RemotePadOutput> GetSlippiRemotePad(int index, int max_frame_count, int32_t frame = INT32_MAX);
  void DropOldRemoteInputs(int32_t finalized_frame);
  std::unordered_map<uint8_t, bool> GetActivePlayerIndices() const;
  void ForceDisconnectPlayer(uint8_t player_idx);
  void ForceDisconnect(DisconnectReason reason = DisconnectReason::UNSPECIFIED);
  DisconnectReason GetDisconnectReason() const { return (DisconnectReason)disconnect_reason_.load(std::memory_order_acquire); }
  // A copy: the network thread merges the remote players' selections while the game reads them.
  MatchInfo GetMatchInfo() { std::lock_guard<std::mutex> lk(selection_mutex_); return match_info_; }
  PlayerSelections GetSlippiRemoteChatMessage(bool chat_enabled);
  uint8_t GetSlippiRemoteSentChatMessage(bool chat_enabled);
  int32_t CalcTimeOffsetUs();
  // Late controller read: how many ms before now the remote pad for `frame` arrived, taken from the
  // newest pad received and the 59.94 Hz the pads are sent at (negative: it is still on its way). The
  // least over the remote players; false before any pad of this game has arrived.
  bool RemotePadEarlyMs(int32_t frame, double* early_ms);
  double GetAndResetAvgPingMs();
  // Once a dropped player's last input is settled it repeats up to `frame` (Slippi 3.7.0).
  void FillDroppedPlayerInputs(int32_t frame);
  int LastPingMs() const { return (int)(last_ping_ms_.load(std::memory_order_relaxed)); }
  // Each remote machine's last ping (Teams has up to three), and how many there are. UI thread safe.
  int RemotePingMs(int index) const {
    return index >= 0 && index < REMOTE_PLAYER_MAX ? (int)remote_ping_ms_[index].load(std::memory_order_relaxed) : 0;
  }
  int RemoteCount() const { return (int)remote_count_seen_.load(std::memory_order_relaxed); }
  // The median of the last pre-match pings (NP_MSG_MU_PING) to the first opponent, 0 if none yet.
  int PreMatchPingMs() const { return (int)(prematch_ping_ms_.load(std::memory_order_relaxed)); }
  // The connection's own round trip estimate (ENet's, from its reliable packets and keep-alive pings),
  // the longest over the opponents; 0 before there is one. It starts high and settles over the first
  // seconds of a connection, so early on it reads longer than the truth, never shorter. It is the only
  // ping there is before the first game against Slippi Dolphin, which answers no pre-match ping.
  int ConnectionPingMs() const { return (int)(connection_ping_ms_.load(std::memory_order_relaxed)); }
  void SendChatMessage(int message_id);
  void SendGamePrepStep(const GamePrepStep& step);
  std::array<uint8_t, 6> FetchGamePrepStep(uint8_t index) { return game_prep_.fetch(index); }
  void SendAsync(std::unique_ptr<Packet> packet);
  uint8_t remote_sent_chat_message_id = 0;
  // The opponents' builds (NP_MSG_MU_BUILD), and when the connection completed. early_pads: the
  // build takes early inputs (a trailing byte older builds never sent and never read).
  struct RemoteBuild { bool received = false, mod_view = false; std::string fingerprint, name; bool early_pads = false, pings = false;
                       uint32_t shield_rgb = 0;   // their own shield colour, 0x1000000 | RGB, 0 none sent
                       uint8_t port = 0xFF;
                       bool static_engine = false; std::string ranked_rules_digest;   // their in-game slot
                       bool setup_hash = false;   // they send NP_MSG_MU_SETUP_HASH and wait for ours
                       bool pick_previews = false;   // takes NP_MSG_MU_PICK_PREVIEW
                       uint8_t caps2 = 0;   // the second capability byte (mu_friends_lobby.h), 0 when absent
                       // Which MU_BUILD layout the peer speaks: 0 Melee Unlocked 0.9.200 to 0.9.216 (one byte:
                       // 0x10 arena, 0x20 automatic L-cancel, 0x40 Gecko, 0.9.215 on 0x80 warm-up; arena, Gecko and
                       // warm-up ride ids 0xE3, 0xE4 and 0xE5), 1 YouGame builds 1xx (two bytes, no warm-up),
                       // 2 this build and later (three bytes; warm-up when caps3 has MU_BUILD_CAPS3_WARMUP).
                       uint8_t layout = 2;
                       bool arena = false;         // takes arena room messages
                       bool auto_lcancel = false;     // presses for L-cancels this session (automatic L-cancel)
                       bool gecko = false;            // takes Gecko code messages (NP_MSG_MU_GECKO)
                       bool warmup = false;           // takes and sends warm-up messages (NP_MSG_MU_WARMUP)
                       bool pause_rule = false, no_pause = false;   // MU_BUILD_CAPS3_PAUSE_RULE / _NO_PAUSE
                       bool arena_ffa = false;        // MU_BUILD_CAPS3_ARENA_FFA
                       bool controls = false;         // MU_BUILD_CAPS4_CONTROLS (NP_MSG_MU_CONTROLS)
                       bool melee103 = false;         // MU_BUILD_CAPS4_20XX103
                       bool arena_stage_pick = false; // MU_BUILD_CAPS4_ARENA_STAGE_PICK
                       int8_t port_colour[2] = {-1, -1}; };   // their port colour choices (1.0.2 on), -1 none
  // Warm-up (slippi_warmup.h): tell every opponent whose build takes it that this player is warming up
  // in Training (true) or on the character select (false), and the character picked there (Slippi's
  // external id and costume, 0xFF none: Slippi itself sends a pick only at the lock-in). Any thread.
  void SendWarmup(bool warming, uint8_t character = 0xFF, uint8_t costume = 0);
  // Ends this connection at once, without the goodbye handshake (the peers are reset): the thread stops
  // and the destructor then closes the socket without waiting. For a pairing connection whose only job
  // was to meet the peer (an arena free-for-all of three): its port is wanted again right away, and
  // ENet's sockets share a port while an old one still lingers, taking the new one's packets.
  void Abandon();
  // Remote `index`'s pick as its last warm-up message said: character << 8 | costume, -1 none.
  int RemotePick(int index) const {
    return index >= 0 && index < REMOTE_PLAYER_MAX ? (int)remote_pick_[index].load(std::memory_order_relaxed) : -1;
  }
  // What remote `index` last said: 1 warming up, 0 on the character select, -1 never said (an older
  // build or Slippi Dolphin). UI thread safe.
  int RemoteWarmup(int index) const {
    return index >= 0 && index < REMOTE_PLAYER_MAX ? (int)remote_warmup_[index].load(std::memory_order_relaxed) : -1;
  }
  // The shield colour the opponent on in-game `slot` sent (0 none).
  uint32_t RemoteShieldColor(int slot);
  RemoteBuild GetRemoteBuild(int index);
  // Whether every remote machine has sent NP_MSG_MU_BUILD, which plain Slippi Dolphin never does.
  bool EveryRemoteReadsGoneOwnFrames();
  // The match setup hash (mu_setup_hash.h). SendSetupHash queues ours for every peer (any thread);
  // GetSetupHash gives remote `index`'s latest one.
  struct PeerSetupHash { bool received = false; MuSetupHash value{}; };
  void SendSetupHash(uint64_t inputs, uint64_t setup);
  PeerSetupHash GetSetupHash(int index);
  uint64_t ConnectedAtMs() const { return connected_at_ms_.load(std::memory_order_acquire); }
  // Early inputs. SendEarlyPad goes to the peers that take them (any thread: it only queues).
  // EarlyPads gives remote `index`'s early pads for frames after `latest` (its confirmed pads reach
  // `latest`; older entries are dropped), for the game's prediction.
  void SendEarlyPad(int32_t target, const uint8_t pad[host::early_input::kPadBytes]);
  // Slippi 3.7.0 input suggestion: this side's newest input between frames, for the players that do
  // not take this build's early pads (Slippi Dolphin, and the Static Recomp). Any thread: it only queues.
  void SendSlippiPadSuggestion(int32_t target_frame, uint32_t seq, const uint8_t pad[host::early_input::kPadBytes]);
  void EarlyPads(int index, int32_t latest, std::vector<host::early_input::Entry>& out);
  // Live pick previews (display only). SendPickPreview: the game thread, every CSS frame; sends
  // to the peers that take them, only while CONNECTED, only on change, at most one per
  // MU_PICK_PREVIEW_INTERVAL frames. RemotePickPreview: remote `index`'s latest preview and its
  // port, false when none arrived since the last game start or that player is gone.
  void SendPickPreview(const MuPickPreview& pick, uint32_t frame);
  bool RemotePickPreview(int index, MuPickPreview* out, uint8_t* port);
  // Arena room links (one remote). SendArena queues one message for a peer that takes them (false
  // when it does not, or before its build message arrived); `bulk` sends it on its own reliable
  // channel so the live feed never holds up room messages. PopArena takes the oldest received.
  bool ArenaReady();
  // Direct pausing is off for this match: someone turned it off and every peer's build reads the rule.
  bool PauseOff();
  // The port colour choices this side sent and remote 0's (1v1), -1 none. False before both are known.
  bool PortColours(int8_t mine[2], int8_t theirs[2]);
  bool SendArena(const uint8_t* data, size_t size, bool bulk);
  bool PopArena(std::vector<uint8_t>& out);
  // Gecko codes for the match (one remote): SendGecko queues one gecko_online message for a peer that
  // takes them (false otherwise); PopGecko takes the oldest received.
  bool SendGecko(const std::vector<uint8_t>& message);
  bool PopGecko(std::vector<uint8_t>& out);
  // Tap jump off and tilt stick (controls.h): SendControls queues one controls_wire message for every
  // peer whose build takes them (the send loop drops it for the rest); PopControls takes the oldest
  // received, already checked to come from the port it names.
  bool SendControls(const std::vector<uint8_t>& message);
  bool PopControls(std::vector<uint8_t>& out);
  bool yougame_peer() const { return yougame_peer_.load(); }

 private:
  struct FrameTiming { int32_t frame = 0; uint64_t time_us = 0; };
  struct FrameOffsetData { int idx = 0; std::vector<int32_t> buf; };
  struct ActiveConnectionInfo { uint8_t player_idx = 0; bool is_disconnected = false; };

  // Connection index, used only by machine-scoped messages and clocks.
  uint8_t PlayerIdxFromPort(uint8_t port) const {
    for (int i = 0; i < remote_player_count_; ++i) if (match_info_.remote[i].player_idx == port) return (uint8_t)i;
    return REMOTE_PLAYER_MAX;
  }
  int SenderPort(_ENetPeer* peer) const;
  void OnData(Packet& packet, _ENetPeer* peer);
  void Send(Packet& packet);
  void Disconnect();
  void ThreadFunc();
  bool AreAllPeersDisconnectedForKey(const std::string& key);
  // Player drops (Slippi 3.7.0), network thread: the others are told which inputs this side has of a
  // dropped player, and after a short wait everyone settles on the same last input and despawn frame.
  bool ProcessPlayerDrops();
  void SendPlayerDropReport(uint8_t port);
  void AddPlayerDropReportInputs(uint8_t port, int32_t last_frame, uint8_t count, const uint8_t* inputs);
  bool AreAllConnectionsDisconnected();
  void WriteSelections(Packet& p, const PlayerSelections& s);
  std::unique_ptr<PlayerSelections> ReadSelections(Packet& p);
  std::unique_ptr<PlayerSelections> ReadChatMessage(Packet& p);

  GamePrepQueue game_prep_;
  std::mutex pad_mutex_, ack_mutex_, async_mutex_;
  // timing_mutex_: last_frame_timing_, has_game_started_, frame_offset_data_ (network thread and game
  // thread). chat_mutex_: remote_chat_message_selection_. selection_mutex_: match_info_ (the network
  // thread merges remote selections, the game thread reads, resets and sets its own; the player_idx
  // fields are written once in the constructor and read without it).
  // Selection receipt takes selection_mutex_, timing_mutex_ and pad_mutex_ together so its
  // pre-start decision and queue reset are atomic. Pad sends take pad_mutex_ before
  // ack_mutex_ and async_mutex_; chat replies take chat_mutex_ before async_mutex_.
  // Neither ACK receipt nor the async drain takes pad_mutex_.
  // async_mutex_ is never held while taking another, so no two are ever taken in opposite orders.
  // SetMatchSelections also queues a lock-in's pick preview under selection_mutex_; that takes the
  // leaves build_mutex_ and pick_mutex_ one at a time, never selection_mutex_ under them.
  std::mutex timing_mutex_, chat_mutex_, selection_mutex_;
#ifdef YOUGAME_AGENT_TEST_HOOK
  struct AsyncPacket {
    std::unique_ptr<Packet> packet;
    uint64_t enqueued_ms, generation;
  };
  std::deque<AsyncPacket> async_queue_;
  std::atomic<uint64_t> net_test_generation_{0};
  // Serializes generation changes against due sends. May precede build_mutex_
  // via Send(), and is never held with async_mutex_.
  std::mutex net_test_generation_mutex_;
#else
  std::deque<std::unique_ptr<Packet>> async_queue_;
#endif
  _ENetHost* client_ = nullptr;
  std::unique_ptr<yougame::udp::EnetIngress> own_udp_ingress_;
  bool own_udp_port_unavailable_ = false;
  std::vector<_ENetPeer*> server_;
  std::vector<std::string> public_fallbacks_; // parallel to remote seats, never additional players
  std::thread thread_;
  uint8_t remote_player_count_ = 0; // real remote machines: server_, ACKs, clocks, builds and setup hashes
  std::atomic<bool> do_loop_{true};
  bool is_decider_ = false, has_game_started_ = false;
  std::array<bool, REMOTE_PLAYER_MAX> remote_game_started_{};   // under timing_mutex_
  uint8_t player_idx_ = 0;
  std::unordered_map<std::string, std::map<_ENetPeer*, ActiveConnectionInfo>> active_connections_;
  std::atomic<bool> player_active_[PLAYER_COUNT_MAX] = {};
  std::deque<std::unique_ptr<Pad>> local_pad_queue_;
  std::unique_ptr<PortInputs> port_inputs_;
  bool machine_pad_format_ = false;
  // Each remote port's latest inputs, newest first: what another client may be missing if the
  // player drops (Slippi 3.7.0). By port, like port_inputs_->pads; under pad_mutex_.
  std::deque<Pad> remote_pad_history_[PLAYER_COUNT_MAX];
  std::atomic<int32_t> despawn_frames_[PLAYER_COUNT_MAX] = {};   // by port, 0 until settled
  uint64_t player_drop_times_ms_[PLAYER_COUNT_MAX] = {};         // network thread only
  struct ChecksumEntry { int32_t frame = 0; uint32_t value = 0; } remote_checksums_[REMOTE_PLAYER_MAX];
  uint64_t ping_us_[REMOTE_PLAYER_MAX] = {};
  std::atomic<uint64_t> ping_sample_sum_us_{0}, ping_sample_count_{0};
  std::atomic<uint32_t> last_ping_ms_{0};
  std::atomic<uint32_t> remote_ping_ms_[REMOTE_PLAYER_MAX] = {};
  std::atomic<uint8_t> remote_count_seen_{0};
  std::atomic<uint32_t> prematch_ping_ms_{0};
  std::atomic<uint32_t> connection_ping_ms_{0};
  std::deque<uint32_t> prematch_samples_;   // network thread only
  void SendPrematchPing();
  int32_t last_frame_acked_[REMOTE_PLAYER_MAX] = {};
  FrameOffsetData frame_offset_data_[REMOTE_PLAYER_MAX];
  FrameTiming last_frame_timing_[REMOTE_PLAYER_MAX];
  FrameTiming remote_arrival_[REMOTE_PLAYER_MAX];   // timing_mutex_: the newest pad frame received and when
  std::deque<FrameTiming> ack_timers_[REMOTE_PLAYER_MAX];
  std::atomic<ConnectStatus> status_{ConnectStatus::UNSET};
  std::atomic<uint32_t> pending_disconnect_reason_{0}, disconnect_reason_{0};
  std::vector<int> failed_connections_;
  MatchInfo match_info_;
  std::unique_ptr<PlayerSelections> remote_chat_message_selection_;
  std::mutex build_mutex_;
  RemoteBuild remote_build_[REMOTE_PLAYER_MAX];
  PeerSetupHash setup_hash_[REMOTE_PLAYER_MAX];   // under build_mutex_
  std::atomic<uint64_t> connected_at_ms_{0};
  host::early_input::Buffer early_pads_[REMOTE_PLAYER_MAX];   // under pad_mutex_
  std::mutex arena_mutex_;                       // arena_inbox_ (network thread in, game thread out)
  std::deque<std::vector<uint8_t>> arena_inbox_;
  std::mutex gecko_mutex_;                       // gecko_inbox_ (network thread in, game thread out)
  std::deque<std::vector<uint8_t>> gecko_inbox_;
  std::mutex controls_mutex_;                    // controls_inbox_ (network thread in, game thread out)
  std::deque<std::vector<uint8_t>> controls_inbox_;
  std::atomic<int8_t> remote_warmup_[REMOTE_PLAYER_MAX] = {-1, -1, -1, -1, -1};   // NP_MSG_MU_WARMUP, by remote index
  std::atomic<bool> yougame_peer_{false};
  std::atomic<int32_t> remote_pick_[REMOTE_PLAYER_MAX] = {-1, -1, -1, -1, -1};    // and the pick it carried
  void SendBuild();
  // Pick previews. pick_mutex_ is a leaf: never held while taking another lock (SendAsync and
  // build_mutex_ included). It guards everything below it here.
  void QueuePickPreview(MuPickPreview pick, uint32_t frame, bool locked_now);
  void SendLockedPickPreview();
  std::mutex pick_mutex_;
  MuPickPreviewSender pick_sender_{};
  MuPickPreview pick_local_{};
  bool pick_local_set_ = false;
  uint8_t pick_sent_peers_ = 0;   // remote indices that took previews at the last send
  MuPickPreview pick_remote_[REMOTE_PLAYER_MAX] = {};
  uint8_t pick_remote_port_[REMOTE_PLAYER_MAX] = {};
  bool pick_remote_set_[REMOTE_PLAYER_MAX] = {};
#if MU_SQUAD
  // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
  // squad_search_ starts as the chosen search; in a friend room it turns on (never off) when the
  // friend's build arrives with MU_SQUAD_CAP, so it is read across threads.
  std::atomic<bool> squad_search_{false};
  bool squad_normal_logged_ = false, squad_friend_room_ = false;
  std::atomic<bool> squad_advertised_{false}, squad_peer_cap_{false}, squad_cancelled_{false}, squad_adopted_{false};
  // Friend rooms: the friend's build arrived (and whether it said it joins Squad Strike); a locked
  // selection went either way or a game started (a late Squad Strike cancels, never adopts); this
  // game refused Squad Strike (SquadFriendRefuse).
  std::atomic<bool> squad_peer_build_{false}, squad_peer_friend_cap_{false}, squad_locked_seen_{false},
      squad_refused_{false};
  std::atomic<bool> sent_no_pause_{false};   // this side's MU_BUILD_CAPS3_NO_PAUSE as last sent
  std::atomic<int> sent_port_colour_{-1};     // this side's port colour choices as last sent (first | second << 8)
#ifdef YOUGAME_AGENT_TEST_HOOK
  // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
  bool squad_test_late_lineup_ = false; // guarded by selection_mutex_
#endif
  MuSquadSide squad_local_{}, squad_transmitted_{}, squad_peer_{}, squad_incoming_{}; // selection_mutex_
  bool squad_peer_pending_ = false, squad_peer_selected_ = false;
  bool squad_peer_other_version_ = false; // the peer's last 0xE3 was another MU_SQUAD_VERSION; selection_mutex_
  std::atomic<const char*> squad_cancel_text_{"Squad Strike setup didn't match. Match not started."};
#endif
};

class Matchmaking {
 public:
  // Slippi's mode ids. Ranked is backed by YouGame when launched by its app.
  enum OnlinePlayMode { RANKED = 0, UNRANKED = 1, DIRECT = 2, TEAMS = 3, PARTY = 4 };
  enum ProcessState { IDLE, INITIALIZING, MATCHMAKING, OPPONENT_CONNECTING, CONNECTION_SUCCESS, ERROR_ENCOUNTERED };
  struct MatchSearchSettings { OnlinePlayMode mode = UNRANKED; std::string connect_code; std::vector<std::string> blocked_codes; std::string friend_room;
    // The 4-player Friends lobby (yougame_lobby.h): a YouGame room held by the host or joined from an
    // invite (lobby_room), driven by the 4-door select through lobby_control. Never a Slippi search.
    bool friends_lobby = false;
    std::string lobby_room;
    std::shared_ptr<yougame::lobby::Control> lobby_control;
#if MU_SQUAD
    // Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, used with the author's permission (11-03-2026)
    bool squad = false;
    MuSquadSide squad_side{};
    std::string squad_budget;
#endif
    // Slippi's Unranked plays this game's gameplay (vanilla, native_online_policy.h); set by the
    // online menu's search. Only the launcher's Both Unranked reads it: false searches YouGame alone.
    bool slippi_gameplay = false;
    // An arena game under YouGame (slippi_arena.h): the arena's own room carries it, and these are its
    // fighters' YouGame player ids in ring order (2 to 4). Empty for every other search.
    std::vector<std::string> arena_fighters;
    // An arena free-for-all (slippi_arena.h): a Teams search whose match is made with teams off, three
    // or four fighters each their own team, the stage drawn from the doubles list.
    bool arena_ffa = false;
  };
  // True when an opponent in the match is on the Unranked block list (codes compared ignoring case
  // and spaces). Public for the unit test.
  static bool IsBlockedMatch(OnlinePlayMode mode, const std::vector<std::string>& blocked,
                             const std::vector<UserInfo>& players, int local_index);
  struct MatchmakeResult { std::string id; std::vector<UserInfo> players; std::vector<uint16_t> stages; uint32_t items = 0; };
  // The match a get-ticket-resp describes, read without trusting any of it. ok is false (and error
  // says why) unless there are 2 to 4 players, every port is 1..4 and used once, and one of them is
  // the local player; then no count or index taken from it can pass the fixed per-player arrays.
  struct ParsedTicket {
    bool ok = false;
    std::string error;
    std::string match_id;
    std::vector<UserInfo> players;          // in the ticket's order
    int local_player_index = 0;             // the local player's port - 1
    std::vector<std::string> remote_ips;    // "ip:port" per remote, in the ticket's order
    std::vector<std::string> public_fallbacks; // same shape; empty when no same-router fallback
    bool is_host = false;
    std::vector<uint16_t> stages;
    uint32_t items = 0;
  };
  // Pure parsing of a get-ticket-resp body (JSON text). Never throws; no sockets, no state.
  static ParsedTicket ParseTicket(const std::string& json_text);
  // Local test peering (no matchmaking server): fixed player index, local port and the address of
  // every other player ("ip:port", in player-index order with the local player left out). One
  // remote is a two-player match; three is a four-player (Teams) match.
  struct LocalPeer {
    bool enabled = false;
    int local_index = 0;
    uint16_t local_port = 0;
    std::vector<std::string> remotes;
    int test_stage = -1;  // deterministic stage selection for local regression runs only
    // The remotes' names and connect codes (an arena free-for-all of three, slippi_arena.cpp mesh), one
    // per remote; empty: the harness's "Peer n".
    std::vector<std::string> names, codes;
    // Each machine's game port, in the order above (this machine at local_index): a match with Pro AI
    // seats leaves each owner's next port to its AI (0 and 2 for two people, 1 and 3 their AIs). Empty:
    // the machines take ports 0 to n-1.
    std::vector<int> ports;
  };

  explicit Matchmaking(User* user);
  ~Matchmaking();
  void FindMatch(MatchSearchSettings settings);
  // This search's own local port (arena room links), instead of forced_port or a random one.
  void SetLocalPort(uint16_t port) { port_override_ = port; }
  // An arena room's link or pair search (slippi_online arena_matchmaking): never the match a room's frozen
  // setup is for, so its connection is a plain one.
  void SetRoomLink(bool on) { room_link_ = on; }
  ProcessState GetMatchmakeState() const { return state_; }
  std::string GetErrorMessage() const { std::lock_guard<std::mutex> lk(result_mutex_); return error_msg_; }
  bool IsSearching() const { return state_ == INITIALIZING || state_ == MATCHMAKING || state_ == OPPONENT_CONNECTING; }
  std::unique_ptr<NetplayClient> GetNetplayClient() { std::lock_guard<std::mutex> lk(result_mutex_); return std::move(netplay_client_); }
  int LocalPlayerIndex() const { std::lock_guard<std::mutex> lk(result_mutex_); return local_player_index_; }
  std::vector<UserInfo> GetPlayerInfo() const { std::lock_guard<std::mutex> lk(result_mutex_); return player_info_; }
  // The other players' addresses ("ip:port") as the match named them, in player-index order with the
  // local player left out; empty before a match is found. An arena free-for-all of three pairs its
  // fighters up by Direct searches and plays a match made from these (Matchmaking::local_peer).
  std::vector<std::string> RemoteAddresses() const { std::lock_guard<std::mutex> lk(result_mutex_); return remote_ips_found_; }
  display::Provider DisplayProvider() const { return display_provider_.load(); }
  std::string GetPlayerName(uint8_t port) const;
  std::vector<uint16_t> GetStages() const { std::lock_guard<std::mutex> lk(result_mutex_); return allowed_stages_; }
  // Never more than REMOTE_PLAYER_MAX: callers index fixed per-remote arrays with it.
  uint8_t RemotePlayerCount() const {
    std::lock_guard<std::mutex> lk(result_mutex_);
    const size_t n = player_info_.empty() ? 0 : player_info_.size() - 1;
    return (uint8_t)(n < (size_t)REMOTE_PLAYER_MAX ? n : (size_t)REMOTE_PLAYER_MAX);
  }
  // True when every player but the local one is a bot (asked every frame of a match: no copy).
  bool AllRemotesAreBots() const {
    std::lock_guard<std::mutex> lk(result_mutex_);
    for (size_t i = 0; i < player_info_.size(); ++i) { if ((int)i == local_player_index_) continue; if (!player_info_[i].is_bot) return false; }
    return true;
  }
  MatchmakeResult GetMatchmakeResult() const { std::lock_guard<std::mutex> lk(result_mutex_); return mm_result_; }
  // As Slippi's: Ranked plays Unranked's rules (legal characters, the stage list). Standalone Melee
  // Unlocked never reaches Ranked (native_online_policy.h); YouGame mode does.
  static bool IsFixedRulesMode(OnlinePlayMode m) { return m == UNRANKED || m == RANKED || m == PARTY; }
  static LocalPeer local_peer;          // set from the command line for local 2-4 instance tests
  static uint16_t forced_port;          // 0 = random 41000..50999
  // False in hidden or scripted runs unless --allow-matchmaking: an automated run must never
  // queue on Slippi's servers by accident (local peering is unaffected).
  static bool server_allowed;

 private:
  void MatchmakeThread();
  void startMatchmaking();
  // status: the Unranked status line while it searches (yougame_blend.h kSearch*); null for "searching".
  void startYouGameMatchmaking(const char* status = nullptr);
  bool youGameSearch(yougame::Search* search);     // false: the search has failed (an unreadable code)
  void takeYouGameMatch(const yougame::Match& match);
  // Blended Unranked (yougame_blend.h): YouGame's and Slippi's searches at once, first opponent wins.
  void blendMatchmaking();
  // Slippi's matchmaking client up to a queued ticket: "" once queued, else why not. `stop` ends it
  // early; `avoid` is a port it must not take (the blend's YouGame port).
  std::string openSlippiTicket(const UserInfo& me, const std::function<bool()>& stop, int avoid);
  void dropSlippi();                       // ends Slippi's matchmaking client now
  void handleMatchmaking();
  void handleConnecting();
  void disconnectFromServer();
  void terminateMmConnection();
  struct Ticket;                          // a get-ticket-resp body (defined in slippi_net.cpp)
  bool ingest_ticket(const Ticket& ticket);   // false: the ticket was refused and the search has failed
  void fail(const std::string& message);      // the search ends with this error text

  User* user_;
  // Separate from the menu's YouGame identity; never passed to YouGame's bridge or reporter.
  std::unique_ptr<User> slippi_user_;
  // Matchmaking thread publishes the selected route; only display code consumes it.
  std::atomic<display::Provider> display_provider_{display::Provider::Unknown};
  _ENetHost* client_ = nullptr;
  _ENetPeer* server_ = nullptr;
  bool is_mm_connected_ = false;
  // Slippi 3.7.0: the search runs on the WebSocket matchmaking service (slippi_mm_service.h). client_
  // is then only the socket bound to the netplay port: STUN servers are asked through it for its
  // public address, and one of them is sent a packet now and then so the NAT mapping the opponent
  // will use does not expire while queued.
  std::unique_ptr<mm_service::Session> mm_session_;
  uint32_t stun_host_ = 0;        // the STUN server the keepalives go to, network byte order
  uint16_t stun_port_ = 0;
  bool has_stun_addr_ = false;
  uint64_t last_keepalive_ms_ = 0;
  bool stunBindingRequest(uint32_t host, uint16_t port, mm_service::StunObservation* result);
  void sendStunKeepalive();
  std::atomic<bool> is_mm_terminated_{false};
  std::thread thread_;
  MatchSearchSettings search_settings_;
  std::atomic<ProcessState> state_{IDLE};
  // result_mutex_: what the game thread reads of a search while the matchmaking thread works:
  // error_msg_, local_player_index_, mm_result_, player_info_, allowed_stages_, netplay_client_.
  // The matchmaking thread builds a result aside and swaps it in complete, and stores the error text
  // before the state that announces it. It is a leaf lock: nothing else is taken while it is held
  // (not the user record's mutex, none of the netplay client's), and no netplay client is created or
  // destroyed under it. The matchmaking thread is the only writer after FindMatch, so it reads its
  // own copies without the lock.
  mutable std::mutex result_mutex_;
  std::string error_msg_;
  int local_player_index_ = 0;
  MatchmakeResult mm_result_;
  std::vector<UserInfo> player_info_;
  std::vector<uint16_t> allowed_stages_;
  std::unique_ptr<NetplayClient> netplay_client_;
  // Matchmaking thread only.
  int host_port_ = 0;
  bool room_link_ = false;
  uint16_t port_override_ = 0;
  std::vector<std::string> remote_ips_;
  std::vector<std::string> remote_ips_found_;   // the same, under result_mutex_ (RemoteAddresses)
  std::vector<std::string> public_fallbacks_;
  bool is_host_ = false;
};

// Launched by the app on the base listing: tells it whether Unranked can use a Slippi login when the
// online menu first opens (yougame::slippi_link), read once off the game thread. Remixes send nothing.
void announce_slippi_link(const std::string& user_dir);

// Helpers shared with the EXI device.
uint64_t time_us();
uint64_t time_ms();
std::string utf8_to_shiftjis(const std::string& s);
std::string shiftjis_to_utf8(const std::string& s);
std::string convert_string_for_game(const std::string& input, int length);
std::string truncate_length_char(const std::string& input, int length);
std::string convert_connect_code_for_game(const std::string& input);
bool enet_ready();   // initializes ENet once

}  // namespace slippi
