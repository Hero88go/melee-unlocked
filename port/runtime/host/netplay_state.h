// Neutral netplay state. The renderer, the pad code, the settings panel and the Source Port host ask
// a few questions about a network session (is a match running, which slot is local, what is the
// ping) and hand the game's command channel to whoever runs the session. This header holds those
// answers and that hand-off with no network code of its own: a session sets them, everyone else
// reads them. A build without a session reads "offline" everywhere.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace host::netplay {

// Mode ids shared with the game (MuOnlineMatch.mode in mu_host.h). -1 is offline.
enum Mode : int { kOffline = -1, kUnranked = 1, kDirect = 2, kTeams = 3, kParty = 4 };
// Frames a rollback can reach back; the snapshot pool keeps this many states of the game.
constexpr int kRollbackMaxFrames = 7;

// Launch settings a session reads. lobby_status_file and lobby_code also drive the status mailbox
// and the exit after one negotiated match (host.cpp publish_lobby_status).
struct Config {
  std::string lobby_code;
  int lobby_character = 2;
  std::string lobby_status_file;
  std::string user_dir;
  int delay = 2;   // local input delay in frames
  int chat = 0;
};
inline Config& config() { static Config c; return c; }

// What a session publishes. Written on the simulation thread, read from any thread.
struct Session {
  std::atomic<int> mode{kOffline};       // set from the moment a session is being set up
  std::atomic<bool> in_match{false};     // the match itself is running
  std::atomic<bool> in_menus{false};     // negotiating or waiting, not in the match
  std::atomic<int> local_slot{0};        // the in-game slot (0-3) of the local player
  std::atomic<int> ping_ms{0};
  std::atomic<uint64_t> rollbacks{0};
};
inline Session& session() { static Session s; return s; }

inline int session_mode() { return session().mode.load(std::memory_order_relaxed); }
inline bool is_online_match() { return session().in_match.load(std::memory_order_relaxed); }
inline bool in_online_menus() { return session().in_menus.load(std::memory_order_relaxed); }
inline int local_player_slot() { return session().local_slot.load(std::memory_order_relaxed); }
inline int ping_ms() { return session().ping_ms.load(std::memory_order_relaxed); }
inline uint64_t rollback_count() { return session().rollbacks.load(std::memory_order_relaxed); }
inline void note_rollback() { session().rollbacks.fetch_add(1, std::memory_order_relaxed); }

// Names for the overlay only; they never enter the synchronized game state.
namespace detail {
inline std::mutex& mutex() { static std::mutex m; return m; }
inline std::array<std::string, 4>& names() { static std::array<std::string, 4> n; return n; }
}  // namespace detail
inline void set_player_names(const std::array<std::string, 4>& names) {
  std::lock_guard<std::mutex> lock(detail::mutex());
  detail::names() = names;
}
inline std::array<std::string, 4> player_names() {
  std::lock_guard<std::mutex> lock(detail::mutex());
  return is_online_match() ? detail::names() : std::array<std::string, 4>{};
}

// A match the game should enter without any menu. The Source Port host answers the game's
// online_test_match callback from this: the game then boots to the match scene, waits there for the
// match state (kMatchState) to report a connected, ready match, and starts it.
struct MatchRequest {
  bool requested = false;
  bool harness = false;    // true: an automated test (the game may use scripted pads)
  int mode = kDirect;
  int local_port = 0;      // the controller port the local pad is read from (0-3)
};
namespace detail {
inline MatchRequest& request() { static MatchRequest r; return r; }
}  // namespace detail
inline void set_match_request(const MatchRequest& request) {
  std::lock_guard<std::mutex> lock(detail::mutex());
  detail::request() = request;
}
inline MatchRequest match_request() {
  std::lock_guard<std::mutex> lock(detail::mutex());
  return detail::request();
}

// ---- the game's command channel ----
// The native game sends one command byte and a fixed payload and reads a reply (MuHostApi's command
// callback). These are the commands its rollback engine uses; a session answers kInputs,
// kMatchState and kReportGame, the host itself answers the two snapshot commands.
enum Command : uint8_t {
  kInputs = 0xB0,          // payload 25: frame, finalized frame, checksum, delay, the local pad
  kCaptureState = 0xB1,    // payload 32: frame
  kLoadState = 0xB2,       // payload 32: frame
  kMatchState = 0xB3,      // no payload; reply kMatchStateSize bytes
  kFindOpponent = 0xB4,
  kReportGame = 0xBD,      // payload 368: the finished game
};
enum : int32_t { kCommandOk = 0, kCommandUnknown = -1, kCommandBounds = -2, kCommandNeedsRollback = -3 };
constexpr uint32_t kMatchStateSize = 962;      // the game's match state block (mu_online.c MSRB_SIZE)
constexpr uint32_t kMatchStateError = 357;     // offset of its error text
constexpr uint8_t kStateError = 5;             // connection state: no match can be made
constexpr uint8_t kInputsDisconnected = 3;     // kInputs result: the match is gone

// True for the command ids a network session owns (the rest are the host's own features).
inline bool is_session_command(uint8_t cmd) {
  return (cmd >= 0xB0 && cmd <= 0xC4) || (cmd >= 0xD5 && cmd <= 0xD8) || (cmd >= 0xE3 && cmd <= 0xE5);
}

inline bool payload_valid(uint8_t cmd, const uint8_t* payload, uint32_t size) {
  uint32_t expected = 0;
  switch (cmd) {
    case kInputs: expected = 25; break;
    case kCaptureState: case kLoadState: expected = 32; break;
    case kMatchState: expected = 0; break;
    case kReportGame: expected = 368; break;
    default: return true;
  }
  return size == expected && (!expected || payload);
}

// The session's handler: fills `reply` and returns true for a command it answers.
using CommandHandler = bool (*)(uint8_t cmd, const uint8_t* payload, uint32_t size, std::vector<uint8_t>& reply);
namespace detail {
inline std::atomic<CommandHandler>& handler() { static std::atomic<CommandHandler> h{nullptr}; return h; }
}  // namespace detail
inline void set_command_handler(CommandHandler handler) { detail::handler().store(handler); }
// True once a session installed its handler. The build with the Slippi layer asks this to pick who
// answers the game's session commands: this session, or Slippi's.
inline bool has_command_handler() { return detail::handler().load() != nullptr; }

// Without a session every session command gets the answer "there is no match": the game then plays
// offline (kMatchState) or ends the match as disconnected (kInputs) instead of waiting forever.
inline bool handle(uint8_t cmd, const uint8_t* payload, uint32_t size, std::vector<uint8_t>& reply) {
  if (CommandHandler handler = detail::handler().load()) return handler(cmd, payload, size, reply);
  if (!is_session_command(cmd)) return false;
  if (cmd == kInputs) reply.push_back(kInputsDisconnected);
  if (cmd == kMatchState) {
    static const char text[] = "No network session";
    reply.assign(kMatchStateSize, 0);
    reply[0] = kStateError;
    std::memcpy(reply.data() + kMatchStateError, text, sizeof text);
  }
  return true;
}

// One command from the game: bounds, payload size, then `answer(cmd, payload, size, reply)`.
template <class Answer>
int32_t dispatch(uint8_t cmd, const uint8_t* payload, uint32_t payload_size, uint8_t* response,
                 uint32_t response_capacity, uint32_t* response_size, uint32_t minimum_capacity, Answer&& answer) {
  if (!response_size) return kCommandBounds;
  *response_size = 0;
  if (!response || response_capacity < minimum_capacity || (payload_size && !payload)) return kCommandBounds;
  if (!payload_valid(cmd, payload, payload_size)) return kCommandBounds;
  std::vector<uint8_t> reply;
  if (!answer(cmd, payload, payload_size, reply)) return kCommandUnknown;
  if (reply.size() > response_capacity) return kCommandBounds;
  if (!reply.empty()) std::memcpy(response, reply.data(), reply.size());
  *response_size = (uint32_t)reply.size();
  return kCommandOk;
}

// ---- host options that used to ride on the network layer (source_noslippi.cpp) ----
// The Music slider (0..100). The game reads it through MuHostApi.music_volume and scales its own
// music stream.
int music_volume();
void set_music_volume(int percent);
// Widescreen 16:9 and Fountain of Dreams reflections: asked from the render thread, applied on the
// simulation thread at the next retrace (poll_options), where the game reads the flags.
void request_widescreen(bool on);
void request_fod_reflections(bool on);
bool widescreen();
void poll_options();

}  // namespace host::netplay

#ifdef MELEE_NO_SLIPPI
// The build without the Slippi layer: the names the shared host code uses for it answer from the
// neutral state above, so that code is the same text in both builds.
#include "native_online_policy.h"   // NativeGameplayProfile and the mod identity checks
// Standard headers the replaced headers brought with them; some of the shared code relies on that.
#include <deque>
#include <map>
#include <memory>
#include <thread>
#include <unordered_map>

namespace slippi {

constexpr int ROLLBACK_MAX_FRAMES = host::netplay::kRollbackMaxFrames;
struct Matchmaking {
  enum OnlinePlayMode { RANKED = 0, UNRANKED = 1, DIRECT = 2, TEAMS = 3, PARTY = 4 };
};

inline void init() {}
inline void shutdown() {}
inline void dma_write(uint32_t, uint32_t) {}
inline void dma_read(uint32_t, uint32_t) {}
inline void imm_write(uint32_t, uint32_t) {}
inline uint32_t imm_read(uint32_t) { return 0; }
inline uint32_t gct_load_address() { return 0; }
inline void for_each_served_code_write(const std::function<void(uint32_t, uint32_t)>&) {}
inline uint64_t commands_seen() { return 0; }
inline uint64_t replays_written() { return 0; }
inline bool gct_range(uint32_t*, uint32_t*) { return false; }
inline void discard_current_replay(const char* = nullptr) {}
inline void request_widescreen(bool on) { host::netplay::request_widescreen(on); }
inline bool widescreen() { return host::netplay::widescreen(); }
inline void request_fod_reflections(bool on) { host::netplay::request_fod_reflections(on); }
inline void poll_options() { host::netplay::poll_options(); }

namespace playback {
inline bool enabled() { return false; }
}  // namespace playback

// Music plays through the game's own stream in this build; only the Music slider remains.
namespace jukebox {
using DiscReader = bool (*)(uint32_t offset, void* dst, uint32_t size);
inline void start_song(uint32_t, uint32_t) {}
inline void stop() {}
inline void set_disc_reader(DiscReader) {}
inline void set_music_pack_reader(bool (*)(uint32_t, std::filesystem::path*)) {}
inline bool resolve_music_pack_path(const std::string&, std::filesystem::path*) { return false; }
inline void open_music_packs_folder() {}
inline void set_music_packs_enabled(bool) {}
inline bool music_packs_enabled() { return false; }
inline void set_melee_volume(uint8_t) {}
inline void set_next_song_gain(float) {}
inline void set_paused(bool) {}
inline void mix(int16_t*, size_t, double, double = 32000.0) {}
inline void set_user_volume(int percent) { host::netplay::set_music_volume(percent); }
inline int user_volume() { return host::netplay::music_volume(); }
}  // namespace jukebox

namespace online {
using Config = host::netplay::Config;
inline Config& config() { return host::netplay::config(); }
inline void init() {}
inline void shutdown() {}
inline void set_native_gameplay_profile(NativeGameplayProfile) {}
// The content identity a session may send to the other side; kept so the mod code is unchanged.
struct LocalBuild {
  bool mod_view = false;
  std::string fingerprint;
  std::string name;
  bool allow_unverified = false;
  bool extended_content = false;
};
inline LocalBuild& local_build_storage() { static LocalBuild b; return b; }
inline void set_local_build(const LocalBuild& build) { local_build_storage() = build; }
inline const LocalBuild& local_build() { return local_build_storage(); }

enum Cmd : uint8_t {
  CMD_ONLINE_INPUTS = host::netplay::kInputs, CMD_CAPTURE_SAVESTATE = host::netplay::kCaptureState,
  CMD_LOAD_SAVESTATE = host::netplay::kLoadState, CMD_GET_MATCH_STATE = host::netplay::kMatchState,
  CMD_FIND_OPPONENT = host::netplay::kFindOpponent, CMD_REPORT_GAME = host::netplay::kReportGame,
};
template <class Handler>
int32_t native_command(uint8_t command, const uint8_t* payload, uint32_t payload_size, uint8_t* response,
                       uint32_t response_capacity, uint32_t* response_size, uint32_t minimum_capacity,
                       bool /*native_savestates*/, Handler&& handle) {
  return host::netplay::dispatch(command, payload, payload_size, response, response_capacity, response_size,
                                 minimum_capacity, handle);
}
inline bool handle(uint8_t cmd, const uint8_t* payload, uint32_t size, std::vector<uint8_t>& reply) {
  return host::netplay::handle(cmd, payload, size, reply);
}
constexpr int32_t kRollbackFrameUnknown = INT32_MIN;
inline uint64_t rollback_count() { return host::netplay::rollback_count(); }
inline void note_rollback(int32_t = kRollbackFrameUnknown) { host::netplay::note_rollback(); }
inline bool is_online_match() { return host::netplay::is_online_match(); }
inline int local_player_slot() { return host::netplay::local_player_slot(); }
inline int local_player_index() { return host::netplay::local_player_slot(); }
inline std::array<std::string, 4> player_names_for_overlay() { return host::netplay::player_names(); }
inline int ping_ms() { return host::netplay::ping_ms(); }
inline int session_mode() { return host::netplay::session_mode(); }
inline bool in_online_menus() { return host::netplay::in_online_menus(); }
}  // namespace online

// The settings panel draws a search panel over offline play when one is offered. Nothing offers one
// here: the snapshot is always idle with the panel unavailable, so that code never shows.
namespace native_practice {
enum class Phase : uint8_t { Idle, Searching, Handoff, OnlineFlow, InMatch, Failure, ReturningToPractice };
enum class MatchMode : uint8_t { None = 0xFF, Unranked = 1, Direct = 2 };
struct Snapshot {
  Phase phase = Phase::Idle;
  MatchMode mode = MatchMode::None;
  bool in_practice = false;
  bool tab_available = false;
  bool can_start = false;
  bool cosmetic_profile_locked = false;
  int controller_port = 0;
  int matchmaking_state = 0;
  uint32_t generation = 0;
  uint32_t search_ticks = 0;
  std::string connect_code;
  std::string status;
  std::string detail;
  std::string opponent;
};
inline Snapshot snapshot() { return {}; }
inline void submit_start_unranked() {}
inline void submit_start_direct(const std::string&) {}
inline void submit_cancel() {}
inline void submit_acknowledge_failure() {}
inline void tick() {}
inline void shutdown() {}
inline bool cosmetic_profile_locked() { return false; }
inline const char* match_mode_name(MatchMode) { return ""; }
inline bool normalize_direct_code(const std::string&, std::string*, std::string*) { return false; }
inline std::string format_search_duration(uint32_t) { return {}; }
inline bool phase_forces_input_capture(Phase) { return false; }
inline bool phase_shows_return_overlay(Phase, bool) { return false; }
using NativeBridge = int32_t (*)(int32_t op, int32_t* args, int32_t count);
inline void set_native_bridge(NativeBridge) {}
using ContentBridge = void (*)(int mode, bool activate);
inline void set_content_bridge(ContentBridge) {}
}  // namespace native_practice

}  // namespace slippi
#endif  // MELEE_NO_SLIPPI
