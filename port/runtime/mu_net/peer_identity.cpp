// Loads or creates the persistent peer identity file and signs with it.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "peer_identity.h"
#include "monocypher.h"
#include "monocypher-ed25519.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <windows.h>
#include <bcrypt.h>

namespace mu_net {

bool random_bytes(void* out, size_t size) {
  return BCryptGenRandom(nullptr, static_cast<PUCHAR>(out), static_cast<ULONG>(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0;
}

void PeerIdentity::wipe() {
  crypto_wipe(seed.data(), seed.size());
  crypto_wipe(secret.data(), secret.size());
  valid = false;
}

PeerIdentity identity_from_seed(const Bytes32& seed) {
  PeerIdentity identity;
  identity.seed = seed;
  Bytes32 scratch = seed;   // the key pair call wipes the seed it is given
  crypto_ed25519_key_pair(identity.secret.data(), identity.public_key.data(), scratch.data());
  identity.valid = true;
  return identity;
}

void sign(const PeerIdentity& identity, const uint8_t* message, size_t size, Bytes64& signature) {
  crypto_ed25519_sign(signature.data(), identity.secret.data(), message, size);
}

bool verify(const Bytes32& public_key, const uint8_t* message, size_t size, const Bytes64& signature) {
  return crypto_ed25519_check(signature.data(), public_key.data(), message, size) == 0;
}

namespace {

constexpr size_t kMaxIdentityFile = 4 * 1024 * 1024;   // the lobby file also holds a friends list

// Finds "ed_seed" : "<64 hex digits>" without a JSON library: the identity is the one value this
// subsystem needs from the launcher's file, and the rest of that file is not ours to interpret.
bool seed_from_json(const std::string& text, Bytes32& seed) {
  static const char key[] = "\"ed_seed\"";
  size_t at = text.find(key);
  if (at == std::string::npos) return false;
  at += sizeof key - 1;
  auto skip_space = [&] { while (at < text.size() && (text[at] == ' ' || text[at] == '\t' || text[at] == '\r' || text[at] == '\n')) ++at; };
  skip_space();
  if (at >= text.size() || text[at] != ':') return false;
  ++at;
  skip_space();
  if (at >= text.size() || text[at] != '"') return false;
  ++at;
  if (text.size() - at < 65 || text[at + 64] != '"') return false;
  return from_hex(text.data() + at, 64, seed.data(), seed.size());
}

}  // namespace

IdentityFile load_or_create_identity(const std::string& path_utf8, PeerIdentity& out, std::string* error) {
  auto fail = [&](const char* text) { if (error) *error = text; return IdentityFile::Failed; };
  try {
    const auto path = std::filesystem::u8path(path_utf8);
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
      std::ifstream in(path, std::ios::binary);
      if (!in) return fail("The identity file could not be opened");
      std::string text;
      char block[4096];
      while (in && text.size() <= kMaxIdentityFile) {
        in.read(block, sizeof block);
        text.append(block, (size_t)in.gcount());
      }
      if (text.size() > kMaxIdentityFile) return fail("The identity file is too large to be one");
      Bytes32 seed{};
      if (text.size() == seed.size()) std::memcpy(seed.data(), text.data(), seed.size());
      else if (!seed_from_json(text, seed)) return fail("The identity file holds no usable key");
      out = identity_from_seed(seed);
      crypto_wipe(seed.data(), seed.size());
      crypto_wipe(&text[0], text.size());
      return IdentityFile::Loaded;
    }
    Bytes32 seed{}, x_secret{};
    if (!random_bytes(seed.data(), seed.size()) || !random_bytes(x_secret.data(), x_secret.size()))
      return fail("Windows could not supply random numbers for a new identity");
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    // Written under another name and renamed, so a game that is closed mid-write leaves no
    // half-written identity that the next start would refuse.
    auto partial = path;
    partial += ".new";
    {
      std::ofstream file(partial, std::ios::binary | std::ios::trunc);
      if (!file) return fail("The identity file could not be created");
      file << "{\n  \"ed_seed\": \"" << to_hex(seed.data(), seed.size()) << "\",\n  \"x_secret\": \""
           << to_hex(x_secret.data(), x_secret.size()) << "\"\n}\n";
      if (!file.good()) return fail("The identity file could not be written");
    }
    std::filesystem::rename(partial, path, ec);
    if (ec) { std::filesystem::remove(partial, ec); return fail("The identity file could not be put in place"); }
    out = identity_from_seed(seed);
    crypto_wipe(seed.data(), seed.size());
    crypto_wipe(x_secret.data(), x_secret.size());
    return IdentityFile::Created;
  } catch (...) {
    return fail("The identity file could not be read or written");
  }
}

}  // namespace mu_net
