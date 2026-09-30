// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <nlohmann/json.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace launcher::lobby {
// Test switches. A real launcher leaves them all off.
struct PeerTestOptions {
  bool no_dht = false;          // skip the public DHT entirely: only peers given by address (two in one process)
  long long clock_offset = 0;   // seconds added to this peer's wall clock, to test clock skew
};
// One instance per process with the DHT (it is process-wide); any number with no_dht.
// Runs on the launcher's lobby worker thread.
class PeerLobby {
public:
  PeerLobby(const std::string& directory, const std::string& bootstrap = {}, int listen_port = 0,
            const PeerTestOptions& test = {});
  ~PeerLobby();
  PeerLobby(const PeerLobby&) = delete;
  PeerLobby& operator=(const PeerLobby&) = delete;
  void join(const nlohmann::json& profile);               // update the profile and show it in the public lobby
  void update_profile(const nlohmann::json& profile);     // re-announce (ready, Game Build, mods) without joining
  // Actions: leave, available, profile, chat, friend, friend_code, friend_accept, friend_decline,
  // unfriend, request (target, mode), accept, cancel (request, optional code). Throws a message
  // for the player, in their language, when an action cannot go ahead.
  void command(const std::string& action, const nlohmann::json& data = nlohmann::json::object());
  void presence(const nlohmann::json& status);
  void tick();
  nlohmann::json state() const;
  std::map<std::string, int> pings() const;
  // An accepted match to start, once per request: request, opponent, name, code, build, mode, character.
  bool take_launch(nlohmann::json& launch);
  void add_address(const std::string& host_port);         // contact a player at a known address
  void test_drop(bool incoming, bool outgoing);            // tests: lose every packet one way
  const std::string& id() const;
  bool visible() const;
  int port() const;
private:
  struct Impl;
  std::unique_ptr<Impl> p_;
};

// Shared by the lobby window and the tests.
constexpr int lobby_protocol = 2;        // 1 is 0.8.1; 2 adds refusal reasons, delivery, friend codes and mods
std::string normalize_code(std::string code);             // "abc#123 " -> "ABC#123" (full-width # too)
bool valid_code(const std::string& code);
bool valid_iso_name(const std::string& name);             // a custom ISO's name: plain text, 1 to 32 characters
// A peer's profile with any malformed optional field (has, open, iso) removed.
nlohmann::json clean_profile(const nlohmann::json& profile);
// The ways two players can play each other: "vanilla", "akaneia", "ace", "custom:<hash>".
std::vector<std::string> common_modes(const nlohmann::json& mine, const nlohmann::json& theirs);
// Why `receiver` cannot play `mode` with `sender` (a reason code), or "" when it can.
std::string mode_problem(const nlohmann::json& receiver, const nlohmann::json& sender, const std::string& mode);
bool open_to(const nlohmann::json& profile, const std::string& mode);
std::string iso_hash(const nlohmann::json& profile);
// The Mods folder scan (the text of Mods/.cache/detected.json) as the lobby reads it: the named mod
// discs this PC plays on Static Recomp, each version for the profile's "has" and each disc for a
// match. Relative paths are read from game_dir; a disc that is gone is left out.
struct ModDiscs {
  nlohmann::json has = nlohmann::json::object();     // "akaneia" / "ace" -> version
  std::map<std::string, std::string> paths;          // "akaneia" / "ace" -> the disc on this PC
};
ModDiscs read_mod_scan(const std::string& detected_json, const std::string& game_dir);
bool has_game_save(const std::string& card_dir);
// The disc an agreed match starts from: a named mod's disc from the scan, or the custom ISO when its
// content hash is the one both players chose; vanilla needs none. Returns "" when the match can
// start, else the notice for the player ("lobby.mod_disc_missing", in their language).
std::string find_match_disc(const std::string& mode, const std::map<std::string, std::string>& mod_paths,
                            const std::string& custom_hash, const std::string& custom_iso,
                            const std::string& custom_name, std::string& path, std::string& name);
std::string build_version(const std::string& build);      // "0.8.5:source" -> "0.8.5"
int compare_versions(const std::string& a, const std::string& b);
// One line in <directory>/lobby.log. Never pass a raw IP address.
void lobby_log(const std::string& directory, const std::string& line);
}
