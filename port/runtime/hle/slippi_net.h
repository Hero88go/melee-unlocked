// Slippi Online networking: SFML-compatible packets, the ENet netplay client, the matchmaking
// client and the user record. Ports of Dolphin's SlippiNetplay/SlippiMatchmaking/SlippiUser
// with the same wire formats, so the port can play against stock Slippi clients.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct _ENetHost;
struct _ENetPeer;

namespace slippi {

constexpr int ROLLBACK_MAX_FRAMES = 7;
constexpr int ONLINE_LOCKSTEP_INTERVAL = 30;
constexpr int REMOTE_PLAYER_MAX = 3;
constexpr int PLAYER_COUNT_MAX = REMOTE_PLAYER_MAX + 1;
constexpr int PAD_FULL_SIZE = 0xC;
constexpr int PAD_DATA_SIZE = 0x8;
constexpr uint8_t CHAT_MSG_CHAT_DISABLED = 0x10;
extern const char* const SLIPPI_SEMVER;   // netplay version this port speaks

enum NetMsg : uint8_t {
  NP_MSG_SLIPPI_PAD = 0x80, NP_MSG_SLIPPI_PAD_ACK = 0x81, NP_MSG_SLIPPI_MATCH_SELECTIONS = 0x82, NP_MSG_SLIPPI_CONN_SELECTED = 0x83,
  NP_MSG_SLIPPI_CHAT_MESSAGE = 0x84, NP_MSG_SLIPPI_COMPLETE_STEP = 0x85, NP_MSG_SLIPPI_SYNCED_STATE = 0x86,
  // Melee Unlocked only: the sender's build (retail or a mod's identity). Slippi Dolphin logs an
  // unknown id and ignores it, so it is safe to send to anyone.
  NP_MSG_MU_BUILD = 0xE0,
};

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
  Pad(int32_t f, const uint8_t* data) : frame(f) { for (int i = 0; i < PAD_DATA_SIZE; ++i) buf[i] = data[i]; }
  Pad(int32_t f, int32_t cf, uint32_t c, const uint8_t* data) : Pad(f, data) { checksum_frame = cf; checksum = c; }
};

struct RemotePadOutput {
  int32_t latest_frame = 0, checksum_frame = 0;
  uint32_t checksum = 0;
  uint8_t player_idx = 0;
  bool is_disconnected = false;
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
  void Merge(const PlayerSelections& s);
  void Reset();
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
};

// The user record written by the Slippi Launcher (user.json in the Slippi user folder).
class User {
 public:
  explicit User(std::string user_dir);
  bool AttemptLogin();          // (re)reads user.json
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
 private:
  std::string dir_;
  UserInfo info_;
  bool logged_in_ = false;
  mutable std::mutex mutex_;
  std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
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
  enum class DisconnectReason : uint32_t { UNSPECIFIED = 0, POOR_PERFORMANCE = 1 };

  NetplayClient(std::vector<std::string> addrs, std::vector<uint16_t> ports, uint8_t remote_player_count, uint16_t local_port,
                bool is_decider, uint8_t player_idx);
  ~NetplayClient();

  bool IsDecider() const { return is_decider_; }
  uint8_t LocalPlayerPort() const { return player_idx_; }
  ConnectStatus GetSlippiConnectStatus() const { return status_.load(std::memory_order_acquire); }
  std::vector<int> GetFailedConnections() const { return failed_connections_; }
  void StartSlippiGame();
  void SendSlippiPad(std::unique_ptr<Pad> pad);
  void SetMatchSelections(PlayerSelections& s);
  std::unique_ptr<RemotePadOutput> GetSlippiRemotePad(int index, int max_frame_count);
  void DropOldRemoteInputs(int32_t finalized_frame);
  std::unordered_map<uint8_t, bool> GetActivePlayerIndices() const;
  void ForceDisconnectPlayer(uint8_t player_idx);
  void ForceDisconnect(DisconnectReason reason = DisconnectReason::UNSPECIFIED);
  DisconnectReason GetDisconnectReason() const { return (DisconnectReason)disconnect_reason_.load(std::memory_order_acquire); }
  MatchInfo* GetMatchInfo() { return &match_info_; }
  PlayerSelections GetSlippiRemoteChatMessage(bool chat_enabled);
  uint8_t GetSlippiRemoteSentChatMessage(bool chat_enabled);
  int32_t CalcTimeOffsetUs();
  double GetAndResetAvgPingMs();
  int LastPingMs() const { return (int)(last_ping_ms_.load(std::memory_order_relaxed)); }
  void SendChatMessage(int message_id);
  void SendAsync(std::unique_ptr<Packet> packet);
  uint8_t remote_sent_chat_message_id = 0;
  // The opponents' builds (NP_MSG_MU_BUILD), and when the connection completed.
  struct RemoteBuild { bool received = false, mod_view = false; std::string fingerprint, name; };
  RemoteBuild GetRemoteBuild(int index);
  uint64_t ConnectedAtMs() const { return connected_at_ms_.load(std::memory_order_acquire); }

 private:
  struct FrameTiming { int32_t frame = 0; uint64_t time_us = 0; };
  struct FrameOffsetData { int idx = 0; std::vector<int32_t> buf; };
  struct ActiveConnectionInfo { uint8_t player_idx = 0; bool is_disconnected = false; };

  uint8_t PlayerIdxFromPort(uint8_t port) const { return port > player_idx_ ? port - 1 : port; }
  void OnData(Packet& packet, _ENetPeer* peer);
  void Send(Packet& packet);
  void Disconnect();
  void ThreadFunc();
  bool AreAllPeersDisconnectedForKey(const std::string& key);
  bool AreAllConnectionsDisconnected();
  void WriteSelections(Packet& p, const PlayerSelections& s);
  std::unique_ptr<PlayerSelections> ReadSelections(Packet& p);
  std::unique_ptr<PlayerSelections> ReadChatMessage(Packet& p);

  std::mutex pad_mutex_, ack_mutex_, async_mutex_;
  // timing_mutex_: last_frame_timing_, has_game_started_, frame_offset_data_ (network thread and game
  // thread). chat_mutex_: remote_chat_message_selection_.
  std::mutex timing_mutex_, chat_mutex_;
  std::deque<std::unique_ptr<Packet>> async_queue_;
  _ENetHost* client_ = nullptr;
  std::vector<_ENetPeer*> server_;
  std::thread thread_;
  uint8_t remote_player_count_ = 0;
  std::atomic<bool> do_loop_{true};
  bool is_decider_ = false, has_game_started_ = false;
  uint8_t player_idx_ = 0;
  std::unordered_map<std::string, std::map<_ENetPeer*, ActiveConnectionInfo>> active_connections_;
  std::atomic<bool> player_active_[PLAYER_COUNT_MAX] = {};
  std::deque<std::unique_ptr<Pad>> local_pad_queue_;
  std::deque<std::unique_ptr<Pad>> remote_pad_queue_[REMOTE_PLAYER_MAX];
  struct ChecksumEntry { int32_t frame = 0; uint32_t value = 0; } remote_checksums_[REMOTE_PLAYER_MAX];
  uint64_t ping_us_[REMOTE_PLAYER_MAX] = {};
  std::atomic<uint64_t> ping_sample_sum_us_{0}, ping_sample_count_{0};
  std::atomic<uint32_t> last_ping_ms_{0};
  int32_t last_frame_acked_[REMOTE_PLAYER_MAX] = {};
  FrameOffsetData frame_offset_data_[REMOTE_PLAYER_MAX];
  FrameTiming last_frame_timing_[REMOTE_PLAYER_MAX];
  std::deque<FrameTiming> ack_timers_[REMOTE_PLAYER_MAX];
  std::atomic<ConnectStatus> status_{ConnectStatus::UNSET};
  std::atomic<uint32_t> pending_disconnect_reason_{0}, disconnect_reason_{0};
  std::vector<int> failed_connections_;
  MatchInfo match_info_;
  std::unique_ptr<PlayerSelections> remote_chat_message_selection_;
  std::mutex build_mutex_;
  RemoteBuild remote_build_[REMOTE_PLAYER_MAX];
  std::atomic<uint64_t> connected_at_ms_{0};
  void SendBuild();
};

class Matchmaking {
 public:
  // Slippi's mode ids. RANKED stays defined only as protocol id 0: Melee Unlocked never searches it.
  enum OnlinePlayMode { RANKED = 0, UNRANKED = 1, DIRECT = 2, TEAMS = 3, PARTY = 4 };
  enum ProcessState { IDLE, INITIALIZING, MATCHMAKING, OPPONENT_CONNECTING, CONNECTION_SUCCESS, ERROR_ENCOUNTERED };
  struct MatchSearchSettings { OnlinePlayMode mode = UNRANKED; std::string connect_code; };
  struct MatchmakeResult { std::string id; std::vector<UserInfo> players; std::vector<uint16_t> stages; uint32_t items = 0; };
  // Local test peering (no matchmaking server): fixed player index, local port and the address of
  // every other player ("ip:port", in player-index order with the local player left out). One
  // remote is a two-player match; three is a four-player (Teams) match.
  struct LocalPeer {
    bool enabled = false;
    int local_index = 0;
    uint16_t local_port = 0;
    std::vector<std::string> remotes;
    int test_stage = -1;  // deterministic stage selection for local regression runs only
  };

  explicit Matchmaking(User* user);
  ~Matchmaking();
  void FindMatch(MatchSearchSettings settings);
  ProcessState GetMatchmakeState() const { return state_; }
  std::string GetErrorMessage() const { return error_msg_; }
  bool IsSearching() const { return state_ == INITIALIZING || state_ == MATCHMAKING || state_ == OPPONENT_CONNECTING; }
  std::unique_ptr<NetplayClient> GetNetplayClient() { return std::move(netplay_client_); }
  int LocalPlayerIndex() const { return local_player_index_; }
  std::vector<UserInfo> GetPlayerInfo() const { return player_info_; }
  std::string GetPlayerName(uint8_t port) const { return port < player_info_.size() ? player_info_[port].display_name : ""; }
  std::vector<uint16_t> GetStages() const { return allowed_stages_; }
  uint8_t RemotePlayerCount() const { return player_info_.empty() ? 0 : (uint8_t)(player_info_.size() - 1); }
  MatchmakeResult GetMatchmakeResult() const { return mm_result_; }
  static bool IsFixedRulesMode(OnlinePlayMode m) { return m == UNRANKED || m == PARTY; }
  static LocalPeer local_peer;          // set from the command line for local 2-4 instance tests
  static uint16_t forced_port;          // 0 = random 41000..50999
  // False in hidden or scripted runs unless --allow-matchmaking: an automated run must never
  // queue on Slippi's servers by accident (local peering is unaffected).
  static bool server_allowed;

 private:
  void MatchmakeThread();
  void startMatchmaking();
  void handleMatchmaking();
  void handleConnecting();
  void disconnectFromServer();
  void terminateMmConnection();
  struct Ticket;                          // a get-ticket-resp body (defined in slippi_net.cpp)
  void ingest_ticket(const Ticket& ticket);

  User* user_;
  _ENetHost* client_ = nullptr;
  _ENetPeer* server_ = nullptr;
  bool is_mm_connected_ = false, is_mm_terminated_ = false;
  std::thread thread_;
  MatchSearchSettings search_settings_;
  std::atomic<ProcessState> state_{IDLE};
  std::string error_msg_;
  int host_port_ = 0, local_player_index_ = 0;
  std::vector<std::string> remote_ips_;
  MatchmakeResult mm_result_;
  std::vector<UserInfo> player_info_;
  std::vector<uint16_t> allowed_stages_;
  bool is_host_ = false;
  std::unique_ptr<NetplayClient> netplay_client_;
};

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
