// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <nlohmann/json.hpp>
#include <map>
#include <memory>
#include <string>

namespace launcher::lobby {
// One instance per process. Runs on the launcher's lobby worker thread.
class PeerLobby {
public:
  PeerLobby(const std::string& directory, const std::string& bootstrap = {}, int listen_port = 0);
  ~PeerLobby();
  PeerLobby(const PeerLobby&) = delete;
  PeerLobby& operator=(const PeerLobby&) = delete;
  void join(const nlohmann::json& profile);
  void command(const std::string& action, const nlohmann::json& data = nlohmann::json::object());
  void presence(const nlohmann::json& status);
  void tick();
  nlohmann::json state() const;
  std::map<std::string, int> pings() const;
  const std::string& id() const;
  bool visible() const;
  int port() const;
private:
  struct Impl;
  std::unique_ptr<Impl> p_;
};
}
