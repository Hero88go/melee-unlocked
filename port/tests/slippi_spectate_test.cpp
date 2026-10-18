// The Slippi spectator server answers as Slippi Dolphin's SlippiSpectateServer does: a real ENet
// client on loopback (connected the way slippi-js DolphinConnection connects) gets the same
// connect_reply, menu_event, start_game, game_event and end_game messages, byte for byte, with the
// same cursors and the recording bytes in the base64 payloads.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hle/slippi_spectate.h"
#include "hle/slippi_net.h"
#include "gx/render_options.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <enet/enet.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using json = nlohmann::json;

namespace {
int g_failures = 0;
void check(bool ok, const char* what) {
  if (!ok) { std::printf("FAIL: %s\n", what); ++g_failures; }
}

constexpr uint16_t kPort = 51493;   // not Slippi Dolphin's 51441, so a running Dolphin does not interfere

std::string decode64(const std::string& in) {
  auto value = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    return c == '+' ? 62 : c == '/' ? 63 : -1;
  };
  std::string out;
  int bits = 0, buffer = 0;
  for (char c : in) {
    const int v = value(c);
    if (v < 0) break;
    buffer = buffer << 6 | v;
    bits += 6;
    if (bits >= 8) { bits -= 8; out += (char)((buffer >> bits) & 0xFF); }
  }
  return out;
}

struct Client {
  ENetHost* host = nullptr;
  ENetPeer* peer = nullptr;
  std::vector<std::string> received;
  // Dolphin sends its stored menu event, emptied at the end of a game, to a client whose menu
  // cursor differs: one zero-length packet after each game for a client that saw a menu event.
  int empties = 0;
  // slippi-js: enet.createClient({peers: 1, channels: 3, down: 0, up: 0}), then connect.
  bool connect() {
    host = enet_host_create(nullptr, 1, 3, 0, 0);
    if (!host) return false;
    ENetAddress address{};
    enet_address_set_host(&address, "127.0.0.1");
    address.port = kPort;
    peer = enet_host_connect(host, &address, 3, 1337);
    if (!peer) return false;
    ENetEvent event;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < until)
      if (enet_host_service(host, &event, 10) > 0 && event.type == ENET_EVENT_TYPE_CONNECT) return true;
    return false;
  }
  void send(const std::string& text) {
    enet_peer_send(peer, 0, enet_packet_create(text.data(), text.size(), ENET_PACKET_FLAG_RELIABLE));
    enet_host_flush(host);
  }
  // Collects messages until `count` have arrived or the time runs out.
  void pump(size_t count, int ms = 3000) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    ENetEvent event;
    while (received.size() < count && std::chrono::steady_clock::now() < until) {
      if (enet_host_service(host, &event, 10) > 0 && event.type == ENET_EVENT_TYPE_RECEIVE) {
        if (event.packet->dataLength == 0) ++empties;
        else received.emplace_back((const char*)event.packet->data, event.packet->dataLength);
        if (std::getenv("SPECTATE_TEST_VERBOSE"))
          std::printf("got: %s\n", event.packet->dataLength ? received.back().c_str() : "(empty)");
        enet_packet_destroy(event.packet);
      }
    }
  }
  void close() {
    if (peer) enet_peer_disconnect_now(peer, 0);
    if (host) enet_host_destroy(host);
    host = nullptr; peer = nullptr;
  }
};

void feed(std::initializer_list<uint8_t> bytes) {
  std::vector<uint8_t> v(bytes);
  slippi::spectate::write(v.data(), (uint32_t)v.size());
}
}  // namespace

int run() {
  gx::RenderOptions::live_slippi_spectator() = true;
  gx::RenderOptions::live_slippi_spectator_port() = kPort;
  slippi::spectate::start();
  for (int i = 0; i < 300 && !slippi::spectate::running(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  check(slippi::spectate::running() && slippi::spectate::port() == kPort, "the server serves on the configured port");

  // A menu frame before anyone connects: kept as the latest menu event.
  feed({0x3E, 0x01, 0x02, 0x03});
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  check(enet_initialize() == 0, "client ENet starts");
  Client client;
  check(client.connect(), "a client connects over ENet");
  client.send("{\"type\":\"connect_request\",\"cursor\":0}");
  client.pump(2);
  check(client.received.size() >= 2, "connect_reply and the menu event arrive");
  if (client.received.size() >= 2) {
    // nlohmann's dump orders the keys, as in Dolphin.
    check(client.received[0] == std::string("{\"cursor\":0,\"nick\":\"Slippi Online\",\"type\":\"connect_reply\",\"version\":\"") +
                                    slippi::SLIPPI_SEMVER + "\"}", "connect_reply is Dolphin's");
    const json menu = json::parse(client.received[1], nullptr, false);
    check(!menu.is_discarded() && menu["type"] == "menu_event" && menu.size() == 2, "menu_event has type and payload only");
    if (!menu.is_discarded()) check(decode64(menu["payload"]) == std::string("\x3E\x01\x02\x03", 4), "menu_event carries the whole menu frame");
  }

  // One game as the EXI device sends it: start, the command table, Game Start, a frame, Game End.
  slippi::spectate::start_game();
  feed({0x35, 0x07, 0x36, 0x00, 0x02, 0x3C, 0x00, 0x04});   // command table
  feed({0x36, 0xAA, 0xBB});                                 // Game Start: closes a game_event
  feed({0x37, 0x01});                                       // (not in the table here; only bytes matter)
  feed({0x3C, 0x00, 0x00, 0x00, 0x01});                     // Frame Bookend: closes a game_event
  feed({0x39, 0x02});                                       // Game End: closes a game_event
  slippi::spectate::end_game();
  client.pump(7);
  check(client.received.size() == 7, "start_game, three game_events and end_game arrive");
  check(client.empties == 1, "one empty menu event after the game, as Dolphin sends");
  if (client.received.size() == 7) {
    check(client.received[2] == "{\"cursor\":0,\"next_cursor\":1,\"type\":\"start_game\"}", "start_game is Dolphin's, cursor 0");
    const json e1 = json::parse(client.received[3], nullptr, false), e2 = json::parse(client.received[4], nullptr, false),
               e3 = json::parse(client.received[5], nullptr, false);
    check(!e1.is_discarded() && e1["type"] == "game_event" && e1["cursor"] == 1 && e1["next_cursor"] == 2, "first game_event, cursor 1");
    check(!e2.is_discarded() && e2["cursor"] == 2 && !e3.is_discarded() && e3["cursor"] == 3, "game_event cursors follow on");
    if (!e1.is_discarded()) check(decode64(e1["payload"]) == std::string("\x35\x07\x36\x00\x02\x3C\x00\x04\x36\xAA\xBB", 11), "table and Game Start in one payload");
    if (!e2.is_discarded()) check(decode64(e2["payload"]) == std::string("\x37\x01\x3C\x00\x00\x00\x01", 7), "frame events up to the bookend");
    if (!e3.is_discarded()) check(decode64(e3["payload"]) == std::string("\x39\x02", 2), "Game End alone");
    check(client.received[6] == "{\"cursor\":4,\"dolphin_closed\":false,\"next_cursor\":5,\"type\":\"end_game\"}", "end_game is Dolphin's, cursor 4");
    const std::string payload = !e1.is_discarded() ? e1.dump() : "";
    check(client.received[3] == payload, "game_event text is nlohmann's ordered dump");
  }

  // A second client joining at the menu is not caught up on the finished game.
  Client late;
  check(late.connect(), "a second client connects");
  late.send("{\"type\":\"connect_request\",\"cursor\":0}");
  late.pump(1);
  check(late.received.size() == 1 && late.received[0].find("\"cursor\":5") != std::string::npos,
        "a client joining after the game gets the next cursor and no catch-up");
  late.pump(2, 300);
  check(late.received.size() == 1 && late.empties == 0, "nothing else is sent to it at the menu");

  // The next game continues the cursors (Dolphin's m_cursor_offset).
  slippi::spectate::start_game();
  feed({0x36, 0x01});
  client.pump(9);
  check(client.received.size() == 9 && client.received[7] == "{\"cursor\":5,\"next_cursor\":6,\"type\":\"start_game\"}",
        "the second game starts at cursor 5");

  late.close();
  client.close();
  slippi::spectate::stop();
  check(!slippi::spectate::running(), "the server stops");
  enet_deinitialize();
  std::printf(g_failures ? "slippi spectate: %d failure(s)\n" : "slippi spectate: ok\n", g_failures);
  return g_failures ? 1 : 0;
}

// --capture <port> <out> <seconds>: a spectator client for a running game. Connects as slippi-js
// does (retrying while the game starts), then writes the decoded game_event payloads of the
// first game to <out> (the .slp raw element, to compare with the game's own .slp) and one line
// per message to <out>.txt. Ends at that game's end_game or after <seconds>.
int capture(uint16_t port, const char* out_path, int seconds) {
  if (enet_initialize() != 0) return 2;
  const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  std::string game, lines;
  bool connected = false, started = false, ended = false;
  while (!connected && std::chrono::steady_clock::now() < until) {
    Client c;
    c.host = enet_host_create(nullptr, 1, 3, 0, 0);
    ENetAddress address{};
    enet_address_set_host(&address, "127.0.0.1");
    address.port = port;
    c.peer = enet_host_connect(c.host, &address, 3, 1337);
    ENetEvent event;
    const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < give_up)
      if (enet_host_service(c.host, &event, 10) > 0 && event.type == ENET_EVENT_TYPE_CONNECT) { connected = true; break; }
    if (!connected) { c.close(); continue; }
    c.send("{\"type\":\"connect_request\",\"cursor\":0}");
    while (!ended && std::chrono::steady_clock::now() < until) {
      if (enet_host_service(c.host, &event, 10) <= 0) continue;
      if (event.type == ENET_EVENT_TYPE_DISCONNECT) break;
      if (event.type != ENET_EVENT_TYPE_RECEIVE) continue;
      const std::string text((const char*)event.packet->data, event.packet->dataLength);
      enet_packet_destroy(event.packet);
      const json m = json::parse(text, nullptr, false);
      if (m.is_discarded() || !m.is_object() || m.find("type") == m.end()) { lines += "(not json, " + std::to_string(text.size()) + " bytes)\n"; continue; }
      const std::string type = m["type"];
      lines += type + (m.find("cursor") != m.end() ? " " + m["cursor"].dump() : std::string()) + "\n";
      if (type == "start_game") started = true;
      else if (type == "game_event" && started) game += decode64(m["payload"].get<std::string>());
      else if (type == "end_game" && started) ended = true;
    }
    c.close();
  }
  if (FILE* f = std::fopen(out_path, "wb")) { std::fwrite(game.data(), 1, game.size(), f); std::fclose(f); }
  if (FILE* f = std::fopen((std::string(out_path) + ".txt").c_str(), "wb")) { std::fwrite(lines.data(), 1, lines.size(), f); std::fclose(f); }
  enet_deinitialize();
  std::printf("capture: connected %d, game %s, %zu bytes\n", connected ? 1 : 0, ended ? "complete" : started ? "incomplete" : "not seen", game.size());
  return connected && ended ? 0 : 1;
}

int main(int argc, char** argv) {
  if (argc == 5 && std::string(argv[1]) == "--capture")
    return capture((uint16_t)std::atoi(argv[2]), argv[3], std::atoi(argv[4]));
  try {
    return run();
  } catch (const std::exception& e) {
    std::printf("FAIL: exception: %s\n", e.what());
    std::fflush(stdout);
    std::_Exit(1);   // the server thread is still running: leave without its destructor
  }
}
