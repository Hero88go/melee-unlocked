// The signed record of one finished P2P game, written as a JSON file for a later reporter to pick up.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "mu_net_util.h"
#include "peer_identity.h"
#include "session_types.h"
#include <string>

namespace mu_net {

constexpr size_t kGameEndPayloadSize = 368;   // the game's end-of-game report (command 0xBD)

struct GameResult {
  // What was played and by whom.
  Route route = Route::P2PDirect;
  std::string build_id;
  Bytes32 descriptor_digest{};
  Bytes16 match_id{};
  uint8_t local_slot = 0;
  Bytes32 peer_identity{};
  // The confirmed inputs: BLAKE2b-256 over every frame both pads were known for, in frame order.
  Bytes32 transcript_digest{};
  int32_t transcript_frames = 0;
  bool transcript_complete = true;    // false when a frame's pads were gone before they could be hashed
  // Whether the peer's digest for the same frame count equals ours: 1 yes, 0 no, -1 not known.
  int peer_agreement = -1;
  bool desync = false;                // a state checksum disagreed during play
  // Outcome fields, as the game reported them.
  bool has_outcome = false;
  uint8_t mode = 0;
  uint32_t frame_length = 0, game_index = 0, tiebreak_index = 0;
  int winner = -1;
  uint8_t end_method = 0;
  int lras_initiator = -1;
  uint32_t synced_timer = 0;
  struct Player {
    uint8_t slot_type = 0, stocks = 0;
    float damage_done = 0;
    uint8_t synced_stocks = 0;
    uint16_t synced_damage = 0;
  } players[4];
  Bytes32 game_info_hash{};           // BLAKE2b-256 of the game info block the game ended with
  bool disconnected = false;          // the session lost the peer before the game reported its end
};

// Fills the outcome fields from the game's end-of-game payload. False (and no change) when the
// payload is shorter than kGameEndPayloadSize.
bool parse_game_end(const uint8_t* payload, size_t size, GameResult& result);

// The body object exactly as it is signed and stored.
std::string result_body(const GameResult& result);

// Writes {"body":<body>,"identity":"<hex>","signature":"<hex>"} to `path_utf8`, through a
// temporary file. The signature is Ed25519 over the domain string "MeleeUnlockedNet1 result" and
// the body's exact bytes. This is one player's claim; a second file from the peer, with the same
// descriptor and transcript digests, is what makes it a two-sided record.
bool write_result(const std::string& path_utf8, const GameResult& result, const PeerIdentity& identity, std::string* error);

// Checks a result file's signature against the identity it names. `signer` gets that identity.
bool verify_result_text(const std::string& file_text, Bytes32* signer);

}  // namespace mu_net
