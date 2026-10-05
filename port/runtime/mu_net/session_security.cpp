// Handshake (signed ephemeral key exchange) and XChaCha20-Poly1305 packet protection with replay windows.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "session_security.h"
#include "session_types.h"
#include "monocypher.h"
#include <algorithm>

namespace mu_net {
namespace {

const uint8_t kHelloMagic[4] = {'M', 'U', 'N', 'H'};
const char kHelloDomain[] = "MeleeUnlockedNet1 hello";
const char kKeyDomain[] = "MeleeUnlockedNet1 session key";
const char kTranscriptDomain[] = "MeleeUnlockedNet1 transcript";
const char kDirectionDomain[] = "MeleeUnlockedNet1 direction";
constexpr size_t kHelloSigned = kHelloSize - 64;   // everything in front of the signature

void hello_message(const uint8_t* hello, std::vector<uint8_t>& out) {
  out.assign(kHelloDomain, kHelloDomain + sizeof kHelloDomain - 1);
  out.insert(out.end(), hello, hello + kHelloSigned);
}

// Nonce: the 64-bit sequence, then the channel, then zero. Each direction has its own key and one
// counter for both channels, so a (key, nonce) pair is never used twice.
void make_nonce(uint64_t seq, uint8_t channel, uint8_t nonce[24]) {
  std::memset(nonce, 0, 24);
  for (int i = 0; i < 8; ++i) nonce[i] = (uint8_t)(seq >> (8 * i));
  nonce[8] = channel;
}

void make_ad(uint8_t channel, uint8_t ad[6]) {
  ad[0] = 'M'; ad[1] = 'U'; ad[2] = 'N'; ad[3] = '1'; ad[4] = kPacketVersion; ad[5] = channel;
}

}  // namespace

const char* describe(SecurityError error) {
  switch (error) {
    case SecurityError::None: return "accepted";
    case SecurityError::TooShort: return "too short";
    case SecurityError::TooLong: return "too long";
    case SecurityError::BadMagic: return "not a mu_net message";
    case SecurityError::BadVersion: return "another protocol version";
    case SecurityError::BadChannel: return "wrong channel";
    case SecurityError::BadSignature: return "signature does not verify";
    case SecurityError::WrongPeer: return "signed by another identity than the expected one";
    case SecurityError::Reflected: return "our own hello";
    case SecurityError::WeakKey: return "unusable key exchange";
    case SecurityError::NotReady: return "before the handshake";
    case SecurityError::Replay: return "replayed or too old";
    case SecurityError::BadMac: return "does not authenticate";
    case SecurityError::Duplicate: return "repeated hello";
  }
  return "rejected";
}

SessionSecurity::SessionSecurity() = default;
SessionSecurity::~SessionSecurity() { wipe(); }

void SessionSecurity::wipe() {
  crypto_wipe(eph_secret_.data(), eph_secret_.size());
  crypto_wipe(send_key_.data(), send_key_.size());
  crypto_wipe(recv_key_.data(), recv_key_.size());
  begun_ = established_ = false;
}

bool SessionSecurity::begin(const PeerIdentity& identity, const Bytes32* expected_peer, const Bytes32* ephemeral_secret) {
  wipe();
  if (!identity.valid) return false;
  identity_public_ = identity.public_key;
  has_expected_ = expected_peer != nullptr;
  if (expected_peer) expected_peer_ = *expected_peer;
  if (ephemeral_secret) eph_secret_ = *ephemeral_secret;
  else if (!random_bytes(eph_secret_.data(), eph_secret_.size())) return false;
  crypto_x25519_public_key(eph_public_.data(), eph_secret_.data());
  Bytes24 nonce{};
  if (!random_bytes(nonce.data(), nonce.size())) return false;

  hello_.clear();
  Writer w(hello_);
  w.bytes(kHelloMagic, 4);
  w.u16(kProtocolVersion);
  w.u16(0);   // flags: none defined, must be zero
  w.bytes(identity_public_.data(), identity_public_.size());
  w.bytes(eph_public_.data(), eph_public_.size());
  w.bytes(nonce.data(), nonce.size());
  std::vector<uint8_t> message;
  hello_message(hello_.data(), message);
  Bytes64 signature{};
  sign(identity, message.data(), message.size(), signature);
  w.bytes(signature.data(), signature.size());

  peer_hello_.clear();
  send_seq_ = 0;
  control_seen_ = input_seen_ = false;
  control_last_ = input_top_ = input_mask_ = 0;
  decider_ = false;
  begun_ = hello_.size() == kHelloSize;
  return begun_;
}

SecurityError SessionSecurity::accept_hello(const uint8_t* data, size_t size) {
  if (!begun_) return SecurityError::NotReady;
  if (!data || size < 6) return SecurityError::TooShort;
  if (std::memcmp(data, kHelloMagic, 4) != 0) return SecurityError::BadMagic;
  // The version is read before the length: another version may well have another length, and the
  // player should hear "different version", not "damaged packet".
  const uint16_t version = (uint16_t)(data[4] | (data[5] << 8));
  if (version != kProtocolVersion) return SecurityError::BadVersion;
  if (size < kHelloSize) return SecurityError::TooShort;
  if (size > kHelloSize) return SecurityError::TooLong;
  if (data[6] != 0 || data[7] != 0) return SecurityError::BadVersion;   // flags this build does not know
  if (established_)
    return peer_hello_.size() == size && std::memcmp(peer_hello_.data(), data, size) == 0 ? SecurityError::Duplicate : SecurityError::Replay;

  Bytes32 identity{}, eph{};
  std::memcpy(identity.data(), data + 8, 32);
  std::memcpy(eph.data(), data + 40, 32);
  if (eph == eph_public_) return SecurityError::Reflected;
  if (has_expected_ && crypto_verify32(identity.data(), expected_peer_.data()) != 0) return SecurityError::WrongPeer;
  Bytes64 signature{};
  std::memcpy(signature.data(), data + kHelloSigned, 64);
  std::vector<uint8_t> message;
  hello_message(data, message);
  if (!verify(identity, message.data(), message.size(), signature)) return SecurityError::BadSignature;

  Bytes32 shared{};
  crypto_x25519(shared.data(), eph_secret_.data(), eph.data());
  if (all_zero(shared)) return SecurityError::WeakKey;

  decider_ = std::lexicographical_compare(eph_public_.begin(), eph_public_.end(), eph.begin(), eph.end());
  const Bytes32& first = decider_ ? eph_public_ : eph;
  const Bytes32& second = decider_ ? eph : eph_public_;
  const uint8_t* first_hello = decider_ ? hello_.data() : data;
  const uint8_t* second_hello = decider_ ? data : hello_.data();

  Bytes32 master{}, transcript{};
  crypto_blake2b_ctx ctx;
  crypto_blake2b_init(&ctx, master.size());
  crypto_blake2b_update(&ctx, reinterpret_cast<const uint8_t*>(kKeyDomain), sizeof kKeyDomain - 1);
  crypto_blake2b_update(&ctx, shared.data(), shared.size());
  crypto_blake2b_update(&ctx, first.data(), first.size());
  crypto_blake2b_update(&ctx, second.data(), second.size());
  crypto_blake2b_final(&ctx, master.data());

  crypto_blake2b_init(&ctx, transcript.size());
  crypto_blake2b_update(&ctx, reinterpret_cast<const uint8_t*>(kTranscriptDomain), sizeof kTranscriptDomain - 1);
  crypto_blake2b_update(&ctx, first_hello, kHelloSize);
  crypto_blake2b_update(&ctx, second_hello, kHelloSize);
  crypto_blake2b_final(&ctx, transcript.data());

  // One key for each direction, named by the sender's ephemeral key. Both hellos are folded in
  // through the transcript, so a key only ever protects the conversation that agreed on it.
  auto direction = [&](const Bytes32& sender, Bytes32& key) {
    crypto_blake2b_keyed_init(&ctx, key.size(), master.data(), master.size());
    crypto_blake2b_update(&ctx, reinterpret_cast<const uint8_t*>(kDirectionDomain), sizeof kDirectionDomain - 1);
    crypto_blake2b_update(&ctx, transcript.data(), transcript.size());
    crypto_blake2b_update(&ctx, sender.data(), sender.size());
    crypto_blake2b_final(&ctx, key.data());
  };
  direction(eph_public_, send_key_);
  direction(eph, recv_key_);
  crypto_wipe(shared.data(), shared.size());
  crypto_wipe(master.data(), master.size());
  crypto_wipe(eph_secret_.data(), eph_secret_.size());   // not needed again; forward secrecy from here

  peer_identity_ = identity;
  peer_eph_ = eph;
  peer_hello_.assign(data, data + size);
  established_ = true;
  return SecurityError::None;
}

bool SessionSecurity::seal(uint8_t channel, const uint8_t* plain, size_t size, std::vector<uint8_t>& packet) {
  if (!established_ || channel >= kChannelCount || size > kMaxPlaintext || (!plain && size)) return false;
  const uint64_t seq = send_seq_++;
  packet.resize(kPacketHeader + size + kPacketMac);
  packet[0] = kPacketVersion;
  packet[1] = channel;
  for (int i = 0; i < 8; ++i) packet[2 + i] = (uint8_t)(seq >> (8 * i));
  uint8_t nonce[24], ad[6];
  make_nonce(seq, channel, nonce);
  make_ad(channel, ad);
  crypto_aead_lock(packet.data() + kPacketHeader, packet.data() + kPacketHeader + size, send_key_.data(), nonce, ad, sizeof ad, plain, size);
  return true;
}

SecurityError SessionSecurity::open(uint8_t channel, const uint8_t* packet, size_t size, std::vector<uint8_t>& plain) {
  plain.clear();
  if (!packet || size < kPacketHeader + kPacketMac) return SecurityError::TooShort;
  if (size > kMaxPacket) return SecurityError::TooLong;
  if (packet[0] != kPacketVersion) return SecurityError::BadVersion;
  if (channel >= kChannelCount || packet[1] != channel) return SecurityError::BadChannel;
  if (!established_) return SecurityError::NotReady;
  uint64_t seq = 0;
  for (int i = 0; i < 8; ++i) seq |= (uint64_t)packet[2 + i] << (8 * i);

  // The replay decision is made before decrypting (cheap refusal of a flood of old packets) and
  // recorded only after the packet verified (a forged sequence number moves nothing).
  if (channel == kChannelControl) {
    if (control_seen_ && seq <= control_last_) return SecurityError::Replay;
  } else if (input_seen_) {
    if (seq <= input_top_) {
      const uint64_t behind = input_top_ - seq;
      if (behind >= kReplayWindow || (input_mask_ >> behind) & 1) return SecurityError::Replay;
    }
  }

  const size_t length = size - kPacketHeader - kPacketMac;
  plain.resize(length);
  uint8_t nonce[24], ad[6];
  make_nonce(seq, channel, nonce);
  make_ad(channel, ad);
  if (crypto_aead_unlock(plain.data(), packet + kPacketHeader + length, recv_key_.data(), nonce, ad, sizeof ad, packet + kPacketHeader, length) != 0) {
    plain.clear();
    return SecurityError::BadMac;
  }

  if (channel == kChannelControl) {
    control_seen_ = true;
    control_last_ = seq;
  } else if (!input_seen_) {
    input_seen_ = true;
    input_top_ = seq;
    input_mask_ = 1;
  } else if (seq > input_top_) {
    const uint64_t ahead = seq - input_top_;
    input_mask_ = ahead >= kReplayWindow ? 0 : input_mask_ << ahead;
    input_mask_ |= 1;
    input_top_ = seq;
  } else {
    input_mask_ |= (uint64_t)1 << (input_top_ - seq);
  }
  return SecurityError::None;
}

}  // namespace mu_net
