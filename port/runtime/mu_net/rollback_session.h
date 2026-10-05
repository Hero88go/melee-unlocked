// The session object a host wires to the game's online commands: connect, handshake, then answer B0 (inputs) and B3 (match state).
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "input_transport.h"
#include "peer_connector.h"
#include "peer_identity.h"
#include "result_outbox.h"
#include "session_types.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mu_net {

// Sizes of the two replies the game reads (its own contract, big-endian inside).
constexpr size_t kInputsReplySize = 297;
constexpr size_t kMatchStateReplySize = 962;
constexpr size_t kInputsPayloadSize = 25;
// First byte of the inputs reply.
enum InputsResult : uint8_t { kInputsNormal = 1, kInputsSkip = 2, kInputsDisconnected = 3, kInputsAdvance = 4 };

// The session's only way out to the host. Any of them may be empty. `log` and
// `set_emulation_speed` can be called from the session's own pump thread, so they must be safe to
// call from a thread other than the simulation's.
struct SessionCallbacks {
  std::function<void(const char* line)> log;
  std::function<uint64_t()> now_us;                    // monotonic; a steady clock is used when empty
  std::function<void(double speed)> set_emulation_speed;
};

struct SessionOptions {
  uint64_t connect_limit_us = 8 * 1000000;   // from start() to both descriptors verified
  uint64_t input_limit_us = 7 * 1000000;     // a wait for the peer's inputs longer than this ends the match
  int32_t game_frame_us = 16683;             // the game's 59.94 Hz frame, which the time sync is built on
  int32_t host_frame_us = 16667;             // what the host's pacer ticks at speed 1.0
  bool time_sync = true;                     // shed, advance and speed corrections every 30 frames
  // A thread that pumps the network about every millisecond, so packets are stamped when they
  // arrive and not when the simulation next asks. Tests turn it off and call tick() themselves.
  bool pump_thread = true;
  // Games this side plays over one connection; 0 is no limit. The session plays the smaller of the
  // two sides' numbers, and one game with a peer that does not say (protocol revision 0).
  uint32_t max_games = 1;
  // The stages a game after the first is drawn from (next_descriptor). Not part of the descriptor:
  // two sides with different pools disagree on the next digest and the session ends there.
  std::vector<uint16_t> stage_pool;
  uint64_t next_limit_us = 20 * 1000000;     // after this side's NEXT: how long the peer's may take
  // Tests only: 0 behaves as a revision 0 peer (no INFO, no NEXT, both ignored when received).
  uint8_t minor = kProtocolMinor;
};

struct EndpointInfo {
  uint16_t local_port = 0;                // UDP port to open; 0 lets the system choose
  std::vector<Endpoint> candidates;       // where the peer may be reached; empty = wait for it to connect
  bool has_expected_peer = false;         // from an invitation or the lobby
  Bytes32 expected_peer{};
};

enum class SessionState : uint8_t {
  Idle,          // not started, or stopped
  Connecting,    // no path to the peer yet
  Handshaking,   // a path is up; hellos are in flight
  Verifying,     // keys agreed; descriptor digests are being compared
  Ready,         // both sides hold the same descriptor; the game may start frame 1
  Playing,
  Ended,         // the game reported its end
  Failed,
};

// Where a session stands between two games (meaningful once a game has ended).
enum class NextGame : uint8_t {
  None,      // no game has ended yet, or the next one has already begun
  Waiting,   // this side sent NEXT; the peer's has not arrived
  Agreed,    // both sent NEXT with the same digest: begin_next_game() may be called
  Over,      // no further game: the limit, a quit, a disconnect, a desync, a refusal or no answer
};

struct SessionCounters {
  uint64_t waits = 0;            // times the game had to wait for the peer's inputs
  uint64_t stalls = 0;           // frames skipped while waiting
  uint64_t sync_skips = 0;       // frames shed by the time sync
  uint64_t advances = 0;         // extra frames run by the time sync
  uint64_t checksums_compared = 0, checksum_mismatches = 0;
  uint64_t packets_rejected = 0; // refused by size, version, authentication or replay
  uint64_t messages_malformed = 0;
  uint32_t ping_us = 0;
  int32_t time_offset_us = 0;    // positive: this side's frame clock is ahead
  int32_t local_frame = 0, remote_frame = 0;   // latest committed frame of each side
  int32_t transcript_frames = 0;
  bool desync = false;
  bool peer_gone = false;
  TransportStats transport;
};

class RollbackSession {
 public:
  RollbackSession();
  ~RollbackSession();
  RollbackSession(const RollbackSession&) = delete;
  RollbackSession& operator=(const RollbackSession&) = delete;

  // Before start(). The identity signs the handshake and, later, the result file.
  void configure(const SessionCallbacks& callbacks, const SessionOptions& options);
  void set_identity(const PeerIdentity& identity);
  // Only when the descriptor already names this side's session key (a lobby that exchanged the
  // keys before the game started): the X25519 secret that belongs to it. Otherwise a fresh key is
  // made for every start() and written into the session's copy of the descriptor.
  void set_session_secret(const Bytes32& x25519_secret);

  // Opens the socket and begins connecting. Returns at once; false with failure_text() set when
  // the descriptor is not playable or the socket cannot be opened.
  bool start(const SessionDescriptor& descriptor, int local_slot, const EndpointInfo& endpoint);
  // The same over a link the caller owns (tests, or a host that connected the peer itself). The
  // link must outlive the session or its stop().
  bool start(const SessionDescriptor& descriptor, int local_slot, Link& link, const Bytes32* expected_peer = nullptr);

  // Pumps the network. Never waits. Harmless to call from the host as well as the pump thread.
  void tick();

  // Command 0xB0. `reply` always comes back kInputsReplySize bytes long.
  void on_inputs(int32_t frame, int32_t finalized, uint32_t finalized_checksum, uint8_t delay,
                 const uint8_t pad[kPadGameSize], std::vector<uint8_t>& reply);
  // The same from the raw 25-byte command payload. A short payload answers "disconnected".
  void on_inputs(const uint8_t* payload, size_t size, std::vector<uint8_t>& reply);
  // Command 0xB3. `reply` always comes back kMatchStateReplySize bytes long.
  void match_state_reply(std::vector<uint8_t>& reply);
  // Command 0xBD, with its 368-byte payload.
  void on_game_end(const uint8_t* payload, size_t size);
  // Command 0xBA, and any other end of the session. Tells the peer, closes the path, speed 1.0.
  void stop();

  // More than one game over one connection. on_game_end() sends NEXT when another game may follow;
  // next_game() says where that stands and, when it is Over, `why` gets one plain sentence.
  NextGame next_game(std::string* why = nullptr) const;
  // With next_game() == Agreed: the session becomes Ready for the next game with its descriptor,
  // fresh frame numbers, transcript, checksums and counters. The finished game's result() is gone
  // after this, so a host writes its result file first. False in any other state.
  bool begin_next_game();
  uint32_t game_index() const;                // 1 for the first game of the session

  SessionState state() const;
  std::string failure_text() const;
  SessionCounters counters() const;
  uint16_t local_port() const;
  Bytes32 descriptor_digest() const;          // meaningful from Verifying on
  SessionDescriptor descriptor() const;       // with the handshake's keys filled in
  // The record of the game for result_outbox. False before a game was played.
  bool result(GameResult& out) const;
  // Convenience: result() then write_result() with the session's identity.
  bool write_result_file(const std::string& path_utf8, std::string* error) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mu_net
