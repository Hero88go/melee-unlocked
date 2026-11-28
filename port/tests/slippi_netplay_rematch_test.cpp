// Four real loopback clients: every opponent must discard its previous game's
// input queue and accept the new match's confirmed input numbering.
// SPDX-License-Identifier: GPL-2.0-or-later
#include <enet/enet.h>
#include "slippi_net.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <thread>

using namespace slippi;
namespace {
void require(bool condition, const char* why) {
  if (!condition) throw std::runtime_error(why);
}
void await(const std::function<bool()>& condition, const char* why) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  while (!condition()) {
    require(std::chrono::steady_clock::now() < end, why);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}
void select(NetplayClient& client, int player, int round) {
  PlayerSelections s;
  s.player_idx = (uint8_t)player; s.character_id = (uint8_t)(player + 1);
  s.is_character_selected = s.is_stage_selected = true;
  s.stage_id = 31; s.rng_offset = 100 + round;
  client.SetMatchSelections(s);
}
// Four consecutive UDP ports nobody holds. The test used 51970 to 51973 every time, so a second copy
// running at once (the two Windows builds' ctest side by side) failed to connect.
uint16_t free_base() {
  const uint32_t seed = (uint32_t)std::chrono::steady_clock::now().time_since_epoch().count();
  for (uint32_t attempt = 0; attempt < 64; ++attempt) {
    const uint16_t base = (uint16_t)(42000u + ((seed + attempt * 7919u) % 3800u) * 4u);
    ENetSocket socks[4] = {ENET_SOCKET_NULL, ENET_SOCKET_NULL, ENET_SOCKET_NULL, ENET_SOCKET_NULL};
    int bound = 0;
    for (int j = 0; j < 4; ++j) {
      socks[j] = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
      ENetAddress a; a.host = ENET_HOST_ANY; a.port = (enet_uint16)(base + j);
      if (socks[j] == ENET_SOCKET_NULL || enet_socket_bind(socks[j], &a) != 0) break;
      ++bound;
    }
    for (ENetSocket s : socks) if (s != ENET_SOCKET_NULL) enet_socket_destroy(s);
    if (bound == 4) return base;
  }
  return 51970;
}
void send(NetplayClient& client, int player, int frame, int round) {
  auto pad = std::make_unique<Pad>(frame);
  std::memset(pad->buf, round * 16 + player, PAD_DATA_SIZE);
  client.SendSlippiPad(std::move(pad));
}
}
int main() {
  try {
    require(enet_ready(), "ENet initialization");
    std::unique_ptr<NetplayClient> clients[4];
    const uint16_t base = free_base();
    for (int i = 0; i < 4; ++i) {
      std::vector<std::string> addresses(3, "127.0.0.1");
      std::vector<uint16_t> ports;
      for (int j = 0; j < 4; ++j) if (i != j) ports.push_back(base + j);
      clients[i] = std::make_unique<NetplayClient>(addresses, ports, 3, base + i, i == 0, (uint8_t)i);
    }
    await([&] {
      for (auto& c : clients) if (c->GetSlippiConnectStatus() != NetplayClient::ConnectStatus::CONNECTED) return false;
      return true;
    }, "four peers failed to connect");
    for (int i = 0; i < 4; ++i) select(*clients[i], i, 0);
    await([&] {
      for (auto& c : clients) for (int k = 0; k < 3; ++k)   // a 4-player session: remotes 0-2 (the arrays hold 5)
        if (!c->GetMatchInfo().remote[k].is_character_selected) return false;
      return true;
    }, "initial selections did not arrive");
    for (auto& c : clients) c->StartSlippiGame();
    for (int frame = 1; frame <= 90; ++frame)
      for (int i = 0; i < 4; ++i) send(*clients[i], i, frame, 0);
    await([&] {
      for (auto& c : clients) for (int p = 0; p < 3; ++p)
        if (c->GetSlippiRemotePad(p, 128)->latest_frame != 90) return false;
      return true;
    }, "initial confirmed inputs did not arrive");
    // All three selection orders vary between sockets. A shared started flag
    // only clears the first remote, leaving two queues headed at frame 90.
    for (int i : {2, 0, 3, 1}) select(*clients[i], i, 1);
    await([&] {
      for (auto& c : clients) for (int k = 0; k < 3; ++k)
        if (c->GetMatchInfo().remote[k].rng_offset != 101) return false;
      return true;
    }, "rematch selections did not arrive");
    for (auto& c : clients) for (int p = 0; p < 3; ++p)
      require(c->GetSlippiRemotePad(p, 128)->latest_frame == 0, "stale doubles input survived rematch selection");
    for (auto& c : clients) c->StartSlippiGame();
    for (int frame = 1; frame <= 3; ++frame)
      for (int i = 0; i < 4; ++i) send(*clients[i], i, frame, 1);
    await([&] {
      for (auto& c : clients) for (int p = 0; p < 3; ++p)
        if (c->GetSlippiRemotePad(p, 128)->latest_frame != 3) return false;
      return true;
    }, "rematch did not restart at frame one for every peer");
    for (auto& c : clients) for (int p = 0; p < 3; ++p) {
      auto pad = c->GetSlippiRemotePad(p, 128);
      require(!pad->data.empty() && pad->data[0] == 16 + pad->player_idx,
              "rematch used another player's or previous game's input");
    }
    std::puts("PASS: four loopback peers cleared every stale queue and received new rematch inputs");
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
