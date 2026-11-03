// SPDX-License-Identifier: GPL-2.0-or-later
#include "slippi_game_prep.h"
#include "native_slippi_bridge.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

void prep() {
  struct FakePeer {
    slippi::GamePrepQueue received;
    std::vector<uint8_t> sent;
    void send(FakePeer& other, const slippi::GamePrepStep& step) {
      sent = {0x85}; sent.insert(sent.end(), step.begin(), step.end());
      slippi::GamePrepStep incoming{};
      std::copy_n(sent.begin() + 1, 5, incoming.begin());
      other.received.receive(incoming);
    }
  } a, b;
  CHECK(b.received.fetch(0) == (std::array<uint8_t, 6>{}));
  a.send(b, {0, 2, 1, 3, 4});
  CHECK(a.sent == (std::vector<uint8_t>{0x85, 0, 2, 1, 3, 4}));
  CHECK(b.received.fetch(0) == (std::array<uint8_t, 6>{1, 0, 2, 1, 3, 4}));
  CHECK(b.received.fetch(0) == (std::array<uint8_t, 6>{1, 0, 2, 1, 3, 4}));
  a.send(b, {1, 20, 2, 8, 9});
  CHECK(b.received.fetch(1) == (std::array<uint8_t, 6>{1, 0, 20, 2, 8, 9}));
  CHECK(b.received.fetch(0) == (std::array<uint8_t, 6>{}));
  b.send(a, {255, 3, 4, 5, 6}); // indices are opaque bytes, including wrap
  CHECK(a.received.fetch(255)[0] == 1);
  a.received.clear(); // StartSlippiGame resets the queue between games, as Slippi does
  CHECK(a.received.fetch(255)[0] == 0);
  CHECK(a.received.fetch(0)[0] == 0);
  for (int i = 0; i < 100; ++i) CHECK(a.received.fetch(0)[1] == 0); // no timeout/skip
  CHECK(slippi::online::command_payload_valid(slippi::online::CMD_GP_COMPLETE_STEP, a.sent.data() + 1, 5));
  CHECK(!slippi::online::command_payload_valid(slippi::online::CMD_GP_COMPLETE_STEP, a.sent.data() + 1, 4));
  std::puts("yougame_ranked: fake peer GamePrep layout, ordering, repeated polls and no timeout ok");
}

int main() { prep(); }
