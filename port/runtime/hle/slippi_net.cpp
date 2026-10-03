// Slippi Online networking (see slippi_net.h). Wire-compatible port of Dolphin's
// SlippiNetplayClient, SlippiMatchmaking and the user record.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "slippi_net.h"
#include "slippi_online.h"
#include "slippi_report.h"
#include "host.h"
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <enet/enet.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <thread>

using json = nlohmann::json;

namespace slippi {

const char* const SLIPPI_SEMVER = "3.6.4";

uint64_t time_us() { return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
uint64_t time_ms() { return time_us() / 1000; }

// ---------------------------------------------------------------- strings
static std::wstring utf8_to_wide(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, 0);
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
  return w;
}
static std::string wide_to_cp(const std::wstring& w, UINT cp) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(cp, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, 0);
  WideCharToMultiByte(cp, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
  return s;
}
std::string utf8_to_shiftjis(const std::string& s) { return wide_to_cp(utf8_to_wide(s), 932); }
std::string shiftjis_to_utf8(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(932, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, 0);
  MultiByteToWideChar(932, 0, s.data(), (int)s.size(), &w[0], n);
  return wide_to_cp(w, CP_UTF8);
}
std::string truncate_length_char(const std::string& input, int length) {
  // Count code points, not bytes (UTF8ToUTF32 / resize / UTF32toUTF8 in Dolphin).
  std::string out;
  int count = 0;
  for (size_t i = 0; i < input.size() && count < length; ) {
    unsigned char c = (unsigned char)input[i];
    size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
    out.append(input, i, len);
    i += len; ++count;
  }
  return out;
}
static void convert_narrow_special_shiftjis(std::string& input) {
  static const std::unordered_map<char, uint16_t> table = {
      {'!', 0x8149}, {'"', 0x8168}, {'#', 0x8194}, {'$', 0x8190}, {'%', 0x8193}, {'&', 0x8195}, {'\'', 0x8166}, {'(', 0x8169},
      {')', 0x816a}, {'*', 0x8196}, {'+', 0x817b}, {',', 0x8143}, {'-', 0x817c}, {'.', 0x8144}, {'/', 0x815e}, {':', 0x8146},
      {';', 0x8147}, {'<', 0x8183}, {'=', 0x8181}, {'>', 0x8184}, {'?', 0x8148}, {'@', 0x8197}, {'[', 0x816d}, {'\\', 0x815f},
      {']', 0x816e}, {'^', 0x814f}, {'_', 0x8151}, {'`', 0x814d}, {'{', 0x816f}, {'|', 0x8162}, {'}', 0x8170}, {'~', 0x8160},
  };
  size_t pos = 0;
  while (pos < input.size()) {
    char c = input[pos];
    if ((unsigned char)c & 0x80) { pos += 2; continue; }
    auto it = table.find(c);
    if (it == table.end()) { ++pos; continue; }
    input.erase(pos, 1);
    // Dolphin inserts the little-endian bytes of the char16 in reverse, which yields big-endian order.
    input.insert(input.begin() + pos, (char)(it->second & 0xFF));
    input.insert(input.begin() + pos, (char)(it->second >> 8));
    pos += 2;
  }
}
std::string convert_string_for_game(const std::string& input, int length) {
  std::string sj = utf8_to_shiftjis(truncate_length_char(input, length));
  convert_narrow_special_shiftjis(sj);
  sj.resize(length * 2 + 1);
  return sj;
}
std::string convert_connect_code_for_game(const std::string& input) {
  std::string code;
  for (char c : input) { if (c == '#') { code += (char)0x81; code += (char)0x94; } else code += c; }
  code.resize(8 + 2);
  return code;
}

// ---------------------------------------------------------------- ENet
static bool g_enet_ready = false;
bool enet_ready() {
  if (!g_enet_ready) { if (enet_initialize() < 0) { host::log("slippi: enet_initialize failed"); return false; } g_enet_ready = true; }
  return true;
}
static void wakeup_thread(ENetHost* host) {
  if (!host) return;
  ENetAddress address;
  if (host->address.port != 0) address.port = host->address.port;
  else enet_socket_get_address(host->socket, &address);
  address.host = 0x0100007f;
  uint8_t byte = 0;
  ENetBuffer buf; buf.data = &byte; buf.dataLength = 1;
  enet_socket_send(host->socket, &address, &buf, 1);
}
static int ENET_CALLBACK intercept_callback(ENetHost* host, ENetEvent* event) {
  if (host->receivedDataLength == 1 && host->receivedData[0] == 0) { event->type = (ENetEventType)42; return 1; }
  return 0;
}
static std::string peer_key(ENetPeer* p) { std::stringstream s; s << p->address.host << "-" << p->address.port; return s.str(); }

// ---------------------------------------------------------------- Packet
void Packet::append(const void* data, size_t size) { const uint8_t* p = (const uint8_t*)data; buf_.insert(buf_.end(), p, p + size); }
Packet& Packet::operator<<(uint8_t v) { buf_.push_back(v); return *this; }
Packet& Packet::operator<<(uint16_t v) { buf_.push_back((uint8_t)(v >> 8)); buf_.push_back((uint8_t)v); return *this; }
Packet& Packet::operator<<(uint32_t v) { for (int i = 3; i >= 0; --i) buf_.push_back((uint8_t)(v >> (8 * i))); return *this; }
Packet& Packet::operator<<(const std::string& s) { *this << (uint32_t)s.size(); append(s.data(), s.size()); return *this; }
bool Packet::check(size_t n) { if (!ok_ || pos_ + n > buf_.size()) { ok_ = false; return false; } return true; }
Packet& Packet::operator>>(uint8_t& v) { if (check(1)) v = buf_[pos_++]; return *this; }
Packet& Packet::operator>>(bool& v) { uint8_t b = 0; *this >> b; v = b != 0; return *this; }
Packet& Packet::operator>>(uint16_t& v) { if (check(2)) { v = (uint16_t)((buf_[pos_] << 8) | buf_[pos_ + 1]); pos_ += 2; } return *this; }
Packet& Packet::operator>>(uint32_t& v) {
  if (check(4)) { v = ((uint32_t)buf_[pos_] << 24) | ((uint32_t)buf_[pos_ + 1] << 16) | ((uint32_t)buf_[pos_ + 2] << 8) | buf_[pos_ + 3]; pos_ += 4; }
  return *this;
}
Packet& Packet::operator>>(int32_t& v) { uint32_t u = 0; *this >> u; v = (int32_t)u; return *this; }
Packet& Packet::operator>>(std::string& s) {
  uint32_t n = 0; *this >> n;
  if (check(n)) { s.assign((const char*)&buf_[pos_], n); pos_ += n; }
  return *this;
}

// ---------------------------------------------------------------- selections
void PlayerSelections::Merge(const PlayerSelections& s) {
  rng_offset = s.rng_offset;
  if (s.is_stage_selected) { stage_id = s.stage_id; is_stage_selected = true; alt_stage_mode = s.alt_stage_mode; }
  if (s.is_character_selected) { character_id = s.character_id; character_color = s.character_color; team_id = s.team_id; is_character_selected = true; }
}
void PlayerSelections::Reset() { character_id = character_color = team_id = 0; is_character_selected = false; stage_id = 0; is_stage_selected = false; rng_offset = 0; }

// ---------------------------------------------------------------- user
User::User(std::string user_dir) : dir_(std::move(user_dir)) { if (AttemptLogin()) RefreshFromServer(); }
void User::RefreshFromServer() {
  UserInfo me = GetUserInfo();
  if (me.uid.empty()) return;
  if (Matchmaking::local_peer.enabled) return;   // local test peering stays off Slippi's servers
  std::shared_ptr<std::atomic<bool>> alive = alive_;
  std::thread([this, me, alive] {
    std::string response; int status = 0;
    if (!report::http_get("https://users.slippi.gg/user/" + me.uid, &response, &status) || status != 200) {
      host::log("slippi: profile refresh unavailable (HTTP %d); keeping user.json", status);
      return;
    }
    json j = json::parse(response, nullptr, false);
    if (j.is_discarded() || !alive->load()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string name = j.value("displayName", ""), code = j.value("connectCode", "");
    if (!name.empty()) info_.display_name = name;
    if (!code.empty()) info_.connect_code = code;
    const std::string latest = j.value("latestVersion", "");
    if (!latest.empty()) info_.latest_version = latest;
    if (j.count("chatMessages") && j["chatMessages"].is_array() && j["chatMessages"].size() == 16) {
      info_.chat_messages.clear();
      for (auto& m : j["chatMessages"]) info_.chat_messages.push_back(m.get<std::string>());
    }
    host::log("slippi: profile refreshed from server: %s (%s)", info_.display_name.c_str(), info_.connect_code.c_str());
  }).detach();
}
bool User::AttemptLogin() {
  std::ifstream f(dir_ + "/user.json");
  if (!f) { logged_in_ = false; return false; }
  try {
    json j = json::parse(f);
    std::lock_guard<std::mutex> lock(mutex_);
    info_.uid = j.value("uid", "");
    info_.play_key = j.value("playKey", "");
    info_.display_name = j.value("displayName", "");
    info_.connect_code = j.value("connectCode", "");
    info_.latest_version = j.value("latestVersion", "");
    info_.chat_messages.clear();
    if (j.count("chatMessages") && j["chatMessages"].is_array()) for (auto& m : j["chatMessages"]) info_.chat_messages.push_back(m.get<std::string>());
    if (info_.chat_messages.size() != 16) info_.chat_messages = GetDefaultChatMessages();
    bool was = logged_in_;
    logged_in_ = !info_.uid.empty() && !info_.play_key.empty();
    if (logged_in_ && !was) host::log("slippi: logged in as %s (%s)", info_.display_name.c_str(), info_.connect_code.c_str());
  } catch (const std::exception& e) {
    host::log("slippi: cannot parse %s/user.json: %s", dir_.c_str(), e.what());
    logged_in_ = false;
  }
  return logged_in_;
}
void User::LogOut() { std::lock_guard<std::mutex> lock(mutex_); logged_in_ = false; info_ = UserInfo(); host::log("slippi: logged out (user.json left in place)"); }
std::vector<std::string> User::GetUserChatMessages() const { UserInfo me = GetUserInfo(); return me.chat_messages.size() == 16 ? me.chat_messages : GetDefaultChatMessages(); }
std::vector<std::string> User::GetDefaultChatMessages() {
  return {"ggs", "one more", "brb", "good luck", "well played", "that was fun", "thanks", "too good",
          "sorry", "my b", "lol", "wow", "gotta go", "one sec", "let's play again later", "bad connection"};
}

DirectCodes::DirectCodes(std::string path) : path_(std::move(path)) { Load(); }
void DirectCodes::Load() {
  codes_.clear();
  std::ifstream f(path_);
  if (!f) return;
  try {
    json j = json::parse(f);
    if (j.is_array()) for (auto& e : j) { std::string c = e.is_object() ? e.value("connectCode", "") : e.is_string() ? e.get<std::string>() : ""; if (!c.empty()) codes_.push_back(c); }
  } catch (...) {}
}
void DirectCodes::Save() {
  json j = json::array();
  for (auto& c : codes_) j.push_back({{"connectCode", c}, {"lastPlayed", ""}});
  std::ofstream f(path_);
  if (f) f << j.dump(2);
}
std::string DirectCodes::get(int index) const { return index >= 0 && index < (int)codes_.size() ? codes_[index] : "1"; }
void DirectCodes::AddOrUpdateCode(const std::string& code) {
  codes_.erase(std::remove(codes_.begin(), codes_.end(), code), codes_.end());
  codes_.insert(codes_.begin(), code);
  Save();
}

// ---------------------------------------------------------------- netplay client
NetplayClient::NetplayClient(std::vector<std::string> addrs, std::vector<uint16_t> ports, uint8_t remote_player_count, uint16_t local_port,
                             bool is_decider, uint8_t player_idx) {
  host::log("slippi: netplay client: local port %u, decider %d, player index %u, %u remote players", local_port, is_decider, player_idx, remote_player_count);
  is_decider_ = is_decider;
  remote_player_count_ = remote_player_count;
  player_idx_ = player_idx;
  for (int i = 0, j = 0; i < REMOTE_PLAYER_MAX; ++i, ++j) {
    if (j == player_idx) ++j;
    match_info_.remote[i] = PlayerSelections();
    match_info_.remote[i].player_idx = (uint8_t)j;
    last_frame_acked_[i] = 0;
  }
  ENetAddress local_addr;
  ENetAddress* local = nullptr;
  if (local_port > 0) { local_addr.host = ENET_HOST_ANY; local_addr.port = local_port; local = &local_addr; }
  client_ = enet_host_create(local, 10, 3, 0, 0);
  if (!client_) { host::log("slippi: cannot create ENet client"); status_.store(ConnectStatus::FAILED); return; }
  for (int i = 0; i < remote_player_count; ++i) {
    ENetAddress addr;
    enet_address_set_host(&addr, addrs[i].c_str());
    addr.port = ports[i];
    ENetPeer* peer = enet_host_connect(client_, &addr, 3, 0);
    server_.push_back(peer);
    if (!peer) { host::log("slippi: cannot create peer for %s:%u", addrs[i].c_str(), ports[i]); continue; }
    ActiveConnectionInfo info;
    info.player_idx = match_info_.remote[i].player_idx;
    active_connections_[peer_key(peer)][peer] = info;
    player_active_[info.player_idx].store(true, std::memory_order_release);
  }
  status_.store(ConnectStatus::INITIATED, std::memory_order_release);
  thread_ = std::thread(&NetplayClient::ThreadFunc, this);
}

NetplayClient::~NetplayClient() {
  do_loop_.store(false);
  if (client_) wakeup_thread(client_);
  if (thread_.joinable()) thread_.join();
  if (!server_.empty()) Disconnect();
  if (client_) { enet_host_destroy(client_); client_ = nullptr; }
  host::log("slippi: netplay client cleanup complete");
}

void NetplayClient::OnData(Packet& packet, ENetPeer* peer) {
  uint8_t mid = 0;
  if (!(packet >> mid)) { host::log("slippi: empty netplay packet"); return; }
  switch (mid) {
    case NP_MSG_SLIPPI_PAD: {
      uint64_t cur_time = time_us();
      int32_t frame, checksum_frame; uint32_t checksum; uint8_t packet_player_port;
      if (!(packet >> frame) || !(packet >> packet_player_port) || !(packet >> checksum_frame) || !(packet >> checksum)) { host::log("slippi: pad packet too small"); break; }
      uint8_t pidx = PlayerIdxFromPort(packet_player_port);
      if (pidx >= remote_player_count_) { host::log("slippi: pad packet with invalid player idx %u", pidx); break; }
      const int pad_data_offset = 14;
      int conn_idx = 0;
      for (int i = 0; i < (int)server_.size(); ++i)
        if (peer->address.host == server_[i]->address.host && peer->address.port == server_[i]->address.port) { conn_idx = i; break; }
      std::string key = peer_key(peer);
      int live = 0; bool current_active = false;
      for (auto& c : active_connections_[key]) { if (c.second.is_disconnected) continue; if (c.first == peer) current_active = true; ++live; }
      if (current_active && live > 1 && player_idx_ < packet_player_port) {
        server_[conn_idx] = peer;
        for (auto& c : active_connections_[key]) {
          if (c.first == peer || c.second.is_disconnected) continue;
          enet_peer_disconnect(c.first, 0);
          c.second.is_disconnected = true;
        }
      }
      {
        // The game thread writes the frame timing when it sends its pad and reads the offsets for
        // time sync: both sides under timing_mutex_.
        std::lock_guard<std::mutex> lk(timing_mutex_);
        FrameTiming timing = last_frame_timing_[pidx];
        if (!has_game_started_) { timing.frame = 0; timing.time_us = cur_time; }
        int64_t opponent_send_time_us = (int64_t)cur_time - (int64_t)(ping_us_[pidx] / 2);
        int64_t frame_diff_offset_us = 16683 * (int64_t)(timing.frame - frame);
        int64_t time_offset_us = opponent_send_time_us - (int64_t)timing.time_us + frame_diff_offset_us;
        auto& fod = frame_offset_data_[pidx];
        if ((int)fod.buf.size() < ONLINE_LOCKSTEP_INTERVAL) fod.buf.push_back((int32_t)time_offset_us);
        else fod.buf[fod.idx] = (int32_t)time_offset_us;
        fod.idx = (fod.idx + 1) % ONLINE_LOCKSTEP_INTERVAL;
      }
      int64_t inputs_to_copy;
      {
        std::lock_guard<std::mutex> lk(pad_mutex_);
        const uint8_t* data = packet.data();
        int32_t head_frame = remote_pad_queue_[pidx].empty() ? 0 : remote_pad_queue_[pidx].front()->frame;
        inputs_to_copy = (int64_t)frame - head_frame;
        if (pad_data_offset + inputs_to_copy * PAD_DATA_SIZE > (int64_t)packet.size()) { host::log("slippi: pad packet too small for %lld inputs", (long long)inputs_to_copy); break; }
        if (inputs_to_copy > 128) { host::log("slippi: pad packet with too many frames (%lld)", (long long)inputs_to_copy); break; }
        for (int64_t i = inputs_to_copy - 1; i >= 0; --i)
          remote_pad_queue_[pidx].push_front(std::make_unique<Pad>((int32_t)(frame - i), &data[pad_data_offset + i * PAD_DATA_SIZE]));
        remote_checksums_[pidx] = {checksum_frame, checksum};
      }
      if (inputs_to_copy > 0) {
        Packet ack;
        ack << (uint8_t)NP_MSG_SLIPPI_PAD_ACK << frame << player_idx_;
        ENetPacket* epac = enet_packet_create(ack.data(), ack.size(), ENET_PACKET_FLAG_UNSEQUENCED);
        enet_peer_send(peer, 2, epac);
      }
      break;
    }
    case NP_MSG_SLIPPI_PAD_ACK: {
      std::lock_guard<std::mutex> lk(ack_mutex_);
      int32_t frame; uint8_t packet_player_port;
      if (!(packet >> frame) || !(packet >> packet_player_port)) { host::log("slippi: ack packet too small"); break; }
      uint8_t pidx = PlayerIdxFromPort(packet_player_port);
      if (pidx >= remote_player_count_) break;
      last_frame_acked_[pidx] = std::max(last_frame_acked_[pidx], frame);
      auto& timers = ack_timers_[pidx];
      while (!timers.empty() && timers.front().frame < frame) timers.pop_front();
      if (timers.empty() || timers.front().frame != frame) break;
      uint64_t send_time = timers.front().time_us;
      timers.pop_front();
      ping_us_[pidx] = time_us() - send_time;
      ping_sample_sum_us_.fetch_add(ping_us_[pidx], std::memory_order_relaxed);
      ping_sample_count_.fetch_add(1, std::memory_order_relaxed);
      if (pidx == 0) last_ping_ms_.store((uint32_t)(ping_us_[pidx] / 1000), std::memory_order_relaxed);
      if (frame % 600 == 0 && pidx == 0) host::log("slippi: ping %llu ms", (unsigned long long)(ping_us_[0] / 1000));
      break;
    }
    case NP_MSG_SLIPPI_MATCH_SELECTIONS: {
      auto s = ReadSelections(packet);
      if (!s->error) {
        uint8_t idx = PlayerIdxFromPort(s->player_idx);
        if (idx >= remote_player_count_) break;
        host::log("slippi: received selections from player %u (char %u color %u stage %u)", s->player_idx, s->character_id, s->character_color, s->stage_id);
        match_info_.remote[idx].Merge(*s);
        { std::lock_guard<std::mutex> lk(timing_mutex_); has_game_started_ = false; }
        { std::lock_guard<std::mutex> lk(pad_mutex_); remote_pad_queue_[idx].clear(); }
      }
      break;
    }
    case NP_MSG_SLIPPI_CHAT_MESSAGE: {
      auto s = ReadChatMessage(packet);
      if (!s->error) { std::lock_guard<std::mutex> lk(chat_mutex_); remote_chat_message_selection_ = std::move(s); }
      break;
    }
    case NP_MSG_MU_BUILD: {
      // 'M' 'U' 'B' version, player port, mod view, fingerprint length + bytes, name length + bytes.
      uint8_t m, u, b, version, port, mod_view, fp_len, name_len;
      if (!(packet >> m >> u >> b >> version >> port >> mod_view >> fp_len) || m != 'M' || u != 'U' || b != 'B' || version != 1) {
        host::log("slippi: invalid build message");
        break;
      }
      std::string fp, name;
      for (uint8_t i = 0; i < fp_len; ++i) { uint8_t ch; if (!(packet >> ch)) break; fp.push_back((char)ch); }
      if (packet >> name_len)
        for (uint8_t i = 0; i < name_len; ++i) { uint8_t ch; if (!(packet >> ch)) break; name.push_back((char)ch); }
      const uint8_t pidx = PlayerIdxFromPort(port);
      if (pidx >= remote_player_count_) { host::log("slippi: build message with invalid player idx %u", pidx); break; }
      {
        std::lock_guard<std::mutex> lk(build_mutex_);
        remote_build_[pidx] = {true, mod_view != 0, fp, name};
      }
      host::log("slippi: opponent %u build: %s%s%s", port, mod_view ? "mod " : "retail game",
                mod_view ? name.c_str() : "", mod_view ? (" " + fp.substr(0, 16)).c_str() : "");
      break;
    }
    case NP_MSG_SLIPPI_CONN_SELECTED: break;
    case NP_MSG_SLIPPI_COMPLETE_STEP: break;   // Ranked game preparation: no Ranked in Melee Unlocked
    case NP_MSG_SLIPPI_SYNCED_STATE: break;    // Ranked tiebreak recovery: no Ranked in Melee Unlocked
    default: host::log("slippi: unknown netplay message %u", mid); break;
  }
}

void NetplayClient::SendBuild() {
  // Tests only: behave like Slippi Dolphin, which never sends a build.
  if (std::getenv("MELEE_TEST_NO_BUILD_MESSAGE")) { host::log("slippi: build message suppressed (test)"); return; }
  const auto& me = slippi::online::local_build();
  auto p = std::make_unique<Packet>();
  *p << (uint8_t)NP_MSG_MU_BUILD << (uint8_t)'M' << (uint8_t)'U' << (uint8_t)'B' << (uint8_t)1 << player_idx_
     << (uint8_t)(me.mod_view ? 1 : 0);
  const std::string fp = me.mod_view ? me.fingerprint.substr(0, 64) : std::string();
  const std::string name = me.mod_view ? me.name.substr(0, 64) : std::string();
  *p << (uint8_t)fp.size();
  for (char ch : fp) *p << (uint8_t)ch;
  *p << (uint8_t)name.size();
  for (char ch : name) *p << (uint8_t)ch;
  host::log("slippi: sending build: %s%s", me.mod_view ? "mod " : "retail game", me.mod_view ? name.c_str() : "");
  SendAsync(std::move(p));
}

NetplayClient::RemoteBuild NetplayClient::GetRemoteBuild(int index) {
  std::lock_guard<std::mutex> lk(build_mutex_);
  if (index < 0 || index >= REMOTE_PLAYER_MAX) return {};
  return remote_build_[index];
}

void NetplayClient::WriteSelections(Packet& p, const PlayerSelections& s) {
  p << (uint8_t)NP_MSG_SLIPPI_MATCH_SELECTIONS << s.character_id << s.character_color << s.is_character_selected << s.player_idx
    << s.stage_id << s.is_stage_selected << s.rng_offset << s.team_id << s.alt_stage_mode;
}
std::unique_ptr<PlayerSelections> NetplayClient::ReadSelections(Packet& p) {
  auto s = std::make_unique<PlayerSelections>();
  if (!(p >> s->character_id >> s->character_color >> s->is_character_selected >> s->player_idx >> s->stage_id >> s->is_stage_selected
        >> s->rng_offset >> s->team_id >> s->alt_stage_mode)) { host::log("slippi: invalid selection packet"); s->error = true; }
  return s;
}
std::unique_ptr<PlayerSelections> NetplayClient::ReadChatMessage(Packet& p) {
  auto s = std::make_unique<PlayerSelections>();
  if (!(p >> s->message_id) || !(p >> s->player_idx)) { s->error = true; return s; }
  static const int allowed[] = {136, 129, 130, 132, 34, 40, 33, 36, 72, 66, 68, 65, 24, 18, 20, 17, CHAT_MSG_CHAT_DISABLED};
  bool ok = false;
  for (int a : allowed) if (a == s->message_id) ok = true;
  if (!ok) { host::log("slippi: invalid chat message %d", s->message_id); s->error = true; }
  return s;
}

void NetplayClient::Send(Packet& packet) {
  for (size_t i = 0; i < server_.size(); ++i) {
    auto conn = active_connections_.find(peer_key(server_[i]));
    if (conn != active_connections_.end()) {
      auto p = conn->second.find(server_[i]);
      if (p != conn->second.end() && p->second.is_disconnected) continue;
    }
    uint8_t mid = packet.data()[0];
    enet_uint32 flags = ENET_PACKET_FLAG_RELIABLE;
    uint8_t channel = 0;
    if (mid == NP_MSG_SLIPPI_PAD || mid == NP_MSG_SLIPPI_PAD_ACK) { flags = ENET_PACKET_FLAG_UNSEQUENCED; channel = 1; }
    ENetPacket* epac = enet_packet_create(packet.data(), packet.size(), flags);
    enet_peer_send(server_[i], channel, epac);
  }
}

void NetplayClient::Disconnect() {
  status_.store(ConnectStatus::DISCONNECTED, std::memory_order_release);
  if (active_connections_.empty()) return;
  for (auto& conn : active_connections_)
    for (auto& peer : conn.second) enet_peer_disconnect(peer.first, pending_disconnect_reason_.load(std::memory_order_acquire));
  ENetEvent ev;
  while (enet_host_service(client_, &ev, 3000) > 0) {
    if (ev.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(ev.packet);
    else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) host::log("slippi: got disconnect from peer %u", ev.peer->address.port);
  }
  for (auto& conn : active_connections_) for (auto& peer : conn.second) enet_peer_reset(peer.first);
  active_connections_.clear();
  for (auto& a : player_active_) a.store(false, std::memory_order_release);
  server_.clear();
}

void NetplayClient::SendAsync(std::unique_ptr<Packet> packet) {
  if (status_.load(std::memory_order_acquire) == ConnectStatus::DISCONNECTED) return;
  { std::lock_guard<std::mutex> lk(async_mutex_); async_queue_.push_back(std::move(packet)); }
  wakeup_thread(client_);
}

void NetplayClient::ThreadFunc() {
  uint64_t start_time = time_ms();
  const uint64_t timeout = 8000;
  std::vector<bool> connections(remote_player_count_, false);
  std::vector<ENetAddress> remote_addrs;
  for (int i = 0; i < remote_player_count_; ++i) remote_addrs.push_back(server_[i]->address);

  while (status_.load(std::memory_order_acquire) == ConnectStatus::INITIATED) {
    ENetEvent ev;
    int net = enet_host_service(client_, &ev, 500);
    if (net > 0) {
      switch (ev.type) {
        case ENET_EVENT_TYPE_RECEIVE: {
          if (!ev.peer) break;
          Packet rpac(ev.packet->data, ev.packet->dataLength);
          OnData(rpac, ev.peer);
          enet_packet_destroy(ev.packet);
          break;
        }
        case ENET_EVENT_TYPE_DISCONNECT:
          if (ev.peer) host::log("slippi: disconnect event from %x:%u while connecting", ev.peer->address.host, ev.peer->address.port);
          break;
        case ENET_EVENT_TYPE_CONNECT: {
          if (!ev.peer) break;
          int early_idx = 0;
          for (int i = 0; i < (int)remote_addrs.size(); ++i)
            if (remote_addrs[i].host == ev.peer->address.host && remote_addrs[i].port == ev.peer->address.port) { early_idx = i; break; }
          ActiveConnectionInfo info;
          info.player_idx = match_info_.remote[early_idx].player_idx;
          active_connections_[peer_key(ev.peer)][ev.peer] = info;
          player_active_[info.player_idx].store(true, std::memory_order_release);
          bool already = false;
          for (size_t i = 0; i < server_.size(); ++i)
            if (connections[i] && ev.peer->address.host == server_[i]->address.host && ev.peer->address.port == server_[i]->address.port) { server_[i] = ev.peer; already = true; break; }
          if (already) break;
          // Exact address first: several players can share one host (local multi-instance
          // tests, or a LAN behind one address), and matching the host alone would hand this
          // peer another player's slot. Falls back to the host alone as before.
          int slot = -1;
          for (size_t i = 0; i < server_.size() && slot < 0; ++i)
            if (!connections[i] && remote_addrs[i].host == ev.peer->address.host && remote_addrs[i].port == ev.peer->address.port) slot = (int)i;
          for (size_t i = 0; i < server_.size() && slot < 0; ++i)
            if (!connections[i] && remote_addrs[i].host == ev.peer->address.host) slot = (int)i;
          if (slot >= 0) {
            host::log("slippi: connected to %x:%u", ev.peer->address.host, ev.peer->address.port);
            server_[slot] = ev.peer;
            connections[slot] = true;
          }
          break;
        }
        default: break;
      }
    }
    bool all = true;
    for (int i = 0; i < remote_player_count_; ++i) if (!connections[i]) all = false;
    if (all) {
      client_->intercept = intercept_callback;
      host::log("slippi: online connection successful");
      connected_at_ms_.store(time_ms(), std::memory_order_release);
      status_.store(ConnectStatus::CONNECTED, std::memory_order_release);
      SendBuild();
      break;
    }
    if (time_ms() - start_time >= timeout || !do_loop_.load()) {
      for (int i = 0; i < remote_player_count_; ++i) if (!connections[i]) failed_connections_.push_back(i);
      status_.store(ConnectStatus::FAILED, std::memory_order_release);
      host::log("slippi: online connection failed");
      return;
    }
  }

  // Test only. MELEE_NET_LAG_MS=N holds every received match packet for N ms before it is handled,
  // so two instances on one machine (tools/online_pair.py) see the late inputs that cause rollbacks,
  // which a loopback connection never produces. Unset, as it always is for a player, nothing changes.
  static const uint64_t lag_ms = [] {
    const char* s = std::getenv("MELEE_NET_LAG_MS");
    const long v = s ? std::atol(s) : 0;
    if (v > 0) host::log("slippi: MELEE_NET_LAG_MS: holding received packets %ld ms (testing)", v);
    return v > 0 ? (uint64_t)v : 0;
  }();
  struct Held { uint64_t due; std::vector<uint8_t> data; ENetPeer* peer; };
  std::deque<Held> held;
  auto deliver_due = [&] {
    while (!held.empty() && held.front().due <= time_ms()) {
      Packet rpac(held.front().data.data(), held.front().data.size());
      OnData(rpac, held.front().peer);
      held.pop_front();
    }
  };

  while (do_loop_.load()) {
    if (status_.load(std::memory_order_acquire) == ConnectStatus::DISCONNECTED) break;
    deliver_due();
    for (auto& conn : active_connections_) {
      for (auto& peer : conn.second) {
        if (peer.second.is_disconnected || player_active_[peer.second.player_idx].load(std::memory_order_acquire)) continue;
        enet_peer_disconnect(peer.first, pending_disconnect_reason_.load(std::memory_order_acquire));
        peer.second.is_disconnected = true;
      }
    }
    ENetEvent ev;
    int net = enet_host_service(client_, &ev, held.empty() ? 250 : 1);
    for (;;) {
      std::unique_ptr<Packet> p;
      { std::lock_guard<std::mutex> lk(async_mutex_); if (async_queue_.empty()) break; p = std::move(async_queue_.front()); async_queue_.pop_front(); }
      Send(*p);
    }
    if (net <= 0) continue;
    switch (ev.type) {
      case ENET_EVENT_TYPE_RECEIVE: {
        if (lag_ms) {
          held.push_back({time_ms() + lag_ms, std::vector<uint8_t>(ev.packet->data, ev.packet->data + ev.packet->dataLength), ev.peer});
          enet_packet_destroy(ev.packet);
          break;
        }
        Packet rpac(ev.packet->data, ev.packet->dataLength);
        OnData(rpac, ev.peer);
        enet_packet_destroy(ev.packet);
        break;
      }
      case ENET_EVENT_TYPE_DISCONNECT: {
        held.clear();   // their peer is going away; nothing held for it may be handled after
        std::string key = peer_key(ev.peer);
        if (active_connections_.count(key) && active_connections_[key].count(ev.peer)) active_connections_[key][ev.peer].is_disconnected = true;
        bool all_peers_gone = AreAllPeersDisconnectedForKey(key);
        if (all_peers_gone && active_connections_.count(key) && active_connections_[key].count(ev.peer))
          player_active_[active_connections_[key][ev.peer].player_idx].store(false, std::memory_order_release);
        bool connected_client = false;
        for (size_t i = 0; i < server_.size(); ++i)
          if (ev.peer->address.host == server_[i]->address.host && ev.peer->address.port == server_[i]->address.port) connected_client = true;
        if (connected_client && ev.data != 0) disconnect_reason_.store(ev.data, std::memory_order_release);
        host::log("slippi: disconnect from %x:%u (all peers gone: %d, connected client: %d)", ev.peer->address.host, ev.peer->address.port, all_peers_gone, connected_client);
        if (connected_client && all_peers_gone && AreAllConnectionsDisconnected()) do_loop_.store(false);
        break;
      }
      case ENET_EVENT_TYPE_CONNECT: {
        int late_idx = 0;
        for (int i = 0; i < (int)server_.size(); ++i)
          if (server_[i]->address.host == ev.peer->address.host && server_[i]->address.port == ev.peer->address.port) { late_idx = i; break; }
        ActiveConnectionInfo info;
        info.player_idx = match_info_.remote[late_idx].player_idx;
        active_connections_[peer_key(ev.peer)][ev.peer] = info;
        player_active_[info.player_idx].store(true, std::memory_order_release);
        break;
      }
      default: break;
    }
  }
  Disconnect();
}

void NetplayClient::StartSlippiGame() {
  local_pad_queue_.clear();
  {
    std::lock_guard<std::mutex> lk(timing_mutex_);
    has_game_started_ = false;
    for (int i = 0; i < remote_player_count_; ++i) last_frame_timing_[i] = {0, time_us()};
  }
  {
    std::lock_guard<std::mutex> lk(ack_mutex_);
    for (int i = 0; i < remote_player_count_; ++i) { last_frame_acked_[i] = 0; ack_timers_[i].clear(); }
  }
  match_info_.Reset();
}

void NetplayClient::SendSlippiPad(std::unique_ptr<Pad> pad) {
  ConnectStatus st = status_.load(std::memory_order_acquire);
  if (st == ConnectStatus::FAILED || st == ConnectStatus::DISCONNECTED) return;
  if (pad) local_pad_queue_.push_front(std::move(pad));
  int min_ack = INT_MAX;
  {
    std::lock_guard<std::mutex> lk(ack_mutex_);
    for (int i = 0; i < remote_player_count_; ++i) {
      if (!player_active_[match_info_.remote[i].player_idx].load(std::memory_order_acquire)) continue;
      min_ack = std::min(min_ack, last_frame_acked_[i]);
    }
  }
  if (!local_pad_queue_.empty()) min_ack = std::max(min_ack, local_pad_queue_.front()->frame - 128);
  while (!local_pad_queue_.empty() && local_pad_queue_.back()->frame < min_ack) local_pad_queue_.pop_back();
  if (local_pad_queue_.empty()) return;
  int32_t frame = local_pad_queue_.front()->frame;
  auto spac = std::make_unique<Packet>();
  *spac << (uint8_t)NP_MSG_SLIPPI_PAD << frame << player_idx_ << local_pad_queue_.front()->checksum_frame << local_pad_queue_.front()->checksum;
  for (auto& p : local_pad_queue_) spac->append(p->buf, PAD_DATA_SIZE);
  SendAsync(std::move(spac));
  uint64_t t = time_us();
  {
    std::lock_guard<std::mutex> lk(timing_mutex_);
    has_game_started_ = true;
    for (int i = 0; i < remote_player_count_; ++i) last_frame_timing_[i] = {frame, t};
  }
  {
    std::lock_guard<std::mutex> lk(ack_mutex_);
    for (int i = 0; i < remote_player_count_; ++i) ack_timers_[i].push_back({frame, t});
  }
}

void NetplayClient::SetMatchSelections(PlayerSelections& s) {
  match_info_.local.Merge(s);
  match_info_.local.player_idx = player_idx_;
  auto spac = std::make_unique<Packet>();
  WriteSelections(*spac, match_info_.local);
  SendAsync(std::move(spac));
}
void NetplayClient::SendChatMessage(int message_id) {
  remote_sent_chat_message_id = (uint8_t)message_id;
  auto spac = std::make_unique<Packet>();
  *spac << (uint8_t)NP_MSG_SLIPPI_CHAT_MESSAGE << message_id << player_idx_;
  SendAsync(std::move(spac));
}
PlayerSelections NetplayClient::GetSlippiRemoteChatMessage(bool chat_enabled) {
  std::lock_guard<std::mutex> lk(chat_mutex_);
  PlayerSelections copied;
  if (remote_chat_message_selection_ && chat_enabled) {
    copied.message_id = remote_chat_message_selection_->message_id;
    copied.player_idx = remote_chat_message_selection_->player_idx;
    remote_chat_message_selection_->message_id = 0;
    remote_chat_message_selection_->player_idx = 0;
  } else {
    copied.message_id = 0; copied.player_idx = 0;
    if (remote_chat_message_selection_ && !chat_enabled && remote_chat_message_selection_->message_id > 0 &&
        remote_chat_message_selection_->message_id != CHAT_MSG_CHAT_DISABLED) {
      auto spac = std::make_unique<Packet>();
      *spac << (uint8_t)NP_MSG_SLIPPI_CHAT_MESSAGE << (int)CHAT_MSG_CHAT_DISABLED << player_idx_;
      SendAsync(std::move(spac));
      remote_sent_chat_message_id = 0;
      remote_chat_message_selection_ = nullptr;
    }
  }
  return copied;
}
uint8_t NetplayClient::GetSlippiRemoteSentChatMessage(bool chat_enabled) {
  if (!chat_enabled) return 0;
  uint8_t id = remote_sent_chat_message_id;
  remote_sent_chat_message_id = 0;
  return id;
}

std::unique_ptr<RemotePadOutput> NetplayClient::GetSlippiRemotePad(int index, int max_frame_count) {
  std::lock_guard<std::mutex> lk(pad_mutex_);
  auto out = std::make_unique<RemotePadOutput>();
  if (remote_pad_queue_[index].empty()) {
    Pad empty(0);
    out->latest_frame = 0;
    out->data.insert(out->data.end(), empty.buf, empty.buf + PAD_FULL_SIZE);
    return out;
  }
  out->latest_frame = 0;
  out->checksum_frame = remote_checksums_[index].frame;
  out->checksum = remote_checksums_[index].value;
  out->player_idx = (uint8_t)(index >= player_idx_ ? index + 1 : index);
  out->is_disconnected = !player_active_[out->player_idx].load(std::memory_order_acquire);
  int count = 0;
  for (auto it = remote_pad_queue_[index].rbegin(); it != remote_pad_queue_[index].rend(); ++it) {
    out->latest_frame = std::max(out->latest_frame, (*it)->frame);
    out->data.insert(out->data.begin(), (*it)->buf, (*it)->buf + PAD_FULL_SIZE);
    if (++count >= max_frame_count) break;
  }
  return out;
}

void NetplayClient::DropOldRemoteInputs(int32_t finalized_frame) {
  std::lock_guard<std::mutex> lk(pad_mutex_);
  for (int i = 0; i < remote_player_count_; ++i)
    while (remote_pad_queue_[i].size() > 1 && remote_pad_queue_[i].back()->frame < finalized_frame) remote_pad_queue_[i].pop_back();
}
std::unordered_map<uint8_t, bool> NetplayClient::GetActivePlayerIndices() const {
  std::unordered_map<uint8_t, bool> r;
  for (uint8_t i = 0; i < PLAYER_COUNT_MAX; ++i) if (player_active_[i].load(std::memory_order_acquire)) r[i] = true;
  return r;
}
void NetplayClient::ForceDisconnectPlayer(uint8_t player_idx) {
  if (player_idx >= PLAYER_COUNT_MAX) return;
  player_active_[player_idx].store(false, std::memory_order_release);
  bool any = false;
  for (uint8_t i = 0; i < remote_player_count_; ++i) if (player_active_[match_info_.remote[i].player_idx].load(std::memory_order_acquire)) any = true;
  if (!any && status_.load(std::memory_order_acquire) == ConnectStatus::CONNECTED) {
    host::log("slippi: all remote players force-disconnected");
    status_.store(ConnectStatus::DISCONNECTED, std::memory_order_release);
  }
  if (client_) wakeup_thread(client_);
}
void NetplayClient::ForceDisconnect(DisconnectReason reason) {
  pending_disconnect_reason_.store((uint32_t)reason, std::memory_order_release);
  disconnect_reason_.store((uint32_t)reason, std::memory_order_release);
  for (uint8_t i = 0; i < remote_player_count_; ++i) ForceDisconnectPlayer(match_info_.remote[i].player_idx);
}
bool NetplayClient::AreAllPeersDisconnectedForKey(const std::string& key) {
  if (!active_connections_.count(key)) return true;
  for (auto& p : active_connections_[key]) if (!p.second.is_disconnected) return false;
  return true;
}
bool NetplayClient::AreAllConnectionsDisconnected() {
  for (auto& c : active_connections_) for (auto& p : c.second) if (!p.second.is_disconnected) return false;
  return true;
}
double NetplayClient::GetAndResetAvgPingMs() {
  uint64_t sum = ping_sample_sum_us_.exchange(0, std::memory_order_relaxed);
  uint64_t count = ping_sample_count_.exchange(0, std::memory_order_relaxed);
  return count ? (double)sum / (double)count / 1000.0 : 0.0;
}
int32_t NetplayClient::CalcTimeOffsetUs() {
  std::vector<int> offsets;
  for (int i = 0; i < remote_player_count_; ++i) {
    if (!player_active_[match_info_.remote[i].player_idx].load(std::memory_order_acquire)) continue;
    std::vector<int32_t> buf;
    { std::lock_guard<std::mutex> lk(timing_mutex_); buf = frame_offset_data_[i].buf; }
    if (buf.empty()) continue;
    std::sort(buf.begin(), buf.end());
    int n = (int)buf.size(), off = (int)((1.0f / 3.0f) * n), end = n - off;
    int sum = 0;
    for (int k = off; k < end; ++k) sum += buf[k];
    int count = end - off;
    if (count <= 0) return 0;
    offsets.push_back(sum / count);
  }
  if (offsets.empty()) return 0;
  return *std::min_element(offsets.begin(), offsets.end());
}

// ---------------------------------------------------------------- matchmaking
Matchmaking::LocalPeer Matchmaking::local_peer;
uint16_t Matchmaking::forced_port = 0;
bool Matchmaking::server_allowed = true;

Matchmaking::Matchmaking(User* user) : user_(user) {}
Matchmaking::~Matchmaking() {
  is_mm_terminated_ = true;
  state_ = ERROR_ENCOUNTERED;
  error_msg_ = "Matchmaking shut down";
  if (thread_.joinable()) thread_.join();
  terminateMmConnection();
}
void Matchmaking::FindMatch(MatchSearchSettings settings) {
  is_mm_connected_ = false;
  search_settings_ = settings;
  error_msg_.clear();
  if (!local_peer.enabled && !server_allowed) {
    error_msg_ = "Matchmaking is off in automated runs";
    state_ = ERROR_ENCOUNTERED;
    host::log("slippi: matchmaking blocked: hidden or scripted run without --allow-matchmaking");
    return;
  }
  state_ = INITIALIZING;
  host::log("slippi: matchmaking started (mode %d, code '%s')", settings.mode, settings.connect_code.c_str());
  thread_ = std::thread(&Matchmaking::MatchmakeThread, this);
}
void Matchmaking::MatchmakeThread() {
  while (IsSearching()) {
    if (is_mm_terminated_) break;
    switch (state_.load()) {
      case INITIALIZING: startMatchmaking(); break;
      case MATCHMAKING: handleMatchmaking(); break;
      case OPPONENT_CONNECTING: handleConnecting(); break;
      default: break;
    }
  }
  terminateMmConnection();
}
void Matchmaking::disconnectFromServer() {
  is_mm_connected_ = false;
  if (!server_) return;
  enet_peer_disconnect(server_, 0);
  ENetEvent ev;
  while (enet_host_service(client_, &ev, 3000) > 0) {
    if (ev.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(ev.packet);
    else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) { server_ = nullptr; return; }
  }
  enet_peer_reset(server_);
  server_ = nullptr;
}
void Matchmaking::terminateMmConnection() {
  disconnectFromServer();
  if (client_) { enet_host_destroy(client_); client_ = nullptr; }
}

static std::string local_address_string(ENetAddress* mm_address) {
  ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
  if (s == ENET_SOCKET_NULL) return "";
  ENetAddress a;
  std::string out;
  if (enet_socket_connect(s, mm_address) != -1 && enet_socket_get_address(s, &a) != -1) {
    struct in_addr in; in.s_addr = a.host;
    char buf[32]; inet_ntop(AF_INET, &in, buf, sizeof buf);
    out = buf;
  }
  enet_socket_destroy(s);
  return out;
}

static int mm_send(ENetPeer* server, const json& msg) {
  std::string s = msg.dump();
  ENetPacket* p = enet_packet_create(s.c_str(), s.size(), ENET_PACKET_FLAG_RELIABLE);
  return enet_peer_send(server, 0, p);
}
static int mm_receive(ENetHost* client, json& msg, int timeout_ms) {
  const int step = 250;
  int attempts = std::max(timeout_ms, step) / step;
  for (int i = 0; i < attempts; ++i) {
    ENetEvent ev;
    int net = enet_host_service(client, &ev, step);
    if (net <= 0) continue;
    if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
      std::string s((const char*)ev.packet->data, ev.packet->dataLength);
      enet_packet_destroy(ev.packet);
      try { msg = json::parse(s); } catch (const std::exception& e) { host::log("slippi: bad matchmaking JSON: %s", e.what()); return -3; }
      return 0;
    }
    if (ev.type == ENET_EVENT_TYPE_DISCONNECT) return -2;
  }
  return -1;
}

struct Matchmaking::Ticket { const json& resp; };

// Local test peering: the get-ticket-resp Slippi's matchmaking server would send this instance.
// Players are listed by port (1-based), the local one flagged isLocalPlayer; everyone is on
// 127.0.0.1 so ipAddress and ipAddressLan are the same. Player 1 (index 0) is the decider. A
// two-player match keeps the six-stage list the local harness always used; with more players
// "stages" is left out, so the parser's default for more than two players applies (no Fountain
// of Dreams).
static json local_peer_ticket(const UserInfo& me, Matchmaking::OnlinePlayMode mode) {
  using MM = Matchmaking;
  const auto& local_peer = MM::local_peer;
  const int n = (int)local_peer.remotes.size() + 1;
  json players = json::array();
  for (int i = 0, r = 0; i < n; ++i) {
    json p;
    const bool local = i == local_peer.local_index;
    std::string addr = local ? "127.0.0.1:" + std::to_string(local_peer.local_port) : local_peer.remotes[r++];
    if (local) {
      p["uid"] = me.uid; p["displayName"] = me.display_name; p["connectCode"] = me.connect_code;
    } else if (n == 2) {
      p["uid"] = "local-peer"; p["displayName"] = "Peer"; p["connectCode"] = "PEER#001";
    } else {
      p["uid"] = "local-peer-" + std::to_string(i + 1); p["displayName"] = "Peer " + std::to_string(i + 1);
      p["connectCode"] = "PEER#00" + std::to_string(i + 1);
    }
    p["port"] = i + 1;
    p["isLocalPlayer"] = local;
    p["ipAddress"] = addr;
    p["ipAddressLan"] = addr;
    players.push_back(p);
  }
  const char* mode_name = mode == MM::DIRECT ? "direct" : mode == MM::TEAMS ? "teams" : mode == MM::PARTY ? "party" : "unranked";
  json resp;
  resp["type"] = "get-ticket-resp";
  resp["matchId"] = "mode." + std::string(mode_name) + "-local-test";
  resp["players"] = players;
  resp["isHost"] = local_peer.local_index == 0;
  if (n == 2) resp["stages"] = {0x2, 0x3, 0x8, 0x1C, 0x1F, 0x20};
  return resp;
}

void Matchmaking::startMatchmaking() {
  client_ = nullptr;
  UserInfo me = user_->GetUserInfo();
  static std::mt19937 rng((uint32_t)time_ms());

  if (local_peer.enabled) {
    // 2-4 local instances peer directly. The match is the get-ticket-resp the server would send,
    // run through the same parser as a real one (player list, local index, decider, addresses).
    host_port_ = local_peer.local_port;
    json resp = local_peer_ticket(me, search_settings_.mode);
    ingest_ticket(Ticket{resp});
    state_ = OPPONENT_CONNECTING;
    std::string peers;
    for (auto& ip : remote_ips_) peers += (peers.empty() ? "" : ", ") + ip;
    host::log("slippi: local peer test: %s, player %d of %zu on port %d (decider %d), peers %s", mm_result_.id.c_str(),
              local_player_index_, player_info_.size(), host_port_, is_host_, peers.c_str());
    return;
  }

  for (int retry = 0; !client_ && retry < 15; ++retry) {
    host_port_ = forced_port ? forced_port : 41000 + (int)(rng() % 10000);
    ENetAddress addr; addr.host = ENET_HOST_ANY; addr.port = (enet_uint16)host_port_;
    client_ = enet_host_create(&addr, 1, 3, 0, 0);
  }
  if (!client_) { state_ = ERROR_ENCOUNTERED; error_msg_ = "Failed to create mm client"; return; }
  ENetAddress addr;
  enet_address_set_host(&addr, "mm.slippi.gg");
  addr.port = 43113;
  server_ = enet_host_connect(client_, &addr, 3, 0);
  if (!server_) { state_ = ERROR_ENCOUNTERED; error_msg_ = "Failed to start connection to mm server"; return; }
  int attempts = 0;
  while (!is_mm_connected_) {
    ENetEvent ev;
    int net = enet_host_service(client_, &ev, 500);
    if (net <= 0 || ev.type != ENET_EVENT_TYPE_CONNECT) {
      if (++attempts >= 20) { state_ = ERROR_ENCOUNTERED; error_msg_ = "Failed to connect to mm server"; host::log("slippi: cannot reach mm.slippi.gg"); return; }
      continue;
    }
    client_->intercept = intercept_callback;
    is_mm_connected_ = true;
    host::log("slippi: connected to matchmaking server");
  }
  std::string lan = local_address_string(&addr);
  std::string lan_addr = lan.empty() ? "" : lan + ":" + std::to_string(host_port_);
  std::vector<uint8_t> code_buf(search_settings_.connect_code.begin(), search_settings_.connect_code.end());
  json req;
  req["type"] = "create-ticket";
  req["user"] = {{"uid", me.uid}, {"playKey", me.play_key}, {"connectCode", me.connect_code}, {"displayName", me.display_name}};
  req["search"] = {{"mode", (int)search_settings_.mode}, {"connectCode", code_buf}};
  req["appVersion"] = SLIPPI_SEMVER;
  req["ipAddressLan"] = lan_addr;
  mm_send(server_, req);
  json resp;
  if (mm_receive(client_, resp, 5000) != 0) { state_ = ERROR_ENCOUNTERED; error_msg_ = "Failed to join mm queue"; return; }
  if (resp.value("type", "") != "create-ticket-resp") { state_ = ERROR_ENCOUNTERED; error_msg_ = "Invalid response when joining mm queue"; host::log("slippi: mm response: %s", resp.dump().c_str()); return; }
  std::string err = resp.value("error", "");
  if (!err.empty()) { state_ = ERROR_ENCOUNTERED; error_msg_ = err; host::log("slippi: mm error: %s", err.c_str()); return; }
  state_ = MATCHMAKING;
  host::log("slippi: matchmaking ticket created");
}

void Matchmaking::handleMatchmaking() {
  if (state_ != MATCHMAKING) return;
  json resp;
  int r = mm_receive(client_, resp, 2000);
  if (r == -1) return;
  if (r != 0) { state_ = ERROR_ENCOUNTERED; error_msg_ = "Lost connection to the mm server"; return; }
  if (resp.value("type", "") != "get-ticket-resp") { state_ = ERROR_ENCOUNTERED; error_msg_ = "Invalid response when getting mm status"; return; }
  std::string err = resp.value("error", "");
  std::string latest = resp.value("latestVersion", "");
  if (!err.empty()) {
    if (!latest.empty()) user_->OverwriteLatestVersion(latest);
    state_ = ERROR_ENCOUNTERED; error_msg_ = err;
    host::log("slippi: mm error: %s", err.c_str());
    return;
  }
  ingest_ticket(Ticket{resp});
  terminateMmConnection();
  state_ = OPPONENT_CONNECTING;
  host::log("slippi: opponent found (decider: %d)", is_host_);
}

// The part of a get-ticket-resp that describes the match: players, local index, the address to
// reach each remote (LAN when on the same external IP), decider, stages and items.
void Matchmaking::ingest_ticket(const Ticket& ticket) {
  const json& resp = ticket.resp;
  netplay_client_ = nullptr;
  remote_ips_.clear();
  player_info_.clear();
  std::string match_id = resp.value("matchId", "");
  host::log("slippi: match id %s", match_id.c_str());
  auto queue = resp["players"];
  if (queue.is_array()) {
    std::string local_external_ip;
    for (auto& el : queue) {
      UserInfo p;
      bool is_local = el.value("isLocalPlayer", false);
      p.uid = el.value("uid", "");
      p.display_name = el.value("displayName", "");
      p.connect_code = el.value("connectCode", "");
      p.port = el.value("port", 0);
      p.is_bot = el.value("isBot", false);
      if (el.count("chatMessages") && el["chatMessages"].is_array()) {
        for (auto& m : el["chatMessages"]) if (m.is_string()) p.chat_messages.push_back(m.get<std::string>());
      }
      if (p.chat_messages.size() != 16) p.chat_messages = User::GetDefaultChatMessages();
      player_info_.push_back(p);
      if (is_local) {
        std::string ip = el.value("ipAddress", "1.1.1.1:123");
        local_external_ip = ip.substr(0, ip.find(':'));
        local_player_index_ = p.port - 1;
      }
    }
    for (auto& el : queue) {
      if (el.value("port", 0) - 1 == local_player_index_) continue;
      std::string ext = el.value("ipAddress", "1.1.1.1:123");
      std::string lan = el.value("ipAddressLan", "1.1.1.1:123");
      if (ext.substr(0, ext.find(':')) != local_external_ip || lan.empty()) remote_ips_.push_back(ext);
      else remote_ips_.push_back(lan);
    }
  }
  is_host_ = resp.value("isHost", false);
  allowed_stages_.clear();
  if (resp.count("stages") && resp["stages"].is_array()) for (auto& s : resp["stages"]) allowed_stages_.push_back((uint16_t)s.get<int>());
  if (allowed_stages_.empty()) {
    allowed_stages_ = {0x3, 0x8, 0x1C, 0x1F, 0x20};
    if (player_info_.size() == 2) allowed_stages_.push_back(0x2);
  }
  mm_result_.id = match_id;
  mm_result_.players = player_info_;
  mm_result_.stages = allowed_stages_;
  mm_result_.items = resp.value("items", 0u);
}


void Matchmaking::handleConnecting() {
  netplay_client_ = nullptr;
  std::vector<std::string> addrs;
  std::vector<uint16_t> ports;
  for (auto& ip : remote_ips_) {
    size_t colon = ip.find(':');
    addrs.push_back(ip.substr(0, colon));
    ports.push_back((uint16_t)std::atoi(ip.substr(colon + 1).c_str()));
  }
  auto client = std::make_unique<NetplayClient>(addrs, ports, (uint8_t)remote_ips_.size(), (uint16_t)host_port_, is_host_, (uint8_t)local_player_index_);
  while (!netplay_client_) {
    auto st = client->GetSlippiConnectStatus();
    if (st == NetplayClient::ConnectStatus::INITIATED) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      if (state_ != OPPONENT_CONNECTING) return;
      continue;
    }
    if (st != NetplayClient::ConnectStatus::CONNECTED && client->GetRemoteBuild(0).received) {
      // It did connect: the opponent's build arrived, then they left (their build check refused
      // this one). Hand the client over so the build check can say why, instead of a plain failure.
      netplay_client_ = std::move(client);
      break;
    }
    if (st != NetplayClient::ConnectStatus::CONNECTED) {
      if (local_peer.enabled) { state_ = ERROR_ENCOUNTERED; error_msg_ = "Could not connect to the local peer"; return; }
      if (search_settings_.mode == TEAMS) { state_ = ERROR_ENCOUNTERED; error_msg_ = "Timed out waiting for other players to connect"; return; }
      host::log("slippi: connection attempt failed, searching again");
      state_ = INITIALIZING;
      return;
    }
    netplay_client_ = std::move(client);
  }
  state_ = CONNECTION_SUCCESS;
}


}  // namespace slippi
