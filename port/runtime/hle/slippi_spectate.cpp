// Slippi spectator server (see slippi_spectate.h). Port of Dolphin's SlippiSpectateServer, same
// ENet host, same messages (nlohmann's dump writes the keys in the same order), same cursors.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "slippi_spectate.h"
#include "slippi_net.h"
#include "host.h"
#include "../gx/render_options.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <enet/enet.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;

namespace slippi::spectate {
namespace {

constexpr size_t kMaxClients = 4;   // Dolphin's MAX_CLIENTS
constexpr size_t kChannels = 2;

struct Socket {
  uint64_t cursor = 0;
  uint64_t menu_cursor = 0;
  bool shook_hands = false;
  ENetPeer* peer = nullptr;
};

// Main thread to server thread (Dolphin's SPSCQueue; both engines can feed from more than one
// thread, so a locked queue here).
std::mutex g_queue_mutex;
std::deque<std::string> g_queue;

// Server thread only.
bool g_in_game = false;
std::map<uint16_t, std::shared_ptr<Socket>> g_sockets;
std::string g_event_concat;
std::vector<std::string> g_event_buffer;
std::string g_menu_event;
uint64_t g_cursor_offset = 0;
uint64_t g_menu_cursor = 0;

std::thread g_thread;
std::atomic<bool> g_stop{false};
std::atomic<bool> g_running{false};
std::atomic<uint16_t> g_port{0};
std::once_flag g_started;

void push(std::string event) {
  if (!g_running) return;   // no server this run: nothing collects the queue
  std::lock_guard<std::mutex> lock(g_queue_mutex);
  g_queue.push_back(std::move(event));
}

bool pop(std::string& event) {
  std::lock_guard<std::mutex> lock(g_queue_mutex);
  if (g_queue.empty()) return false;
  event = std::move(g_queue.front());
  g_queue.pop_front();
  return true;
}

// Common/Base64.hpp: the standard alphabet with '=' padding.
std::string base64(const std::string& in) {
  static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((in.size() + 2) / 3 * 4);
  size_t i = 0;
  for (; i + 2 < in.size(); i += 3) {
    const uint32_t v = (uint32_t)(uint8_t)in[i] << 16 | (uint32_t)(uint8_t)in[i + 1] << 8 | (uint8_t)in[i + 2];
    out += table[v >> 18 & 63]; out += table[v >> 12 & 63]; out += table[v >> 6 & 63]; out += table[v & 63];
  }
  if (i + 1 == in.size()) {
    const uint32_t v = (uint32_t)(uint8_t)in[i] << 16;
    out += table[v >> 18 & 63]; out += table[v >> 12 & 63]; out += "==";
  } else if (i + 2 == in.size()) {
    const uint32_t v = (uint32_t)(uint8_t)in[i] << 16 | (uint32_t)(uint8_t)in[i + 1] << 8;
    out += table[v >> 18 & 63]; out += table[v >> 12 & 63]; out += table[v >> 6 & 63]; out += '=';
  }
  return out;
}

void send(ENetPeer* peer, const std::string& text) {
  ENetPacket* packet = enet_packet_create(text.data(), text.size(), ENET_PACKET_FLAG_RELIABLE);
  enet_peer_send(peer, 0, packet);
}

// SlippiSpectateServer::writeEvents
void write_events(uint16_t peer_id) {
  Socket& socket = *g_sockets[peer_id];
  if (!g_in_game && socket.menu_cursor != g_menu_cursor) {
    send(socket.peer, g_menu_event);
    socket.menu_cursor = g_menu_cursor;
  }
  // A cursor past the end of the buffer is left over from an old game: start it over.
  if (socket.cursor > g_event_buffer.size()) socket.cursor = 0;
  for (uint64_t i = socket.cursor; i < g_event_buffer.size(); i++) {
    send(socket.peer, g_event_buffer[i]);
    socket.cursor++;
  }
}

// SlippiSpectateServer::popEvents
void pop_events() {
  std::string event;
  while (pop(event)) {
    // start_game and end_game are meta-events marking a game's bounds.
    json message = json::parse(event, nullptr, false);
    if (!message.is_discarded() && message.find("type") != message.end()) {
      if (message["type"] == "end_game") {
        const uint32_t cursor = (uint32_t)(g_event_buffer.size() + g_cursor_offset);
        message["cursor"] = cursor;
        message["next_cursor"] = cursor + 1;
        g_menu_cursor = 0;
        g_event_buffer.push_back(message.dump());
        g_cursor_offset += g_event_buffer.size();
        g_menu_event.clear();
        g_in_game = false;
        continue;
      }
      if (message["type"] == "start_game") {
        g_event_buffer.clear();
        const uint32_t cursor = (uint32_t)(g_event_buffer.size() + g_cursor_offset);
        g_in_game = true;
        message["cursor"] = cursor;
        message["next_cursor"] = cursor + 1;
        g_event_buffer.push_back(message.dump());
        continue;
      }
    }
    json game_event;
    if (!g_in_game) {
      game_event["payload"] = base64(event);
      g_menu_cursor += 1;
      game_event["type"] = "menu_event";
      g_menu_event = game_event.dump();
      continue;
    }
    const uint8_t command = (uint8_t)event[0];
    g_event_concat += event;
    // GAME_INIT, FRAME_END, GAME_END and SPLIT_MESSAGE close one game_event.
    if (command == 0x36 || command == 0x3C || command == 0x39 || command == 0x10) {
      const uint32_t cursor = (uint32_t)(g_event_buffer.size() + g_cursor_offset);
      game_event["payload"] = base64(g_event_concat);
      game_event["type"] = "game_event";
      game_event["cursor"] = cursor;
      game_event["next_cursor"] = cursor + 1;
      g_event_buffer.push_back(game_event.dump());
      g_event_concat.clear();
    }
  }
}

// SlippiSpectateServer::handleMessage
void handle_message(const uint8_t* buffer, size_t length, uint16_t peer_id) {
  json message = json::parse(std::string((const char*)buffer, length), nullptr, false);
  if (message.is_discarded() || message.find("type") == message.end()) return;
  if (!message["type"].is_string()) return;
  if (message["type"] != "connect_request") return;
  if (message.find("cursor") == message.end() || !message["cursor"].is_number_integer()) return;
  Socket& socket = *g_sockets[peer_id];
  const uint32_t requested = message["cursor"];
  if (requested >= g_cursor_offset) {
    // Past the events this server has: start over. Otherwise inside the live match.
    socket.cursor = requested > g_event_buffer.size() + g_cursor_offset ? 0 : requested - g_cursor_offset;
  } else {
    socket.cursor = 0;   // too low: bring the client up to the present
  }
  const uint32_t sent_cursor = (uint32_t)socket.cursor + (uint32_t)g_cursor_offset;
  // A client that joins at the menu is not caught up.
  if (!g_in_game) socket.cursor = g_event_buffer.size();
  json reply;
  reply["type"] = "connect_reply";
  reply["nick"] = "Slippi Online";
  reply["version"] = SLIPPI_SEMVER;
  reply["cursor"] = sent_cursor;
  send(socket.peer, reply.dump());
  socket.shook_hands = true;
}

// SlippiSpectateServer::SlippicommSocketThread
void server_thread(uint16_t port) {
  if (enet_initialize() != 0) {
    host::log("spectator: ENet did not start; no spectator server");
    g_running = false;
    return;
  }
  ENetAddress address{};
  address.host = ENET_HOST_ANY;
  address.port = port;
  // Fails while something else holds the port, or for a while after it closed: retry as Dolphin does.
  ENetHost* server = enet_host_create(&address, kMaxClients, kChannels, 0, 0);
  for (int tries = 0; server == nullptr && tries < 20 && !g_stop; ++tries) {
    server = enet_host_create(&address, kMaxClients, kChannels, 0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
  if (server == nullptr) {
    host::log("spectator: could not open port %u (is Slippi Dolphin or another copy running?)", (unsigned)port);
    g_running = false;
    enet_deinitialize();
    return;
  }
  g_port = port;
  host::log("spectator: serving the game on port %u for the Slippi Launcher and overlays", (unsigned)port);
  while (!g_stop) {
    pop_events();
    for (auto& entry : g_sockets)
      if (entry.second->shook_hands) write_events(entry.first);
    ENetEvent event;
    while (enet_host_service(server, &event, 1) > 0) {
      switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT: {
          host::log("spectator: a client connected");
          auto socket = std::make_shared<Socket>();
          socket->peer = event.peer;
          g_sockets[event.peer->incomingPeerID] = socket;
          break;
        }
        case ENET_EVENT_TYPE_RECEIVE:
          handle_message(event.packet->data, event.packet->dataLength, event.peer->incomingPeerID);
          enet_packet_destroy(event.packet);
          break;
        case ENET_EVENT_TYPE_DISCONNECT:
          host::log("spectator: a client disconnected");
          g_sockets.erase(event.peer->incomingPeerID);
          event.peer->data = nullptr;
          break;
        default:
          break;
      }
    }
  }
  // The end_game of a closing game goes out before the host closes (Dolphin queues it from the EXI
  // device's destructor and the server serves on until its own destructor).
  pop_events();
  for (auto& entry : g_sockets)
    if (entry.second->shook_hands) write_events(entry.first);
  enet_host_flush(server);
  g_sockets.clear();
  enet_host_destroy(server);
  enet_deinitialize();
  g_port = 0;
}

}  // namespace

void start() {
  std::call_once(g_started, [] {
    if (!gx::RenderOptions::live_slippi_spectator()) {
      host::log("spectator: off in settings");
      return;
    }
    const int port = gx::RenderOptions::live_slippi_spectator_port();
    if (port < 1 || port > 65535) {
      host::log("spectator: port %d is not valid; no spectator server", port);
      return;
    }
    g_in_game = false;
    g_menu_cursor = 0;
    g_menu_event.clear();
    g_cursor_offset = 0;
    g_stop = false;
    g_running = true;
    g_thread = std::thread(server_thread, (uint16_t)port);
  });
}

void stop() {
  if (!g_running && !g_thread.joinable()) return;
  end_game(true);
  g_stop = true;
  if (g_thread.joinable()) g_thread.join();
  g_running = false;
}

bool running() { return g_running && g_port != 0; }
uint16_t port() { return g_port; }

void write(const uint8_t* payload, uint32_t length) {
  if (!payload || !length) return;
  push(std::string((const char*)payload, length));
}

void start_game() {
  json message;
  message["type"] = "start_game";
  push(message.dump());
}

void end_game(bool dolphin_closed) {
  json message;
  message["type"] = "end_game";
  message["dolphin_closed"] = dolphin_closed;
  push(message.dump());
}

}  // namespace slippi::spectate
