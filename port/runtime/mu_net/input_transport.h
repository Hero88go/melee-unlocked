// The input messages of a mu_net session (COMMIT, ACK, PING, PONG, SAMPLE), the link they travel on, and a deterministic fault shim.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "mu_net_util.h"
#include "session_types.h"
#include <functional>
#include <string>
#include <vector>

namespace mu_net {

// The path to the peer, as the session sees it: two channels, packets in, packets out, never
// blocking. PeerConnector is the real one (ENet over UDP); tests supply an in-memory pair and wrap
// either in FaultLink. A future relay would be one more implementation, enabled only by decision.
struct Link {
  using Receiver = std::function<void(uint8_t channel, const uint8_t* data, size_t size)>;
  virtual ~Link() = default;
  virtual bool send(uint8_t channel, const uint8_t* data, size_t size, bool reliable) = 0;
  // Pumps the network without waiting and hands every packet that arrived to `on_packet`.
  virtual void poll(uint64_t now_us, const Receiver& on_packet) = 0;
  virtual bool connected() const = 0;
  virtual bool failed(std::string* why) const { (void)why; return false; }
  // Counts paths that came up. While several candidate paths are open a send goes to all of them,
  // so the session repeats its hello when this changes.
  virtual uint32_t generation() const { return 1; }
  // Called from inside `on_packet`: keep the path this packet came on and close the others.
  virtual void pin_path() {}
  virtual void flush() {}
  virtual void close() {}
};

// Deterministic packet faults for tests: the same seed gives the same drops, duplicates and
// reordering on every run. Only unreliable sends are touched (the reliable channel's delivery is
// ENet's promise, and the handshake rides on it).
struct FaultPlan {
  uint32_t seed = 1;
  uint32_t drop_per_1000 = 0;
  uint32_t duplicate_per_1000 = 0;
  uint32_t reorder_per_1000 = 0;
  uint32_t max_hold = 3;      // a reordered packet goes out after 1..max_hold later sends
  bool blackout = false;      // drop every unreliable send (a dead direction)
};

class FaultLink : public Link {
 public:
  FaultLink(Link& inner, const FaultPlan& plan) : inner_(inner), plan_(plan), rng_(plan.seed ? plan.seed : 1) {}
  void set_plan(const FaultPlan& plan) { plan_ = plan; }
  bool send(uint8_t channel, const uint8_t* data, size_t size, bool reliable) override;
  void poll(uint64_t now_us, const Receiver& on_packet) override { inner_.poll(now_us, on_packet); }
  bool connected() const override { return inner_.connected(); }
  bool failed(std::string* why) const override { return inner_.failed(why); }
  uint32_t generation() const override { return inner_.generation(); }
  void pin_path() override { inner_.pin_path(); }
  void flush() override { inner_.flush(); }
  void close() override { inner_.close(); }
  uint64_t dropped = 0, duplicated = 0, reordered = 0;

 private:
  struct Held { uint32_t sends_left; uint8_t channel; std::vector<uint8_t> bytes; };
  uint32_t roll();
  Link& inner_;
  FaultPlan plan_;
  uint32_t rng_;
  std::vector<Held> held_;
};

// Plain message types. 0x01..0x0F travel on the control channel (rollback_session), 0x10..0x1F on
// the input channel (here). A type this build does not know is counted and dropped.
enum MessageType : uint8_t {
  kMsgVerify = 0x01,
  kMsgBye = 0x02,
  kMsgResult = 0x03,
  kMsgInfo = 0x04,     // revision 1: the sender's revision and how many games it plays in one session
  kMsgNext = 0x05,     // revision 1: the sender is ready for the next game, with that game's descriptor digest
  kMsgCommit = 0x10,
  kMsgAck = 0x11,
  kMsgPing = 0x12,
  kMsgPong = 0x13,
  kMsgSample = 0x14,   // reserved: provisional sub-frame input, parsed and ignored for now
};

constexpr int kMaxCommitHistory = 128;        // frames one COMMIT can carry
constexpr size_t kCommitHeader = 22;
constexpr size_t kSampleSize = 21;
constexpr int kTimeSyncSamples = 30;
constexpr int kPadRing = 256;                 // frames of pads kept each way
constexpr int kMaxRemoteLead = 200;           // frames past our finalized frame a peer may commit

struct TransportStats {
  uint64_t commits_sent = 0, commits_received = 0;
  uint64_t acks_sent = 0, acks_received = 0;
  uint64_t malformed = 0;          // wrong length, impossible frame numbers, unknown type
  uint64_t gaps = 0;               // a COMMIT whose history did not reach back to what we hold
  uint64_t conflicts = 0;          // a frame committed again with different pad bytes
  uint64_t samples_ignored = 0;
  uint64_t too_far_ahead = 0;
  uint64_t other_game = 0;         // well formed, but numbered for another game of the session: dropped whole
};

// Bytes in, bytes out: no sockets, no clock of its own, no cryptography. The session seals what
// this builds and opens what this parses.
class InputTransport {
 public:
  // `base` is what this game's frame numbers are raised by on the wire (0 for a session's first
  // game). Every frame number this class takes and gives is the game's own, counted from 1.
  void reset(int32_t frame_us, int32_t base = 0);

  // Local side. Frames are committed once, in order, starting at 1; a commit is final.
  bool commit(int32_t frame, const uint8_t pad[kPadWireSize]);
  void set_checksum(int32_t frame, uint32_t checksum);
  // The COMMIT to send now: every frame the peer has not acknowledged, newest first, at most
  // kMaxCommitHistory. Empty while nothing was committed. Also records (frame, now) as this side's
  // frame clock for the time-offset estimate, so it is called once a simulation tick, stalled or not.
  void build_commit(uint64_t now_us, std::vector<uint8_t>& out);
  void build_ping(uint64_t now_us, std::vector<uint8_t>& out) const;

  // Remote side. `reply` is left empty or filled with one message to send back (ACK or PONG).
  // False when the message was refused as malformed.
  bool on_message(const uint8_t* data, size_t size, uint64_t now_us, std::vector<uint8_t>& reply);
  // The finalized frame of the local game. The peer may not commit more than kMaxRemoteLead
  // frames past it, which bounds what one peer can make this side store.
  void set_floor(int32_t finalized) { if (finalized > floor_) floor_ = finalized; }

  int32_t local_latest() const { return local_latest_; }
  int32_t remote_latest() const { return remote_latest_; }   // every frame up to here has arrived
  int32_t peer_ack() const { return peer_ack_; }
  bool local_pad(int32_t frame, uint8_t out[kPadWireSize]) const;
  bool remote_pad(int32_t frame, uint8_t out[kPadWireSize]) const;
  int32_t remote_checksum_frame() const { return remote_checksum_frame_; }
  uint32_t remote_checksum() const { return remote_checksum_; }
  bool has_ping() const { return has_ping_; }
  uint32_t ping_us() const { return ping_us_; }
  // How far this side's frame clock runs ahead of the peer's, in microseconds (negative: behind).
  // The mean of the middle third of the last kTimeSyncSamples samples.
  int32_t time_offset_us() const;
  int time_samples() const { return offset_count_; }
  uint64_t last_progress_us() const { return last_progress_us_; }   // when the peer last delivered a new frame
  const TransportStats& stats() const { return stats_; }

 private:
  struct Slot { int32_t frame = 0; uint8_t pad[kPadWireSize] = {}; };
  static int index(int32_t frame) { return (int)((uint32_t)frame % kPadRing); }
  bool on_commit(Reader& r, size_t size, uint64_t now_us, std::vector<uint8_t>& reply);

  Slot local_[kPadRing], remote_[kPadRing];
  int32_t frame_us_ = 16683;
  int32_t base_ = 0;
  int32_t local_latest_ = 0, remote_latest_ = 0, peer_ack_ = 0, floor_ = 0;
  int32_t newest_remote_seen_ = 0;
  int32_t checksum_frame_ = 0, remote_checksum_frame_ = 0;
  uint32_t checksum_ = 0, remote_checksum_ = 0;
  bool clock_set_ = false;
  int32_t clock_frame_ = 0;
  uint64_t clock_us_ = 0;
  bool has_ping_ = false;
  uint32_t ping_us_ = 0;
  int32_t offsets_[kTimeSyncSamples] = {};
  int offset_count_ = 0, offset_next_ = 0;
  uint64_t last_progress_us_ = 0;
  TransportStats stats_;
};

}  // namespace mu_net
