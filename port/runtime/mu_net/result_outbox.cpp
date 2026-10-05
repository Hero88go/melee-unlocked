// Parses the game's end-of-game payload and writes the signed result file.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "result_outbox.h"
#include "monocypher.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace mu_net {
namespace {

const char kResultDomain[] = "MeleeUnlockedNet1 result";
const char kBodyKey[] = "{\"body\":";
const char kIdentityKey[] = ",\"identity\":\"";
const char kSignatureKey[] = "\",\"signature\":\"";

// Game end payload offsets (the game writes them big-endian).
enum {
  kEndMode = 0, kEndFrameLength = 1, kEndGameIndex = 5, kEndTiebreak = 9, kEndWinner = 13, kEndMethod = 14,
  kEndLras = 15, kEndSyncedTimer = 16, kEndPlayers = 20, kEndPlayerSize = 9, kEndGameInfo = kEndPlayers + 4 * kEndPlayerSize,
  kEndGameInfoSize = 0x138,
};

void append_escaped(std::string& out, const std::string& text) {
  out += '"';
  for (unsigned char c : text) {
    if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
    else if (c < 0x20) { char u[8]; std::snprintf(u, sizeof u, "\\u%04x", c); out += u; }
    else out += (char)c;
  }
  out += '"';
}
void field(std::string& out, const char* name, const std::string& value, bool quote = false) {
  if (out.size() > 1) out += ',';
  out += '"'; out += name; out += "\":";
  if (quote) append_escaped(out, value); else out += value;
}
template <size_t N> std::string hex_of(const std::array<uint8_t, N>& bytes) { return "\"" + to_hex(bytes.data(), N) + "\""; }

void sign_message(const std::string& body, std::vector<uint8_t>& message) {
  message.assign(kResultDomain, kResultDomain + sizeof kResultDomain - 1);
  message.insert(message.end(), body.begin(), body.end());
}

}  // namespace

bool parse_game_end(const uint8_t* p, size_t size, GameResult& result) {
  if (!p || size < kGameEndPayloadSize) return false;
  result.has_outcome = true;
  result.mode = p[kEndMode];
  result.frame_length = get_be32(p + kEndFrameLength);
  result.game_index = get_be32(p + kEndGameIndex);
  result.tiebreak_index = get_be32(p + kEndTiebreak);
  result.winner = (int8_t)p[kEndWinner];
  result.end_method = p[kEndMethod];
  result.lras_initiator = (int8_t)p[kEndLras];
  result.synced_timer = get_be32(p + kEndSyncedTimer);
  for (int i = 0; i < 4; ++i) {
    const uint8_t* q = p + kEndPlayers + i * kEndPlayerSize;
    auto& player = result.players[i];
    player.slot_type = q[0];
    player.stocks = q[1];
    const uint32_t bits = get_be32(q + 2);
    float damage;
    std::memcpy(&damage, &bits, sizeof damage);
    player.damage_done = std::isfinite(damage) ? damage : 0.0f;   // a NaN would not be valid JSON
    player.synced_stocks = q[6];
    player.synced_damage = (uint16_t)((q[7] << 8) | q[8]);
  }
  crypto_blake2b(result.game_info_hash.data(), result.game_info_hash.size(), p + kEndGameInfo, kEndGameInfoSize);
  return true;
}

std::string result_body(const GameResult& r) {
  // One fixed key order and no optional whitespace: the bytes are what is signed, so the writer
  // must produce the same text for the same result on every build.
  std::string out = "{";
  field(out, "format", "1");
  field(out, "protocol", std::to_string(kProtocolVersion));
  field(out, "route", r.route == Route::P2PUnranked ? "p2p_unranked" : "p2p_direct", true);
  field(out, "build_id", r.build_id, true);
  field(out, "descriptor_digest", hex_of(r.descriptor_digest));
  field(out, "match_id", hex_of(r.match_id));
  field(out, "local_slot", std::to_string(r.local_slot));
  field(out, "peer_identity", hex_of(r.peer_identity));
  field(out, "transcript_digest", hex_of(r.transcript_digest));
  field(out, "transcript_frames", std::to_string(r.transcript_frames));
  field(out, "transcript_complete", r.transcript_complete ? "true" : "false");
  field(out, "peer_agreement", r.peer_agreement > 0 ? "match" : r.peer_agreement == 0 ? "mismatch" : "unknown", true);
  field(out, "desync", r.desync ? "true" : "false");
  field(out, "disconnected", r.disconnected ? "true" : "false");
  field(out, "has_outcome", r.has_outcome ? "true" : "false");
  field(out, "mode", std::to_string(r.mode));
  field(out, "frame_length", std::to_string(r.frame_length));
  field(out, "game_index", std::to_string(r.game_index));
  field(out, "tiebreak_index", std::to_string(r.tiebreak_index));
  field(out, "winner", std::to_string(r.winner));
  field(out, "end_method", std::to_string(r.end_method));
  field(out, "lras_initiator", std::to_string(r.lras_initiator));
  field(out, "synced_timer", std::to_string(r.synced_timer));
  std::string players = "[";
  for (int i = 0; i < 4; ++i) {
    const auto& p = r.players[i];
    // Thousandths as integers: "%f" follows the process locale, and a decimal comma would change
    // the signed bytes (and break the JSON) on some PCs.
    double damage = (double)p.damage_done;
    if (damage < 0) damage = 0;
    if (damage > 1e9) damage = 1e9;
    const long long milli = std::llround(damage * 1000.0);
    char text[200];
    std::snprintf(text, sizeof text, "%s{\"slot_type\":%u,\"stocks\":%u,\"damage_done\":%lld.%03lld,\"synced_stocks\":%u,\"synced_damage\":%u}",
                  i ? "," : "", (unsigned)p.slot_type, (unsigned)p.stocks, milli / 1000, milli % 1000, (unsigned)p.synced_stocks,
                  (unsigned)p.synced_damage);
    players += text;
  }
  players += "]";
  field(out, "players", players);
  field(out, "game_info_hash", hex_of(r.game_info_hash));
  out += "}";
  return out;
}

bool write_result(const std::string& path_utf8, const GameResult& result, const PeerIdentity& identity, std::string* error) {
  auto fail = [&](const char* text) { if (error) *error = text; return false; };
  if (!identity.valid) return fail("There is no identity to sign the result with");
  try {
    const std::string body = result_body(result);
    std::vector<uint8_t> message;
    sign_message(body, message);
    Bytes64 signature{};
    sign(identity, message.data(), message.size(), signature);
    std::string text = kBodyKey + body + kIdentityKey + to_hex(identity.public_key.data(), identity.public_key.size()) +
                       kSignatureKey + to_hex(signature.data(), signature.size()) + "\"}\n";
    const auto path = std::filesystem::u8path(path_utf8);
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    auto partial = path;
    partial += ".part";
    {
      std::ofstream file(partial, std::ios::binary | std::ios::trunc);
      if (!file) return fail("The result file could not be created");
      file.write(text.data(), (std::streamsize)text.size());
      if (!file.good()) return fail("The result file could not be written");
    }
    std::filesystem::rename(partial, path, ec);
    if (ec) { std::filesystem::remove(partial, ec); return fail("The result file could not be put in place"); }
    return true;
  } catch (...) {
    return fail("The result file could not be written");
  }
}

bool verify_result_text(const std::string& text, Bytes32* signer) {
  try {
    // The tail is fixed-width hex, so the body is everything between the opening key and the last
    // identity key: no JSON parsing decides which bytes were signed.
    const size_t head = sizeof kBodyKey - 1;
    if (text.size() > 64 * 1024 || text.compare(0, head, kBodyKey) != 0) return false;
    const size_t tail = text.rfind(kIdentityKey);
    if (tail == std::string::npos || tail < head) return false;
    const size_t id_at = tail + sizeof kIdentityKey - 1;
    const size_t sig_key = id_at + 64;
    if (text.size() < sig_key + sizeof kSignatureKey - 1 + 128 + 2) return false;
    if (text.compare(sig_key, sizeof kSignatureKey - 1, kSignatureKey) != 0) return false;
    const size_t sig_at = sig_key + sizeof kSignatureKey - 1;
    if (text.compare(sig_at + 128, 2, "\"}") != 0) return false;
    Bytes32 identity{};
    Bytes64 signature{};
    if (!from_hex(text.data() + id_at, 64, identity.data(), identity.size())) return false;
    if (!from_hex(text.data() + sig_at, 128, signature.data(), signature.size())) return false;
    std::vector<uint8_t> message;
    sign_message(text.substr(head, tail - head), message);
    if (!verify(identity, message.data(), message.size(), signature)) return false;
    if (signer) *signer = identity;
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace mu_net
