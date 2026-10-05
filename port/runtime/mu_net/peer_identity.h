// The player's persistent peer identity: an Ed25519 seed kept in a file the caller names.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "mu_net_util.h"
#include <string>

namespace mu_net {

struct PeerIdentity {
  Bytes32 seed{};
  Bytes64 secret{};      // the expanded signing key (seed and public key)
  Bytes32 public_key{};
  bool valid = false;
  std::string id() const { return to_hex(public_key.data(), public_key.size()); }
  void wipe();
};

enum class IdentityFile { Loaded, Created, Failed };

// Reads the identity at `path_utf8`, or creates one when no file is there.
// Two formats are read: the launcher lobby's identity file (JSON with a 64-digit hex "ed_seed"),
// so the launcher can hand its key to the game by passing that path, and a raw 32-byte seed file.
// A new file is written in the lobby's JSON form ("ed_seed" and "x_secret"), which the launcher
// also loads. An existing file is never rewritten: a file that cannot be read is an error, not a
// reason to replace somebody's identity (and the friends list the launcher keeps beside it).
IdentityFile load_or_create_identity(const std::string& path_utf8, PeerIdentity& out, std::string* error);

// The identity for a known seed (tests, and a seed passed on the command line by the launcher).
PeerIdentity identity_from_seed(const Bytes32& seed);

void sign(const PeerIdentity& identity, const uint8_t* message, size_t size, Bytes64& signature);
bool verify(const Bytes32& public_key, const uint8_t* message, size_t size, const Bytes64& signature);

}  // namespace mu_net
