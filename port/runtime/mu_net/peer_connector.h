// Brings up the UDP path to one peer (ENet, simultaneous open toward every candidate endpoint) and finds peers on the LAN.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "input_transport.h"
#include "peer_identity.h"
#include <functional>
#include <string>
#include <vector>

namespace mu_net {

struct Endpoint {
  std::string address;   // dotted IPv4. Names are not resolved here: a lookup can block for seconds.
  uint16_t port = 0;
};

// Port prediction. Some routers give every new destination the next port up, so when the port the
// lobby saw is no longer the one the other game sends from, the real one is often a neighbour. For
// each candidate on a public address, in the order given: port +1, +2, +3 and -1. Candidates on a
// private, loopback, link-local or carrier-shared address get none (there is no router to guess).
// Never an endpoint that is itself a candidate, never one twice, never more than kMaxPredicted.
// These are only sent the one-byte datagram of the punch, less often than the candidates; no
// connection is dialed to them.
constexpr int kMaxPredicted = 8;
std::vector<Endpoint> predicted_endpoints(const std::vector<Endpoint>& candidates);

enum class ConnectState : uint8_t { Idle, Connecting, Connected, Failed };
// Why no path came up inside the limit, from what arrived on the socket in that time.
enum class ConnectDiagnosis : uint8_t {
  None,
  NoReply,       // nothing arrived from any of the other player's addresses
  LanOnly,       // datagrams arrived from a local network address only, though public ones were given
  PortChanges,   // datagrams arrived from the other player's address but from a port nobody named
  OneWay,        // datagrams arrived from an address that was dialed, yet no connection completed
};
enum class ConnectFailure : uint8_t {
  None,
  NetworkStart,   // the network library would not start
  Bind,           // the local UDP port could not be opened (in use, or blocked)
  BadAddress,     // a candidate is not a dotted IPv4 address with a port
  TooMany,        // more candidates than kMaxCandidates
  TimedOut,       // no candidate answered inside the limit
  PeerClosed,     // the chosen path was closed by the other side or timed out
};

constexpr int kMaxCandidates = 6;
constexpr int kMaxPaths = 8;                        // candidates we dial plus connections that arrive
constexpr uint64_t kConnectLimitUs = 8 * 1000000;   // from connect() to the first path

class PeerConnector : public Link {
 public:
  PeerConnector();
  ~PeerConnector() override;
  PeerConnector(const PeerConnector&) = delete;
  PeerConnector& operator=(const PeerConnector&) = delete;

  // Opens the UDP socket. Port 0 lets the system choose; local_port() tells which it chose.
  bool open(uint16_t local_port, std::string* error);
  uint16_t local_port() const { return local_port_; }
  // Starts dialing every candidate at once and accepting connections that arrive on the socket.
  // Both peers call this toward each other at the same time: each side's outgoing packets open
  // its own NAT mapping for the other's (hole punching). An empty list only listens, which is
  // enough for a host whose port is forwarded. Returns at once; poll() makes the progress.
  // The punch (a one-byte datagram to every candidate every 100 ms, and to the predicted neighbour
  // ports every 300 ms from the second second on) runs from this call until a path is up or the
  // limit passes: the two games do not share a start time, so each simply punches for its whole
  // window. A dial that the network library gives up on (about 15 s) is started again.
  bool connect(const std::vector<Endpoint>& candidates, uint64_t now_us, std::string* error);
  void set_limit_us(uint64_t limit) { limit_us_ = limit; }
  // Lines for the host's log: the paths that come up, the one chosen, and what was heard when none
  // did. Called from poll() and pin_path(), on the caller's thread. They name IP addresses.
  void set_log(std::function<void(const char*)> log);

  ConnectState state() const { return state_; }
  ConnectFailure failure() const { return failure_; }
  ConnectDiagnosis diagnosis() const { return diagnosis_; }   // set with ConnectFailure::TimedOut
  // One sentence for the player, naming what to try when the cause is a blocked path. The game
  // shows the first 120 characters, so the cause comes first.
  const char* failure_text() const;
  std::string peer_address() const;   // of the chosen path, for the log; empty before one is chosen
  // The chosen path in words: its address, which of the addresses given it was (or that it was a
  // guessed port, or one nobody named), and which side dialed it. Empty before one is chosen.
  std::string path_text() const;
  uint64_t packets_refused() const { return refused_; }

  // Link
  bool send(uint8_t channel, const uint8_t* data, size_t size, bool reliable) override;
  void poll(uint64_t now_us, const Receiver& on_packet) override;
  bool connected() const override;
  bool failed(std::string* why) const override;
  uint32_t generation() const override { return generation_; }
  void pin_path() override;
  void flush() override;
  void close() override;

 private:
  struct Impl;
  Impl* impl_;
  uint16_t local_port_ = 0;
  ConnectState state_ = ConnectState::Idle;
  ConnectFailure failure_ = ConnectFailure::None;
  ConnectDiagnosis diagnosis_ = ConnectDiagnosis::None;
  uint64_t limit_us_ = kConnectLimitUs;
  uint32_t generation_ = 0;
  uint64_t refused_ = 0;
};

// LAN discovery: a small signed beacon broadcast on a fixed UDP port. It says "this identity is
// waiting on this port"; the address is the datagram's source. A beacon proves who signed it, not
// who relayed it, so the session handshake still checks the identity before anything is trusted.
struct LanPeer {
  std::string address;
  uint16_t port = 0;       // the game's UDP port, for PeerConnector
  Bytes32 identity{};
  std::string name;
  std::string build_id;
  uint64_t seen_us = 0;
};

class LanBeacon {
 public:
  static constexpr uint16_t kPort = 47633;
  static constexpr size_t kMaxPeers = 32;
  LanBeacon();
  ~LanBeacon();
  LanBeacon(const LanBeacon&) = delete;
  LanBeacon& operator=(const LanBeacon&) = delete;
  bool open(std::string* error);
  // Broadcasts one beacon. Call about once a second while waiting for a LAN opponent.
  bool announce(const PeerIdentity& identity, uint16_t game_port, const std::string& name, const std::string& build_id);
  // Reads what arrived, without waiting. `peers` is updated in place: one entry per identity,
  // entries not heard for 5 s removed, never more than kMaxPeers. Our own beacons are skipped.
  void poll(uint64_t now_us, const Bytes32* own_identity, std::vector<LanPeer>& peers);
  void close();

 private:
  intptr_t socket_;
};

}  // namespace mu_net
