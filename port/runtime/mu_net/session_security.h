// The authenticated handshake and the packet protection of one mu_net gameplay connection.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "mu_net_util.h"
#include "peer_identity.h"
#include <vector>

namespace mu_net {

// ENet channels. Control is reliable and ordered; input is unreliable and unsequenced.
constexpr uint8_t kChannelControl = 0;
constexpr uint8_t kChannelInput = 1;
constexpr uint8_t kChannelCount = 2;

constexpr uint8_t kPacketVersion = 1;       // first byte of every protected packet
constexpr size_t kHelloSize = 160;          // the one plain message, exact
constexpr size_t kPacketHeader = 10;        // version, channel, 64-bit sequence
constexpr size_t kPacketMac = 16;
constexpr size_t kMaxPlaintext = 1200;      // keeps every datagram under a common path MTU with ENet's own header
constexpr size_t kMaxPacket = kPacketHeader + kMaxPlaintext + kPacketMac;
constexpr unsigned kReplayWindow = 64;      // packets of reordering the input channel tolerates

enum class SecurityError : uint8_t {
  None,
  TooShort,
  TooLong,
  BadMagic,
  BadVersion,      // a hello or a packet of a protocol version this build does not speak
  BadChannel,
  BadSignature,
  WrongPeer,       // a valid hello, signed by a key other than the expected one
  Reflected,       // our own hello came back
  WeakKey,         // the key exchange produced the all-zero secret
  NotReady,        // a protected packet before the handshake finished
  Replay,          // a sequence number already seen, or older than the window
  BadMac,
  Duplicate,       // the same hello again (a second path to the same peer): harmless
};
const char* describe(SecurityError error);

class SessionSecurity {
 public:
  SessionSecurity();
  ~SessionSecurity();
  SessionSecurity(const SessionSecurity&) = delete;
  SessionSecurity& operator=(const SessionSecurity&) = delete;

  // Starts a handshake: a fresh ephemeral key and nonce, and the signed hello to send.
  // `expected_peer`, when given, is the only identity whose hello is accepted.
  // `ephemeral_secret`, when given, is used instead of a fresh one (a descriptor that already
  // names this side's session key needs the matching secret).
  bool begin(const PeerIdentity& identity, const Bytes32* expected_peer, const Bytes32* ephemeral_secret = nullptr);
  const std::vector<uint8_t>& hello() const { return hello_; }

  // Size, magic and version are checked before the signature; the signature before anything is kept.
  SecurityError accept_hello(const uint8_t* data, size_t size);
  static bool is_hello(const uint8_t* data, size_t size) { return size >= 4 && data[0] == 'M' && data[1] == 'U' && data[2] == 'N' && data[3] == 'H'; }

  bool established() const { return established_; }
  // True on exactly one side: the one whose ephemeral key sorts first. That side chooses the path.
  bool decider() const { return decider_; }
  const Bytes32& peer_identity() const { return peer_identity_; }
  const Bytes32& local_session_key() const { return eph_public_; }
  const Bytes32& peer_session_key() const { return peer_eph_; }

  // Protects one message. False before the handshake finished or when `size` is over kMaxPlaintext.
  bool seal(uint8_t channel, const uint8_t* plain, size_t size, std::vector<uint8_t>& packet);
  // Verifies and decrypts one packet that arrived on `channel`. Length, version and channel are
  // checked before any cryptography; the replay state moves only after the packet verified.
  SecurityError open(uint8_t channel, const uint8_t* packet, size_t size, std::vector<uint8_t>& plain);

  void wipe();

 private:
  Bytes32 identity_public_{};
  Bytes32 expected_peer_{};
  bool has_expected_ = false;
  Bytes32 eph_secret_{}, eph_public_{};
  Bytes32 peer_identity_{}, peer_eph_{};
  Bytes32 send_key_{}, recv_key_{};
  std::vector<uint8_t> hello_, peer_hello_;
  bool begun_ = false, established_ = false, decider_ = false;
  uint64_t send_seq_ = 0;
  bool control_seen_ = false;
  uint64_t control_last_ = 0;       // control arrives in order: each sequence must exceed the last
  bool input_seen_ = false;
  uint64_t input_top_ = 0;          // highest sequence seen on the input channel
  uint64_t input_mask_ = 0;         // bit n: sequence input_top_ - n was seen
};

}  // namespace mu_net
