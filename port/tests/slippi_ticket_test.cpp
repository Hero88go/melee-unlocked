// The matchmaking ticket parser (slippi::Matchmaking::ParseTicket): a ticket whose player list
// would index past the fixed per-player arrays, or whose fields have the wrong type, is refused
// without throwing, and a valid two-player or four-player ticket gives the same fields as before.
// No sockets and no server: the parser is pure.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "slippi_net.h"
#include <cstdio>
#include <string>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

namespace {
using Ticket = slippi::Matchmaking::ParsedTicket;

std::string player(int port, bool local, const std::string& ip, const std::string& lan) {
  return "{\"uid\":\"uid" + std::to_string(port) + "\",\"displayName\":\"Name " + std::to_string(port) +
         "\",\"connectCode\":\"CODE#" + std::to_string(port) + "\",\"port\":" + std::to_string(port) +
         ",\"isLocalPlayer\":" + (local ? "true" : "false") + ",\"ipAddress\":\"" + ip + "\",\"ipAddressLan\":\"" + lan + "\"}";
}
std::string ticket(const std::vector<std::string>& players, const std::string& extra = "") {
  std::string s = "{\"type\":\"get-ticket-resp\",\"matchId\":\"mode.unranked-test\",\"isHost\":true,\"players\":[";
  for (size_t i = 0; i < players.size(); ++i) s += (i ? "," : "") + players[i];
  return s + "]" + extra + "}";
}
Ticket parse(const std::string& text) { return slippi::Matchmaking::ParseTicket(text); }
// A refused ticket says why and never reports ok.
bool refused(const std::string& text) { Ticket t = parse(text); return !t.ok && !t.error.empty(); }
}  // namespace

int main() {
  const std::string a = player(1, true, "1.2.3.4:41000", "192.168.0.2:41000");
  const std::string b = player(2, false, "5.6.7.8:42000", "10.0.0.9:42000");

  // ---- valid, two players: remote on another external address, so its external address is used.
  {
    Ticket t = parse(ticket({a, b}, ",\"stages\":[2,3,8,28,31,32],\"items\":5"));
    CHECK(t.ok && t.error.empty());
    CHECK(t.match_id == "mode.unranked-test");
    CHECK(t.players.size() == 2);
    CHECK(t.players[0].uid == "uid1" && t.players[0].display_name == "Name 1" && t.players[0].connect_code == "CODE#1" && t.players[0].port == 1);
    CHECK(t.players[1].uid == "uid2" && t.players[1].display_name == "Name 2" && t.players[1].connect_code == "CODE#2" && t.players[1].port == 2);
    CHECK(!t.players[0].is_bot && !t.players[1].is_bot);
    CHECK(t.players[0].chat_messages.size() == 16 && t.players[1].chat_messages.size() == 16);   // the defaults
    CHECK(t.local_player_index == 0);
    CHECK(t.remote_ips.size() == 1 && t.remote_ips[0] == "5.6.7.8:42000");
    CHECK(t.is_host);
    CHECK((t.stages == std::vector<uint16_t>{0x2, 0x3, 0x8, 0x1C, 0x1F, 0x20}));
    CHECK(t.items == 5);
  }
  // The local player second, no stage list: the two-player default list, not the decider.
  {
    std::string s = "{\"type\":\"get-ticket-resp\",\"matchId\":\"m\",\"isHost\":false,\"players\":[" +
                    player(1, false, "5.6.7.8:42000", "10.0.0.9:42000") + "," + player(2, true, "1.2.3.4:41000", "192.168.0.2:41000") + "]}";
    Ticket t = parse(s);
    CHECK(t.ok);
    CHECK(t.local_player_index == 1);
    CHECK(t.remote_ips.size() == 1 && t.remote_ips[0] == "5.6.7.8:42000");
    CHECK(!t.is_host);
    CHECK((t.stages == std::vector<uint16_t>{0x3, 0x8, 0x1C, 0x1F, 0x20, 0x2}));
    CHECK(t.items == 0);
  }

  // ---- valid, four players: local on port 3; port 2 shares the local external address (LAN
  // address used), port 4 shares it with an empty LAN address (external used); no stage list gives
  // the default for more than two players.
  {
    Ticket t = parse(ticket({player(1, false, "5.6.7.8:42000", "10.0.0.9:42000"), player(2, false, "1.2.3.4:43000", "192.168.0.3:43000"),
                             player(3, true, "1.2.3.4:41000", "192.168.0.2:41000"), player(4, false, "1.2.3.4:44000", "")}));
    CHECK(t.ok && t.error.empty());
    CHECK(t.players.size() == 4);
    for (int i = 0; i < 4; ++i) CHECK(t.players[i].port == i + 1 && t.players[i].uid == "uid" + std::to_string(i + 1));
    CHECK(t.local_player_index == 2);
    CHECK(t.remote_ips.size() == 3);
    CHECK(t.remote_ips[0] == "5.6.7.8:42000" && t.remote_ips[1] == "192.168.0.3:43000" && t.remote_ips[2] == "1.2.3.4:44000");
    CHECK((t.stages == std::vector<uint16_t>{0x3, 0x8, 0x1C, 0x1F, 0x20}));
    CHECK(t.is_host && t.items == 0);
  }
  // Sixteen chat messages and a bot flag are taken; a short list falls back to the defaults.
  {
    std::string chat = "[";
    for (int i = 0; i < 16; ++i) chat += std::string(i ? "," : "") + "\"m" + std::to_string(i) + "\"";
    chat += "]";
    std::string bot = "{\"uid\":\"u\",\"displayName\":\"Bot\",\"connectCode\":\"BOT#1\",\"port\":2,\"isLocalPlayer\":false,\"isBot\":true,"
                      "\"ipAddress\":\"5.6.7.8:1\",\"ipAddressLan\":\"\",\"chatMessages\":" + chat + "}";
    Ticket t = parse(ticket({a, bot}));
    CHECK(t.ok && t.players[1].is_bot && t.players[1].chat_messages.size() == 16);
    CHECK(t.players[1].chat_messages[0] == "m0" && t.players[1].chat_messages[15] == "m15");
  }

  // ---- refused: player counts past the arrays or too few.
  CHECK(refused(ticket({a, b, player(3, false, "5.6.7.8:1", ""), player(4, false, "5.6.7.8:2", ""), player(4, false, "5.6.7.8:3", "")})));
  CHECK(refused(ticket({a, b, player(3, false, "5.6.7.8:1", ""), player(4, false, "5.6.7.8:2", ""), player(5, false, "5.6.7.8:3", "")})));
  CHECK(refused(ticket({a})));
  CHECK(refused(ticket({})));
  // ---- refused: ports outside 1..4, used twice, or no local player.
  CHECK(refused(ticket({a, player(0, false, "5.6.7.8:1", "")})));
  CHECK(refused(ticket({a, player(9, false, "5.6.7.8:1", "")})));
  CHECK(refused(ticket({a, player(-1, false, "5.6.7.8:1", "")})));
  CHECK(refused(ticket({player(0, true, "1.2.3.4:1", ""), b})));
  CHECK(refused(ticket({player(9, true, "1.2.3.4:1", ""), b})));
  CHECK(refused(ticket({a, player(1, false, "5.6.7.8:1", "")})));
  CHECK(refused(ticket({player(1, false, "1.2.3.4:1", ""), b})));
  // ---- refused: wrong types where the player list or a port is expected.
  CHECK(refused("{\"type\":\"get-ticket-resp\",\"matchId\":\"m\"}"));                       // players missing
  CHECK(refused("{\"type\":\"get-ticket-resp\",\"players\":null}"));
  CHECK(refused("{\"type\":\"get-ticket-resp\",\"players\":7}"));
  CHECK(refused("{\"type\":\"get-ticket-resp\",\"players\":\"two\"}"));
  CHECK(refused("{\"type\":\"get-ticket-resp\",\"players\":{\"0\":1,\"1\":2}}"));
  CHECK(refused(ticket({a, "null"})));
  CHECK(refused(ticket({a, "7"})));
  CHECK(refused(ticket({a, "[1,2]"})));
  CHECK(refused(ticket({a, "{\"uid\":\"u\",\"port\":\"2\",\"isLocalPlayer\":false}"})));     // text where a number is expected
  CHECK(refused(ticket({a, "{\"uid\":\"u\",\"port\":null,\"isLocalPlayer\":false}"})));
  CHECK(refused(ticket({a, "{\"uid\":\"u\",\"isLocalPlayer\":false}"})));                    // port missing
  CHECK(refused(ticket({"{\"uid\":\"u\",\"port\":1,\"isLocalPlayer\":\"yes\"}", b})));       // the only local flag is not a boolean
  // ---- refused: not a ticket at all.
  CHECK(refused(""));
  CHECK(refused("not json"));
  CHECK(refused("null"));
  CHECK(refused("[]"));
  CHECK(refused("42"));

  // ---- wrong types in fields that have a default: the ticket is still read, with the default.
  {
    std::string odd = "{\"uid\":7,\"displayName\":null,\"connectCode\":[1],\"port\":2,\"isLocalPlayer\":false,\"isBot\":\"no\","
                      "\"ipAddress\":12,\"ipAddressLan\":null,\"chatMessages\":\"hi\"}";
    std::string s = "{\"type\":\"get-ticket-resp\",\"matchId\":5,\"isHost\":\"yes\",\"players\":[" + a + "," + odd +
                    "],\"stages\":[\"x\",null,31],\"items\":\"many\"}";
    Ticket t = parse(s);
    CHECK(t.ok);
    CHECK(t.match_id.empty());
    CHECK(t.players.size() == 2 && t.players[1].uid.empty() && t.players[1].display_name.empty() && t.players[1].connect_code.empty());
    CHECK(!t.players[1].is_bot && t.players[1].chat_messages.size() == 16);
    CHECK(t.remote_ips.size() == 1 && t.remote_ips[0] == "1.1.1.1:123");   // the same placeholder a missing address gets
    CHECK(!t.is_host);
    CHECK((t.stages == std::vector<uint16_t>{0x1F}));
    CHECK(t.items == 0);
  }
  {
    Ticket t = parse(ticket({a, b}, ",\"stages\":\"all\",\"items\":null"));
    CHECK(t.ok && t.items == 0);
    CHECK((t.stages == std::vector<uint16_t>{0x3, 0x8, 0x1C, 0x1F, 0x20, 0x2}));
  }
  std::printf("slippi ticket test passed\n");
  return 0;
}
