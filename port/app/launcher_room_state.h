// Launcher rendezvous. The running games use their existing arena and match protocol.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <vector>

namespace launcher::room {
using json = nlohmann::json;
inline const char* variant_name(int variant) {
  static const char* names[] = {"Standard", "Akaneia", "ACE", "20XX Melee 1.03"};
  return variant >= 0 && variant < 4 ? names[variant] : "Unknown";
}
inline bool valid_code(const std::string& code) {
  if (code.size() < 6 || code.size() > 12) return false;
  for (char c : code) if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9')) return false;
  return true;
}
struct Member { std::string name; bool ready = false; };
struct State {
  std::string code, me, host, name, arena, error;
  int variant = 0, revision = 0;
  bool connected = false, configured = false, started = false, launching = false, launch_due = false;
  std::map<std::string, Member> members;
  std::vector<json> out;
  bool hosting() const { return connected && !me.empty() && me == host; }
  bool can_start() const {
    if (!hosting() || started || launching || members.size() < 2) return false;
    for (const auto& member : members) if (!member.second.ready) return false;
    return true;
  }
  void send(json data, const std::string& to = {}) {
    json frame{{"t", "msg"}, {"d", std::move(data)}};
    if (!to.empty()) frame["to"] = to;
    out.push_back(std::move(frame));
  }
  void describe(const std::string& to = {}) {
    send({{"kind", "mu-launcher-room"}, {"variant", variant}, {"name", name},
          {"arena", arena}, {"started", started}}, to);
  }
  void ready(bool available, const std::string& reason = {}) {
    if (members.count(me)) members[me].ready = available;
    send({{"kind", "mu-launcher-ready"}, {"ready", available}, {"reason", reason}}, host);
  }
  // Authenticated sender ids come from Rooms, never from the application's data.
  bool receive(const json& frame) {
    if (!frame.is_object()) return false;
    const std::string type = frame.value("t", std::string());
    if (type == "probe") { out.push_back({{"t", "probe-ack"}, {"n", frame.value("n", json())}}); return false; }
    if (type == "welcome") { me = frame.value("me", std::string()); connected = true; }
    if (frame.count("host") && frame["host"].is_string()) {
      const std::string next = frame["host"];
      if (!host.empty() && next != host) {
        error = "The host left the room."; connected = false; return false;
      }
      host = next;
    }
    revision = frame.value("revision", revision);
    if (frame.count("players") && frame["players"].is_array()) {
      std::map<std::string, Member> roster;
      for (const auto& player : frame["players"]) {
        const std::string id = player.value("id", std::string());
        if (id.empty() || player.value("away", false)) continue;
        roster[id] = {player.value("name", std::string("Player")), members.count(id) && members[id].ready};
      }
      members = std::move(roster);
    }
    if (type == "welcome") {
      if (hosting()) describe();
      else send({{"kind", "mu-launcher-hello"}}, host);
      return hosting();
    }
    if (type != "msg" || !frame.count("d") || !frame["d"].is_object()) return false;
    const json& data = frame["d"];
    const std::string from = frame.value("from", std::string()), kind = data.value("kind", std::string());
    if (hosting() && kind == "mu-launcher-hello" && members.count(from)) describe(from);
    if (hosting() && kind == "mu-launcher-ready" && members.count(from)) {
      members[from].ready = data.value("ready", false);
      if (!members[from].ready) error = data.value("reason", std::string("A player is missing this version."));
      if (started && members[from].ready) describe(from);
    }
    if (kind != "mu-launcher-room" || from != host || hosting()) return false;
    const int proposed = data.value("variant", -1);
    const std::string target = data.value("arena", std::string());
    if (proposed < 0 || proposed > 3 || !valid_code(target)) return false;
    if (configured && (variant != proposed || arena != target)) return false;
    const bool first_configuration = !configured;
    configured = true;
    variant = proposed; name = data.value("name", std::string("Room")); arena = target;
    if (data.value("started", false) && !started) { started = true; launch_due = true; }
    return first_configuration;
  }
  bool start() {
    if (!can_start()) return false;
    launching = true; launch_due = true; error.clear(); return true;
  }
  // Called after the host game has opened the arena. Guests then join an existing host.
  void host_ready() {
    if (!hosting() || !launching) return;
    launching = false; started = true; describe();
  }
};
}  // namespace launcher::room
