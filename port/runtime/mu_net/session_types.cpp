// Canonical serialization and digest of the session descriptor.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "session_types.h"
#include "monocypher.h"

namespace mu_net {
namespace {

const char kDescriptorMagic[4] = {'M', 'U', 'S', 'D'};
const char kDigestDomain[] = "MeleeUnlockedNet1 descriptor";

}  // namespace

bool validate(const SessionDescriptor& d, std::string* why) {
  auto refuse = [&](const char* text) { if (why) *why = text; return false; };
  if (d.protocol_version != kProtocolVersion) return refuse("The match was set up for a different network version");
  if (d.route != Route::P2PDirect && d.route != Route::P2PUnranked) return refuse("The match uses a mode this build does not have");
  if (d.build_id.empty() || d.build_id.size() > kMaxBuildIdBytes) return refuse("The match has no usable build id");
  if (d.input_delay < kMinInputDelay || d.input_delay > kMaxInputDelay) return refuse("The input delay is outside 1 to 15 frames");
  if (d.rollback_horizon != kRollbackHorizon) return refuse("The rollback window is not the 7 frames this build plays");
  if (d.features & ~kKnownFeatures) return refuse("The match asks for a feature this build does not have");
  // The reserved bit is refused on purpose: nothing in this build acts on SAMPLE messages, so a
  // match that negotiated them would silently run without them.
  if (d.features & kFeatureSubframeSamples) return refuse("Sub-frame input is not available in this build");
  if (d.stocks == 0 || d.stocks > 99) return refuse("The stock count is not playable");
  // One remote player for now. The game's input reply has room for three; the session does not.
  if (!d.players[0].present || !d.players[1].present || d.players[2].present || d.players[3].present)
    return refuse("P2P matches are two players, in the first two slots");
  for (const auto& p : d.players)
    if (p.name.size() > kMaxNameBytes) return refuse("A player name is too long");
  return true;
}

void serialize(const SessionDescriptor& d, std::vector<uint8_t>& out) {
  out.clear();
  Writer w(out);
  w.bytes(reinterpret_cast<const uint8_t*>(kDescriptorMagic), 4);
  w.u16(kDescriptorFormat);
  w.u16(d.protocol_version);
  w.u8((uint8_t)d.route);
  w.str8(d.build_id, kMaxBuildIdBytes);
  w.bytes(d.content_hash.data(), d.content_hash.size());
  w.u32(d.rules_profile);
  w.bytes(d.match_id.data(), d.match_id.size());
  for (const auto& p : d.players) {
    w.u8(p.present ? 1 : 0);
    w.u8(p.character);
    w.u8(p.color);
    w.str8(p.name, kMaxNameBytes);
    w.bytes(p.identity_key.data(), p.identity_key.size());
    w.bytes(p.session_key.data(), p.session_key.size());
  }
  w.u16(d.stage);
  w.u8(d.alt_stage);
  w.u32(d.rng_seed);
  w.u8(d.input_delay);
  w.u8(d.rollback_horizon);
  w.u8(d.stocks);
  w.u32(d.timer_seconds);
  w.u32(d.features);
}

bool parse(const uint8_t* data, size_t size, SessionDescriptor& out) {
  if (!data || size > 1024) return false;   // the longest canonical form is under 600 bytes
  Reader r(data, size);
  uint8_t magic[4];
  if (!r.bytes(magic, 4) || std::memcmp(magic, kDescriptorMagic, 4) != 0) return false;
  if (r.u16() != kDescriptorFormat) return false;
  SessionDescriptor d;
  d.protocol_version = r.u16();
  d.route = (Route)r.u8();
  r.str8(d.build_id, kMaxBuildIdBytes);
  r.bytes(d.content_hash.data(), d.content_hash.size());
  d.rules_profile = r.u32();
  r.bytes(d.match_id.data(), d.match_id.size());
  for (auto& p : d.players) {
    const uint8_t present = r.u8();
    if (present > 1) return false;   // one spelling per value, or two byte strings could mean one descriptor
    p.present = present == 1;
    p.character = r.u8();
    p.color = r.u8();
    r.str8(p.name, kMaxNameBytes);
    r.bytes(p.identity_key.data(), p.identity_key.size());
    r.bytes(p.session_key.data(), p.session_key.size());
  }
  d.stage = r.u16();
  d.alt_stage = r.u8();
  d.rng_seed = r.u32();
  d.input_delay = r.u8();
  d.rollback_horizon = r.u8();
  d.stocks = r.u8();
  d.timer_seconds = r.u32();
  d.features = r.u32();
  if (!r.done()) return false;
  out = d;
  return true;
}

Bytes32 digest(const SessionDescriptor& descriptor) {
  std::vector<uint8_t> bytes;
  serialize(descriptor, bytes);
  Bytes32 result{};
  crypto_blake2b_ctx ctx;
  crypto_blake2b_init(&ctx, result.size());
  crypto_blake2b_update(&ctx, reinterpret_cast<const uint8_t*>(kDigestDomain), sizeof kDigestDomain - 1);
  crypto_blake2b_update(&ctx, bytes.data(), bytes.size());
  crypto_blake2b_final(&ctx, result.data());
  return result;
}

SessionDescriptor next_descriptor(const SessionDescriptor& current, const Bytes32& current_digest, uint32_t next_game,
                                  const std::vector<uint16_t>& stage_pool) {
  // One hash per game, chained through the digest of the game before: the first game's seed and
  // both players' keys are in that digest, so the stream is this session's own and neither side
  // can steer a draw after the handshake.
  static const char kNextDomain[] = "MeleeUnlockedNet1 next";
  const uint8_t index[4] = {(uint8_t)next_game, (uint8_t)(next_game >> 8), (uint8_t)(next_game >> 16), (uint8_t)(next_game >> 24)};
  Bytes32 h{};
  crypto_blake2b_ctx ctx;
  crypto_blake2b_init(&ctx, h.size());
  crypto_blake2b_update(&ctx, reinterpret_cast<const uint8_t*>(kNextDomain), sizeof kNextDomain - 1);
  crypto_blake2b_update(&ctx, current_digest.data(), current_digest.size());
  crypto_blake2b_update(&ctx, index, sizeof index);
  crypto_blake2b_final(&ctx, h.data());
  auto le32 = [&h](size_t at) { return (uint32_t)h[at] | ((uint32_t)h[at + 1] << 8) | ((uint32_t)h[at + 2] << 16) | ((uint32_t)h[at + 3] << 24); };
  SessionDescriptor next = current;
  std::memcpy(next.match_id.data(), h.data(), next.match_id.size());
  next.rng_seed = le32(16);
  if (!stage_pool.empty()) next.stage = stage_pool[le32(20) % stage_pool.size()];
  return next;
}

}  // namespace mu_net
