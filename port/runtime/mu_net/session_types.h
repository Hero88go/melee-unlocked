// What two peers must agree on before frame 1 of a Melee Unlocked P2P match: routes, the session descriptor and its digest.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "mu_net_util.h"
#include <string>
#include <vector>

namespace mu_net {

// The gameplay protocol. A peer that speaks another number is refused in the handshake; there is
// no fallback between versions inside one session.
constexpr uint16_t kProtocolVersion = 1;
// The revision inside version 1. It never changes a layout or a rule of game 1, so it is not in the
// handshake: a peer says it in INFO, and a peer that says nothing is revision 0. Revision 1 adds
// more than one game in a session (INFO, NEXT, the per-game frame base of the input messages).
constexpr uint8_t kProtocolMinor = 1;
constexpr uint16_t kDescriptorFormat = 1;   // the canonical serialization below
// Wire frame numbers of game k of a session start at (k - 1) x kGameFrameSpan, so an input message
// still in flight from a finished game can never be read as a frame of the next one.
constexpr int32_t kGameFrameSpan = 1 << 24;
constexpr uint32_t kMaxSessionGames = 127;  // the frame bases of more games would not fit an i32
constexpr int kRollbackHorizon = 7;         // frames the game can roll back; the stall rule is built on it
constexpr int kMaxSlots = 4;
constexpr int kPadWireSize = 8;             // pad bytes that travel
constexpr int kPadGameSize = 12;            // pad bytes the game is handed (the last four are zero)
constexpr int kMinInputDelay = 1;
constexpr int kMaxInputDelay = 15;
constexpr size_t kMaxNameBytes = 31;
constexpr size_t kMaxBuildIdBytes = 64;

// New typed routes. These are not the existing online mode integers and never travel to that
// system; the hosted routes of the plan get their numbers when they are built.
enum class Route : uint8_t {
  P2PDirect = 1,     // a known peer: invitation, endpoint or LAN
  P2PUnranked = 2,   // found through the launcher's DHT lobby
};

enum FeatureBits : uint32_t {
  // Reserved for provisional sub-frame input (SAMPLE messages). Always 0 for now: a descriptor
  // with it set is refused, so a build that implements it cannot be mistaken for this one.
  kFeatureSubframeSamples = 1u << 0,
  kFeaturePauseAllowed = 1u << 1,
};
constexpr uint32_t kKnownFeatures = kFeatureSubframeSamples | kFeaturePauseAllowed;

struct PlayerSlot {
  bool present = false;
  uint8_t character = 0;   // the game's external character id
  uint8_t color = 0;
  std::string name;        // UTF-8, at most kMaxNameBytes
  // All zero means "taken from the handshake": the session writes the keys it saw into its copy
  // before it computes the digest. A value given here must match the handshake or the start fails.
  Bytes32 identity_key{};  // Ed25519 public key
  Bytes32 session_key{};   // X25519 ephemeral public key of this session
};

struct SessionDescriptor {
  uint16_t protocol_version = kProtocolVersion;
  Route route = Route::P2PDirect;
  std::string build_id;        // the build both sides run, at most kMaxBuildIdBytes
  Bytes32 content_hash{};      // game content both sides load (disc and gameplay mods)
  uint32_t rules_profile = 0;  // id of the rule set; 0 = the project's default singles rules
  Bytes16 match_id{};          // chosen by whoever proposed the match
  PlayerSlot players[kMaxSlots];
  uint16_t stage = 0x1F;
  uint8_t alt_stage = 0;
  uint32_t rng_seed = 0;
  uint8_t input_delay = 2;
  uint8_t rollback_horizon = kRollbackHorizon;
  uint8_t stocks = 4;
  uint32_t timer_seconds = 8 * 60;
  uint32_t features = 0;
};

// Checks the limits this build can play. `why` gets one plain sentence on refusal.
bool validate(const SessionDescriptor& descriptor, std::string* why);
// Canonical bytes: fixed field order, little-endian, every slot written whether present or not.
// Two descriptors serialize to the same bytes exactly when every field is equal.
void serialize(const SessionDescriptor& descriptor, std::vector<uint8_t>& out);
// Bounds-checked inverse of serialize. False on short, long or unknown-format input.
bool parse(const uint8_t* data, size_t size, SessionDescriptor& out);
// BLAKE2b-256 over a domain string and the canonical bytes.
Bytes32 digest(const SessionDescriptor& descriptor);
// The descriptor of game `next_game` (2 for the second) of a session, made from the descriptor of
// the game before it and that descriptor's digest and from nothing else, so both sides make the
// same one without a message: a new match id, a new seed, and a stage drawn from `stage_pool` (the
// stage stays when the pool is empty). Players, keys and rules carry over.
SessionDescriptor next_descriptor(const SessionDescriptor& current, const Bytes32& current_digest, uint32_t next_game,
                                  const std::vector<uint16_t>& stage_pool);

}  // namespace mu_net
